#include "Jit/symbolizer.h"

#include "Jit/log.h"
#include "Jit/util.h"

#include <cxxabi.h>
#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>

namespace jit {

Symbolizer::Symbolizer(const char* exe_path) {
  int exe_fd = ::open(exe_path, O_RDONLY);
  if (exe_fd == -1) {
    JIT_LOG("Could not open %s: %s", exe_path, ::strerror(errno));
    return;
  }
  // Close the file descriptor. We don't need to keep it around for the mapping
  // to be valid and if we leave it lying around then some CPython tests fail
  // because they rely on specific file descriptor numbers.
  SCOPE_EXIT(::close(exe_fd));
  struct stat statbuf;
  int stat_result = ::fstat(exe_fd, &statbuf);
  if (stat_result == -1) {
    JIT_LOG("Could not stat %s: %s", exe_path, ::strerror(errno));
    return;
  }
  off_t exe_size_signed = statbuf.st_size;
  JIT_CHECK(exe_size_signed >= 0, "exe size should not be negative");
  exe_size_ = static_cast<size_t>(exe_size_signed);
  exe_ = reinterpret_cast<char*>(
      ::mmap(nullptr, exe_size_, PROT_READ, MAP_PRIVATE, exe_fd, 0));
  if (exe_ == reinterpret_cast<char*>(MAP_FAILED)) {
    JIT_LOG("could not mmap");
    exe_ = nullptr;
    return;
  }
  auto elf = reinterpret_cast<ElfW(Ehdr)*>(exe_);
  auto shdr = reinterpret_cast<ElfW(Shdr)*>(exe_ + elf->e_shoff);
  const char* str = exe_ + shdr[elf->e_shstrndx].sh_offset;
  for (int i = 0; i < elf->e_shnum; i++) {
    if (shdr[i].sh_size) {
      if (std::strcmp(&str[shdr[i].sh_name], ".symtab") == 0) {
        symtab_ = reinterpret_cast<ElfW(Shdr)*>(&shdr[i]);
      } else if (std::strcmp(&str[shdr[i].sh_name], ".strtab") == 0) {
        strtab_ = reinterpret_cast<ElfW(Shdr)*>(&shdr[i]);
      }
    }
  }
  if (symtab_ == nullptr) {
    JIT_LOG("could not find symtab");
    deinit();
    return;
  }
  if (strtab_ == nullptr) {
    JIT_LOG("could not find strtab");
    deinit();
    return;
  }
}

std::optional<std::string_view> Symbolizer::cache(
    const void* func,
    const char* name) {
  cache_[func] = name;
  return cache_[func];
}

static bool hasELFMagic(const void* addr) {
  auto elf_hdr = reinterpret_cast<const ElfW(Ehdr)*>(addr);
  return (elf_hdr->e_ident[0] == ELFMAG0) && (elf_hdr->e_ident[1] == ELFMAG1) &&
      (elf_hdr->e_ident[2] == ELFMAG2) && (elf_hdr->e_ident[3] == ELFMAG3);
}

struct SymbolResult {
  const void* func;
  std::optional<std::string_view> name;
  Symbolizer* symbolizer;
};

// Return 0 to continue iteration and non-zero to stop.
static int findSymbolIn(struct dl_phdr_info* info, size_t, void* data) {
  // Continue until the first dynamic library is found. Looks like a bunch of
  // platforms put the main executable as the first entry, which has an empty
  // name. Skip it.
  if (info->dlpi_name == nullptr || info->dlpi_name[0] == 0) {
    return 0;
  }
  // Ignore linux-vdso.so.1 since it does not have an actual file attached.
  std::string_view name{info->dlpi_name};
  if (name.find("linux-vdso") != name.npos) {
    return 0;
  }
  if (info->dlpi_addr == 0) {
    JIT_LOG("Invalid ELF object '%s'", info->dlpi_name);
    return 0;
  }
  if (!hasELFMagic(reinterpret_cast<void*>(info->dlpi_addr))) {
    JIT_LOG(
        "Bad ELF magic at %p in %s",
        reinterpret_cast<void*>(info->dlpi_addr),
        info->dlpi_name);
    return 0;
  }
  int fd = ::open(info->dlpi_name, O_RDONLY);
  if (fd < 0) {
    JIT_LOG("Failed opening %s: %s", info->dlpi_name, ::strerror(errno));
    return 0;
  }
  SCOPE_EXIT(::close(fd));
  struct stat statbuf;
  if (::fstat(fd, &statbuf) < 0) {
    JIT_LOG("Failed stat: %s", ::strerror(errno));
    return 0;
  }
  void* mapping =
      ::mmap(nullptr, statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (mapping == MAP_FAILED) {
    JIT_LOG("Failed mmap: %s", ::strerror(errno));
    return 0;
  }
  SCOPE_EXIT(::munmap(mapping, statbuf.st_size));
  uint8_t* elf_obj = static_cast<uint8_t*>(mapping);
  auto elf_hdr = reinterpret_cast<ElfW(Ehdr)*>(elf_obj);
  if (elf_hdr->e_shoff == 0) {
    JIT_LOG("No section header table in %s", info->dlpi_name);
    return 0;
  }
  // Get the number of entries in the section header table (`e_shnum`). If this
  // value is zero, the number of entries is in the `sh_size` field of the
  // first entry in the section header table.
  auto sec_hdrs = reinterpret_cast<ElfW(Shdr)*>(elf_obj + elf_hdr->e_shoff);
  uint64_t num_sec_hdrs = elf_hdr->e_shnum;
  if (num_sec_hdrs == 0) {
    num_sec_hdrs = sec_hdrs[0].sh_size;
  }
  // Iterate through the symbol tables and search for symbols with the given
  // function address.
  for (uint64_t i = 0; i < num_sec_hdrs; i++) {
    ElfW(Shdr)* sec_hdr = &sec_hdrs[i];
    if (sec_hdr->sh_type != SHT_SYMTAB) {
      continue;
    }
    ElfW(Shdr)* sym_tab_hdr = &sec_hdrs[i];
    ElfW(Shdr)* str_tab_hdr = &sec_hdrs[sym_tab_hdr->sh_link];
    uint64_t nsyms = sym_tab_hdr->sh_size / sym_tab_hdr->sh_entsize;
    auto symtab_start =
        reinterpret_cast<ElfW(Sym)*>(elf_obj + sym_tab_hdr->sh_offset);
    ElfW(Sym)* symtab_end = symtab_start + nsyms;
    auto strtab =
        reinterpret_cast<const char*>(elf_obj + str_tab_hdr->sh_offset);
    for (ElfW(Sym)* sym = symtab_start; sym != symtab_end; sym++) {
      if (ELF64_ST_TYPE(sym->st_info) != STT_FUNC) {
        // We only care about symbols associated with executable code
        continue;
      }
      auto addr = reinterpret_cast<void*>(info->dlpi_addr + sym->st_value);
      auto result = reinterpret_cast<SymbolResult*>(data);
      if (addr == result->func) {
        // Cache here; the lifetime of the strtab ends at the end of this
        // function.
        result->name =
            result->symbolizer->cache(result->func, &strtab[sym->st_name]);
        return 1;
      }
    }
  }
  return 0;
}

std::optional<std::string_view> Symbolizer::symbolize(const void* func) {
  // Try the cache first. We might have looked it up before.
  auto cached = cache_.find(func);
  if (cached != cache_.end()) {
    return cached->second;
  }
  // Then try dladdr. It might be able to find the symbol.
  Dl_info info;
  if (::dladdr(func, &info) != 0 && info.dli_sname != nullptr) {
    return cache(func, info.dli_sname);
  }
  if (!isInitialized()) {
    return std::nullopt;
  }
  // Fall back to reading our own ELF header.
  auto sym = reinterpret_cast<ElfW(Sym)*>(exe_ + symtab_->sh_offset);
  const char* str = exe_ + strtab_->sh_offset;
  for (size_t i = 0; i < symtab_->sh_size / sizeof(ElfW(Sym)); i++) {
    if (reinterpret_cast<void*>(sym[i].st_value) == func) {
      return cache(func, str + sym[i].st_name);
    }
  }
  // Fall back to reading dynamic symbols. The name is cached inside
  // findSymbolIn.
  SymbolResult result = {func, std::nullopt, this};
  int found = ::dl_iterate_phdr(findSymbolIn, &result);
  JIT_CHECK(
      (found > 0) == result.name.has_value(),
      "result.name should match return value of dl_iterate_phdr");
  return result.name;
}

void Symbolizer::deinit() {
  if (exe_ == nullptr) {
    // Something went wrong in the constructor; don't try to deinit.
    return;
  }
  int result = ::munmap(reinterpret_cast<void*>(exe_), exe_size_);
  if (result != 0) {
    JIT_LOG("Could not unmap exe: %s", ::strerror(errno));
  }
  exe_ = nullptr;
  exe_size_ = 0;
  symtab_ = nullptr;
  strtab_ = nullptr;
  cache_.clear();
}

std::optional<std::string> demangle(const std::string& mangled_name) {
  int status;
  char* demangled_name =
      abi::__cxa_demangle(mangled_name.c_str(), nullptr, nullptr, &status);
  if (demangled_name == nullptr) {
    if (status == -1) {
      JIT_DLOG("Could not allocate memory for demangled name");
    } else if (status == -2) {
      JIT_DLOG("Mangled name '%s' is not valid", mangled_name);
      // Couldn't demangle. Oh well. Probably better to have some name than
      // none at all.
      return mangled_name;
    } else if (status == -3) {
      JIT_DLOG("Invalid input to __cxa_demangle");
    }
    return std::nullopt;
  }
  std::string result{demangled_name};
  std::free(demangled_name);
  return result;
}

} // namespace jit

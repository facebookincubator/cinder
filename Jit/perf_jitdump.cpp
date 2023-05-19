// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/perf_jitdump.h"

#include "Jit/log.h"
#include "Jit/pyjit.h"
#include "Jit/threaded_compile.h"
#include "Jit/util.h"
#include "pycore_ceval.h"

#include <elf.h>
#include <fcntl.h>
#include <fmt/format.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <regex>
#include <sstream>
#include <tuple>

#ifdef __x86_64__
// Use the cheaper rdtsc by default. If you disable this for some reason, or
// run on a non-x86_64 architecture, you need to add '-k1' to your 'perf
// record' command.
#define PERF_USE_RDTSC
#endif

#ifdef PERF_USE_RDTSC
#include <x86intrin.h>
#endif

namespace jit::perf {

const std::string kDefaultSymbolPrefix{"__CINDER_INFRA_JIT"};
const std::string kFuncSymbolPrefix{"__CINDER_JIT"};
const std::string kShadowFrameSymbolPrefix{"__CINDER_SHDW_FRAME_JIT"};
int jit_perfmap = 0;
std::string perf_jitdump_dir;

namespace {

struct FileInfo {
  std::string filename;
  std::string filename_format;
  std::FILE* file{nullptr};
};

FileInfo g_pid_map;

FileInfo g_jitdump_file;
void* g_jitdump_mmap_addr = nullptr;
const size_t kJitdumpMmapSize = 1;

// C++-friendly wrapper around strerror_r().
std::string string_error(int errnum) {
  char buf[1024];
  return strerror_r(errnum, buf, sizeof(buf));
}

class FileLock {
 public:
  FileLock(std::FILE* file, bool exclusive) : fd_{fileno(file)} {
    auto operation = exclusive ? LOCK_EX : LOCK_SH;
    while (true) {
      auto ret = ::flock(fd_, operation);
      if (ret == 0) {
        return;
      }
      if (ret == -1 && errno == EINTR) {
        continue;
      }
      JIT_CHECK(
          false,
          "flock(%d, %d) failed: %s",
          fd_,
          operation,
          string_error(errno));
    }
  }

  ~FileLock() {
    auto ret = ::flock(fd_, LOCK_UN);
    JIT_CHECK(
        ret == 0, "flock(%d, LOCK_UN) failed: %s", fd_, string_error(errno));
  }

  DISALLOW_COPY_AND_ASSIGN(FileLock);

 private:
  int fd_;
};

class SharedFileLock : public FileLock {
 public:
  SharedFileLock(std::FILE* file) : FileLock{file, false} {}
};

class ExclusiveFileLock : public FileLock {
 public:
  ExclusiveFileLock(std::FILE* file) : FileLock{file, true} {}
};

// This file writes out perf jitdump files, to be used by 'perf inject' and
// 'perf report'. The format is documented here:
// https://raw.githubusercontent.com/torvalds/linux/master/tools/perf/Documentation/jitdump-specification.txt.

enum Flags {
  JITDUMP_FLAGS_ARCH_TIMESTAMP = 1,
};

struct FileHeader {
  uint32_t magic;
  uint32_t version;
  uint32_t total_size;
  uint32_t elf_mach;
  uint32_t pad1;
  uint32_t pid;
  uint64_t timestamp;
  uint64_t flags;
};

enum RecordType {
  JIT_CODE_LOAD = 0,
  JIT_CODE_MOVE = 1,
  JIT_CODE_DEBUG_INFO = 2,
  JIT_CODE_CLOSE = 3,
  JIT_CODE_UNWINDING_INFO = 4,
};

struct RecordHeader {
  uint32_t type;
  uint32_t total_size;
  uint64_t timestamp;
};

struct CodeLoadRecord : RecordHeader {
  uint32_t pid;
  uint32_t tid;
  uint64_t vma;
  uint64_t code_addr;
  uint64_t code_size;
  uint64_t code_index;
};

// The gettid() syscall doesn't have a C wrapper.
pid_t gettid() {
  return syscall(SYS_gettid);
}

// Get a timestamp for the current event.
uint64_t getTimestamp() {
#ifdef PERF_USE_RDTSC
  return __rdtsc();
#else
  static const uint64_t kNanosPerSecond = 1000000000;
  struct timespec tm;
  int ret = clock_gettime(CLOCK_MONOTONIC, &tm);
  if (ret < 0) {
    return -1;
  }
  return tm.tv_sec * kNanosPerSecond + tm.tv_nsec;
#endif
}

FileInfo openFileInfo(std::string filename_format) {
  auto filename = fmt::format(fmt::runtime(filename_format), getpid());
  auto file = std::fopen(filename.c_str(), "w+");
  if (file == nullptr) {
    JIT_LOG("Couldn't open %s for writing (%s)", filename, string_error(errno));
    return {};
  }
  return {filename, filename_format, file};
}

FileInfo openPidMap() {
  if (!jit_perfmap) {
    return {};
  }

  FileInfo perf_map = openFileInfo("/tmp/perf-{}.map");
  JIT_DLOG("Opened JIT perf-map file: %s", perf_map.filename);
  return perf_map;
}

// If enabled, open the jitdump file, and write out its header.
FileInfo openJitdumpFile() {
  if (perf_jitdump_dir.empty()) {
    return {};
  }

  JIT_CHECK(
      perf_jitdump_dir.at(0) == '/', "jitdump directory path isn't absolute");
  auto info = openFileInfo(fmt::format("{}/jit-{{}}.dump", perf_jitdump_dir));
  if (info.file == nullptr) {
    return {};
  }
  auto fd = fileno(info.file);

  // mmap() the jitdump file so perf inject can find it.
  auto g_jitdump_mmap_addr =
      mmap(nullptr, kJitdumpMmapSize, PROT_EXEC, MAP_PRIVATE, fd, 0);
  JIT_CHECK(
      g_jitdump_mmap_addr != MAP_FAILED,
      "marker mmap of jitdump file failed: %s",
      string_error(errno));

  // Write out the file header.
  FileHeader header;
  header.magic = 0x4a695444;
  header.version = 1;
  header.total_size = sizeof(header);
#ifdef __x86_64__
  header.elf_mach = EM_X86_64;
#elif defined(__aarch64__)
  header.elf_mach = EM_AARCH64;
#else
#error Please provide the ELF e_machine value for your architecture.
#endif
  header.pad1 = 0;
  header.pid = getpid();
  header.timestamp = getTimestamp();
#ifdef PERF_USE_RDTSC
  header.flags = JITDUMP_FLAGS_ARCH_TIMESTAMP;
#else
  header.flags = 0;
#endif

  std::fwrite(&header, sizeof(header), 1, info.file);
  std::fflush(info.file);
  return info;
}

void initFiles() {
  static bool inited = false;
  if (inited) {
    return;
  }
  g_pid_map = openPidMap();
  g_jitdump_file = openJitdumpFile();
  inited = true;
}

// Parses a JIT entry and returns a tuple containing the
// code address, code size, and entry name. An example of an entry is:
// 7fa873c00148 360 __CINDER_JIT:__main__:foo2
std::tuple<const void*, unsigned int, const char*> parseJitEntry(
    const char* entry) {
  std::string_view entry_view = entry;
  size_t space_pos_1 = entry_view.find(' ');

  // Extract the hexadecimal code address
  const char* code_addr_str = entry_view.substr(0, space_pos_1).data();
  unsigned long long code_addr_val = 0;
  std::from_chars(
      code_addr_str, code_addr_str + space_pos_1, code_addr_val, 16);
  const void* code_addr = reinterpret_cast<const void*>(code_addr_val);

  // Find the second space character
  size_t space_pos_2 = entry_view.find(' ', space_pos_1 + 1);

  // Extract the hexadecimal code size
  const char* code_size_str =
      entry_view.substr(space_pos_1 + 1, space_pos_2).data();
  uint32_t code_size;
  std::from_chars(
      code_size_str,
      code_size_str + (space_pos_2 - space_pos_1 - 1),
      code_size,
      16);

  // Extract the entry name
  const char* entry_name = entry_view.substr(space_pos_2 + 1).data();

  return std::make_tuple(code_addr, code_size, entry_name);
}

// Copy the contents of from_name to to_name. Returns a std::FILE* at the end
// of to_name on success, or nullptr on failure.
std::FILE* copyFile(const std::string& from_name, const std::string& to_name) {
  auto from = std::fopen(from_name.c_str(), "r");
  if (from == nullptr) {
    JIT_LOG(
        "Couldn't open %s for reading (%s)", from_name, string_error(errno));
    return nullptr;
  }
  auto to = std::fopen(to_name.c_str(), "w+");
  if (to == nullptr) {
    std::fclose(from);
    JIT_LOG("Couldn't open %s for writing (%s)", to_name, string_error(errno));
    return nullptr;
  }

  char buf[4096];
  while (true) {
    auto bytes_read = std::fread(&buf, 1, sizeof(buf), from);
    auto bytes_written = std::fwrite(&buf, 1, bytes_read, to);
    if (bytes_read < sizeof(buf) && std::feof(from)) {
      // We finished successfully.
      std::fflush(to);
      std::fclose(from);
      return to;
    }
    if (bytes_read == 0 || bytes_written < bytes_read) {
      JIT_LOG("Error copying %s to %s", from_name, to_name);
      std::fclose(from);
      std::fclose(to);
      return nullptr;
    }
  }
}

// Copy the contents of the parent perf map file to the child perf map file.
// Returns 1 on success and 0 on failure.
int copyJitFile(const std::string& parent_filename) {
  auto parent_file = std::fopen(parent_filename.c_str(), "r");
  if (parent_file == nullptr) {
    JIT_LOG(
        "Couldn't open %s for reading (%s)",
        parent_filename,
        string_error(errno));
    return 0;
  }

  char buf[1024];
  while (std::fgets(buf, sizeof(buf), parent_file) != nullptr) {
    buf[strcspn(buf, "\n")] = '\0';
    auto jit_entry = parseJitEntry(buf);
    try {
      PyUnstable_WritePerfMapEntry(
          std::get<0>(jit_entry),
          std::get<1>(jit_entry),
          std::get<2>(jit_entry));
    } catch (const std::invalid_argument& e) {
      JIT_LOG("Error: Invalid JIT entry: %s \n", buf);
    }
  }
  std::fclose(parent_file);
  return 1;
}

// Copy the JIT entries from the parent perf map file to the child perf map
// file. This is used when perf-trampoline is enabled, as the perf map file
// will also include trampoline entries. We only want to copy the JIT entries.
// Returns 1 on success, and 0 on failure.
int copyJitEntries(const std::string& parent_filename) {
  auto parent_file = std::fopen(parent_filename.c_str(), "r");
  if (parent_file == nullptr) {
    JIT_LOG(
        "Couldn't open %s for reading (%s)",
        parent_filename,
        string_error(errno));
    return 0;
  }

  char buf[1024];
  while (std::fgets(buf, sizeof(buf), parent_file) != nullptr) {
    if (std::strstr(buf, "__CINDER_") != nullptr) {
      buf[strcspn(buf, "\n")] = '\0';
      auto jit_entry = parseJitEntry(buf);
      try {
        PyUnstable_WritePerfMapEntry(
            std::get<0>(jit_entry),
            std::get<1>(jit_entry),
            std::get<2>(jit_entry));
      } catch (const std::invalid_argument& e) {
        JIT_LOG("Error: Invalid JIT entry: %s \n", buf);
      }
    }
  }
  std::fclose(parent_file);
  return 1;
}

bool isPerfTrampolineActive() {
  PyThreadState* tstate = PyThreadState_GET();
  return tstate->interp->eval_frame &&
      tstate->interp->eval_frame != _PyEval_EvalFrameDefault;
}

// Copy the perf pid map from the parent process into a new file for this child
// process.
void copyFileInfo(FileInfo& info) {
  if (info.file == nullptr) {
    return;
  }

  std::fclose(info.file);
  auto parent_filename = info.filename;
  auto child_filename =
      fmt::format(fmt::runtime(info.filename_format), getpid());
  info = {};

  if (parent_filename.starts_with("/tmp/perf-") &&
      parent_filename.ends_with(".map") && isPerfTrampolineActive()) {
    if (!copyJitEntries(parent_filename)) {
      JIT_LOG(
          "Failed to copy JIT entries from %s to %s",
          parent_filename,
          child_filename);
    }
  } else if (
      parent_filename.starts_with("/tmp/perf-") &&
      parent_filename.ends_with(".map") && _PyJIT_IsEnabled()) {
    // The JIT is still enabled: copy the file to allow for more compilation
    // in this process.
    if (!copyJitFile(parent_filename)) {
      JIT_LOG(
          "Failed to copy perf map file from %s to %s",
          parent_filename,
          child_filename);
    }
  } else {
    unlink(child_filename.c_str());
    if (_PyJIT_IsEnabled()) {
      // The JIT is still enabled: copy the file to allow for more compilation
      // in this process.
      if (auto new_pid_map = copyFile(parent_filename, child_filename)) {
        info.filename = child_filename;
        info.file = new_pid_map;
      }
    } else {
      // The JIT has been disabled: hard link the file to save disk space. Don't
      // open it in this process, to avoid messing with the parent's file.
      if (::link(parent_filename.c_str(), child_filename.c_str()) != 0) {
        JIT_LOG(
            "Failed to link %s to %s: %s",
            child_filename,
            parent_filename,
            string_error(errno));
      } else {
        // Poke the file's atime to keep tmpwatch at bay.
        std::FILE* file = std::fopen(parent_filename.c_str(), "r");
        if (file != nullptr) {
          std::fclose(file);
        }
      }
      info.file = nullptr;
      info.filename = "";
    }
  }
}

void copyParentPidMap() {
  copyFileInfo(g_pid_map);
}

void copyJitdumpFile() {
  auto ret = munmap(g_jitdump_mmap_addr, kJitdumpMmapSize);
  JIT_CHECK(
      ret == 0, "marker unmap of jitdump file failed: %s", string_error(errno));

  copyFileInfo(g_jitdump_file);
  if (g_jitdump_file.file == nullptr) {
    return;
  }

  g_jitdump_mmap_addr = mmap(
      nullptr,
      kJitdumpMmapSize,
      PROT_EXEC,
      MAP_PRIVATE,
      fileno(g_jitdump_file.file),
      0);
}

} // namespace

void registerFunction(
    const std::vector<std::pair<void*, std::size_t>>& code_sections,
    const std::string& name,
    const std::string& prefix) {
  ThreadedCompileSerialize guard;

  initFiles();

  for (auto& section_and_size : code_sections) {
    void* code = section_and_size.first;
    std::size_t size = section_and_size.second;
    auto jit_entry = prefix + ":" + name;
    PyUnstable_WritePerfMapEntry(
        static_cast<const void*>(code), size, jit_entry.c_str());
  }

  if (auto file = g_jitdump_file.file) {
    // Make sure no parent or child process writes concurrently.
    ExclusiveFileLock write_lock(file);

    static uint64_t code_index = 0;
    for (auto& section_and_size : code_sections) {
      auto const prefixed_name = prefix + ":" + name;

      void* code = section_and_size.first;
      std::size_t size = section_and_size.second;
      CodeLoadRecord record;
      record.type = JIT_CODE_LOAD;

      record.total_size = sizeof(record) + prefixed_name.size() + 1 + size;
      record.timestamp = getTimestamp();
      record.pid = getpid();
      record.tid = gettid();
      record.vma = record.code_addr = reinterpret_cast<uint64_t>(code);
      record.code_size = size;
      record.code_index = code_index++;

      std::fwrite(&record, sizeof(record), 1, file);
      std::fwrite(prefixed_name.data(), 1, prefixed_name.size() + 1, file);
      std::fwrite(code, 1, size, file);
    }
    std::fflush(file);
  }
}

void afterForkChild() {
  copyParentPidMap();
  copyJitdumpFile();
}

} // namespace jit::perf

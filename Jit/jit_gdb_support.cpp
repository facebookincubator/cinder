// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/jit_gdb_support.h"

#include "Jit/compiler.h"
#include "Jit/log.h"
#include "Jit/util.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>

int g_gdb_support = 0;
int g_gdb_write_elf_objects = 0;
int g_gdb_stubs_support = 0;

/* Begin GDB hook */

/* These definitions must be kept in-sync with those in gdb/gdb/jit.h, which
 * is sadly not exported by GDB, so we have to resort to copypasta :-/ */
typedef enum {
  JIT_NOACTION = 0,
  JIT_REGISTER_FN,
  JIT_UNREGISTER_FN
} JITActions;

typedef struct _jit_code_entry {
  struct _jit_code_entry* next_entry;
  struct _jit_code_entry* prev_entry;
  const char* symfile_addr;
  uint64_t symfile_size;
} JITCodeEntry;

typedef struct {
  uint32_t version;
  // This should be JITActions, but need to be specific about the size.
  uint32_t action_flag;
  JITCodeEntry* relevant_entry;
  JITCodeEntry* first_entry;
} JITDescriptor;

/* This sets up the hook that GDB uses to register new symbols. GDB will set a
 * breakpoint inside of it to grab new symbol information when it's called.
 * Need to make sure it's not optimized away. */
void __attribute__((noinline)) __jit_debug_register_code() {
  __asm("");
};

/* We will add new code entries to the link list rooted here. If the JIT ever
 * becomes multithreaded this will need to be protected by a mutex. */
JITDescriptor __jit_debug_descriptor = {1, JIT_NOACTION, NULL, NULL};

/* End GDB hook */

// Forward declarations.
typedef struct ELFObjectContext ELFObjectContext;
typedef struct ELFObject ELFObject;
static ELFObjectContext* elfctx_new(
    struct jit_string_t* filename,
    int lineno,
    struct jit_string_t* function_name,
    void* code,
    int code_size,
    int stack_size);
static void elfctx_free(ELFObjectContext* ctx);
static void elfctx_build_object(ELFObjectContext* ctx);
static ELFObject* elfctx_get_object_ptr(ELFObjectContext* ctx);
static size_t elfctx_get_object_size(ELFObjectContext* ctx);

static int
register_elf_ctx(ELFObjectContext* ctx, const char* type, void* ptr) {
  elfctx_build_object(ctx);

  // Now we allocate enough space for both the JITCodeEntry and the
  // ELFObject.
  size_t elf_object_size = elfctx_get_object_size(ctx);
  char* raw =
      static_cast<char*>(calloc(1, sizeof(JITCodeEntry) + elf_object_size));
  if (raw == NULL) {
    JIT_DLOG("Failed to allocate space for JITCodeEntry + ELFObject");
    return 0;
  }

  char* elf_object_start = raw + sizeof(JITCodeEntry);

  memcpy(elf_object_start, elfctx_get_object_ptr(ctx), elf_object_size);

  if (g_gdb_write_elf_objects) {
    // Write the ELF object to /tmp
    struct jit_string_t* filename =
        ss_sprintf_alloc("/tmp/cinder_%s_%p_elf", type, ptr);
    int fd;
    if ((fd = open(ss_get_string(filename), O_CREAT | O_RDWR, 0600))) {
      if (write(fd, elf_object_start, elf_object_size) < 0) {
        JIT_DLOG("Failed to write to %s", ss_get_string(filename));
      }
      close(fd);
    }
    ss_free(filename);
  }

  JITCodeEntry* entry = (JITCodeEntry*)raw;

  entry->symfile_addr = elf_object_start;
  entry->symfile_size = elf_object_size;

  // Link into the list.
  entry->prev_entry = NULL;
  JITCodeEntry* next_entry = __jit_debug_descriptor.first_entry;
  entry->next_entry = next_entry;
  if (next_entry) {
    next_entry->prev_entry = entry;
  }
  __jit_debug_descriptor.first_entry = entry;
  __jit_debug_descriptor.relevant_entry = entry;
  __jit_debug_descriptor.action_flag = JIT_REGISTER_FN;

  // Call the registration hook.
  __jit_debug_register_code();

  return 1;
}

int register_raw_debug_symbol(
    const char* function_name,
    const char* filename,
    int lineno,
    void* code_addr,
    size_t code_size,
    size_t stack_size) {
  if (!g_gdb_support) {
    return 1;
  }

  ELFObjectContext* ctx = elfctx_new(
      ss_sprintf_alloc("%s", filename),
      lineno,
      ss_sprintf_alloc("%s", function_name),
      code_addr,
      code_size,
      stack_size);

  if (ctx == NULL) {
    JIT_DLOG("Failed to allocate ELFObjectContext");
    return 0;
  }

  if (!register_elf_ctx(ctx, function_name, code_addr)) {
    elfctx_free(ctx);
    return 0;
  }

  JIT_DLOG(
      "Registered debug symbol at %p (%zd bytes) for %s at %p (%d bytes)",
      reinterpret_cast<void*>(elfctx_get_object_ptr(ctx)),
      elfctx_get_object_size(ctx),
      function_name,
      code_addr,
      code_size);

  elfctx_free(ctx);

  return 1;
}

int register_pycode_debug_symbol(
    PyCodeObject* codeobj,
    const char* fullname,
    jit::CompiledFunction* compiled_func) {
  if (!g_gdb_support) {
    return 1;
  }

  int code_size = compiled_func->codeSize();
  int stack_size = compiled_func->stackSize();
  auto code = reinterpret_cast<void*>(compiled_func->vectorcallEntry());
  if (code_size < 1) {
    JIT_DLOG(
        "Not registering symbol at %p because it has an invalid size %d",
        code,
        code_size);
    return 0;
  }
  if (stack_size == -1) {
    JIT_DLOG(
        "Not registering symbol at %p because it has an invalid "
        "stack size %d",
        code,
        stack_size);
    return 0;
  }

  PyObject* sourcefile = codeobj->co_filename;
  const char* filename = "<unknown>";
  if (sourcefile != NULL && PyUnicode_Check(sourcefile)) {
    filename = PyUnicode_AsUTF8(sourcefile);
    if (filename == NULL) {
      filename = "<filename failed to encode to UTF8>";
      JIT_DLOG("Failed to encode filename for ELFObjectContext");
    }
  }

  ELFObjectContext* ctx = elfctx_new(
      ss_sprintf_alloc("%s", filename),
      codeobj->co_firstlineno,
      ss_sprintf_alloc("%s", fullname),
      code,
      code_size,
      stack_size);

  if (ctx == NULL) {
    JIT_DLOG("Failed to allocate ELFObjectContext");
    return 0;
  }

  if (!register_elf_ctx(ctx, "PyFunctionObject", code)) {
    elfctx_free(ctx);
    return 0;
  }

  JIT_DLOG(
      "Registered debug symbol at %p (%zd bytes) for a function at %p "
      "(%d bytes)",
      reinterpret_cast<void*>(elfctx_get_object_ptr(ctx)),
      elfctx_get_object_size(ctx),
      code,
      code_size);

  elfctx_free(ctx);

  return 1;
}

/* In-memory ELF object generation -- largely borrowed from LuaJIT's
 * implementation. There is much magic here... I've tried to rationalize it a
 * bit and bring it more in line with Facebook's coding standards, but there's
 * only so much that can be done. */

/* ELF definitions. */
typedef struct ELFHeader {
  uint8_t emagic[4];
  uint8_t eclass;
  uint8_t eendian;
  uint8_t eversion;
  uint8_t eosabi;
  uint8_t eabiversion;
  uint8_t epad[7];
  uint16_t type;
  uint16_t machine;
  uint32_t version;
  uintptr_t entry;
  uintptr_t phofs;
  uintptr_t shofs;
  uint32_t flags;
  uint16_t ehsize;
  uint16_t phentsize;
  uint16_t phnum;
  uint16_t shentsize;
  uint16_t shnum;
  uint16_t shstridx;
} ELFHeader;

typedef struct ELFSectionHeader {
  uint32_t name;
  uint32_t type;
  uintptr_t flags;
  uintptr_t addr;
  uintptr_t ofs;
  uintptr_t size;
  uint32_t link;
  uint32_t info;
  uintptr_t align;
  uintptr_t entsize;
} ELFSectionHeader;

#define ELFSECT_IDX_ABS 0xfff1

enum {
  ELFSECT_TYPE_PROGBITS = 1,
  ELFSECT_TYPE_SYMTAB = 2,
  ELFSECT_TYPE_STRTAB = 3,
  ELFSECT_TYPE_NOBITS = 8
};

#define ELFSECT_FLAGS_WRITE 1
#define ELFSECT_FLAGS_ALLOC 2
#define ELFSECT_FLAGS_EXEC 4

typedef struct ELFSymbol {
  uint32_t name;
  uint8_t info;
  uint8_t other;
  uint16_t sectidx;
  uintptr_t value;
  uint64_t size;
} ELFSymbol;

enum {
  ELFSYM_TYPE_FUNC = 2,
  ELFSYM_TYPE_FILE = 4,
  ELFSYM_BIND_LOCAL = 0 << 4,
  ELFSYM_BIND_GLOBAL = 1 << 4,
};

/* DWARF definitions. */
#define DWRF_CIE_VERSION 1

enum {
  DWRF_CFA_nop = 0x0,
  DWRF_CFA_offset_extended = 0x5,
  DWRF_CFA_def_cfa = 0xc,
  DWRF_CFA_def_cfa_offset = 0xe,
  DWRF_CFA_offset_extended_sf = 0x11,
  DWRF_CFA_advance_loc = 0x40,
  DWRF_CFA_offset = 0x80
};

enum { DWRF_EH_PE_udata4 = 3, DWRF_EH_PE_textrel = 0x20 };

enum { DWRF_TAG_compile_unit = 0x11 };

enum { DWRF_children_no = 0, DWRF_children_yes = 1 };

enum {
  DWRF_AT_name = 0x03,
  DWRF_AT_stmt_list = 0x10,
  DWRF_AT_low_pc = 0x11,
  DWRF_AT_high_pc = 0x12
};

enum { DWRF_FORM_addr = 0x01, DWRF_FORM_data4 = 0x06, DWRF_FORM_string = 0x08 };

enum {
  DWRF_LNS_extended_op = 0,
  DWRF_LNS_copy = 1,
  DWRF_LNS_advance_pc = 2,
  DWRF_LNS_advance_line = 3
};

enum { DWRF_LNE_end_sequence = 1, DWRF_LNE_set_address = 2 };

enum {
  /* Yes, the order is strange, but correct. */
  DWRF_REG_AX,
  DWRF_REG_DX,
  DWRF_REG_CX,
  DWRF_REG_BX,
  DWRF_REG_SI,
  DWRF_REG_DI,
  DWRF_REG_BP,
  DWRF_REG_SP,
  DWRF_REG_8,
  DWRF_REG_9,
  DWRF_REG_10,
  DWRF_REG_11,
  DWRF_REG_12,
  DWRF_REG_13,
  DWRF_REG_14,
  DWRF_REG_15,
  DWRF_REG_RA,
};

/* Minimal list of sections for the in-memory ELF object. */
enum {
  ELF_SECT_NULL,
  ELF_SECT_text,
  ELF_SECT_shstrtab,
  ELF_SECT_strtab,
  ELF_SECT_symtab,
  ELF_SECT_debug_info,
  ELF_SECT_debug_abbrev,
  ELF_SECT_debug_line,
  ELF_SECT__MAX
};

enum { ELF_SYM_UNDEF, ELF_SYM_FILE, ELF_SYM_FUNC, ELF_SYM__MAX };

/* In-memory ELF object. */
typedef struct ELFObject {
  ELFHeader hdr; /* ELF header. */
  ELFSectionHeader sect[ELF_SECT__MAX]; /* ELF sections. */
  ELFSymbol sym[ELF_SYM__MAX]; /* ELF symbol table. */
  uint8_t space[4096]; /* Space for various section data. */
} ELFObject;

/* Template for in-memory ELF header. */
static const ELFHeader elfhdr_template = {
    .emagic = {0x7f, 'E', 'L', 'F'},
    .eclass = 2,
    .eendian = 1,
    .eversion = 1,
    .eosabi = 0, /* Nope, it's not 3. */
    .eabiversion = 0,
    .epad = {0, 0, 0, 0, 0, 0, 0},
    .type = 1,
    .machine = 62,
    .version = 1,
    .entry = 0,
    .phofs = 0,
    .shofs = offsetof(ELFObject, sect),
    .flags = 0,
    .ehsize = sizeof(ELFHeader),
    .phentsize = 0,
    .phnum = 0,
    .shentsize = sizeof(ELFSectionHeader),
    .shnum = ELF_SECT__MAX,
    .shstridx = ELF_SECT_shstrtab};

/* Context for generating the ELF object for the GDB JIT API. */
typedef struct ELFObjectContext {
  uint8_t* p; /* Pointer to next address in obj.space. */
  uint8_t* startp; /* Pointer to start address in obj.space. */
  struct jit_string_t* function_name; /* The python function name */
  struct jit_string_t* filename; /* The python file the function came from */
  int lineno; /* The first line of the python definition */
  uintptr_t code_addr; /* Machine code address. */
  uint32_t code_size; /* Size of machine code. */
  uint32_t stack_size; /* Stack adjustment for trace itself. */
  size_t objsize; /* Final size of ELF object. */
  ELFObject obj; /* In-memory ELF object. */
} ELFObjectContext;

/* Append a null-terminated string. */
static uint32_t elfctx_append_string(ELFObjectContext* ctx, const char* str) {
  uint8_t* p = ctx->p;
  uint32_t ofs = (uint32_t)(p - ctx->startp);
  do {
    *p++ = (uint8_t)*str;
  } while (*str++);
  ctx->p = p;
  return ofs;
}

/* Append a SLEB128 value. */
static void elfctx_append_sleb128(ELFObjectContext* ctx, int32_t v) {
  uint8_t* p = ctx->p;
  for (; (uint32_t)(v + 0x40) >= 0x80; v >>= 7) {
    *p++ = (uint8_t)((v & 0x7f) | 0x80);
  }
  *p++ = (uint8_t)(v & 0x7f);
  ctx->p = p;
}

/* Append a ULEB128 to buffer. */
static void elfctx_append_uleb128(ELFObjectContext* ctx, uint32_t v) {
  uint8_t* p = ctx->p;
  for (; v >= 0x80; v >>= 7) {
    *p++ = (char)((v & 0x7f) | 0x80);
  }
  *p++ = (char)v;
  ctx->p = p;
}

/* Shortcuts to generate DWARF structures. */
#define DWRF_U8(x) (*p++ = (x))
#define DWRF_I8(x) (*(int8_t*)p = (x), p++)
#define DWRF_U16(x) (*(uint16_t*)p = (x), p += 2)
#define DWRF_U32(x) (*(uint32_t*)p = (x), p += 4)
#define DWRF_ADDR(x) (*(uintptr_t*)p = (x), p += sizeof(uintptr_t))
#define DWRF_UV(x) (ctx->p = p, elfctx_append_uleb128(ctx, (x)), p = ctx->p)
#define DWRF_SV(x) (ctx->p = p, elfctx_append_sleb128(ctx, (x)), p = ctx->p)
#define DWRF_STR(str) (ctx->p = p, elfctx_append_string(ctx, (str)), p = ctx->p)
#define DWRF_ALIGNNOP(s)           \
  while ((uintptr_t)p & ((s)-1)) { \
    *p++ = DWRF_CFA_nop;           \
  }
#define DWRF_SECTION(name, stmt)                              \
  {                                                           \
    uint32_t* szp_##name = (uint32_t*)p;                      \
    p += 4;                                                   \
    stmt;                                                     \
    *szp_##name = (uint32_t)((p - (uint8_t*)szp_##name) - 4); \
  }

/* Initialize ELF section headers. */
static void elf_secthdr(ELFObjectContext* ctx) {
  ELFSectionHeader* sect;

  *ctx->p++ = '\0'; /* Empty string at start of string table. */

#define SECTDEF(id, tp, al)                        \
  sect = &ctx->obj.sect[ELF_SECT_##id];            \
  sect->name = elfctx_append_string(ctx, "." #id); \
  sect->type = ELFSECT_TYPE_##tp;                  \
  sect->align = (al)

  SECTDEF(text, NOBITS, 16);
  sect->flags = ELFSECT_FLAGS_ALLOC | ELFSECT_FLAGS_EXEC;
  sect->addr = ctx->code_addr;
  sect->ofs = 0;
  sect->size = ctx->code_size;

  /*SECTDEF(eh_frame, PROGBITS, sizeof(uintptr_t));
  sect->flags = ELFSECT_FLAGS_ALLOC;*/

  SECTDEF(shstrtab, STRTAB, 1);
  SECTDEF(strtab, STRTAB, 1);

  SECTDEF(symtab, SYMTAB, sizeof(uintptr_t));
  sect->ofs = offsetof(ELFObject, sym);
  sect->size = sizeof(ctx->obj.sym);
  sect->link = ELF_SECT_strtab;
  sect->entsize = sizeof(ELFSymbol);
  sect->info = ELF_SYM_FUNC;

  SECTDEF(debug_info, PROGBITS, 1);
  SECTDEF(debug_abbrev, PROGBITS, 1);
  SECTDEF(debug_line, PROGBITS, 1);

#undef SECTDEF
}

/* Initialize symbol table. */
static void elf_init_symtab(ELFObjectContext* ctx) {
  ELFSymbol* sym;

  *ctx->p++ = '\0'; /* Empty string at start of string table. */

  sym = &ctx->obj.sym[ELF_SYM_FILE];
  sym->name = elfctx_append_string(ctx, "cinderjit");
  sym->sectidx = ELFSECT_IDX_ABS;
  sym->info = ELFSYM_TYPE_FILE | ELFSYM_BIND_LOCAL;

  sym = &ctx->obj.sym[ELF_SYM_FUNC];
  sym->name = elfctx_append_string(
      ctx,
      ctx->function_name == NULL ? "<unknown>"
                                 : ss_get_string(ctx->function_name));
  sym->sectidx = ELF_SECT_text;
  sym->value = 0;
  sym->size = ctx->code_size;
  sym->info = ELFSYM_TYPE_FUNC | ELFSYM_BIND_GLOBAL;
}

/* Initialize .debug_info section. */
static void elf_init_debuginfo(ELFObjectContext* ctx) {
  uint8_t* p = ctx->p;

  DWRF_SECTION(
      info, DWRF_U16(2); /* DWARF version. */
      DWRF_U32(0); /* Abbrev offset. */
      DWRF_U8(sizeof(uintptr_t)); /* Pointer size. */

      DWRF_UV(1); /* Abbrev #1: DWRF_TAG_compile_unit. */
      DWRF_STR(
          ctx->filename == NULL ? "<unknown>" : ss_get_string(ctx->filename));
      DWRF_ADDR(ctx->code_addr); /* DWRF_AT_low_pc. */
      DWRF_ADDR(ctx->code_addr + ctx->code_size); /* DWRF_AT_high_pc. */
      DWRF_U32(0); /* DWRF_AT_stmt_list. */
  )

  ctx->p = p;
}

/* Initialize .debug_abbrev section. */
static void elf_init_debugabbrev(ELFObjectContext* ctx) {
  uint8_t* p = ctx->p;

  /* Abbrev #1: DWRF_TAG_compile_unit. */
  DWRF_UV(1);
  DWRF_UV(DWRF_TAG_compile_unit);
  DWRF_U8(DWRF_children_no);
  DWRF_UV(DWRF_AT_name);
  DWRF_UV(DWRF_FORM_string);
  DWRF_UV(DWRF_AT_low_pc);
  DWRF_UV(DWRF_FORM_addr);
  DWRF_UV(DWRF_AT_high_pc);
  DWRF_UV(DWRF_FORM_addr);
  DWRF_UV(DWRF_AT_stmt_list);
  DWRF_UV(DWRF_FORM_data4);
  DWRF_U8(0);
  DWRF_U8(0);
  DWRF_U8(0);

  ctx->p = p;
}

#define DWRF_LINE(op, s) \
  (DWRF_U8(DWRF_LNS_extended_op), DWRF_UV(1 + (s)), DWRF_U8((op)))

/* Initialize .debug_line section. */
static void elf_init_debugline(ELFObjectContext* ctx) {
  uint8_t* p = ctx->p;

  DWRF_SECTION(
      line, DWRF_U16(2); /* DWARF version. */
      DWRF_SECTION(header, DWRF_U8(1); /* Minimum instruction length. */
                   DWRF_U8(1); /* is_stmt. */
                   DWRF_I8(0); /* Line base for special opcodes. */
                   DWRF_U8(2); /* Line range for special opcodes. */
                   DWRF_U8(3 + 1); /* Opcode base at DWRF_LNS_advance_line+1. */
                   DWRF_U8(0);
                   DWRF_U8(1);
                   DWRF_U8(1); /* Standard opcode lengths. */
                   /* Directory table. */
                   DWRF_U8(0);
                   /* File name table. */
                   DWRF_STR(
                       ctx->filename == NULL ? "<unknown>"
                                             : ss_get_string(ctx->filename));
                   DWRF_UV(0);
                   DWRF_UV(0);
                   DWRF_UV(0);
                   DWRF_U8(0);)

          DWRF_LINE(DWRF_LNE_set_address, sizeof(uintptr_t));
      DWRF_ADDR(ctx->code_addr);
      if (ctx->lineno) {
        DWRF_U8(DWRF_LNS_advance_line);
        DWRF_SV(ctx->lineno - 1);
      };
      DWRF_U8(DWRF_LNS_copy);
      DWRF_U8(DWRF_LNS_advance_pc);
      DWRF_UV(ctx->code_size);
      DWRF_LINE(DWRF_LNE_end_sequence, 0);)

  ctx->p = p;
}

#undef DWRF_LINE

/* Undef shortcuts. */
#undef DWRF_U8
#undef DWRF_I8
#undef DWRF_U16
#undef DWRF_U32
#undef DWRF_ADDR
#undef DWRF_UV
#undef DWRF_SV
#undef DWRF_STR
#undef DWRF_ALIGNNOP
#undef DWRF_SECTION

/* Type of a section initializer callback. */
typedef void (*ELFSectionInitFn)(ELFObjectContext* ctx);

/* Call section initializer and set the section offset and size. */
static void
elf_init_section(ELFObjectContext* ctx, int sect, ELFSectionInitFn init) {
  ctx->startp = ctx->p;
  ctx->obj.sect[sect].ofs = (uintptr_t)((char*)ctx->p - (char*)&ctx->obj);
  init(ctx);
  ctx->obj.sect[sect].size = (uintptr_t)(ctx->p - ctx->startp);
}

#define ALIGN_SECTION(p, a) \
  ((p) = (uint8_t*)(((uintptr_t)(p) + ((a)-1)) & ~(uintptr_t)((a)-1)))

/* Build in-memory ELF object. */
static void elfctx_build_object(ELFObjectContext* ctx) {
  ELFObject* obj = &ctx->obj;
  /* Fill in ELF header and clear structures. */
  memcpy(&obj->hdr, &elfhdr_template, sizeof(ELFHeader));
  memset(&obj->sect, 0, sizeof(ELFSectionHeader) * ELF_SECT__MAX);
  memset(&obj->sym, 0, sizeof(ELFSymbol) * ELF_SYM__MAX);
  /* Initialize sections. */
  ctx->p = obj->space;
  elf_init_section(ctx, ELF_SECT_shstrtab, elf_secthdr);
  elf_init_section(ctx, ELF_SECT_strtab, elf_init_symtab);
  elf_init_section(ctx, ELF_SECT_debug_info, elf_init_debuginfo);
  elf_init_section(ctx, ELF_SECT_debug_abbrev, elf_init_debugabbrev);
  elf_init_section(ctx, ELF_SECT_debug_line, elf_init_debugline);
  ALIGN_SECTION(ctx->p, sizeof(uintptr_t));
  ctx->objsize = (size_t)((char*)ctx->p - (char*)obj);
  JIT_DCHECK(
      ctx->objsize < sizeof(ELFObject),
      "ELFObject.space overflowed, ctx->objsize is %zd",
      ctx->objsize);
}

#undef ALIGN_SECTION

static ELFObjectContext* elfctx_new(
    struct jit_string_t* filename,
    int lineno,
    struct jit_string_t* function_name,
    void* code,
    int code_size,
    int stack_size) {
  JIT_DCHECK(code_size >= 0, "code_size must be greater than zero");
  JIT_DCHECK(stack_size >= 0, "stack_size must be greater than zero");
  ELFObjectContext* ctx =
      static_cast<ELFObjectContext*>(calloc(1, sizeof(ELFObjectContext)));
  if (ctx == NULL) {
    return NULL;
  }

  ctx->code_addr = (uintptr_t)code;
  ctx->code_size = (uint32_t)code_size;
  ctx->stack_size = (uint32_t)stack_size;
  ctx->filename = filename;
  ctx->lineno = lineno;
  ctx->function_name = function_name;

  return ctx;
}

static void elfctx_free(ELFObjectContext* ctx) {
  ss_free(ctx->filename);
  ss_free(ctx->function_name);
  free(ctx);
}

static ELFObject* elfctx_get_object_ptr(ELFObjectContext* ctx) {
  return &ctx->obj;
}

static size_t elfctx_get_object_size(ELFObjectContext* ctx) {
  return ctx->objsize;
}

#include "Jit/codegen/gen_asm.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include "Python.h"
#include "classloader.h"
#include "frameobject.h"
#include "internal/pycore_pystate.h"

#include "Jit/lir/generator.h"

#include "Jit/codegen/autogen.h"
#include "Jit/codegen/gen_asm_utils.h"
#include "Jit/codegen/postalloc.h"
#include "Jit/codegen/postgen.h"
#include "Jit/codegen/regalloc.h"
#include "Jit/frame.h"
#include "Jit/hir/analysis.h"
#include "Jit/hir/printer.h"
#include "Jit/jit_gdb_support.h"
#include "Jit/jit_rt.h"
#include "Jit/log.h"
#include "Jit/perf_jitdump.h"
#include "Jit/pyjit.h"

using namespace asmjit;
using namespace jit::hir;
using namespace jit::util;

namespace jit {
namespace codegen {

void RestoreOriginalGeneratorRBP(x86::Emitter* as) {
  size_t original_rbp_offset =
      GET_STRUCT_MEMBER_OFFSET(GenDataFooter, originalRbp);
  as->mov(x86::rbp, x86::ptr(x86::rbp, original_rbp_offset));
}

void EmitEpilogueUnlinkFrame(
    x86::Builder* as,
    x86::Gp tstate_r,
    void (*unlink_frame_func)(PyThreadState*),
    void (*unlink_tiny_frame_func)(PyThreadState*),
    FrameMode frameMode) {
  if (tstate_r != x86::rdi) {
    as->mov(x86::rdi, tstate_r);
  }
  auto saved_rax_ptr = x86::ptr(x86::rbp, -8);
  if (frameMode != FrameMode::kNone) {
    as->mov(saved_rax_ptr, x86::rax);
    if (frameMode == FrameMode::kTiny) {
      as->call(reinterpret_cast<uint64_t>(unlink_tiny_frame_func));
    } else {
      as->call(reinterpret_cast<uint64_t>(unlink_frame_func));
    }
    as->mov(x86::rax, saved_rax_ptr);
  }
}

// Scratch register used by the various deopt trampolines.
//
// NB: This MUST be r15. If you change the register you'll also need to change
// the deopt trampoline code that saves all registers.
static const auto deopt_scratch_reg = x86::r15;

JitRuntime* NativeGeneratorFactory::rt = nullptr;
// these functions call int returning functions and convert their output from
// int (32 bits) to uint64_t (64 bits). This is solely because the code
// generator cannot support an operand size other than 64 bits at this moment. A
// future diff will make it support different operand sizes so that this
// function can be removed.
extern "C" uint64_t
_Invoke_PyObject_SetAttr(PyObject* v, PyObject* name, PyObject* value) {
  return PyObject_SetAttr(v, name, value);
}

extern "C" uint64_t
_Invoke_PyObject_SetItem(PyObject* container, PyObject* sub, PyObject* value) {
  return PyObject_SetItem(container, sub, value);
}

class AsmJitException : public std::exception {
 public:
  AsmJitException(Error err, std::string expr, std::string message) noexcept
      : err(err), expr(std::move(expr)), message(std::move(message)) {}

  const char* what() const noexcept override {
    return message.c_str();
  }

  Error const err;
  std::string const expr;
  std::string const message;
};

class ThrowableErrorHandler : public ErrorHandler {
 public:
  void handleError(Error err, const char* message, BaseEmitter*) override {
    throw AsmJitException(err, "<unknown>", message);
  }
};

#define ASM_CHECK_THROW(exp)                         \
  {                                                  \
    auto err = (exp);                                \
    if (err != kErrorOk) {                           \
      auto message = DebugUtils::errorAsString(err); \
      throw AsmJitException(err, #exp, message);     \
    }                                                \
  }

#define ASM_CHECK(exp, what)             \
  {                                      \
    auto err = (exp);                    \
    JIT_CHECK(                           \
        err == kErrorOk,                 \
        "Failed generating %s: %s",      \
        (what),                          \
        DebugUtils::errorAsString(err)); \
  }

#ifdef __ASM_DEBUG
extern "C" void ___debug_helper(const char* name) {
  fprintf(stderr, "Entering %s...\n", name);
}
#endif

Runtime NativeGenerator::s_jit_asm_code_rt_;

constexpr int NUM_REG_ARGS = sizeof(ARGUMENT_REGS) / sizeof(ARGUMENT_REGS[0]);

PhyLocation get_arg_location_phy_location(int arg) {
  if (arg < NUM_REG_ARGS) {
    return ARGUMENT_REGS[arg];
  }

  JIT_CHECK(false, "only six first registers should be used");
  return 0;
}

void* NativeGenerator::GetEntryPoint() {
  if (entry_ != nullptr) {
    // already compiled
    return entry_;
  }

  JIT_CHECK(as_ == nullptr, "x86::Builder should not have been initialized.");

  CodeHolder code;
  code.init(rt_->codeInfo());
  ThrowableErrorHandler eh;
  code.setErrorHandler(&eh);

  as_ = new x86::Builder(&code);

  env_.as = as_;
  env_.hard_exit_label = as_->newLabel();
  env_.gen_resume_entry_label = as_->newLabel();

  CollectOptimizableLoadMethods();
  auto num_lm_caches = env_.optimizable_load_call_methods_.size() / 2;

  auto func = GetFunction();
  auto num_la_caches =
      func->CountInstrs([](const Instr& instr) { return instr.IsLoadAttr(); });
  auto num_sa_caches =
      func->CountInstrs([](const Instr& instr) { return instr.IsStoreAttr(); });
  auto num_lat_caches = func->CountInstrs(
      [](const Instr& instr) { return instr.IsFillTypeAttrCache(); });

  env_.rt = runtime();
  PyCodeObject* code_obj = func->code;
  env_.code_rt = env_.rt->AllocateRuntime(
      func->pyfunc,
      code_obj,
      func->frameMode,
      num_lm_caches,
      num_la_caches,
      num_sa_caches,
      num_lat_caches);

  // Prepare the location for where our arguments will go, these
  // will be initialized in the prologue
  int total_args = func->numArgs();

  for (int i = 0; i < total_args && i < NUM_REG_ARGS; i++) {
    env_.arg_locations.push_back(get_arg_location_phy_location(i));
  }

  jit::lir::LIRGenerator lirgen(GetFunction(), &env_);
  auto lir_func = lirgen.TranslateFunction();

  JIT_LOGIF(
      g_dump_lir,
      "LIR for %s after generation:\n%s",
      GetFunction()->fullname,
      *lir_func);

  PostGenerationRewrite post_gen(lir_func.get(), &env_);
  post_gen.run();

  LinearScanAllocator lsalloc(lir_func.get());
  lsalloc.run();

  env_.spill_size = lsalloc.getSpillSize();
  env_.changed_regs = lsalloc.getChangedRegs();
  env_.exit_label = as_->newLabel();
  env_.exit_for_yield_label = as_->newLabel();
  env_.frame_mode = GetFunction()->frameMode;
  if (GetFunction()->code->co_flags & kCoFlagsAnyGenerator) {
    env_.initial_yield_spill_size_ = lsalloc.initialYieldSpillSize();
  }

  // TODO: need to revisit after the old backend is removed.
  auto setPredefined = [&](const char* name) {
    auto operand = map_get(env_.output_map, name)->output();
    if (lsalloc.isPredefinedUsed(operand)) {
      env_.predefined_.insert(name);
    }
  };

  setPredefined("__asm_extra_args");
  setPredefined("__asm_tstate");

  PostRegAllocRewrite post_rewrite(lir_func.get(), &env_);
  post_rewrite.run();

  lir_func_ = std::move(lir_func);

  JIT_LOGIF(
      g_dump_lir,
      "LIR for %s after register allocation:\n%s",
      GetFunction()->fullname,
      *lir_func_);

  try {
    generateCode(code);
  } catch (const AsmJitException& ex) {
    String s;
    as_->dump(s);
    JIT_CHECK(
        false,
        "Failed to emit code for '%s': '%s' failed with '%s'\n\n"
        "Builder contents on failure:\n%s",
        funcFullname(env_.code_rt->GetFunction()),
        ex.expr,
        ex.message,
        s.data());
  }

  /* After code generation CodeHolder->codeSize() *should* return the actual
   * size of the generated code. This relies on the implementation of
   * JitRuntime::_add and may break in the future.
   */

  JIT_DCHECK(code.codeSize() < INT_MAX, "Code size is larger than INT_MAX");
  compiled_size_ = static_cast<int>(code.codeSize());
  return entry_;
}

int NativeGenerator::GetCompiledFunctionSize() const {
  return compiled_size_;
}

int NativeGenerator::GetCompiledFunctionStackSize() const {
  return env_.fixed_frame_size;
}

int NativeGenerator::GetCompiledFunctionSpillStackSize() const {
  return spill_stack_size_;
}

void NativeGenerator::generateFunctionEntry() {
  as_->push(x86::rbp);
  as_->mov(x86::rbp, x86::rsp);
}

int NativeGenerator::setupFrameAndSaveCallerRegisters() {
  // During execution, the stack looks like this: (items marked with *
  // represent 0 or more words, items marked with ? represent 0 or 1 words,
  // items marked with ^ share the space from the item above):
  //
  // +-----------------------+
  // | * memory arguments    |
  // |   return address      |
  // |   saved rbp           | <-- rbp
  // | * spilled values      |
  // | ? alignment padding   |
  // | * callee-saved regs   |
  // | ? call arg buffer     |
  // | ^ LOAD_METHOD scratch | <-- rsp
  // +-----------------------+

  auto saved_regs = env_.changed_regs & CALLEE_SAVE_REGS;
  int saved_regs_size = saved_regs.count() * 8;
  // Make sure we have at least one word for scratch in the epilogue.
  spill_stack_size_ = env_.spill_size;
  int spill_stack = std::max(spill_stack_size_, 8);

  int load_method_scratch = env_.optimizable_load_call_methods_.empty() ? 0 : 8;
  int arg_buffer_size = std::max(load_method_scratch, env_.max_arg_buffer_size);

  if ((spill_stack + saved_regs_size + arg_buffer_size) % 16 != 0) {
    spill_stack += 8;
  }

  // Allocate stack space and save the size of the function's stack.
  as_->sub(x86::rsp, spill_stack);
  env_.fixed_frame_size = spill_stack + saved_regs_size;

  // Push used callee-saved registers.
  while (!saved_regs.Empty()) {
    as_->push(x86::gpq(saved_regs.GetFirst()));
    saved_regs.RemoveFirst();
  }

  if (arg_buffer_size > 0) {
    as_->sub(x86::rsp, arg_buffer_size);
  }

  return load_method_scratch;
}

x86::Gp get_arg_location(int arg) {
  auto phyloc = get_arg_location_phy_location(arg);

  if (phyloc.is_register()) {
    return x86::gpq(phyloc);
  }

  JIT_CHECK(false, "should only be used with first six args");
}

void NativeGenerator::generateLinkFrame() {
  auto load_args = [&] {
    as_->mov(x86::rdi, reinterpret_cast<intptr_t>(GetFunction()->code.get()));
    as_->mov(
        x86::rsi, reinterpret_cast<intptr_t>(GetFunction()->globals.get()));
  };
  switch (GetFunction()->frameMode) {
    case FrameMode::kNone:
      as_->call(reinterpret_cast<uint64_t>(PyThreadState_Get));
      break;
    case FrameMode::kTiny:
      load_args();
      as_->call(reinterpret_cast<uint64_t>(JITRT_AllocateAndLinkTinyFrame));
      break;
    case FrameMode::kNormal:
      load_args();
      as_->call(reinterpret_cast<uint64_t>(JITRT_AllocateAndLinkFrame));
      break;
  }
  as_->mov(x86::r11, x86::rax); // tstate
}

int hasPrimitiveFirstArg(PyCodeObject* code) {
  _Py_CODEUNIT* rawcode = code->co_rawcode;
  JIT_CHECK(
      _Py_OPCODE(rawcode[0]) == CHECK_ARGS, "expected CHECK_ARGS as 1st arg");
  PyObject* checks = PyTuple_GET_ITEM(code->co_consts, _Py_OPARG(rawcode[0]));
  if (PyTuple_GET_SIZE(checks) &&
      PyLong_AsLong(PyTuple_GET_ITEM(checks, 0)) == 0 &&
      _PyClassLoader_ResolvePrimitiveType(PyTuple_GET_ITEM(checks, 1)) !=
          TYPED_OBJECT) {
    // first arg is a primitive type, don't want to link the normal frame,
    // we can just signal this by passing 0 for nargsf.  It serves no other
    // purpose in linking the frame
    return 1;
  } else {
    return 0;
  }
}

void NativeGenerator::generatePrologue(
    Label correct_arg_count,
    Label native_entry_point) {
  PyCodeObject* code = GetFunction()->code;

  // the generic entry point, including primitive return boxing if needed
  asmjit::BaseNode* entry_cursor = as_->cursor();

  // same as entry_cursor but only set if we are boxing a primitive return
  asmjit::BaseNode* box_entry_cursor = nullptr;

  // start of the "real" generic entry, after the return-boxing wrapper
  asmjit::BaseNode* generic_entry_cursor = nullptr;

  if (func_->returnsPrimitive()) {
    // If we return a primitive, then in the generic (non-static) entry path we
    // need to box it up (since our caller can't handle an actual primitive
    // return). We do this by generating a small wrapper "function" here that
    // just calls the real function and then boxes the return value before
    // returning.
    Label generic_entry = as_->newLabel();
    Label box_done = as_->newLabel();
    jit::hir::Type ret_type = func_->return_type;
    uint64_t box_func;

    generateFunctionEntry();
    as_->call(generic_entry);

    // if there was an error, there's nothing to box
    as_->test(x86::edx, x86::edx);
    as_->je(box_done);

    if (ret_type <= TCInt8) {
      as_->movsx(x86::edi, x86::al);
      box_func = reinterpret_cast<uint64_t>(JITRT_BoxI32);
    } else if (ret_type <= TCUInt8) {
      as_->movzx(x86::edi, x86::al);
      box_func = reinterpret_cast<uint64_t>(JITRT_BoxU32);
    } else if (ret_type <= TCInt16) {
      as_->movsx(x86::edi, x86::ax);
      box_func = reinterpret_cast<uint64_t>(JITRT_BoxI32);
    } else if (ret_type <= TCUInt16) {
      as_->movzx(x86::edi, x86::ax);
      box_func = reinterpret_cast<uint64_t>(JITRT_BoxU32);
    } else if (ret_type <= TCInt32) {
      as_->mov(x86::edi, x86::eax);
      box_func = reinterpret_cast<uint64_t>(JITRT_BoxI32);
    } else if (ret_type <= TCUInt32) {
      as_->mov(x86::edi, x86::eax);
      box_func = reinterpret_cast<uint64_t>(JITRT_BoxU32);
    } else if (ret_type <= TCInt64) {
      as_->mov(x86::rdi, x86::rax);
      box_func = reinterpret_cast<uint64_t>(JITRT_BoxI64);
    } else if (ret_type <= TCUInt64) {
      as_->mov(x86::rdi, x86::rax);
      box_func = reinterpret_cast<uint64_t>(JITRT_BoxU64);
    } else {
      JIT_CHECK(
          false, "unsupported primitive return type %s", ret_type.toString());
    }

    as_->call(box_func);

    as_->bind(box_done);
    as_->leave();
    as_->ret();
    box_entry_cursor = entry_cursor;
    generic_entry_cursor = as_->cursor();
    as_->bind(generic_entry);
  } else {
    generic_entry_cursor = entry_cursor;
  }

  generateFunctionEntry();

  Label setup_frame = as_->newLabel();
  Label argCheck = as_->newLabel();

  const auto args_past_six_reg = x86::r10;

  _PyTypedArgsInfo* typed_arg_checks = nullptr;
  if (code->co_flags & CO_STATICALLY_COMPILED) {
    // If we've been invoked statically we can skip all of the
    // argument checking because we know our args have been
    // provided correctly.  But if we have primitives we need to
    // unbox them from their boxed ints.  We usually get to
    // avoid this by doing direct invokes from JITed code.

    if (_PyClassLoader_HasPrimitiveArgs(code)) {
      typed_arg_checks = _PyClassLoader_GetTypedArgsInfo(code, true);
      JIT_CHECK(typed_arg_checks != nullptr, "OOM on typed arg checks");
      env_.code_rt->addReference((PyObject*)typed_arg_checks);
      Py_DECREF(typed_arg_checks); // reference is now owned by runtime
      as_->mov(x86::r8, reinterpret_cast<uint64_t>(typed_arg_checks));
      as_->call(reinterpret_cast<uint64_t>(
          JITRT_CallStaticallyWithPrimitiveSignature));
      as_->leave();
      as_->ret();
    } else {
      as_->bt(x86::rdx, _Py_VECTORCALL_INVOKED_STATICALLY_BIT_POS);
      as_->jb(setup_frame);
    }
  }

  as_->test(x86::rcx, x86::rcx); // test for kwargs
  if (!((code->co_flags & (CO_VARARGS | CO_VARKEYWORDS)) ||
        code->co_kwonlyargcount)) {
    // If we have varargs or var kwargs we need to dispatch
    // through our helper regardless if kw args are provided to
    // create the var args tuple and dict and free them on exit
    //
    // Similarly, if the function has keyword-only args, we dispatch
    // through the helper to check that they were, in fact, passed via
    // keyword arguments.
    //
    // There's a lot of other things that happen in
    // the helper so there is potentially a lot of room for optimization
    // here.
    as_->je(argCheck);
  }

  // We don't check the length of the kwnames tuple here, normal callers will
  // never pass the empty tuple.  It is possible for odd callers to still pass
  // the empty tuple in which case we'll just go through the slow binding path.
  as_->call(reinterpret_cast<uint64_t>(JITRT_CallWithKeywordArgs));
  as_->leave();
  as_->ret();

  // check that we have a valid number of args
  if (!(code->co_flags & (CO_VARARGS | CO_VARKEYWORDS))) {
    as_->bind(argCheck);
    as_->cmp(x86::edx, GetFunction()->numArgs());

    // We don't have the correct number of arguments. Call a helper to either
    // fix them up with defaults or raise an approprate exception.
    as_->jz(correct_arg_count);
    as_->mov(x86::rcx, GetFunction()->numArgs());
    as_->call(reinterpret_cast<uint64_t>(JITRT_CallWithIncorrectArgcount));
    as_->leave();
    as_->ret();
  }

  as_->bind(correct_arg_count);
  if (code->co_flags & CO_STATICALLY_COMPILED) {
    if (typed_arg_checks == nullptr) {
      // We weren't called statically, but we've now resolved
      // all arguments to fixed offsets.  Validate that the
      // arguments are correctly typed.
      generateStaticMethodTypeChecks(setup_frame);
    } else if (hasPrimitiveFirstArg(code)) {
      as_->mov(x86::rdx, 0);
    }
  }

  env_.addAnnotation("Generic entry", generic_entry_cursor);

  if (box_entry_cursor) {
    env_.addAnnotation(
        "Generic entry (box primitive return)", box_entry_cursor);
  }

  // Args are now validated, setup frame
  auto frame_cursor = as_->cursor();
  as_->bind(setup_frame);
  // Allocate a Python frame.

  // Save and restore incoming args across the call.
  as_->push(x86::rdi); // func
  as_->push(x86::rsi); // args

  generateLinkFrame();

  as_->pop(x86::r10); // args (moved to r10 so we can replace rsi)
  as_->pop(x86::rax); // func

  // Move arguments into their expected registers and then
  // use r10 as the base for additional args.
  int total_args = GetFunction()->numArgs();
  for (int i = 0; i < total_args && i < NUM_REG_ARGS; i++) {
    as_->mov(get_arg_location(i), x86::ptr(x86::r10, i * sizeof(void*)));
  }
  if (total_args >= NUM_REG_ARGS) {
    // load the location of the remaining args, the backend will
    // deal with loading them from here...
    as_->lea(
        args_past_six_reg, x86::ptr(x86::r10, NUM_REG_ARGS * sizeof(void*)));
  }

  // Finally allocate the saved space required for the actual function
  auto native_entry_cursor = as_->cursor();
  as_->bind(native_entry_point);
  setupFrameAndSaveCallerRegisters();

  env_.addAnnotation("Link frame", frame_cursor);
  env_.addAnnotation("Native entry", native_entry_cursor);
}

static void
emitCompare(x86::Builder* as, x86::Gp lhs, void* rhs, x86::Gp scratch) {
  uint64_t rhsi = reinterpret_cast<uint64_t>(rhs);
  if (!fitsInt32(rhsi)) {
    // in shared mode type can be in a high address
    as->mov(scratch, rhsi);
    as->cmp(lhs, scratch);
  } else {
    as->cmp(lhs, rhsi);
  }
}

static long getLocalForCheck(
    BorrowedRef<PyTupleObject> checks,
    Py_ssize_t check_idx,
    BorrowedRef<PyCodeObject> code) {
  long local = PyLong_AsLong(PyTuple_GET_ITEM(checks, check_idx));
  if (local >= 0) {
    return local;
  }
  // A negative value for local indicates that it's a cell
  JIT_CHECK(
      code->co_cell2arg != nullptr,
      "no cell2arg but negative local %ld",
      local);
  long arg = code->co_cell2arg[-1 * (local + 1)];
  JIT_CHECK(arg != CO_CELL_NOT_AN_ARG, "cell not an arg for local %ld", local);
  return arg;
}

void NativeGenerator::generateStaticMethodTypeChecks(Label setup_frame) {
  PyCodeObject* code = GetFunction()->code;
  _Py_CODEUNIT* rawcode = code->co_rawcode;
  JIT_CHECK(
      _Py_OPCODE(rawcode[0]) == CHECK_ARGS, "expected CHECK_ARGS as 1st arg");
  PyObject* checks = PyTuple_GET_ITEM(code->co_consts, _Py_OPARG(rawcode[0]));
  env_.static_arg_typecheck_failed_label = as_->newLabel();
  if (!PyTuple_GET_SIZE(checks)) {
    return;
  }
  auto next_arg = as_->newLabel();
  for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(checks); i += 2) {
    auto check_cursor = as_->cursor();
    long local = getLocalForCheck(checks, i, code);
    PyObject* type_descr = PyTuple_GET_ITEM(checks, i + 1);
    bool last_arg = i == PyTuple_GET_SIZE(checks) - 2;
    int optional;
    PyTypeObject* type =
        _PyClassLoader_ResolveReferenceType(type_descr, &optional);

    JIT_CHECK(
        type != (PyTypeObject*)&PyObject_Type,
        "shouldn't generate type checks for object");

    if (last_arg) {
      // jump to setup frame on last arg
      next_arg = setup_frame;
    }

    as_->mov(x86::r8, x86::ptr(x86::rsi, local * 8)); // load local
    as_->mov(
        x86::r8, x86::ptr(x86::r8, offsetof(PyObject, ob_type))); // load type
    if (optional) {
      // check if the value is None
      emitCompare(as_, x86::r8, Py_TYPE(Py_None), x86::rax);
      as_->je(next_arg);
    }

    // common case: check if we have the exact right type
    emitCompare(as_, x86::r8, type, x86::rax);
    as_->je(next_arg);

    if (type->tp_flags & Py_TPFLAGS_BASETYPE) {
      // We need to check the object's MRO and see if the declared type
      // is present in it.  Technically we don't need to check the last
      // entry that will be object but the code gen is a little bit simpler
      // if we include it.
      Label arg_loop = as_->newLabel();
      as_->mov(x86::r10, reinterpret_cast<uint64_t>(type));

      // PyObject *r8 = r8->tp_mro;
      as_->mov(x86::r8, x86::ptr(x86::r8, offsetof(PyTypeObject, tp_mro)));
      // Py_ssize_t r11 = r8->ob_size;
      as_->mov(x86::r11, x86::ptr(x86::r8, offsetof(PyVarObject, ob_size)));
      // PyObject *r8 = &r8->ob_item[0];
      as_->add(x86::r8, offsetof(PyTupleObject, ob_item));
      // PyObject *r11 = &r8->ob_item[r11];
      as_->lea(x86::r11, x86::ptr(x86::r8, x86::r11, 3));

      as_->bind(arg_loop);
      as_->cmp(x86::ptr(x86::r8), x86::r10);
      as_->je(next_arg);
      as_->add(x86::r8, sizeof(PyObject*));
      as_->cmp(x86::r8, x86::r11);
      as_->jne(arg_loop);
    }

    // no args match, bail to normal vector call to report error
    as_->jmp(env_.static_arg_typecheck_failed_label);
    if (!last_arg) {
      as_->bind(next_arg);
      next_arg = as_->newLabel();
    }
    env_.addAnnotation(
        fmt::format("StaticTypeCheck[{}]", type->tp_name), check_cursor);
    Py_DECREF(type);
  }
}

void NativeGenerator::generateEpilogue(BaseNode* epilogue_cursor) {
  as_->setCursor(epilogue_cursor);

  // now we can use all the caller save registers except for RAX
  FrameMode frameMode = GetFunction()->frameMode;
  as_->bind(env_.exit_label);

  if (GetFunction()->code->co_flags & kCoFlagsAnyGenerator) {
    // Set generator state to "completed". We access the state via RBP which
    // points to the of spill data and bottom of GenDataFooter.
    auto state_offs = GET_STRUCT_MEMBER_OFFSET(GenDataFooter, state);
    as_->mov(
        x86::ptr(x86::rbp, state_offs, sizeof(GenDataFooter::state)),
        _PyJitGenState_Completed);
    as_->bind(env_.exit_for_yield_label);
    RestoreOriginalGeneratorRBP(as_->as<x86::Emitter>());
  }
  EmitEpilogueUnlinkFrame(
      as_, x86::rdi, JITRT_UnlinkFrame, JITRT_UnlinkTinyFrame, frameMode);

  // If we return a primitive, set edx to 1 to indicate no error (in case of
  // error, deopt will set it to 0 and jump to hard_exit_label, skipping this.)
  if (func_->returnsPrimitive()) {
    as_->mov(x86::edx, 1);
  }

  as_->bind(env_.hard_exit_label);

  auto saved_regs = env_.changed_regs & CALLEE_SAVE_REGS;
  if (!saved_regs.Empty()) {
    // Reset rsp to point at our callee-saved registers and restore them.
    JIT_CHECK(env_.fixed_frame_size != -1, "fixed frame size not initialized");
    as_->lea(x86::rsp, x86::ptr(x86::rbp, -env_.fixed_frame_size));

    std::vector<int> pop_regs;
    while (!saved_regs.Empty()) {
      int reg = saved_regs.GetFirst();
      pop_regs.push_back(reg);
      saved_regs.RemoveFirst();
    }
    for (auto riter = pop_regs.rbegin(); riter != pop_regs.rend(); ++riter) {
      as_->pop(x86::gpq(*riter));
    }
  }

  as_->leave();
  as_->ret();

  env_.addAnnotation("Epilogue", epilogue_cursor);
  if (env_.function_indirections.size()) {
    auto jit_helpers = as_->cursor();
    for (auto& x : env_.function_indirections) {
      Label trampoline = as_->newLabel();
      as_->bind(trampoline);
      as_->mov(x86::r10, reinterpret_cast<uint64_t>(x.first));
      as_->jmp(reinterpret_cast<uint64_t>(jit_trampoline_));
      x.second.trampoline = trampoline;
    }
    env_.addAnnotation("JitHelpers", jit_helpers);
  }
}

void NativeGenerator::generateDeoptExits() {
  if (env_.deopt_exits.empty()) {
    return;
  }

  auto& deopt_exits = env_.deopt_exits;

  auto deopt_cursor = as_->cursor();
  auto deopt_exit = as_->newLabel();
  std::sort(deopt_exits.begin(), deopt_exits.end(), [](auto& a, auto& b) {
    return a.deopt_meta_index < b.deopt_meta_index;
  });
  // Generate stage 1 trampolines (one per guard). These push the index of the
  // appropriate `DeoptMetadata` and then jump to the stage 2 trampoline.
  for (const auto& exit : deopt_exits) {
    as_->bind(exit.label);
    as_->push(exit.deopt_meta_index);
    as_->jmp(deopt_exit);
  }
  // Generate the stage 2 trampoline (one per function). This saves the address
  // of the final part of the JIT-epilogue that is responsible for restoring
  // callee-saved registers and returning, our scratch register (since we need
  // it), and jumps to the final trampoline.
  as_->bind(deopt_exit);
  as_->push(deopt_scratch_reg);
  as_->push(deopt_scratch_reg);
  as_->lea(deopt_scratch_reg, x86::ptr(env_.hard_exit_label));
  as_->mov(x86::ptr(x86::rsp, kPointerSize), deopt_scratch_reg);
  auto trampoline = GetFunction()->code->co_flags & kCoFlagsAnyGenerator
      ? deopt_trampoline_generators_
      : deopt_trampoline_;
  as_->mov(deopt_scratch_reg, reinterpret_cast<uint64_t>(trampoline));
  as_->jmp(deopt_scratch_reg);
  env_.addAnnotation("Deoptimization exits", deopt_cursor);
}

void NativeGenerator::generateResumeEntry() {
  // Arbitrary scratch register for use throughout this function. Can be changed
  // to pretty much anything which doesn't conflict with arg registers.
  const auto scratch_r = x86::r8;

  // arg #1 - rdi = PyGenObject* generator
  const auto gen_r = x86::rdi;
  // arg #2 - rsi = PyObject* sent_value
  // arg #3 - rdx = tstate
  // arg #4 - rcx = finish_yield_from
  // Arg regs must not be modified as they may be used by the next resume stage.
  auto cursor = as_->cursor();
  as_->bind(env_.gen_resume_entry_label);

  generateFunctionEntry();
  setupFrameAndSaveCallerRegisters();

  // Setup RBP to use storage in generator rather than stack.

  // Pointer to GenDataFooter. Could be any conflict-free register.
  const auto jit_data_r = x86::r9;

  // jit_data_r = gen->gi_jit_data
  size_t gi_jit_data_offset =
      GET_STRUCT_MEMBER_OFFSET(PyGenObject, gi_jit_data);
  as_->mov(jit_data_r, x86::ptr(gen_r, gi_jit_data_offset));

  // Store linked frame address
  size_t link_address_offset =
      GET_STRUCT_MEMBER_OFFSET(GenDataFooter, linkAddress);
  as_->mov(scratch_r, x86::ptr(x86::rbp));
  as_->mov(x86::ptr(jit_data_r, link_address_offset), scratch_r);

  // Store return address
  size_t return_address_offset =
      GET_STRUCT_MEMBER_OFFSET(GenDataFooter, returnAddress);
  as_->mov(scratch_r, x86::ptr(x86::rbp, 8));
  as_->mov(x86::ptr(jit_data_r, return_address_offset), scratch_r);

  // Store "original" RBP
  size_t original_rbp_offset =
      GET_STRUCT_MEMBER_OFFSET(GenDataFooter, originalRbp);
  as_->mov(x86::ptr(jit_data_r, original_rbp_offset), x86::rbp);

  // RBP = gen->gi_jit_data
  as_->mov(x86::rbp, jit_data_r);

  // Resume generator execution: load and clear yieldPoint, then jump to the
  // resume target.
  size_t yield_point_offset =
      GET_STRUCT_MEMBER_OFFSET(GenDataFooter, yieldPoint);
  as_->mov(scratch_r, x86::ptr(x86::rbp, yield_point_offset));
  as_->mov(x86::qword_ptr(x86::rbp, yield_point_offset), 0);
  size_t resume_target_offset =
      GET_STRUCT_MEMBER_OFFSET(GenYieldPoint, resume_target_);
  as_->jmp(x86::ptr(scratch_r, resume_target_offset));

  env_.addAnnotation("Resume entry point", cursor);
}

void NativeGenerator::generateStaticEntryPoint(
    Label native_entry_point,
    Label static_jmp_location) {
  // Static entry point is the first thing in the method, we'll
  // jump back to hit it so that we have a fixed offset to jump from
  auto static_link_cursor = as_->cursor();
  Label static_entry_point = as_->newLabel();
  as_->bind(static_entry_point);

  generateFunctionEntry();

  // Save incoming args across link call...
  int total_args = GetFunction()->numArgs();

  if (GetFunction()->frameMode != FrameMode::kNone) {
    int pushed_args = std::min(total_args, NUM_REG_ARGS);
    // save native args across frame linkage call
    if (pushed_args % 2 != 0) {
      as_->push(x86::rax);
    }
    for (int i = 0; i < pushed_args; i++) {
      as_->push(get_arg_location(i));
    }

    generateLinkFrame();
    // restore native args across frame linkage
    for (int i = pushed_args - 1; i >= 0; i--) {
      as_->pop(get_arg_location(i));
    }
    if (pushed_args % 2 != 0) {
      as_->pop(x86::rax);
    }
  } else {
    uint64_t tstate =
        reinterpret_cast<uint64_t>(&_PyRuntime.gilstate.tstate_current);
    if (fitsInt32(tstate)) {
      as_->mov(x86::r11, x86::ptr(tstate));
    } else {
      as_->mov(x86::r11, tstate);
      as_->mov(x86::r11, x86::ptr(x86::r11));
    }
  }

  if (total_args > NUM_REG_ARGS) {
    as_->lea(x86::r10, x86::ptr(x86::rbp, 16));
  }
  as_->jmp(native_entry_point);
  env_.addAnnotation("StaticLinkFrame", static_link_cursor);
  auto static_entry_point_cursor = as_->cursor();

  as_->bind(static_jmp_location);
  as_->short_().jmp(static_entry_point);
  env_.addAnnotation("StaticEntryPoint", static_entry_point_cursor);
}

void NativeGenerator::generateCode(CodeHolder& codeholder) {
  // The body must be generated before the prologue to determine how much spill
  // space to allocate.
  auto prologue_cursor = as_->cursor();
  generateAssemblyBody();

  auto epilogue_cursor = as_->cursor();

  as_->setCursor(prologue_cursor);

  Label correct_arg_count = as_->newLabel();
  Label native_entry_point = as_->newLabel();

  PyCodeObject* code = GetFunction()->code;

  Label static_jmp_location = as_->newLabel();

  bool has_static_entry = (code->co_flags & CO_STATICALLY_COMPILED) &&
      !GetFunction()->uses_runtime_func;
  if (has_static_entry) {
    // Setup an entry point for direct static to static
    // calls using the native calling convention
    generateStaticEntryPoint(native_entry_point, static_jmp_location);
  }

  // Setup an entry for when we have the correct number of arguments
  // This will be dispatched back to from JITRT_CallWithIncorrectArgcount and
  // JITRT_CallWithKeywordArgs when we need to perform complicated
  // argument binding.
  auto arg_reentry_cursor = as_->cursor();
  Label correct_args_entry = as_->newLabel();
  as_->bind(correct_args_entry);
  generateFunctionEntry();
  as_->short_().jmp(correct_arg_count);
  env_.addAnnotation("Reentry with processed args", arg_reentry_cursor);

  // Setup the normal entry point that expects that implements the
  // vectorcall convention
  auto entry_label = as_->newLabel();
  as_->bind(entry_label);
  generatePrologue(correct_arg_count, native_entry_point);

  generateEpilogue(epilogue_cursor);

  if (GetFunction()->code->co_flags & kCoFlagsAnyGenerator) {
    generateResumeEntry();
  }

  if (env_.static_arg_typecheck_failed_label.isValid()) {
    auto static_typecheck_cursor = as_->cursor();
    as_->bind(env_.static_arg_typecheck_failed_label);
    as_->call(reinterpret_cast<uint64_t>(JITRT_ReportStaticArgTypecheckErrors));
    as_->leave();
    as_->ret();
    env_.addAnnotation(
        "Static argument typecheck failure stub", static_typecheck_cursor);
  }

  generateDeoptExits();

  ASM_CHECK_THROW(as_->finalize());
  ASM_CHECK_THROW(rt_->add(&entry_, &codeholder));

  // ------------- orig_entry
  // ^
  // | JITRT_STATIC_ENTRY_OFFSET (2 bytes, optional)
  // | JITRT_CALL_REENTRY_OFFSET (6 bytes)
  // v
  // ------------- entry_
  void* orig_entry = entry_;
  if (has_static_entry) {
    JIT_CHECK(
        codeholder.labelOffset(static_jmp_location) ==
            codeholder.labelOffset(entry_label) + JITRT_STATIC_ENTRY_OFFSET,
        "bad static-entry offset %d ",
        codeholder.labelOffset(entry_label) -
            codeholder.labelOffset(static_jmp_location));
  }
  JIT_CHECK(
      codeholder.labelOffset(correct_args_entry) ==
          codeholder.labelOffset(entry_label) + JITRT_CALL_REENTRY_OFFSET,
      "bad re-entry offset");

  entry_ = ((char*)entry_) + codeholder.labelOffset(entry_label);

  for (auto& entry : env_.unresolved_gen_entry_labels) {
    entry.first->setResumeTarget(
        codeholder.labelOffsetFromBase(entry.second) +
        codeholder.baseAddress());
  }

  // After code generation CodeHolder->codeSize() *should* return the actual
  // size of the generated code and associated data. This relies on the
  // implementation of asmjit::JitRuntime::_add and may break in the future.
  JIT_DCHECK(
      codeholder.codeSize() < INT_MAX, "Code size is larger than INT_MAX");
  compiled_size_ = codeholder.codeSize();

  JIT_LOGIF(
      g_disas_funcs,
      "Disassembly for %s\n%s",
      GetFunction()->fullname,
      env_.annotations.disassemble(orig_entry, codeholder));

  for (auto& x : env_.function_indirections) {
    Label trampoline = x.second.trampoline;
    *x.second.indirect =
        (void*)(codeholder.labelOffset(trampoline) + codeholder.baseAddress());
  }

  // For perf, we want only the size of the code, so we get that directly from
  // the .text section.
  perf::registerFunction(
      entry_, codeholder.textSection()->realSize(), GetFunction()->fullname);
}

void NativeGenerator::CollectOptimizableLoadMethods() {
  auto func = GetFunction();
  for (auto& block : func->cfg.blocks) {
    const Instr* candidate = nullptr;

    for (auto& instr : block) {
      auto output = instr.GetOutput();
      if (output == nullptr) {
        continue;
      }

      switch (instr.opcode()) {
        case Opcode::kLoadMethod: {
          candidate = reinterpret_cast<const LoadMethod*>(&instr);
          break;
        }
        case Opcode::kLoadMethodSuper: {
          candidate = reinterpret_cast<const LoadMethodSuper*>(&instr);
          break;
        }
        case Opcode::kCallMethod: {
          if (candidate != nullptr &&
              hir::modelReg(instr.GetOperand(1)) == candidate->GetOutput()) {
            env_.optimizable_load_call_methods_.emplace(candidate);
            env_.optimizable_load_call_methods_.emplace(&instr);
            candidate = nullptr;
          }
          break;
        }
        default: {
          if (candidate != nullptr && output == candidate->GetOutput()) {
            candidate = nullptr;
          }
          break;
        }
      }
    }
  }
}

#ifdef __ASM_DEBUG
const char* NativeGenerator::GetPyFunctionName() const {
  return PyUnicode_AsUTF8(GetFunction()->code->co_name);
}
#endif

bool canLoadStoreAddr(asmjit::x86::Gp reg, int64_t addr) {
  return reg == x86::rax || (addr >= INT32_MIN && addr <= INT32_MAX);
}

static void raiseUnboundLocalError(PyFrameObject* frame, int name_idx) {
  auto co_vars = JITRT_GetVarnameTuple(frame->f_code, &name_idx);
  JIT_CHECK(name_idx >= 0, "Bad name_idx");
  PyObject* exc = PyExc_UnboundLocalError;
  const char* fmt = "local variable '%.200s' referenced before assignment";
  if (co_vars == frame->f_code->co_freevars) {
    exc = PyExc_NameError;
    fmt =
        "free variable '%.200s' referenced before assignment in enclosing "
        "scope";
  }
  format_exc_check_arg(
      PyThreadState_Get(), exc, fmt, PyTuple_GetItem(co_vars, name_idx));
}

static void raiseAttributeError(PyFrameObject* frame, int field_idx) {
  auto code = frame->f_code;
  auto co_consts = code->co_consts;
  PyObject* descr = PyTuple_GetItem(co_consts, field_idx);
  PyObject* name = PyTuple_GetItem(descr, PyTuple_GET_SIZE(descr) - 1);
  PyErr_SetString(PyExc_AttributeError, PyUnicode_AsUTF8(name));
}

static void raiseAttributeErrorNone(PyFrameObject* frame, int name_idx) {
  auto co_names = frame->f_code->co_names;
  PyErr_Format(
      PyExc_AttributeError,
      "'NoneType' object has no attribute '%U'",
      PyTuple_GET_ITEM(co_names, name_idx));
}

static PyFrameObject* allocateFrameForDeopt(
    PyThreadState* tstate,
    CodeRuntime* code_rt) {
  PyCodeObject* co = code_rt->GetCode();
  PyObject* globals = code_rt->GetGlobals();
  PyObject* builtins = code_rt->GetBuiltins();
  Py_INCREF(builtins);
  PyFrameObject* frame =
      _PyFrame_NewWithBuiltins_NoTrack(tstate, co, globals, builtins, NULL);
  JIT_CHECK(frame != nullptr, "failed allocating frame");
  tstate->frame = frame;
  return frame;
}

static PyFrameObject* prepareForDeopt(
    const uint64_t* regs,
    Runtime* runtime,
    std::size_t deopt_idx,
    int* err_occurred) {
  const DeoptMetadata& deopt_meta = runtime->getDeoptMetadata(deopt_idx);
  PyThreadState* tstate = _PyThreadState_UncheckedGet();
  PyFrameObject* frame;
  if (deopt_meta.code_rt->frameMode() == jit::hir::FrameMode::kNone) {
    // TODO(mpage) - Use JIT_MaterializeTopFrame once no-frame mode is fully
    // supported. Until no-frame mode is fully supported, it is not safe to
    // materialize a frame for a function compiled with no-frame mode unless we
    // are about to deopt. In the non-deopt case the JIT-compiled function
    // won't know that it should unlink the frame when it returns.
    frame = allocateFrameForDeopt(tstate, deopt_meta.code_rt);
  } else {
    frame = JIT_MaterializeTopFrame(tstate);
  }
  reifyFrame(frame, deopt_meta, regs);
  if (!PyErr_Occurred()) {
    auto reason = deopt_meta.reason;
    switch (reason) {
      case DeoptReason::kGuardFailure: {
        runtime->guardFailed(deopt_meta);
        break;
      }
      case DeoptReason::kUnhandledNone:
        raiseAttributeErrorNone(frame, deopt_meta.eh_name_index);
        break;
      case DeoptReason::kUnhandledNullField:
        raiseAttributeError(frame, deopt_meta.eh_name_index);
        break;
      case DeoptReason::kUnhandledUnboundLocal:
        raiseUnboundLocalError(frame, deopt_meta.eh_name_index);
        break;
      case DeoptReason::kUnhandledException:
        JIT_CHECK(false, "unhandled exception without error set");
        break;
      case DeoptReason::kRaise:
        // This code mirrors what happens in _PyEval_EvalFrameDefault although
        // I'm not sure how to test it. Not clear it can happen with JIT.
#ifdef NDEBUG
        if (!PyErr_Occurred()) {
          PyErr_SetString(
              PyExc_SystemError, "error return without exception set");
        }
#else
        JIT_CHECK(PyErr_Occurred(), "Error return without exception set");
#endif
        break;
      case jit::DeoptReason::kRaiseStatic:
        JIT_CHECK(false, "Lost exception when raising static exception");
        break;
      case DeoptReason::kReraise:
        PyErr_SetString(PyExc_RuntimeError, "No active exception to reraise");
        break;
    }
  }

  if (deopt_meta.action == DeoptAction::kUnwind) {
    PyTraceBack_Here(frame);

    // Grab f_stacktop and clear it so the partially-cleared stack isn't
    // accessible to destructors running in the loop.
    PyObject** sp = frame->f_stacktop - 1;
    frame->f_stacktop = nullptr;

    // Clear and decref value stack; as in ceval.c at exit_returning label
    for (; sp >= frame->f_valuestack; sp--) {
      Py_XDECREF(*sp);
    }
    JITRT_UnlinkFrame(tstate);
    return nullptr;
  }

  *err_occurred = (deopt_meta.reason != DeoptReason::kGuardFailure);

  return frame;
}

static PyObject* resumeInInterpreter(PyFrameObject* frame, int err_occurred) {
  if (frame->f_gen) {
    (reinterpret_cast<PyGenObject*>(frame->f_gen))->gi_jit_data = NULL;
  }
  PyObject* result = PyEval_EvalFrameEx(frame, err_occurred);
  // The interpreter loop handles unlinking the frame from the execution stack
  // so we just need to decref.
  if (Py_REFCNT(frame) > 1) {
    // If the frame escaped it needs to be tracked
    Py_DECREF(frame);
    if (!_PyObject_GC_IS_TRACKED(frame)) {
      PyObject_GC_Track(frame);
    }
  } else {
    Py_DECREF(frame);
  }
  return result;
}

void* generateDeoptTrampoline(asmjit::JitRuntime& rt, bool generator_mode) {
  CodeHolder code;
  code.init(rt.codeInfo());
  x86::Builder a(&code);
  Annotations annot;

  auto annot_cursor = a.cursor();
  // Save registers.
  //
  // When we get here the stack looks like:
  //
  // +-------------------------+
  // | ...
  // | index of deopt metadata |
  // | address of epilogue     |
  // | r15                     | <-- rsp
  // +-------------------------+
  a.push(x86::r14);
  a.push(x86::r13);
  a.push(x86::r12);
  a.push(x86::r11);
  a.push(x86::r10);
  a.push(x86::r9);
  a.push(x86::r8);
  a.push(x86::rdi);
  a.push(x86::rsi);
  a.push(x86::rbp);
  a.push(x86::rsp);
  a.push(x86::rbx);
  a.push(x86::rdx);
  a.push(x86::rcx);
  a.push(x86::rax);
  annot.add("saveRegisters", &a, annot_cursor);

  if (generator_mode) {
    // Restore original RBP for use in epilogue.
    RestoreOriginalGeneratorRBP(a.as<x86::Emitter>());
  }

  // Prep the frame for evaluation in the interpreter.
  //
  // We pass the array of saved registers, a pointer to the runtime, and the
  // index of deopt metadata
  annot_cursor = a.cursor();
  a.mov(x86::rdi, x86::rsp);
  a.mov(x86::rsi, reinterpret_cast<uint64_t>(NativeGenerator::runtime()));
  auto deopt_meta_addr =
      x86::ptr(x86::rsp, (PhyLocation::NUM_GP_REGS + 1) * kPointerSize);
  a.mov(x86::rdx, deopt_meta_addr);
  // We no longer need the index of the deopt metadata after prepareForDeopt
  // returns, so we reuse the space on the stack to store whether or not we're
  // deopting into a except/finally block.
  a.lea(x86::rcx, deopt_meta_addr);
  a.call(reinterpret_cast<uint64_t>(prepareForDeopt));

  // If we return a primitive and prepareForDeopt returned null, we need that
  // null in edx to signal error to our caller. Since this trampoline is shared,
  // we do this move unconditionally, but even if not needed, it's harmless. (To
  // eliminate it, we'd need another trampoline specifically for deopt of
  // primitive-returning functions, just to do this one move.)
  a.mov(x86::edx, x86::eax);

  // Clean up saved registers.
  //
  // We have to restore our scratch register manually since it's callee-saved
  // and the stage 2 trampoline used it to hold the address of this
  // trampoline. We can't rely on the JIT epilogue to restore it for us, as the
  // JIT-compiled code may not have used it.
  a.add(x86::rsp, (PhyLocation::NUM_GP_REGS - 1) * kPointerSize);
  a.pop(deopt_scratch_reg);
  annot.add("prepareForDeopt", &a, annot_cursor);

  // Resume execution in the interpreter if we are not unwinding.
  annot_cursor = a.cursor();
  auto done = a.newLabel();
  a.test(x86::rax, x86::rax);
  a.jz(done);
  a.mov(x86::rdi, x86::rax);
  // This is the same stack location as `deopt_meta_addr` above.
  a.mov(x86::rsi, x86::ptr(x86::rsp, kPointerSize));
  a.call(reinterpret_cast<uint64_t>(resumeInInterpreter));
  annot.add("resumeInInterpreter", &a, annot_cursor);

  // Now we're done. Get the address of the epilogue and jump there.
  annot_cursor = a.cursor();
  a.bind(done);
  a.pop(x86::rdi);
  a.jmp(x86::rdi);
  annot.add("jumpToRealEpilogue", &a, annot_cursor);

  auto name =
      generator_mode ? "deopt_trampoline_generators" : "deopt_trampoline";
  void* result{nullptr};
  ASM_CHECK(a.finalize(), name);
  ASM_CHECK(rt.add(&result, &code), name);
  JIT_LOGIF(
      g_disas_funcs,
      "Disassembly for %s\n%s",
      name,
      annot.disassemble(result, code));

  auto code_size = code.textSection()->realSize();
  register_raw_debug_symbol(name, __FILE__, __LINE__, result, code_size, 0);
  perf::registerFunction(result, code_size, name);

  return result;
}

void* generateJitTrampoline(asmjit::JitRuntime& rt) {
  CodeHolder code;
  code.init(rt.codeInfo());
  x86::Builder a(&code);
  Annotations annot;

  auto annot_cursor = a.cursor();

  a.push(x86::rbp);
  a.mov(x86::rbp, x86::rsp);
  // save space for compiled out arg, and keep stack 16-byte aligned
  a.sub(x86::rsp, sizeof(void*) * 2);

  // save incoming arg registers
  const int saved_reg_count = 6;
  a.push(x86::r9);
  a.push(x86::r8);
  a.push(x86::rcx);
  a.push(x86::rdx);
  a.push(x86::rsi);
  a.push(x86::rdi);

  annot.add("saveRegisters", &a, annot_cursor);

  // r10 contains the function object from our stub
  a.mov(x86::rdi, x86::r10);
  a.mov(x86::rsi, x86::rsp);
  a.lea(
      x86::rdx,
      x86::ptr(
          x86::rsp, sizeof(void*) * saved_reg_count)); // compiled indicator

  a.call(reinterpret_cast<uint64_t>(JITRT_CompileFunction));
  a.cmp(x86::byte_ptr(x86::rsp, sizeof(void*) * saved_reg_count), 0);
  auto compile_failed = a.newLabel();
  a.je(compile_failed);

  // restore registers, and jump to JITed code
  a.pop(x86::rdi);
  a.pop(x86::rsi);
  a.pop(x86::rdx);
  a.pop(x86::rcx);
  a.pop(x86::r8);
  a.pop(x86::r9);
  a.leave();
  a.jmp(x86::rax);

  auto name = "JitTrampoline";
  a.bind(compile_failed);
  a.leave();
  a.ret();
  ASM_CHECK(a.finalize(), name);
  void* result{nullptr};
  ASM_CHECK(rt.add(&result, &code), name);

  JIT_LOGIF(
      g_disas_funcs,
      "Disassembly for %s\n%s",
      name,
      annot.disassemble(result, code));

  auto code_size = code.textSection()->realSize();
  register_raw_debug_symbol(name, __FILE__, __LINE__, result, code_size, 0);
  perf::registerFunction(result, code_size, name);

  return result;
}

void NativeGenerator::generateAssemblyBody() {
  auto as = env_.as;
  auto& blocks = lir_func_->basicblocks();
  for (auto& basicblock : blocks) {
    env_.block_label_map.emplace(basicblock, as->newLabel());
  }

  for (lir::BasicBlock* basicblock : blocks) {
    as->bind(map_get(env_.block_label_map, basicblock));
    for (auto& instr : basicblock->instructions()) {
      asmjit::BaseNode* cursor = as->cursor();
      autogen::AutoTranslator::getInstance().translateInstr(&env_, instr.get());
      if (instr->origin() != nullptr) {
        env_.addAnnotation(instr.get(), cursor);
      }
    }
  }
}

bool NativeGenerator::isPredefinedUsed(const char* name) {
  return env_.predefined_.count(name);
}

} // namespace codegen
} // namespace jit

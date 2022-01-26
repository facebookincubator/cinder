// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/codegen/gen_asm.h"

#include "Python.h"
#include "classloader.h"
#include "frameobject.h"
#include "internal/pycore_pystate.h"
#include "internal/pycore_shadow_frame.h"

#include "Jit/codegen/autogen.h"
#include "Jit/codegen/gen_asm_utils.h"
#include "Jit/codegen/postalloc.h"
#include "Jit/codegen/postgen.h"
#include "Jit/codegen/regalloc.h"
#include "Jit/frame.h"
#include "Jit/hir/analysis.h"
#include "Jit/hir/hir.h"
#include "Jit/hir/printer.h"
#include "Jit/jit_gdb_support.h"
#include "Jit/jit_rt.h"
#include "Jit/lir/dce.h"
#include "Jit/lir/generator.h"
#include "Jit/log.h"
#include "Jit/perf_jitdump.h"
#include "Jit/pyjit.h"
#include "Jit/util.h"

#include <fmt/format.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <list>
#include <unordered_map>
#include <vector>

using namespace asmjit;
using namespace jit::hir;
using namespace jit::util;

namespace jit {
namespace codegen {

namespace {

namespace shadow_frame {
static constexpr int kFrameSize = sizeof(_PyShadowFrame);
// Shadow stack frames appear at the beginning of native frames for jitted
// functions
static constexpr x86::Mem kFramePtr = x86::ptr(x86::rbp, -kFrameSize);
#define FIELD_OFF(field)              \
  -kFrameSize + int {                 \
    (offsetof(_PyShadowFrame, field)) \
  }
static constexpr x86::Mem kInFramePrevPtr = x86::ptr(x86::rbp, FIELD_OFF(prev));
static constexpr x86::Mem kInFrameDataPtr = x86::ptr(x86::rbp, FIELD_OFF(data));
#undef FIELD_OFF

static constexpr x86::Mem getStackTopPtr(x86::Gp tstate_reg) {
  return x86::ptr(tstate_reg, offsetof(PyThreadState, shadow_frame));
}
} // namespace shadow_frame

} // namespace

void RestoreOriginalGeneratorRBP(x86::Emitter* as) {
  size_t original_rbp_offset = offsetof(GenDataFooter, originalRbp);
  as->mov(x86::rbp, x86::ptr(x86::rbp, original_rbp_offset));
}

void NativeGenerator::generateEpilogueUnlinkFrame(
    x86::Gp tstate_r,
    bool is_generator) {
  // It's safe to use caller saved registers in this function
  auto scratch_reg = tstate_r == x86::rsi ? x86::rdx : x86::rsi;
  x86::Mem shadow_stack_top_ptr = shadow_frame::getStackTopPtr(tstate_r);

  // Check bit 0 of _PyShadowFrame::data to see if a frame needs
  // unlinking. This bit will be set (pointer kind == PYSF_PYFRAME) if so.
  // scratch_reg = tstate->shadow_frame
  as_->mov(scratch_reg, shadow_stack_top_ptr);
  static_assert(
      PYSF_PYFRAME == 1 && _PyShadowFrame_NumPtrKindBits == 2,
      "Unexpected constants");
  as_->bt(x86::qword_ptr(scratch_reg, offsetof(_PyShadowFrame, data)), 0);

  // Unlink shadow frame. The send implementation handles unlinking these for
  // generators.
  if (!is_generator) {
    // tstate->shadow_frame = ((_PyShadowFrame*)scratch_reg)->prev
    as_->mov(
        scratch_reg,
        x86::qword_ptr(scratch_reg, offsetof(_PyShadowFrame, prev)));
    as_->mov(shadow_stack_top_ptr, scratch_reg);
  }

  // Unlink PyFrame if needed
  asmjit::Label done = as_->newLabel();
  as_->jnc(done);
  auto saved_rax_ptr = x86::ptr(x86::rbp, -8);
  as_->mov(saved_rax_ptr, x86::rax);
  if (tstate_r != x86::rdi) {
    as_->mov(x86::rdi, tstate_r);
  }
  as_->call(reinterpret_cast<uint64_t>(JITRT_UnlinkFrame));
  as_->mov(x86::rax, saved_rax_ptr);
  as_->bind(done);
}

// Scratch register used by the various deopt trampolines.
//
// NB: This MUST be r15. If you change the register you'll also need to change
// the deopt trampoline code that saves all registers.
static const auto deopt_scratch_reg = x86::r15;

JitRuntime* NativeGeneratorFactory::rt = nullptr;
Runtime* NativeGeneratorFactory::s_jit_asm_code_rt_ = nullptr;

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
  auto num_lat_caches = func->env.numLoadAttrCaches();

  env_.rt = NativeGeneratorFactory::runtime();
  PyCodeObject* code_obj = func->code;
  env_.code_rt = env_.rt->allocateCodeRuntime(
      code_obj,
      GetFunction()->globals,
      func->frameMode,
      num_lm_caches,
      num_la_caches,
      num_sa_caches,
      num_lat_caches);

  // TODO(bsimmers): If we have a good reason to violate this JIT_CHECK(), we
  // could transfer the references to the CodeRuntime instead.
  JIT_CHECK(
      GetFunction()->env.references().empty(),
      "Environment should not contain any references");

  jit::lir::LIRGenerator lirgen(GetFunction(), &env_);
  auto lir_func = lirgen.TranslateFunction();

  JIT_LOGIF(
      g_dump_lir,
      "LIR for %s after generation:\n%s",
      GetFunction()->fullname,
      *lir_func);

  PostGenerationRewrite post_gen(lir_func.get(), &env_);
  post_gen.run();

  eliminateDeadCode(lir_func.get());

  LinearScanAllocator lsalloc(lir_func.get(), frame_header_size_);
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
        GetFunction()->fullname,
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
  env_.code_rt->set_frame_size(env_.frame_size);
  return entry_;
}

int NativeGenerator::GetCompiledFunctionSize() const {
  return compiled_size_;
}

int NativeGenerator::GetCompiledFunctionStackSize() const {
  return env_.frame_size;
}

int NativeGenerator::GetCompiledFunctionSpillStackSize() const {
  return spill_stack_size_;
}

void NativeGenerator::generateFunctionEntry() {
  as_->push(x86::rbp);
  as_->mov(x86::rbp, x86::rsp);
}

void NativeGenerator::loadTState(x86::Gp dst_reg) {
  uint64_t tstate =
      reinterpret_cast<uint64_t>(&_PyRuntime.gilstate.tstate_current);
  if (fitsInt32(tstate)) {
    as_->mov(dst_reg, x86::ptr(tstate));
  } else {
    as_->mov(dst_reg, tstate);
    as_->mov(dst_reg, x86::ptr(dst_reg));
  }
}

void NativeGenerator::linkOnStackShadowFrame(
    x86::Gp tstate_reg,
    x86::Gp scratch_reg) {
  const jit::hir::Function* func = GetFunction();
  jit::hir::FrameMode frame_mode = func->frameMode;
  using namespace shadow_frame;
  x86::Mem shadow_stack_top_ptr = getStackTopPtr(tstate_reg);
  // Save old top of shadow stack
  as_->mov(scratch_reg, shadow_stack_top_ptr);
  as_->mov(kInFramePrevPtr, scratch_reg);
  // Set data
  if (frame_mode == jit::hir::FrameMode::kNormal) {
    as_->mov(scratch_reg, x86::ptr(tstate_reg, offsetof(PyThreadState, frame)));
    static_assert(
        PYSF_PYFRAME == 1 && _PyShadowFrame_NumPtrKindBits == 2,
        "Unexpected constant");
    as_->bts(scratch_reg, 0);
  } else {
    uintptr_t data =
        _PyShadowFrame_MakeData(env_.code_rt, PYSF_CODE_RT, PYSF_JIT);
    as_->mov(scratch_reg, data);
  }
  as_->mov(kInFrameDataPtr, scratch_reg);
  // Set our shadow frame as top of shadow stack
  as_->lea(scratch_reg, kFramePtr);
  as_->mov(shadow_stack_top_ptr, scratch_reg);
}

void NativeGenerator::initializeFrameHeader(
    x86::Gp tstate_reg,
    x86::Gp scratch_reg) {
  // Save pointer to the CodeRuntime
  // TODO(mpage) - This is only necessary in the prologue when in normal-frame
  // mode. We can lazily fill this when the frame is materialized in
  // shadow-frame mode. Not sure if the added complexity is worth the two
  // instructions we would save...
  as_->mov(scratch_reg, reinterpret_cast<uintptr_t>(env_.code_rt));
  as_->mov(
      x86::ptr(
          x86::rbp,
          -static_cast<int>(offsetof(FrameHeader, code_rt)) - kPointerSize),
      scratch_reg);
  // Generator shadow frames live in generator objects and only get linked in
  // on the first resume.
  if (!isGen()) {
    linkOnStackShadowFrame(tstate_reg, scratch_reg);
  }
}

int NativeGenerator::setupFrameAndSaveCallerRegisters(x86::Gp tstate_reg) {
  // During execution, the stack looks like the diagram below. The column to
  // left indicates how many words on the stack each line occupies.
  //
  // Legend:
  //  - <empty> - 1 word
  //  - N       - A fixed number of words > 1
  //  - *       - 0 or more words
  //  - ?       - 0 or 1 words
  //  - ^       - shares the space with the item above
  //
  // +-----------------------+
  // | * memory arguments    |
  // |   return address      |
  // |   saved rbp           | <-- rbp
  // | N frame header        | See frame.h
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
  int spill_stack = std::max(spill_stack_size_, 8) + frame_header_size_;

  int load_method_scratch = env_.optimizable_load_call_methods_.empty() ? 0 : 8;
  int arg_buffer_size = std::max(load_method_scratch, env_.max_arg_buffer_size);

  if ((spill_stack + saved_regs_size + arg_buffer_size) % 16 != 0) {
    spill_stack += 8;
  }

  // Allocate stack space and save the size of the function's stack.
  as_->sub(x86::rsp, spill_stack);
  env_.last_callee_saved_reg_off = spill_stack + saved_regs_size;

  x86::Gp scratch_reg = x86::rax;
  as_->push(scratch_reg);
  initializeFrameHeader(tstate_reg, scratch_reg);
  as_->pop(scratch_reg);

  // Push used callee-saved registers.
  while (!saved_regs.Empty()) {
    as_->push(x86::gpq(saved_regs.GetFirst()));
    saved_regs.RemoveFirst();
  }

  if (arg_buffer_size > 0) {
    as_->sub(x86::rsp, arg_buffer_size);
  }

  env_.frame_size = spill_stack + saved_regs_size + arg_buffer_size;

  return load_method_scratch;
}

x86::Gp get_arg_location(int arg) {
  auto phyloc = get_arg_location_phy_location(arg);

  if (phyloc.is_register()) {
    return x86::gpq(phyloc);
  }

  JIT_CHECK(false, "should only be used with first six args");
}

void NativeGenerator::loadOrGenerateLinkFrame(
    asmjit::x86::Gp tstate_reg,
    const std::vector<std::pair<asmjit::x86::Gp, asmjit::x86::Gp>>& save_regs) {
  auto load_tstate_and_move = [&]() {
    loadTState(tstate_reg);
    for (const auto& pair : save_regs) {
      if (pair.first != pair.second) {
        as_->mov(pair.second, pair.first);
      }
    }
  };

  if (isGen()) {
    load_tstate_and_move();
    return;
  }

  switch (GetFunction()->frameMode) {
    case FrameMode::kShadow:
      load_tstate_and_move();
      break;
    case FrameMode::kNormal: {
      bool align_stack = save_regs.size() % 2;
      for (const auto& pair : save_regs) {
        as_->push(pair.first);
      }
      if (align_stack) {
        as_->push(x86::rax);
      }

      as_->mov(x86::rdi, reinterpret_cast<intptr_t>(GetFunction()->code.get()));
      as_->mov(
          x86::rsi, reinterpret_cast<intptr_t>(GetFunction()->globals.get()));

      as_->call(reinterpret_cast<uint64_t>(JITRT_AllocateAndLinkFrame));
      as_->mov(tstate_reg, x86::rax);

      if (align_stack) {
        as_->pop(x86::rax);
      }
      for (auto iter = save_regs.rbegin(); iter != save_regs.rend(); ++iter) {
        as_->pop(iter->second);
      }
      break;
    }
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

  bool returns_primitive = func_->returnsPrimitive();
  bool returns_double = func_->returnsPrimitiveDouble();

  if (returns_primitive) {
    // If we return a primitive, then in the generic (non-static) entry path we
    // need to box it up (since our caller can't handle an actual primitive
    // return). We do this by generating a small wrapper "function" here that
    // just calls the real function and then boxes the return value before
    // returning.
    Label generic_entry = as_->newLabel();
    Label box_done = as_->newLabel();
    Label error = as_->newLabel();
    jit::hir::Type ret_type = func_->return_type;
    Annotations annot;
    uint64_t box_func;

    bool returns_enum = ret_type <= TCEnum;

    generateFunctionEntry();
    if (returns_enum) {
      as_->push(x86::rdx);
      as_->push(0xdeadbeef); // extra push to maintain alignment
      annot.add("saveRegisters", as_, entry_cursor);
    }
    as_->call(generic_entry);

    // if there was an error, there's nothing to box
    if (returns_double) {
      as_->ptest(x86::xmm1, x86::xmm1);
      as_->je(error);
    } else if (returns_enum) {
      as_->test(x86::edx, x86::edx);
      as_->je(error);
    } else {
      as_->test(x86::edx, x86::edx);
      as_->je(box_done);
    }

    if (ret_type <= TCBool) {
      as_->movzx(x86::edi, x86::al);
      box_func = reinterpret_cast<uint64_t>(JITRT_BoxBool);
    } else if (ret_type <= TCInt8) {
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
    } else if (returns_double) {
      // xmm0 already contains the return value
      box_func = reinterpret_cast<uint64_t>(JITRT_BoxDouble);
    } else if (returns_enum) {
      as_->mov(x86::rdi, x86::rax);

      Label box_int = as_->newLabel();
      as_->pop(x86::rdx);
      as_->pop(x86::rdx);
      as_->bt(x86::rdx, _Py_VECTORCALL_INVOKED_STATICALLY_BIT_POS);
      as_->jb(box_int);

      as_->mov(x86::rsi, reinterpret_cast<uint64_t>(ret_type.typeSpec()));
      as_->call(reinterpret_cast<uint64_t>(JITRT_BoxEnum));
      as_->jmp(box_done);

      as_->bind(box_int);
      box_func = reinterpret_cast<uint64_t>(JITRT_BoxI64);
    } else {
      JIT_CHECK(
          false, "unsupported primitive return type %s", ret_type.toString());
    }

    as_->call(box_func);

    as_->bind(box_done);
    as_->leave();
    as_->ret();

    if (returns_double) {
      as_->bind(error);
      as_->xor_(x86::rax, x86::rax);
      as_->leave();
      as_->ret();
    } else if (returns_enum) {
      as_->bind(error);
      as_->pop(x86::rdx);
      as_->pop(x86::rdx);
      as_->leave();
      as_->ret();
    }

    box_entry_cursor = entry_cursor;
    generic_entry_cursor = as_->cursor();
    as_->bind(generic_entry);
  } else {
    generic_entry_cursor = entry_cursor;
  }

  generateFunctionEntry();

  Label setup_frame = as_->newLabel();
  Label argCheck = as_->newLabel();

  if (code->co_flags & CO_STATICALLY_COMPILED) {
    // If we've been invoked statically we can skip all of the
    // argument checking because we know our args have been
    // provided correctly.  But if we have primitives we need to
    // unbox them from their boxed ints.  We usually get to
    // avoid this by doing direct invokes from JITed code.
    if (func_->has_primitive_args) {
      env_.code_rt->addReference(func_->prim_args_info);
      as_->mov(
          x86::r8, reinterpret_cast<uint64_t>(func_->prim_args_info.get()));
      as_->call(reinterpret_cast<uint64_t>(
          JITRT_CallStaticallyWithPrimitiveSignature));
      as_->leave();
      as_->ret();
    } else {
      as_->bt(x86::rdx, _Py_VECTORCALL_INVOKED_STATICALLY_BIT_POS);
      as_->jb(setup_frame);
    }
  }

  if (!func_->has_primitive_args) {
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
    // the empty tuple in which case we'll just go through the slow binding
    // path.
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
      as_->call(
          (returns_double
               ? reinterpret_cast<uint64_t>(
                     JITRT_CallWithIncorrectArgcountFPReturn)
               : reinterpret_cast<uint64_t>(JITRT_CallWithIncorrectArgcount)));
      as_->leave();
      as_->ret();
    }
  }

  as_->bind(correct_arg_count);
  if (code->co_flags & CO_STATICALLY_COMPILED) {
    if (!func_->has_primitive_args) {
      // We weren't called statically, but we've now resolved
      // all arguments to fixed offsets.  Validate that the
      // arguments are correctly typed.
      generateStaticMethodTypeChecks(setup_frame);
    } else if (func_->has_primitive_first_arg) {
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

  constexpr auto kFuncPtrReg = x86::rax;
  constexpr auto kArgsReg = x86::r10;
  constexpr auto kArgsPastSixReg = kArgsReg;

  loadOrGenerateLinkFrame(
      x86::r11,
      {
          {x86::rdi, kFuncPtrReg}, // func
          {x86::rsi, kArgsReg} // args
      });

  // Move arguments into their expected registers and then
  // use r10 as the base for additional args.
  int total_args = GetFunction()->numArgs();
  for (int i = 0; i < total_args && i < NUM_REG_ARGS; i++) {
    as_->mov(get_arg_location(i), x86::ptr(kArgsReg, i * sizeof(void*)));
  }
  if (total_args >= NUM_REG_ARGS) {
    // load the location of the remaining args, the backend will
    // deal with loading them from here...
    as_->lea(kArgsPastSixReg, x86::ptr(kArgsReg, NUM_REG_ARGS * sizeof(void*)));
  }

  // Finally allocate the saved space required for the actual function
  auto native_entry_cursor = as_->cursor();
  as_->bind(native_entry_point);

  setupFrameAndSaveCallerRegisters(x86::r11);

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

void NativeGenerator::generateStaticMethodTypeChecks(Label setup_frame) {
  // JITRT_CallWithIncorrectArgcount uses the fact that our checks are set up
  // from last to first argument - we order the jumps so that the common case of
  // no defaulted arguments comes first, and end up with the following
  // structure: generic entry: compare defaulted arg count to 0 if zero: go to
  // first check compare defaulted arg count to 1 if zero: go to second check
  // ...
  // This is complicated a bit by the fact that not every argument will have a
  // check, as we elide the dynamic ones. For that, we do bookkeeping and assign
  // all defaulted arg counts up to the next local to the same label.
  const std::vector<TypedArgument>& checks = GetFunction()->typed_args;
  env_.static_arg_typecheck_failed_label = as_->newLabel();
  if (!checks.size()) {
    return;
  }
  // We build a vector of labels corresponding to [first_check, second_check,
  // ..., setup_frame] which will have |checks| + 1 elements, and the
  // first_check label will precede the first check.
  auto table_label = as_->newLabel();
  as_->lea(x86::r8, x86::ptr(table_label));
  as_->lea(x86::r8, x86::ptr(x86::r8, x86::rcx, 3));
  as_->jmp(x86::r8);
  auto jump_table_cursor = as_->cursor();
  as_->align(AlignMode::kAlignCode, 8);
  as_->bind(table_label);
  std::vector<Label> arg_labels;
  int defaulted_arg_count = 0;
  Py_ssize_t check_index = checks.size() - 1;
  // Each check might be a label that hosts multiple arguments, as dynamic
  // arguments aren't checked. We need to account for this in our bookkeeping.
  auto next_arg = as_->newLabel();
  arg_labels.emplace_back(next_arg);
  while (defaulted_arg_count < GetFunction()->numArgs()) {
    as_->align(AlignMode::kAlignCode, 8);
    as_->jmp(next_arg);

    if (check_index >= 0) {
      long local = checks.at(check_index).locals_idx;
      if (GetFunction()->numArgs() - defaulted_arg_count - 1 == local) {
        if (check_index == 0) {
          next_arg = setup_frame;
        } else {
          check_index--;
          next_arg = as_->newLabel();
        }
        arg_labels.emplace_back(next_arg);
      }
    }

    defaulted_arg_count++;
  }
  env_.addAnnotation(
      fmt::format("Jump to first non-defaulted argument"), jump_table_cursor);

  as_->align(AlignMode::kAlignCode, 8);
  as_->bind(arg_labels[0]);
  for (Py_ssize_t i = checks.size() - 1; i >= 0; i--) {
    auto check_cursor = as_->cursor();
    const TypedArgument& arg = checks.at(i);
    env_.code_rt->addReference(arg.pytype);
    next_arg = arg_labels[checks.size() - i];

    as_->mov(x86::r8, x86::ptr(x86::rsi, arg.locals_idx * 8)); // load local
    as_->mov(
        x86::r8, x86::ptr(x86::r8, offsetof(PyObject, ob_type))); // load type
    if (arg.optional) {
      // check if the value is None
      emitCompare(as_, x86::r8, Py_TYPE(Py_None), x86::rax);
      as_->je(next_arg);
    }

    // common case: check if we have the exact right type
    emitCompare(as_, x86::r8, arg.pytype, x86::rax);
    as_->je(next_arg);

    if (arg.pytype->tp_flags & Py_TPFLAGS_BASETYPE) {
      // We need to check the object's MRO and see if the declared type
      // is present in it.  Technically we don't need to check the last
      // entry that will be object but the code gen is a little bit simpler
      // if we include it.
      Label arg_loop = as_->newLabel();
      as_->mov(x86::r10, reinterpret_cast<uint64_t>(arg.pytype.get()));

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
    bool last_check = i == 0;
    if (!last_check) {
      as_->bind(next_arg);
    }
    env_.addAnnotation(
        fmt::format("StaticTypeCheck[{}]", arg.pytype->tp_name), check_cursor);
  }
}

void NativeGenerator::generateEpilogue(BaseNode* epilogue_cursor) {
  as_->setCursor(epilogue_cursor);

  // now we can use all the caller save registers except for RAX
  as_->bind(env_.exit_label);

  bool is_gen = GetFunction()->code->co_flags & kCoFlagsAnyGenerator;
  if (is_gen) {
    // Set generator state to "completed". We access the state via RBP which
    // points to the of spill data and bottom of GenDataFooter.
    auto state_offs = offsetof(GenDataFooter, state);
    as_->mov(
        x86::ptr(x86::rbp, state_offs, sizeof(GenDataFooter::state)),
        _PyJitGenState_Completed);
    as_->bind(env_.exit_for_yield_label);
    RestoreOriginalGeneratorRBP(as_->as<x86::Emitter>());
  }

  generateEpilogueUnlinkFrame(x86::rdi, is_gen);

  // If we return a primitive, set edx/xmm1 to 1 to indicate no error (in case
  // of error, deopt will set it to 0 and jump to hard_exit_label, skipping
  // this.)
  if (func_->returnsPrimitive()) {
    if (func_->returnsPrimitiveDouble()) {
      // Loads an *integer* 1 in XMM1.. value doesn't matter,
      // but it needs to be non-zero. See pg 124,
      // https://www.agner.org/optimize/optimizing_assembly.pdf
      as_->pcmpeqw(x86::xmm1, x86::xmm1);
      as_->psrlq(x86::xmm1, 63);
    } else {
      as_->mov(x86::edx, 1);
    }
  }

  as_->bind(env_.hard_exit_label);

  auto saved_regs = env_.changed_regs & CALLEE_SAVE_REGS;
  if (!saved_regs.Empty()) {
    // Reset rsp to point at our callee-saved registers and restore them.
    JIT_CHECK(
        env_.last_callee_saved_reg_off != -1,
        "offset to callee saved regs not initialized");
    as_->lea(x86::rsp, x86::ptr(x86::rbp, -env_.last_callee_saved_reg_off));

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
    const auto& deopt_meta = env_.rt->getDeoptMetadata(exit.deopt_meta_index);
    emitCall(env_, deopt_exit, deopt_meta.instr_offset());
  }
  // Generate the stage 2 trampoline (one per function). This saves the address
  // of the final part of the JIT-epilogue that is responsible for restoring
  // callee-saved registers and returning, our scratch register, whose original
  // contents may be needed during frame reification, and jumps to the final
  // trampoline.
  //
  // Right now the top of the stack looks like:
  //
  // +-------------------------+ <-- end of JIT's fixed frame
  // | index of deopt metadata |
  // | saved rip               |
  // +-------------------------+
  //
  // and we need to pass our scratch register and the address of the epilogue
  // to the global deopt trampoline. The code below leaves the stack with the
  // following layout:
  //
  // +-------------------------+ <-- end of JIT's fixed frame
  // | index of deopt metadata |
  // | saved rip               |
  // | padding                 |
  // | address of epilogue     |
  // | r15                     |
  // +-------------------------+
  //
  // The global deopt trampoline expects that our scratch register is at the
  // top of the stack so that it can save the remaining registers immediately
  // after it, forming a contiguous array of all registers.
  //
  // If you change this make sure you update that code!
  as_->bind(deopt_exit);
  // Add padding to keep the stack aligned
  as_->push(deopt_scratch_reg);
  // Save space for the epilogue
  as_->push(deopt_scratch_reg);
  // Save our scratch register
  as_->push(deopt_scratch_reg);
  // Save the address of the epilogue
  as_->lea(deopt_scratch_reg, x86::ptr(env_.hard_exit_label));
  as_->mov(x86::ptr(x86::rsp, kPointerSize), deopt_scratch_reg);
  auto trampoline = GetFunction()->code->co_flags & kCoFlagsAnyGenerator
      ? deopt_trampoline_generators_
      : deopt_trampoline_;
  as_->mov(deopt_scratch_reg, reinterpret_cast<uint64_t>(trampoline));
  as_->jmp(deopt_scratch_reg);
  env_.addAnnotation("Deoptimization exits", deopt_cursor);
}

void NativeGenerator::linkDeoptPatchers(const asmjit::CodeHolder& code) {
  JIT_CHECK(code.hasBaseAddress(), "code not generated!");
  uint64_t base = code.baseAddress();
  for (const auto& udp : env_.pending_deopt_patchers) {
    uint64_t patchpoint = base + code.labelOffset(udp.patchpoint);
    uint64_t deopt_exit = base + code.labelOffset(udp.deopt_exit);
    udp.patcher->link(patchpoint, deopt_exit);
  }
}

void NativeGenerator::linkIPtoBCMappings(const asmjit::CodeHolder& code) {
  JIT_CHECK(code.hasBaseAddress(), "code not generated!");
  uint64_t base = code.baseAddress();
  for (const auto& mapping : env_.pending_ip_to_bc_offs) {
    uintptr_t ip = base + code.labelOffsetFromBase(mapping.ip);
    env_.code_rt->addIPtoBCOff(ip, mapping.bc_off);
  }
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
  setupFrameAndSaveCallerRegisters(x86::rdx);

  // Setup RBP to use storage in generator rather than stack.

  // Pointer to GenDataFooter. Could be any conflict-free register.
  const auto jit_data_r = x86::r9;

  // jit_data_r = gen->gi_jit_data
  size_t gi_jit_data_offset = offsetof(PyGenObject, gi_jit_data);
  as_->mov(jit_data_r, x86::ptr(gen_r, gi_jit_data_offset));

  // Store linked frame address
  size_t link_address_offset = offsetof(GenDataFooter, linkAddress);
  as_->mov(scratch_r, x86::ptr(x86::rbp));
  as_->mov(x86::ptr(jit_data_r, link_address_offset), scratch_r);

  // Store return address
  size_t return_address_offset = offsetof(GenDataFooter, returnAddress);
  as_->mov(scratch_r, x86::ptr(x86::rbp, 8));
  as_->mov(x86::ptr(jit_data_r, return_address_offset), scratch_r);

  // Store "original" RBP
  size_t original_rbp_offset = offsetof(GenDataFooter, originalRbp);
  as_->mov(x86::ptr(jit_data_r, original_rbp_offset), x86::rbp);

  // RBP = gen->gi_jit_data
  as_->mov(x86::rbp, jit_data_r);

  // Resume generator execution: load and clear yieldPoint, then jump to the
  // resume target.
  size_t yield_point_offset = offsetof(GenDataFooter, yieldPoint);
  as_->mov(scratch_r, x86::ptr(x86::rbp, yield_point_offset));
  as_->mov(x86::qword_ptr(x86::rbp, yield_point_offset), 0);
  size_t resume_target_offset = GenYieldPoint::resumeTargetOffset();
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

  std::vector<std::pair<x86::Gp, x86::Gp>> save_regs;
  if (!isGen()) {
    int pushed_args = std::min(total_args, NUM_REG_ARGS);
    save_regs.reserve(pushed_args);

    for (int i = 0; i < pushed_args; i++) {
      auto loc = get_arg_location(i);
      save_regs.emplace_back(loc, loc);
    }
  }

  loadOrGenerateLinkFrame(x86::r11, save_regs);

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
  as_->long_().jmp(correct_arg_count);
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
    if (GetFunction()->returnsPrimitive()) {
      if (GetFunction()->returnsPrimitiveDouble()) {
        as_->call(reinterpret_cast<uint64_t>(
            JITRT_ReportStaticArgTypecheckErrorsWithDoubleReturn));
      } else {
        as_->call(reinterpret_cast<uint64_t>(
            JITRT_ReportStaticArgTypecheckErrorsWithPrimitiveReturn));
      }
    } else {
      as_->call(
          reinterpret_cast<uint64_t>(JITRT_ReportStaticArgTypecheckErrors));
    }
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

  linkDeoptPatchers(codeholder);
  linkIPtoBCMappings(codeholder);

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
      g_dump_asm,
      "Disassembly for %s\n%s",
      GetFunction()->fullname,
      env_.annotations.disassemble(orig_entry, codeholder));

  for (auto& x : env_.function_indirections) {
    Label trampoline = x.second.trampoline;
    *x.second.indirect =
        (void*)(codeholder.labelOffset(trampoline) + codeholder.baseAddress());
  }

  const hir::Function* func = GetFunction();
  std::string prefix = [&] {
    switch (func->frameMode) {
      case FrameMode::kNormal:
        return perf::kFuncSymbolPrefix;
      case FrameMode::kShadow:
        return perf::kShadowFrameSymbolPrefix;
    }
    JIT_CHECK(false, "Invalid frame mode");
  }();
  // For perf, we want only the size of the code, so we get that directly from
  // the .text section.
  perf::registerFunction(
      entry_, codeholder.textSection()->realSize(), func->fullname, prefix);
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

static void raiseUnboundLocalError(BorrowedRef<> name) {
  PyErr_Format(
      PyExc_UnboundLocalError,
      "local variable '%.200U' referenced before assignment",
      name);
}

static void raiseUnboundFreevarError(BorrowedRef<> name) {
  PyErr_Format(
      PyExc_NameError,
      "free variable '%.200U' referenced before assignment in enclosing scope",
      name);
}

static void raiseAttributeError(BorrowedRef<> receiver, BorrowedRef<> name) {
  PyErr_Format(
      PyExc_AttributeError,
      "'%.50s' object has no attribute '%U'",
      Py_TYPE(receiver)->tp_name,
      name);
}

static PyFrameObject* prepareForDeopt(
    const uint64_t* regs,
    Runtime* runtime,
    std::size_t deopt_idx,
    int* err_occurred,
    const JITRT_CallMethodKind* call_method_kind) {
  const DeoptMetadata& deopt_meta = runtime->getDeoptMetadata(deopt_idx);
  PyThreadState* tstate = _PyThreadState_UncheckedGet();
  PyFrameObject* frame = nullptr;
  Ref<PyFrameObject> f = materializePyFrameForDeopt(tstate);
  frame = f.release();
  // Transfer ownership of shadow frame to the interpreter. The associated
  // Python frame will be ignored during future attempts to materialize the
  // stack.
  _PyShadowFrame_SetOwner(tstate->shadow_frame, PYSF_INTERP);
  Ref<> deopt_obj =
      reifyFrame(frame, deopt_idx, deopt_meta, regs, call_method_kind);
  if (!PyErr_Occurred()) {
    auto reason = deopt_meta.reason;
    switch (reason) {
      case DeoptReason::kGuardFailure: {
        runtime->guardFailed(deopt_meta);
        break;
      }
      case DeoptReason::kUnhandledNullField:
        raiseAttributeError(deopt_obj, deopt_meta.eh_name);
        break;
      case DeoptReason::kUnhandledUnboundLocal:
        raiseUnboundLocalError(deopt_meta.eh_name);
        break;
      case DeoptReason::kUnhandledUnboundFreevar:
        raiseUnboundFreevarError(deopt_meta.eh_name);
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

    // Unlink frames. No unlink for generator shadow frames as this is handled
    // by the send implementation.
    if (!deopt_meta.code_rt->isGen()) {
      _PyShadowFrame_Pop(tstate, tstate->shadow_frame);
    }
    JITRT_UnlinkFrame(tstate);
    return nullptr;
  }

  *err_occurred = (deopt_meta.reason != DeoptReason::kGuardFailure);

  // We need to maintain the invariant that there is at most one shadow frame
  // on the shadow stack for each frame on the Python stack. Unless we are a
  // a generator, the interpreter will insert a new entry on the shadow stack
  // when execution resumes there, so we remove our entry.
  if (!deopt_meta.code_rt->isGen()) {
    _PyShadowFrame_Pop(tstate, tstate->shadow_frame);
  }

  return frame;
}

static PyObject* resumeInInterpreter(PyFrameObject* frame, int err_occurred) {
  if (frame->f_gen) {
    auto gen = reinterpret_cast<PyGenObject*>(frame->f_gen);
    // It's safe to call JITRT_GenJitDataFree directly here, rather than
    // through _PyJIT_GenDealloc. Ownership of all references have been
    // transferred to the frame.
    JITRT_GenJitDataFree(gen);
    gen->gi_jit_data = nullptr;
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
  // When we get here the stack has the following layout. The space on the
  // stack for the call arg buffer / LOAD_METHOD scratch space is always safe
  // to read, but its contents will depend on the function being compiled as
  // well as the program point at which deopt occurs. We pass a pointer to it
  // into the frame reification code so that it can properly reconstruct the
  // interpreter's stack when the the result of a LOAD_METHOD is on the
  // stack. See the comments in reifyStack in deopt.cpp for more details.
  //
  // +-------------------------+
  // | ...                     |
  // | ? call arg buffer       |
  // | ^ LOAD_METHOD scratch   |
  // +-------------------------+ <-- end of JIT's fixed frame
  // | index of deopt metadata |
  // | saved rip               |
  // | padding                 |
  // | address of epilogue     |
  // | r15                     | <-- rsp
  // +-------------------------+
  //
  // Save registers for use in frame reification. Once these are saved we're
  // free to clobber any caller-saved registers.
  //
  // IF YOU USE CALLEE-SAVED REGISTERS YOU HAVE TO RESTORE THEM MANUALLY BEFORE
  // THE EXITING THE TRAMPOLINE.
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

  // Set up a stack frame for the trampoline so that:
  //
  // 1. Runtime code in the JIT that is used to update PyFrameObjects can find
  //    the saved rip at the expected location immediately following the end of
  //    the JIT's fixed frame.
  // 2. The JIT-compiled function shows up in C stack straces when it is
  //    deopting. Only the deopt trampoline will appear in the trace if
  //    we don't open a frame.
  //
  // Right now the stack has the following layout:
  //
  // +-------------------------+ <-- end of JIT's fixed frame
  // | index of deopt metadata |
  // | saved rip               |
  // | padding                 |
  // | address of epilogue     |
  // | r15                     |
  // | ...                     |
  // | rax                     | <-- rsp
  // +-------------------------+
  //
  // We want our frame to look like:
  //
  // +-------------------------+ <-- end of JIT's fixed frame
  // | saved rip               |
  // | saved rbp               | <-- rbp
  // | index of deopt metadata |
  // | address of epilogue     |
  // | r15                     |
  // | ...                     |
  // | rax                     | <-- rsp
  // +-------------------------+
  //
  // Load the saved rip passed to us from the JIT-compiled function, which
  // resides where we're supposed to save rbp.
  auto saved_rbp_addr =
      x86::ptr(x86::rsp, (PhyLocation::NUM_GP_REGS + 2) * kPointerSize);
  a.mov(x86::rdi, saved_rbp_addr);
  // Save rbp and set up our frame
  a.mov(saved_rbp_addr, x86::rbp);
  a.lea(x86::rbp, saved_rbp_addr);
  // Load the index of the deopt metadata, which resides where we're supposed to
  // save rip.
  auto saved_rip_addr = x86::ptr(x86::rbp, kPointerSize);
  a.mov(x86::rsi, saved_rip_addr);
  a.mov(saved_rip_addr, x86::rdi);
  // Save the index of the deopt metadata
  auto deopt_meta_addr = x86::ptr(x86::rbp, -kPointerSize);
  a.mov(deopt_meta_addr, x86::rsi);

  // Prep the frame for evaluation in the interpreter.
  //
  // We pass the array of saved registers, a pointer to the runtime, and the
  // index of deopt metadata
  annot_cursor = a.cursor();
  a.mov(x86::rdi, x86::rsp);
  a.mov(
      x86::rsi, reinterpret_cast<uint64_t>(NativeGeneratorFactory::runtime()));
  a.mov(x86::rdx, deopt_meta_addr);
  // We no longer need the index of the deopt metadata after prepareForDeopt
  // returns, so we reuse the space on the stack to store whether or not we're
  // deopting into a except/finally block.
  a.lea(x86::rcx, deopt_meta_addr);
  auto call_method_kind_addr = x86::ptr(x86::rbp, 2 * kPointerSize);
  a.lea(x86::r8, call_method_kind_addr);
  static_assert(
      std::is_same_v<
          decltype(prepareForDeopt),
          PyFrameObject*(
              const uint64_t*,
              Runtime*,
              std::size_t,
              int*,
              const JITRT_CallMethodKind*)>,
      "prepareForDeopt has unexpected signature");
  a.call(reinterpret_cast<uint64_t>(prepareForDeopt));

  // If we return a primitive and prepareForDeopt returned null, we need that
  // null in edx/xmm1 to signal error to our caller. Since this trampoline is
  // shared, we do this move unconditionally, but even if not needed, it's
  // harmless. (To eliminate it, we'd need another trampoline specifically for
  // deopt of primitive-returning functions, just to do this one move.)
  a.mov(x86::edx, x86::eax);
  a.movq(x86::xmm1, x86::eax);

  // Clean up saved registers.
  //
  // This isn't strictly necessary but saves 128 bytes on the stack if we end
  // up resuming in the interpreter.
  a.add(x86::rsp, (PhyLocation::NUM_GP_REGS - 1) * kPointerSize);
  // We have to restore our scratch register manually since it's callee-saved
  // and the stage 2 trampoline used it to hold the address of this
  // trampoline. We can't rely on the JIT epilogue to restore it for us, as the
  // JIT-compiled code may not have spilled it.
  a.pop(deopt_scratch_reg);
  annot.add("prepareForDeopt", &a, annot_cursor);

  // Resume execution in the interpreter if we are not unwinding.
  annot_cursor = a.cursor();
  auto done = a.newLabel();
  a.test(x86::rax, x86::rax);
  a.jz(done);
  a.mov(x86::rdi, x86::rax);
  a.mov(x86::rsi, deopt_meta_addr);
  a.call(reinterpret_cast<uint64_t>(resumeInInterpreter));
  annot.add("resumeInInterpreter", &a, annot_cursor);

  // Now we're done. Get the address of the epilogue and jump there.
  annot_cursor = a.cursor();
  a.bind(done);
  auto epilogue_addr = x86::ptr(x86::rbp, -2 * kPointerSize);
  a.mov(x86::rdi, epilogue_addr);
  // Remove our frame from the stack
  a.leave();
  // Clear the saved rip. Normally this would be handled by a `ret`; we must
  // clear it manually because we're jumping directly to the epilogue.
  a.sub(x86::rsp, -kPointerSize);
  a.jmp(x86::rdi);
  annot.add("jumpToRealEpilogue", &a, annot_cursor);

  auto name =
      generator_mode ? "deopt_trampoline_generators" : "deopt_trampoline";
  void* result{nullptr};
  ASM_CHECK(a.finalize(), name);
  ASM_CHECK(rt.add(&result, &code), name);
  JIT_LOGIF(
      g_dump_asm,
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
      g_dump_asm,
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

int NativeGenerator::calcFrameHeaderSize(const hir::Function* func) {
  return func == nullptr ? 0 : sizeof(FrameHeader);
}

} // namespace codegen
} // namespace jit

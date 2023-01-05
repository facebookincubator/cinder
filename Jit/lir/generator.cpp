// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/lir/generator.h"

#include "Python.h"
#include "cinder/exports.h"
#include "internal/pycore_interp.h"
#include "internal/pycore_pyerrors.h"
#include "internal/pycore_pystate.h"
#include "internal/pycore_shadow_frame.h"
#include "listobject.h"
#include "pystate.h"

#include "Jit/codegen/x86_64.h"
#include "Jit/containers.h"
#include "Jit/deopt.h"
#include "Jit/frame.h"
#include "Jit/hir/analysis.h"
#include "Jit/inline_cache.h"
#include "Jit/jit_rt.h"
#include "Jit/lir/block_builder.h"
#include "Jit/log.h"
#include "Jit/pyjit.h"
#include "Jit/runtime_support.h"
#include "Jit/threaded_compile.h"
#include "Jit/util.h"

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <functional>
#include <sstream>

extern "C" {
int eval_frame_handle_pending(PyThreadState*);
}

// XXX: this file needs to be revisited when we optimize HIR-to-LIR translation
// in codegen.cpp/h. Currently, this file is almost an identical copy from
// codegen.cpp with some interfaces changes so that it works with the new
// LIR.

using namespace jit::hir;

namespace jit::lir {

// These functions call their counterparts and convert its output from int (32
// bits) to uint64_t (64 bits). This is solely because the code generator cannot
// support an operand size other than 64 bits at this moment. A future diff will
// make it support different operand sizes so that this function can be removed.

extern "C" PyObject* __Invoke_PyList_Extend(
    PyThreadState* tstate,
    PyListObject* list,
    PyObject* iterable) {
  PyObject* none_val = _PyList_Extend(list, iterable);
  if (none_val == nullptr) {
    if (_PyErr_ExceptionMatches(tstate, PyExc_TypeError) &&
        Py_TYPE(iterable)->tp_iter == nullptr && !PySequence_Check(iterable)) {
      _PyErr_Clear(tstate);
      _PyErr_Format(
          tstate,
          PyExc_TypeError,
          "Value after * must be an iterable, not %.200s",
          Py_TYPE(iterable)->tp_name);
    }
  }

  return none_val;
}

LIRGenerator::LIRGenerator(
    const jit::hir::Function* func,
    jit::codegen::Environ* env)
    : func_(func),
      env_(env),
      entry_block_(nullptr),
      exit_block_(nullptr),
      temp_id(0),
      label_id(0) {
  for (int i = 0, n = func->env.numLoadTypeAttrCaches(); i < n; i++) {
    load_type_attr_caches_.emplace_back(
        Runtime::get()->allocateLoadTypeAttrCache());
  }
}

BasicBlock* LIRGenerator::GenerateEntryBlock() {
  auto block = lir_func_->allocateBasicBlock();
  auto bindVReg = [&](const std::string& name, int phy_reg) {
    auto instr = block->allocateInstr(Instruction::kBind, nullptr);
    instr->output()->setVirtualRegister();
    instr->allocatePhyRegisterInput(phy_reg);
    env_->output_map.emplace(name, instr);
    return instr;
  };

  env_->asm_extra_args =
      bindVReg("__asm_extra_args", jit::codegen::PhyLocation::R10);
  env_->asm_tstate = bindVReg("__asm_tstate", jit::codegen::PhyLocation::R11);
  if (func_->uses_runtime_func) {
    env_->asm_func = bindVReg("__asm_func", jit::codegen::PhyLocation::RAX);
  }

  return block;
}

BasicBlock* LIRGenerator::GenerateExitBlock() {
  auto block = lir_func_->allocateBasicBlock();
  auto instr = block->allocateInstr(Instruction::kMove, nullptr);
  instr->output()->setPhyRegister(jit::codegen::PhyLocation::RDI);
  instr->allocateLinkedInput(env_->asm_tstate);
  return block;
}

void LIRGenerator::AnalyzeCopies() {
  // Find all HIR instructions in the input that would end with a copy, and
  // assign their output the same vreg as the input, effectively performing
  // copy propagation during lowering.
  //
  // TODO(bsimmers): We should really be emitting copies during lowering and
  // eliminating them after the fact, to keep this information localized to the
  // lowering code.

  for (auto& block : func_->cfg.blocks) {
    for (auto& instr : block) {
      // XXX(bsimmers) Cast doesn't have to be a special case once it deopts
      // and always returns its input.
      if (instr.GetOutput() != nullptr && !instr.IsCast() &&
          hir::isPassthrough(instr)) {
        env_->copy_propagation_map.emplace(
            instr.GetOutput()->name(), instr.GetOperand(0)->name());
      }
    }
  }
}

std::unique_ptr<jit::lir::Function> LIRGenerator::TranslateFunction() {
  env_->operand_to_fix.clear();

  AnalyzeCopies();

  auto function = std::make_unique<jit::lir::Function>();
  lir_func_ = function.get();

  // generate entry block and exit block
  entry_block_ = GenerateEntryBlock();

  UnorderedMap<const hir::BasicBlock*, TranslatedBlock> bb_map;
  std::vector<const hir::BasicBlock*> translated;
  auto translate_block = [&](const hir::BasicBlock* hir_bb) {
    bb_map.emplace(hir_bb, TranslateOneBasicBlock(hir_bb));
    translated.emplace_back(hir_bb);
  };

  // Translate all reachable blocks.
  auto hir_entry = GetHIRFunction()->cfg.entry_block;
  translate_block(hir_entry);
  for (size_t i = 0; i < translated.size(); ++i) {
    auto hir_term = translated[i]->GetTerminator();
    for (int succ = 0, num_succs = hir_term->numEdges(); succ < num_succs;
         succ++) {
      auto hir_succ = hir_term->successor(succ);
      if (bb_map.count(hir_succ)) {
        continue;
      }
      translate_block(hir_succ);
    }
  }

  exit_block_ = GenerateExitBlock();

  // Connect all successors.
  entry_block_->addSuccessor(bb_map[hir_entry].first);
  for (auto hir_bb : translated) {
    auto hir_term = hir_bb->GetTerminator();
    auto last_bb = bb_map[hir_bb].last;
    switch (hir_term->opcode()) {
      case Opcode::kBranch: {
        auto branch = static_cast<const Branch*>(hir_term);
        auto target_lir_bb = bb_map[branch->target()].first;
        last_bb->addSuccessor(target_lir_bb);
        break;
      }
      case Opcode::kCondBranch:
      case Opcode::kCondBranchCheckType:
      case Opcode::kCondBranchIterNotDone: {
        auto condbranch = static_cast<const CondBranchBase*>(hir_term);
        auto target_lir_true_bb = bb_map[condbranch->true_bb()].first;
        auto target_lir_false_bb = bb_map[condbranch->false_bb()].first;
        last_bb->addSuccessor(target_lir_true_bb);
        last_bb->addSuccessor(target_lir_false_bb);
        last_bb->getLastInstr()->allocateLabelInput(target_lir_true_bb);
        last_bb->getLastInstr()->allocateLabelInput(target_lir_false_bb);
        break;
      }
      case Opcode::kReturn: {
        last_bb->addSuccessor(exit_block_);
        break;
      }
      default:
        break;
    }
  }

  FixPhiNodes(bb_map);
  FixOperands();

  return function;
}

void LIRGenerator::AppendGuard(
    BasicBlockBuilder& bbb,
    std::string_view kind,
    const DeoptBase& instr,
    std::string_view guard_var) {
  auto deopt_id = bbb.makeDeoptMetadata();

  fmt::memory_buffer buf;
  auto buf_ins = std::back_inserter(buf);
  fmt::format_to(buf_ins, "Guard {}, {}", kind, deopt_id);

  JIT_CHECK(
      guard_var.empty() == (kind == "AlwaysFail"),
      "MakeGuard expects a register name to guard iff the kind is not "
      "AlwaysFail");
  if (!guard_var.empty()) {
    buf.append(std::string_view(", "));
    buf.append(guard_var);
  } else {
    buf.append(std::string_view(", 0"));
  }

  if (instr.IsGuardIs()) {
    const auto& guard = static_cast<const GuardIs&>(instr);
    auto guard_ptr = static_cast<void*>(guard.target());
    env_->code_rt->addReference(guard.target());
    fmt::format_to(buf_ins, ", {}", guard_ptr);
  } else if (instr.IsGuardType()) {
    const auto& guard = static_cast<const GuardType&>(instr);
    // TODO(T101999851): Handle non-Exact types
    JIT_CHECK(guard.target().isExact(), "Only exact type guards are supported");
    PyTypeObject* guard_type = guard.target().uniquePyType();
    JIT_CHECK(guard_type != nullptr, "Ensure unique representation exists");
    env_->code_rt->addReference(reinterpret_cast<PyObject*>(guard_type));
    fmt::format_to(buf_ins, ", {}", reinterpret_cast<void*>(guard_type));
  } else {
    buf.append(std::string_view(", 0"));
  }

  auto& regstates = instr.live_regs();
  for (const auto& reg_state : regstates) {
    fmt::format_to(buf_ins, ", {}", reg_state.reg->name());
  }

  bbb.AppendCode(buf);
}

// Attempt to emit a type-specialized call, returning true if successful.
bool LIRGenerator::TranslateSpecializedCall(
    BasicBlockBuilder& bbb,
    const VectorCallBase& instr) {
  Register* callable = instr.func();
  if (!callable->type().hasValueSpec(TObject)) {
    return false;
  }
  auto callee = callable->type().objectSpec();
  auto type = Py_TYPE(callee);
  if (PyType_HasFeature(type, Py_TPFLAGS_HEAPTYPE) ||
      PyType_IsSubtype(type, &PyModule_Type)) {
    // Heap types and ModuleType subtypes support __class__ reassignment, so we
    // can't rely on the object's type.
    return false;
  }

  // TODO(bsimmers): This is where we can go bananas with specializing calls to
  // things like tuple(), list(), etc, hardcoding or inlining calls to tp_new
  // and tp_init as appropriate. For now, we simply support any callable with a
  // vectorcall.
  if (Py_TYPE(callee) == &PyCFunction_Type) {
    if (PyCFunction_GET_FUNCTION(callee) == (PyCFunction)&builtin_next) {
      if (instr.numArgs() == 1) {
        bbb.AppendCall(
            instr.dst(), Ci_Builtin_Next_Core, instr.arg(0), nullptr);
        return true;
      } else if (instr.numArgs() == 2) {
        bbb.AppendCall(
            instr.dst(), Ci_Builtin_Next_Core, instr.arg(0), instr.arg(1));
        return true;
      }
    }
    switch (
        PyCFunction_GET_FLAGS(callee) &
        (METH_VARARGS | METH_FASTCALL | METH_NOARGS | METH_O | METH_KEYWORDS)) {
      case METH_NOARGS:
        if (instr.numArgs() == 0) {
          bbb.AppendCall(
              instr.dst(),
              PyCFunction_GET_FUNCTION(callee),
              PyCFunction_GET_SELF(callee),
              nullptr);
          return true;
        }
        break;
      case METH_O:
        if (instr.numArgs() == 1) {
          bbb.AppendCall(
              instr.dst(),
              PyCFunction_GET_FUNCTION(callee),
              PyCFunction_GET_SELF(callee),
              instr.arg(0));
          return true;
        }
        break;
    }
  }

  auto func = _PyVectorcall_Function(callee);
  if (func == nullptr ||
      func == reinterpret_cast<vectorcallfunc>(PyEntry_LazyInit)) {
    // Bail if the object doesn't support vectorcall, or if it's a function
    // that hasn't been initialized yet.
    return false;
  }

  fmt::memory_buffer buf;
  auto buf_ins = std::back_inserter(buf);
  fmt::format_to(
      buf_ins,
      "Vectorcall {}, {}, 0, {}",
      instr.dst()->name(),
      reinterpret_cast<uint64_t>(func),
      reinterpret_cast<uint64_t>(callee));
  for (size_t i = 0, num_args = instr.numArgs(); i < num_args; i++) {
    fmt::format_to(buf_ins, ", {}", instr.arg(i));
  }
  buf.append(std::string_view(", 0"));
  bbb.AppendCode(buf);
  return true;
}

static void emitVectorCall(
    BasicBlockBuilder& bbb,
    const VectorCallBase& instr,
    size_t flags,
    bool kwnames) {
  std::stringstream ss;
  ss << "Vectorcall " << *instr.dst() << ", "
     << reinterpret_cast<uint64_t>(_PyObject_Vectorcall) << ", " << flags
     << ", " << instr.func()->name();
  auto nargs = instr.numArgs();
  for (size_t i = 0; i < nargs; i++) {
    ss << ", " << *instr.arg(i);
  }
  if (!kwnames) {
    ss << ", 0";
  }
  bbb.AppendCode(ss.str());
}

void LIRGenerator::emitExceptionCheck(
    const jit::hir::DeoptBase& i,
    jit::lir::BasicBlockBuilder& bbb) {
  Register* out = i.GetOutput();
  if (out->isA(TBottom)) {
    AppendGuard(bbb, "AlwaysFail", i);
  } else {
    std::string_view kind = out->isA(TCSigned) ? "NotNegative" : "NotZero";
    AppendGuard(bbb, kind, i, out->name());
  }
}

void LIRGenerator::MakeIncref(
    BasicBlockBuilder& bbb,
    const hir::Instr& instr,
    bool xincref) {
  Register* obj = instr.GetOperand(0);
#ifdef Py_IMMORTAL_INSTANCES
  if (!obj->type().couldBe(TMortalObject)) {
    // don't generate anything for immortal object
    return;
  }
#endif

  auto end_incref = GetSafeLabelName();
  if (xincref) {
    auto cont = GetSafeLabelName();
    bbb.AppendCode("JumpIf {}, {}, {}", obj, cont, end_incref);
    bbb.AppendLabel(cont);
  }

  auto r1 = GetSafeTempName();
  bbb.AppendCode("Load {}, {}, {:#x}", r1, obj, offsetof(PyObject, ob_refcnt));

#ifdef Py_DEBUG
  auto r0 = GetSafeTempName();
  bbb.AppendCode(
      "Load {}, {:#x}", r0, reinterpret_cast<uint64_t>(&_Py_RefTotal));
  bbb.AppendCode("Inc {}", r0);
  bbb.AppendCode(
      "Store {}, {:#x}", r0, reinterpret_cast<uint64_t>(&_Py_RefTotal));
#endif

#ifdef Py_IMMORTAL_INSTANCES
  if (obj->type().couldBe(TImmortalObject)) {
    auto mortal = GetSafeLabelName();
    bbb.AppendCode("BitTest {}, {}", r1, kImmortalBitPos);
    bbb.AppendCode("BranchC {}", end_incref);
    bbb.AppendLabel(mortal);
  }
#endif

  bbb.AppendCode("Inc {}", r1);
  bbb.AppendCode("Store {}, {}, {:#x}", r1, obj, offsetof(PyObject, ob_refcnt));
  bbb.AppendLabel(end_incref);
}

void LIRGenerator::MakeDecref(
    BasicBlockBuilder& bbb,
    const jit::hir::Instr& instr,
    bool xdecref) {
  Register* obj = instr.GetOperand(0);

#ifdef Py_IMMORTAL_INSTANCES
  if (!obj->type().couldBe(TMortalObject)) {
    // don't generate anything for immortal object
    return;
  }
#endif

  auto end_decref = GetSafeLabelName();
  if (xdecref) {
    auto cont = GetSafeLabelName();
    bbb.AppendCode("JumpIf {}, {}, {}", obj, cont, end_decref);
    bbb.AppendLabel(cont);
  }

  auto r1 = GetSafeTempName();
  auto r2 = GetSafeTempName();

  bbb.AppendCode("Load {}, {}, {:#x}", r1, obj, offsetof(PyObject, ob_refcnt));

#ifdef Py_DEBUG
  auto r0 = GetSafeTempName();
  bbb.AppendCode(
      "Load {}, {:#x}", r0, reinterpret_cast<uint64_t>(&_Py_RefTotal));
  bbb.AppendCode("Dec {}", r0);
  bbb.AppendCode(
      "Store {}, {:#x}", r0, reinterpret_cast<uint64_t>(&_Py_RefTotal));
#endif

#ifdef Py_IMMORTAL_INSTANCES
  if (obj->type().couldBe(TImmortalObject)) {
    auto mortal = GetSafeLabelName();
    bbb.AppendCode("BitTest {}, {}", r1, kImmortalBitPos);
    bbb.AppendCode("BranchC {}", end_decref);
    bbb.AppendLabel(mortal);
  }
#endif

  auto dealloc = GetSafeLabelName();
  bbb.AppendCode("Sub {}, {}, 1", r2, r1);
  bbb.AppendCode("Store {}, {}, {:#x}", r2, obj, offsetof(PyObject, ob_refcnt));

  bbb.AppendCode("BranchNZ {}", end_decref);
  bbb.AppendLabel(dealloc);
  if (_PyJIT_MultipleCodeSectionsEnabled()) {
    bbb.SetBlockSection(dealloc, codegen::CodeSection::kCold);
  }

  bbb.AppendInvoke(JITRT_Dealloc, obj);
  bbb.AppendLabel(end_decref);
}

// Checks if a type has reasonable == semantics, that is that
// object identity implies equality when compared by Python.  This
// is true for most types, but not true for floats where nan is
// not equal to nan.  But it is true for container types containing
// those floats where PyObject_RichCompareBool is used and it short
// circuits on object identity.
bool isTypeWithReasonablePointerEq(Type t) {
  return t <= TArray || t <= TBytesExact || t <= TDictExact ||
      t <= TListExact || t <= TSetExact || t <= TTupleExact ||
      t <= TTypeExact || t <= TLongExact || t <= TBool || t <= TFunc ||
      t <= TGen || t <= TNoneType || t <= TSlice;
}

namespace {
void finishYield(
    BasicBlockBuilder& bbb,
    std::stringstream& ss,
    const DeoptBase* instr) {
  for (const RegState& rs : instr->live_regs()) {
    ss << ", " << rs.reg->name();
  }
  ss << ", " << instr->live_regs().size();
  ss << ", " << bbb.makeDeoptMetadata();
  bbb.AppendCode(ss.str());
}
} // namespace

static int bytes_from_cint_type(Type type) {
  if (type <= TCInt8 || type <= TCUInt8) {
    return 1;
  } else if (type <= TCInt16 || type <= TCUInt16) {
    return 2;
  } else if (type <= TCInt32 || type <= TCUInt32) {
    return 3;
  } else if (type <= TCInt64 || type <= TCUInt64) {
    return 4;
  }
  JIT_CHECK(false, "bad primitive int type: (%d)", type);
  // NOTREACHED
}

#define FOREACH_FAST_BUILTIN(V) \
  V(Long)                       \
  V(List)                       \
  V(Tuple)                      \
  V(Bytes)                      \
  V(Unicode)                    \
  V(Dict)                       \
  V(Type)

#define INVOKE_CHECK(name)                                       \
  extern "C" uint64_t __Invoke_Py##name##_Check(PyObject* obj) { \
    int result = Py##name##_Check(obj);                          \
    return result == 0 ? 0 : 1;                                  \
  }

FOREACH_FAST_BUILTIN(INVOKE_CHECK)

#undef INVOKE_CHECK

static void emitSubclassCheck(
    BasicBlockBuilder& bbb,
    std::string dst,
    Register* obj,
    Type type) {
  // Fast path: a subset of builtin types that have Py_TPFLAGS
  uint64_t fptr = 0;
#define GET_FPTR(name)                                            \
  if (type <= T##name) {                                          \
    fptr = reinterpret_cast<uint64_t>(__Invoke_Py##name##_Check); \
  } else
  FOREACH_FAST_BUILTIN(GET_FPTR) {
    JIT_CHECK(false, "unsupported subclass check in CondBranchCheckType");
  }
#undef GET_FPTR
  bbb.AppendCode("Call {}, {:#x}, {}", dst, fptr, obj);
}

#undef FOREACH_FAST_BUILTIN

static ssize_t shadowFrameOffsetBefore(const InlineBase* instr) {
  return -instr->inlineDepth() * ssize_t{kJITShadowFrameSize};
}

static ssize_t shadowFrameOffsetOf(const InlineBase* instr) {
  return shadowFrameOffsetBefore(instr) - ssize_t{kJITShadowFrameSize};
}

// x86 encodes scales as size==2**X, so this does log2(num_bytes), but we have
// a limited set of inputs.
static uint8_t multiplierFromSize(int num_bytes) {
  switch (num_bytes) {
    case 1:
      return 0;
    case 2:
      return 1;
    case 4:
      return 2;
    case 8:
      return 3;
    default:
      break;
  }
  JIT_CHECK(false, "unexpected num_bytes %d", num_bytes);
}

LIRGenerator::TranslatedBlock LIRGenerator::TranslateOneBasicBlock(
    const hir::BasicBlock* hir_bb) {
  BasicBlockBuilder bbb(env_, lir_func_);

  for (auto& i : *hir_bb) {
    auto opcode = i.opcode();
    bbb.setCurrentInstr(&i);
    switch (opcode) {
      case Opcode::kLoadArg: {
        auto instr = static_cast<const LoadArg*>(&i);
        if (instr->arg_idx() < env_->arg_locations.size() &&
            env_->arg_locations[instr->arg_idx()] != PhyLocation::REG_INVALID) {
          bbb.appendInstr(
              instr->dst(), Instruction::kLoadArg, Imm{instr->arg_idx()});
          break;
        }
        size_t reg_count = env_->arg_locations.size();
        for (auto loc : env_->arg_locations) {
          if (loc == PhyLocation::REG_INVALID) {
            reg_count--;
          }
        }
        Instruction* extra_args = env_->asm_extra_args;
        int32_t offset = (instr->arg_idx() - reg_count) * kPointerSize;
        bbb.appendInstr(
            instr->dst(), Instruction::kMove, Ind{extra_args, offset});
        break;
      }
      case Opcode::kLoadCurrentFunc: {
        hir::Register* dest = i.GetOutput();
        Instruction* func = env_->asm_func;
        bbb.appendInstr(dest, Instruction::kMove, func);
        break;
      }
      case Opcode::kMakeCell: {
        auto instr = static_cast<const MakeCell*>(&i);
        bbb.AppendCall(instr->dst(), PyCell_New, instr->GetOperand(0));
        break;
      }
      case Opcode::kStealCellItem:
      case Opcode::kLoadCellItem: {
        hir::Register* dest = i.GetOutput();
        Instruction* src_base = bbb.getDefInstr(i.GetOperand(0));
        constexpr int32_t kOffset = offsetof(PyCellObject, ob_ref);
        bbb.appendInstr(dest, Instruction::kMove, Ind{src_base, kOffset});
        break;
      }
      case Opcode::kSetCellItem: {
        auto instr = static_cast<const SetCellItem*>(&i);
        bbb.AppendCode(
            "Store {}, {}, {}",
            instr->GetOperand(1),
            instr->GetOperand(0),
            offsetof(PyCellObject, ob_ref));
        break;
      }
      case Opcode::kLoadConst: {
        auto instr = static_cast<const LoadConst*>(&i);
        Type ty = instr->type();

        if (ty <= TCDouble) {
          // Loads the bits of the double constant into an integer register.
          auto spec_value = bit_cast<uint64_t>(ty.doubleSpec());
          Instruction* double_bits = bbb.appendInstr(
              Instruction::kMove,
              OutVReg{OperandBase::k64bit},
              Imm{spec_value});
          // Moves the value into a floating point register.
          bbb.appendInstr(instr->dst(), Instruction::kMove, double_bits);
          break;
        }

        intptr_t spec_value = ty.hasIntSpec()
            ? ty.intSpec()
            : reinterpret_cast<intptr_t>(ty.asObject());
        bbb.appendInstr(
            instr->dst(),
            Instruction::kMove,
            // Could be integral or pointer, keep as kObject for now.
            Imm{static_cast<uint64_t>(spec_value), OperandBase::kObject});
        break;
      }
      case Opcode::kLoadVarObjectSize: {
        hir::Register* dest = i.GetOutput();
        Instruction* src_base = bbb.getDefInstr(i.GetOperand(0));
        constexpr int32_t kOffset = offsetof(PyVarObject, ob_size);
        bbb.appendInstr(dest, Instruction::kMove, Ind{src_base, kOffset});
        break;
      }
      case Opcode::kLoadFunctionIndirect: {
        // format will pass this down as a constant
        auto instr = static_cast<const LoadFunctionIndirect*>(&i);
        bbb.AppendCall(
            instr->dst(),
            JITRT_LoadFunctionIndirect,
            instr->funcptr(),
            instr->descr());
        break;
      }
      case Opcode::kIntConvert: {
        auto instr = static_cast<const IntConvert*>(&i);
        if (instr->type() <= TCUnsigned) {
          bbb.AppendCode("ConvertUnsigned {}, {}", instr->dst(), instr->src());
        } else {
          JIT_CHECK(
              instr->type() <= TCSigned,
              "Unexpected IntConvert type %s",
              instr->type());
          bbb.AppendCode("Convert {}, {}", instr->dst(), instr->src());
        }
        break;
      }
      case Opcode::kIntBinaryOp: {
        auto instr = static_cast<const IntBinaryOp*>(&i);
        std::string op;
        std::string convert;
        std::string extra_arg = "";
        uint64_t helper = 0;
        switch (instr->op()) {
          case BinaryOpKind::kAdd:
            op = "Add";
            break;
          case BinaryOpKind::kAnd:
            op = "And";
            break;
          case BinaryOpKind::kSubtract:
            op = "Sub";
            break;
          case BinaryOpKind::kXor:
            op = "Xor";
            break;
          case BinaryOpKind::kOr:
            op = "Or";
            break;
          case BinaryOpKind::kMultiply:
            op = "Mul";
            break;
          case BinaryOpKind::kLShift:
            switch (bytes_from_cint_type(instr->GetOperand(0)->type())) {
              case 1:
              case 2:
                convert = "Convert";
              case 3:
                helper = reinterpret_cast<uint64_t>(JITRT_ShiftLeft32);
                break;
              case 4:
                helper = reinterpret_cast<uint64_t>(JITRT_ShiftLeft64);
                break;
            }
            break;
          case BinaryOpKind::kRShift:
            switch (bytes_from_cint_type(instr->GetOperand(0)->type())) {
              case 1:
              case 2:
                convert = "Convert";
              case 3:
                helper = reinterpret_cast<uint64_t>(JITRT_ShiftRight32);
                break;
              case 4:
                helper = reinterpret_cast<uint64_t>(JITRT_ShiftRight64);
                break;
            }
            break;
          case BinaryOpKind::kRShiftUnsigned:
            switch (bytes_from_cint_type(instr->GetOperand(0)->type())) {
              case 1:
              case 2:
                convert = "ConvertUnsigned";
              case 3:
                helper = reinterpret_cast<uint64_t>(JITRT_ShiftRightUnsigned32);
                break;
              case 4:
                helper = reinterpret_cast<uint64_t>(JITRT_ShiftRightUnsigned64);
                break;
            }
            break;
          case BinaryOpKind::kFloorDivide:
            op = "Div";
            extra_arg = "0, ";
            break;
          case BinaryOpKind::kFloorDivideUnsigned:
            op = "DivUn";
            extra_arg = "0, ";
            break;
          case BinaryOpKind::kModulo:
            switch (bytes_from_cint_type(instr->GetOperand(0)->type())) {
              case 1:
              case 2:
                convert = "Convert";
              case 3:
                helper = reinterpret_cast<uint64_t>(JITRT_Mod32);
                break;
              case 4:
                helper = reinterpret_cast<uint64_t>(JITRT_Mod64);
                break;
            }
            break;
          case BinaryOpKind::kModuloUnsigned:
            switch (bytes_from_cint_type(instr->GetOperand(0)->type())) {
              case 1:
              case 2:
                convert = "ConvertUnsigned";
              case 3:
                helper = reinterpret_cast<uint64_t>(JITRT_ModUnsigned32);
                break;
              case 4:
                helper = reinterpret_cast<uint64_t>(JITRT_ModUnsigned64);
                break;
            }
            break;
          case BinaryOpKind::kPower:
            switch (bytes_from_cint_type(instr->GetOperand(0)->type())) {
              case 1:
              case 2:
                convert = "Convert";
              case 3:
                helper = reinterpret_cast<uint64_t>(JITRT_Power32);
                break;
              case 4:
                helper = reinterpret_cast<uint64_t>(JITRT_Power64);
                break;
            };
            break;
          case BinaryOpKind::kPowerUnsigned:
            switch (bytes_from_cint_type(instr->GetOperand(0)->type())) {
              case 1:
              case 2:
                convert = "ConvertUnsigned";
              case 3:
                helper = reinterpret_cast<uint64_t>(JITRT_PowerUnsigned32);
                break;
              case 4:
                helper = reinterpret_cast<uint64_t>(JITRT_PowerUnsigned64);
                break;
            };
            break;
          default:
            JIT_CHECK(false, "not implemented");
            break;
        }
        if (helper != 0) {
          std::string left = instr->left()->name();
          std::string right = instr->right()->name();
          if (convert != "") {
            std::string ltmp = GetSafeTempName();
            std::string rtmp = GetSafeTempName();
            std::string type = convert == "Convert" ? "CInt32" : "CUInt32";
            bbb.AppendCode("{} {}:{}, {}", convert, ltmp, type, left);
            bbb.AppendCode("{} {}:{}, {}", convert, rtmp, type, right);
            left = ltmp;
            right = rtmp;
          }
          bbb.AppendCode(
              "Call {} {:#x}, {}, {}", instr->dst(), helper, left, right);
        } else {
          bbb.AppendCode(
              "{} {}, {} {}, {}",
              op,
              instr->dst(),
              extra_arg,
              instr->left(),
              instr->right());
        }

        break;
      }
      case Opcode::kDoubleBinaryOp: {
        auto instr = static_cast<const DoubleBinaryOp*>(&i);

        if (instr->op() == BinaryOpKind::kPower) {
          bbb.AppendCall(
              instr->dst(), JITRT_PowerDouble, instr->left(), instr->right());
          break;
        }

        std::string op;
        switch (instr->op()) {
          case BinaryOpKind::kAdd: {
            op = "Fadd";
            break;
          }
          case BinaryOpKind::kSubtract: {
            op = "Fsub";
            break;
          }
          case BinaryOpKind::kMultiply: {
            op = "Fmul";
            break;
          }
          case BinaryOpKind::kTrueDivide: {
            op = "Fdiv";
            break;
          }
          default: {
            JIT_CHECK(false, "Invalid operation for DoubleBinaryOp");
            break;
          }
        }

        // Our formatter for Registers tries to be clever with constant values,
        // and this backfires in certain situations (it converts registers to
        // immediates). We have to manually format the name and type here to
        // work around that.
        auto codestr = fmt::format(
            "{} {}, {}:{}, {}:{}",
            op,
            instr->dst(),
            instr->left()->name(),
            instr->left()->type().unspecialized(),
            instr->right()->name(),
            instr->right()->type().unspecialized());
        bbb.AppendCode(codestr);
        break;
      }
      case Opcode::kPrimitiveCompare: {
        auto instr = static_cast<const PrimitiveCompare*>(&i);
        std::string op;
        switch (instr->op()) {
          case PrimitiveCompareOp::kEqual:
            op = "Equal";
            break;
          case PrimitiveCompareOp::kNotEqual:
            op = "NotEqual";
            break;
          case PrimitiveCompareOp::kGreaterThanUnsigned:
            op = "GreaterThanUnsigned";
            break;
          case PrimitiveCompareOp::kGreaterThan:
            op = "GreaterThanSigned";
            break;
          case PrimitiveCompareOp::kLessThanUnsigned:
            op = "LessThanUnsigned";
            break;
          case PrimitiveCompareOp::kLessThan:
            op = "LessThanSigned";
            break;
          case PrimitiveCompareOp::kGreaterThanEqualUnsigned:
            op = "GreaterThanEqualUnsigned";
            break;
          case PrimitiveCompareOp::kGreaterThanEqual:
            op = "GreaterThanEqualSigned";
            break;
          case PrimitiveCompareOp::kLessThanEqualUnsigned:
            op = "LessThanEqualUnsigned";
            break;
          case PrimitiveCompareOp::kLessThanEqual:
            op = "LessThanEqualSigned";
            break;
          default:
            JIT_CHECK(false, "not implemented %d", (int)instr->op());
            break;
        }

        if (instr->left()->type() <= TCDouble ||
            instr->right()->type() <= TCDouble) {
          // Manually format the code string, otherwise registers with literal
          // values end up being treated as immediates, and there's no way to
          // load immediates in an XMM register.
          auto codestr = fmt::format(
              "{} {}, {}:{}, {}:{}",
              op,
              instr->dst(),
              instr->left()->name(),
              instr->left()->type().unspecialized(),
              instr->right()->name(),
              instr->right()->type().unspecialized());
          bbb.AppendCode(codestr);
        } else {
          bbb.AppendCode(
              "{} {} {} {}", op, instr->dst(), instr->left(), instr->right());
        }
        break;
      }
      case Opcode::kPrimitiveBoxBool: {
        // Boxing a boolean is a matter of selecting between Py_True and
        // Py_False.
        Register* dest = i.GetOutput();
        Register* src = i.GetOperand(0);
        auto true_addr = reinterpret_cast<uint64_t>(Py_True);
        auto false_addr = reinterpret_cast<uint64_t>(Py_False);
        Instruction* temp_true = bbb.appendInstr(
            Instruction::kMove, OutVReg{OperandBase::k64bit}, Imm{true_addr});
        bbb.appendInstr(
            dest, Instruction::kSelect, src, temp_true, Imm{false_addr});
        break;
      }
      case Opcode::kPrimitiveBox: {
        auto instr = static_cast<const PrimitiveBox*>(&i);
        std::string src = instr->value()->name();
        Type src_type = instr->value()->type();
        std::string tmp = GetSafeTempName();
        uint64_t func = 0;

        if (src_type == TNullptr) {
          // special case for an uninitialized variable, we'll
          // load zero
          bbb.AppendCall(instr->GetOutput(), JITRT_BoxI64, int64_t{0});
          break;
        } else if (src_type <= TCUInt64) {
          func = reinterpret_cast<uint64_t>(JITRT_BoxU64);
        } else if (src_type <= TCInt64) {
          func = reinterpret_cast<uint64_t>(JITRT_BoxI64);
        } else if (src_type <= TCUInt32) {
          func = reinterpret_cast<uint64_t>(JITRT_BoxU32);
        } else if (src_type <= TCInt32) {
          func = reinterpret_cast<uint64_t>(JITRT_BoxI32);
        } else if (src_type <= TCDouble) {
          func = reinterpret_cast<uint64_t>(JITRT_BoxDouble);
        } else if (src_type <= (TCUInt8 | TCUInt16)) {
          bbb.AppendCode(
              "ConvertUnsigned {}:CUInt32, {}:{}", tmp, src, src_type);
          src = tmp;
          func = reinterpret_cast<uint64_t>(JITRT_BoxU32);
          src_type = TCUInt32;
        } else if (src_type <= (TCInt8 | TCInt16)) {
          bbb.AppendCode("Convert {}:CInt32, {}:{}", tmp, src, src_type);
          src = tmp;
          src_type = TCInt32;
          func = reinterpret_cast<uint64_t>(JITRT_BoxI32);
        }

        JIT_CHECK(
            func != 0, "unknown box type %s", src_type.toString().c_str());

        bbb.AppendCode(
            "Call {}, {:#x}, {}:{}", instr->GetOutput(), func, src, src_type);

        break;
      }

      case Opcode::kIsNegativeAndErrOccurred: {
        auto instr = static_cast<const IsNegativeAndErrOccurred*>(&i);
        std::string src_name = instr->reg()->name();
        Type src_type = instr->reg()->type();

        // if (src == -1 && tstate->curexc_type != nullptr) { return -1; }
        // else { return 0; }
        auto is_not_negative = GetSafeTempName();
        // Because a failed unbox to unsigned smuggles the bit pattern for a
        // signed -1 in the unsigned value, we can likewise just treat unsigned
        // as signed for purposes of checking for -1 here.
        if (src_type <= (TCInt64 | TCUInt64)) {
          bbb.AppendCode(
              "NotEqual {}, {}:{}, {:#x}",
              is_not_negative,
              src_name,
              TCInt64,
              static_cast<uint64_t>(-1));
        } else {
          // We do have to widen to at least 32 bits due to calling convention
          // always passing a minimum of 32 bits.
          if (src_type <= (TCBool | TCInt8 | TCUInt8 | TCInt16 | TCUInt16)) {
            std::string tmp_name = GetSafeTempName();
            bbb.AppendCode(
                "Convert {}:CInt32, {}:{}", tmp_name, src_name, src_type);
            src_name = tmp_name;
          }
          bbb.AppendCode(
              "NotEqual {}, {}:{}, {:#x}",
              is_not_negative,
              src_name,
              TCInt32,
              -1);
        }
        bbb.AppendCode("Move {}, {:#x}", instr->dst(), 0);
        auto done = GetSafeLabelName();
        auto check_err = GetSafeLabelName();
        bbb.AppendCode("JumpIf {}, {}, {}", is_not_negative, done, check_err);
        bbb.AppendLabel(check_err);
        auto curexc_type = GetSafeTempName();
        bbb.AppendCode(
            "Load {}, __asm_tstate, {}",
            curexc_type,
            offsetof(PyThreadState, curexc_type));
        auto is_no_err_set = GetSafeTempName();
        bbb.AppendCode("Equal {}, {}, {:#x}", is_no_err_set, curexc_type, 0);
        auto set_err = GetSafeLabelName();
        bbb.AppendCode("JumpIf {}, {}, {}", is_no_err_set, done, set_err);
        bbb.AppendLabel(set_err);
        // Set to -1 in the error case
        bbb.AppendCode("Dec {}", instr->dst());
        bbb.AppendLabel(done);
        break;
      }

      case Opcode::kPrimitiveUnbox: {
        auto instr = static_cast<const PrimitiveUnbox*>(&i);
        Type ty = instr->type();
        if (ty <= TCBool) {
          uint64_t true_addr = reinterpret_cast<uint64_t>(Py_True);
          bbb.AppendCode(
              "Equal {} {} {:#x}", instr->dst(), instr->value(), true_addr);
        } else if (ty <= TCDouble) {
          // For doubles, we can directly load the offset into the destination.
          Instruction* value = bbb.getDefInstr(instr->value());
          int32_t offset = offsetof(PyFloatObject, ob_fval);
          bbb.appendInstr(instr->dst(), Instruction::kMove, Ind{value, offset});
        } else if (ty <= TCUInt64) {
          bbb.AppendCall(instr->dst(), JITRT_UnboxU64, instr->value());
        } else if (ty <= TCUInt32) {
          bbb.AppendCall(instr->dst(), JITRT_UnboxU32, instr->value());
        } else if (ty <= TCUInt16) {
          bbb.AppendCall(instr->dst(), JITRT_UnboxU16, instr->value());
        } else if (ty <= TCUInt8) {
          bbb.AppendCall(instr->dst(), JITRT_UnboxU8, instr->value());
        } else if (ty <= TCInt64) {
          bbb.AppendCall(instr->dst(), JITRT_UnboxI64, instr->value());
        } else if (ty <= TCInt32) {
          bbb.AppendCall(instr->dst(), JITRT_UnboxI32, instr->value());
        } else if (ty <= TCInt16) {
          bbb.AppendCall(instr->dst(), JITRT_UnboxI16, instr->value());
        } else if (ty <= TCInt8) {
          bbb.AppendCall(instr->dst(), JITRT_UnboxI8, instr->value());
        } else {
          JIT_CHECK(false, "Cannot unbox type %s", ty.toString().c_str());
        }
        break;
      }
      case Opcode::kPrimitiveUnaryOp: {
        auto instr = static_cast<const PrimitiveUnaryOp*>(&i);
        switch (instr->op()) {
          case PrimitiveUnaryOpKind::kNegateInt:
            bbb.AppendCode("Negate {}, {}", instr->GetOutput(), instr->value());
            break;
          case PrimitiveUnaryOpKind::kInvertInt:
            bbb.AppendCode("Invert {}, {}", instr->GetOutput(), instr->value());
            break;
          case PrimitiveUnaryOpKind::kNotInt:
            bbb.AppendCode(
                "Equal {}, {}, 0", instr->GetOutput(), instr->value());
            break;
          default:
            JIT_CHECK(false, "not implemented unary op %d", (int)instr->op());
            break;
        }
        break;
      }
      case Opcode::kReturn: {
        // TODO support constant operand to Return
        Register* reg = i.GetOperand(0);
        bbb.AppendCode(
            "Return {}:{}", reg->name(), reg->type().unspecialized());
        break;
      }
      case Opcode::kSetCurrentAwaiter: {
        bbb.AppendInvoke(
            JITRT_SetCurrentAwaiter, i.GetOperand(0), "__asm_tstate");
        break;
      }
      case Opcode::kYieldValue: {
        auto instr = static_cast<const YieldValue*>(&i);
        std::stringstream ss;
        ss << "YieldValue " << instr->dst()->name() << ", __asm_tstate,"
           << instr->reg()->name();
        finishYield(bbb, ss, instr);
        break;
      }
      case Opcode::kInitialYield: {
        auto instr = static_cast<const InitialYield*>(&i);
        std::stringstream ss;
        ss << "YieldInitial " << instr->dst()->name() << ", __asm_tstate, ";
        finishYield(bbb, ss, instr);
        break;
      }
      case Opcode::kYieldAndYieldFrom:
      case Opcode::kYieldFrom:
      case Opcode::kYieldFromHandleStopAsyncIteration: {
        std::stringstream ss;
        if (opcode == Opcode::kYieldAndYieldFrom) {
          ss << "YieldFromSkipInitialSend ";
        } else if (opcode == Opcode::kYieldFrom) {
          ss << "YieldFrom ";
        } else {
          ss << "YieldFromHandleStopAsyncIteration ";
        }
        ss << i.GetOutput()->name() << ", __asm_tstate, "
           << i.GetOperand(0)->name() << ", " << i.GetOperand(1)->name();
        finishYield(bbb, ss, static_cast<const DeoptBase*>(&i));
        break;
      }
      case Opcode::kAssign: {
        auto instr = static_cast<const Assign*>(&i);
        bbb.AppendCode("Assign {}, {}", instr->dst(), instr->reg());
        break;
      }
      case Opcode::kBitCast: {
        // BitCasts are purely informative
        break;
      }
      case Opcode::kCondBranch:
      case Opcode::kCondBranchIterNotDone: {
        Instruction* cond = bbb.getDefInstr(i.GetOperand(0));

        if (opcode == Opcode::kCondBranchIterNotDone) {
          auto iter_done_addr =
              reinterpret_cast<uint64_t>(&jit::g_iterDoneSentinel);
          cond = bbb.appendInstr(
              Instruction::kSub,
              OutVReg{OperandBase::k64bit},
              cond,
              Imm{iter_done_addr});
        }

        bbb.appendInstr(Instruction::kCondBranch, cond);
        break;
      }
      case Opcode::kCondBranchCheckType: {
        auto& instr = static_cast<const CondBranchCheckType&>(i);
        auto type = instr.type();
        auto eq_res_var = GetSafeTempName();
        if (type.isExact()) {
          auto type_var = GetSafeTempName();
          bbb.AppendCode(
              "Load {}, {}, {}",
              type_var,
              instr.reg(),
              offsetof(PyObject, ob_type));
          bbb.AppendCode(
              "Equal {}, {}, {:#x}",
              eq_res_var,
              type_var,
              reinterpret_cast<uint64_t>(instr.type().uniquePyType()));
        } else {
          emitSubclassCheck(bbb, eq_res_var, instr.GetOperand(0), type);
        }
        bbb.AppendCode(
            "CondBranch {}, {}, {}",
            eq_res_var,
            instr.true_bb()->id,
            instr.false_bb()->id);
        break;
      }
      case Opcode::kDeleteAttr: {
        std::string tmp = GetSafeTempName();
        auto instr = static_cast<const DeleteAttr*>(&i);
        PyCodeObject* code = instr->frameState()->code;
        PyObject* name = PyTuple_GET_ITEM(code->co_names, instr->name_idx());
        bbb.AppendCode(
            "Call {}:CInt32, {}, {}, {}, 0",
            tmp,
            reinterpret_cast<uint64_t>(PyObject_SetAttr),
            instr->GetOperand(0),
            reinterpret_cast<uint64_t>(name));
        AppendGuard(bbb, "NotNegative", *instr, tmp);
        break;
      }
      case Opcode::kLoadAttr: {
        auto instr = static_cast<const LoadAttr*>(&i);
        std::string tmp_id = GetSafeTempName();
        PyCodeObject* code = instr->frameState()->code;
        PyObject* name = PyTuple_GET_ITEM(code->co_names, instr->name_idx());

        bbb.AppendCode(
            "Move {}, {:#x}", tmp_id, reinterpret_cast<uint64_t>(name));
        bbb.AppendCall(
            instr->dst(),
            jit::LoadAttrCache::invoke,
            Runtime::get()->allocateLoadAttrCache(),
            instr->GetOperand(0),
            tmp_id);
        break;
      }
      case Opcode::kLoadAttrSpecial: {
        auto instr = static_cast<const LoadAttrSpecial*>(&i);
        bbb.AppendCall(
            instr->GetOutput(),
            special_lookup,
            "__asm_tstate",
            instr->GetOperand(0),
            instr->id());
        break;
      }
      case Opcode::kLoadTypeAttrCacheItem: {
        auto instr = static_cast<const LoadTypeAttrCacheItem*>(&i);
        LoadTypeAttrCache* cache = load_type_attr_caches_.at(instr->cache_id());
        PyObject** addr = &cache->items[instr->item_idx()];
        bbb.appendInstr(instr->dst(), Instruction::kMove, MemImm{addr});
        break;
      }
      case Opcode::kFillTypeAttrCache: {
        auto instr = static_cast<const FillTypeAttrCache*>(&i);
        std::string tmp_id = GetSafeTempName();
        PyCodeObject* code = instr->frameState()->code;
        PyObject* name = PyTuple_GET_ITEM(code->co_names, instr->name_idx());
        bbb.AppendCode(
            "Move {}, {:#x}", tmp_id, reinterpret_cast<uint64_t>(name));
        bbb.AppendCall(
            instr->GetOutput(),
            jit::LoadTypeAttrCache::invoke,
            load_type_attr_caches_.at(instr->cache_id()),
            instr->receiver(),
            tmp_id);
        break;
      }
      case Opcode::kLoadMethod: {
        auto instr = static_cast<const LoadMethod*>(&i);

        std::string tmp_id = GetSafeTempName();
        PyCodeObject* code = instr->frameState()->code;
        PyObject* name = PyTuple_GET_ITEM(code->co_names, instr->name_idx());

        bbb.AppendCode(
            "Move {}, {:#x}", tmp_id, reinterpret_cast<uint64_t>(name));

        auto func = reinterpret_cast<uint64_t>(LoadMethodCache::lookupHelper);
        auto cache_entry = Runtime::get()->allocateLoadMethodCache();
        bbb.AppendCode(
            "Call {}, {:#x}, {:#x}, {}, {}",
            instr->dst(),
            func,
            reinterpret_cast<uint64_t>(cache_entry),
            instr->receiver(),
            tmp_id);

        break;
      }
      case Opcode::kGetLoadMethodInstance: {
        hir::Register* dest = i.GetOutput();
        // TODO(T139682668): Generalize this and rewrite to RDX before regalloc.
        bbb.appendInstr(dest, Instruction::kMove, PhyReg{PhyLocation::RDX});
        break;
      }
      case Opcode::kLoadMethodSuper: {
        auto instr = static_cast<const LoadMethodSuper*>(&i);
        std::string tmp_id = GetSafeTempName();
        PyCodeObject* code = instr->frameState()->code;
        PyObject* name = PyTuple_GET_ITEM(code->co_names, instr->name_idx());
        bbb.AppendCode(
            "Move {}, {:#x}", tmp_id, reinterpret_cast<uint64_t>(name));

        auto func = reinterpret_cast<uint64_t>(JITRT_GetMethodFromSuper);
        bbb.AppendCode(
            "Call {}, {:#x}, {}, {}, {}, {}, {}",
            instr->dst(),
            func,
            instr->global_super(),
            instr->type(),
            instr->receiver(),
            tmp_id,
            instr->no_args_in_super_call() ? 1 : 0);
        break;
      }
      case Opcode::kLoadAttrSuper: {
        auto instr = static_cast<const LoadAttrSuper*>(&i);
        std::string tmp_id = GetSafeTempName();
        PyCodeObject* code = instr->frameState()->code;
        PyObject* name = PyTuple_GET_ITEM(code->co_names, instr->name_idx());

        bbb.AppendCode(
            "Move {}, {:#x}", tmp_id, reinterpret_cast<uint64_t>(name));

        bbb.AppendCall(
            instr->dst(),
            JITRT_GetAttrFromSuper,
            instr->global_super(),
            instr->type(),
            instr->receiver(),
            tmp_id,
            instr->no_args_in_super_call());

        break;
      }
      case Opcode::kBinaryOp: {
        auto bin_op = static_cast<const BinaryOp*>(&i);

        // NB: This needs to be in the order that the values appear in the
        // BinaryOpKind enum
        static const binaryfunc helpers[] = {
            PyNumber_Add,
            PyNumber_And,
            PyNumber_FloorDivide,
            PyNumber_Lshift,
            PyNumber_MatrixMultiply,
            PyNumber_Remainder,
            PyNumber_Multiply,
            PyNumber_Or,
            nullptr, // PyNumber_Power is a ternary op.
            PyNumber_Rshift,
            PyObject_GetItem,
            PyNumber_Subtract,
            PyNumber_TrueDivide,
            PyNumber_Xor,
        };
        JIT_CHECK(
            static_cast<unsigned long>(bin_op->op()) < sizeof(helpers),
            "unsupported binop");
        auto op_kind = static_cast<int>(bin_op->op());

        if (bin_op->op() != BinaryOpKind::kPower) {
          bbb.AppendCall(
              bin_op->dst(), helpers[op_kind], bin_op->left(), bin_op->right());
        } else {
          bbb.AppendCall(
              bin_op->dst(),
              PyNumber_Power,
              bin_op->left(),
              bin_op->right(),
              Py_None);
        }
        break;
      }
      case Opcode::kLongBinaryOp: {
        auto instr = static_cast<const LongBinaryOp*>(&i);
        if (instr->op() == BinaryOpKind::kPower) {
          bbb.AppendCall(
              instr->dst(),
              PyLong_Type.tp_as_number->nb_power,
              instr->left(),
              instr->right(),
              Py_None);
        } else {
          bbb.AppendCall(
              instr->dst(), instr->slotMethod(), instr->left(), instr->right());
        }
        break;
      }
      case Opcode::kUnaryOp: {
        auto unary_op = static_cast<const UnaryOp*>(&i);

        // NB: This needs to be in the order that the values appear in the
        // UnaryOpKind enum
        static const unaryfunc helpers[] = {
            JITRT_UnaryNot,
            PyNumber_Negative,
            PyNumber_Positive,
            PyNumber_Invert,
        };
        JIT_CHECK(
            static_cast<unsigned long>(unary_op->op()) < sizeof(helpers),
            "unsupported unaryop");

        auto op_kind = static_cast<int>(unary_op->op());
        bbb.AppendCall(unary_op->dst(), helpers[op_kind], unary_op->operand());
        break;
      }
      case Opcode::kIsInstance: {
        auto instr = static_cast<const IsInstance*>(&i);
        bbb.AppendCall(
            instr->dst(),
            PyObject_IsInstance,
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kCompare: {
        auto instr = static_cast<const Compare*>(&i);
        if (instr->op() == CompareOp::kIn) {
          bbb.AppendCall(
              instr->dst(),
              JITRT_SequenceContains,
              instr->right(),
              instr->left());
          break;
        }
        if (instr->op() == CompareOp::kNotIn) {
          bbb.AppendCall(
              instr->dst(),
              JITRT_SequenceNotContains,
              instr->right(),
              instr->left());
          break;
        }
        if (instr->op() == CompareOp::kIs || instr->op() == CompareOp::kIsNot) {
          // This case should generally not happen, since Compare<Is> and
          // Compare<IsNot> can be rewritten into PrimitiveCompare. We keep it
          // around because optimization passes should not impact correctness.
          bbb.AppendCall(
              instr->dst(),
              JITRT_CompareIs,
              instr->right(),
              instr->left(),
              static_cast<int>(instr->op()));
          break;
        }
        int op = static_cast<int>(instr->op());
        JIT_CHECK(op >= Py_LT, "invalid compare op %d", op);
        JIT_CHECK(op <= Py_GE, "invalid compare op %d", op);
        bbb.AppendCall(
            instr->dst(),
            PyObject_RichCompare,
            instr->left(),
            instr->right(),
            op);
        break;
      }
      case Opcode::kLongCompare: {
        auto instr = static_cast<const LongCompare*>(&i);

        bbb.AppendCall(
            instr->dst(),
            PyLong_Type.tp_richcompare,
            instr->left(),
            instr->right(),
            static_cast<int>(instr->op()));
        break;
      }
      case Opcode::kUnicodeCompare: {
        auto instr = static_cast<const UnicodeCompare*>(&i);

        bbb.AppendCall(
            instr->dst(),
            PyUnicode_Type.tp_richcompare,
            instr->left(),
            instr->right(),
            static_cast<int>(instr->op()));
        break;
      }
      case Opcode::kUnicodeConcat: {
        auto instr = static_cast<const UnicodeConcat*>(&i);

        bbb.AppendCall(
            instr->dst(),
            PyUnicode_Concat,
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kUnicodeRepeat: {
        auto instr = static_cast<const UnicodeRepeat*>(&i);

        bbb.AppendCall(
            instr->dst(),
            PyUnicode_Type.tp_as_sequence->sq_repeat,
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kCompareBool: {
        auto instr = static_cast<const CompareBool*>(&i);

        if (instr->op() == CompareOp::kIn) {
          if (instr->right()->type() <= TUnicodeExact) {
            bbb.AppendCall(
                instr->dst(),
                PyUnicode_Contains,
                instr->right(),
                instr->left());
          } else {
            bbb.AppendCall(
                instr->dst(),
                PySequence_Contains,
                instr->right(),
                instr->left());
          }
        } else if (instr->op() == CompareOp::kNotIn) {
          bbb.AppendCall(
              instr->dst(),
              JITRT_NotContainsBool,
              instr->right(),
              instr->left());
        } else if (
            (instr->op() == CompareOp::kEqual ||
             instr->op() == CompareOp::kNotEqual) &&
            (instr->left()->type() <= TUnicodeExact ||
             instr->right()->type() <= TUnicodeExact)) {
          bbb.AppendCall(
              instr->dst(),
              JITRT_UnicodeEquals,
              instr->left(),
              instr->right(),
              static_cast<int>(instr->op()));
        } else if (
            (instr->op() == CompareOp::kEqual ||
             instr->op() == CompareOp::kNotEqual) &&
            (isTypeWithReasonablePointerEq(instr->left()->type()) ||
             isTypeWithReasonablePointerEq(instr->right()->type()))) {
          bbb.AppendCall(
              instr->dst(),
              PyObject_RichCompareBool,
              instr->left(),
              instr->right(),
              static_cast<int>(instr->op()));
        } else {
          if (instr->op() == CompareOp::kIs ||
              instr->op() == CompareOp::kIsNot) {
            // This case should generally not happen, since CompareBool<Is> and
            // CompareBool<IsNot> can be rewritten into PrimitiveCompare. We
            // keep it around because optimization passes should not impact
            // correctness.
            auto compare_result = GetSafeTempName();
            bbb.AppendCode(
                "Equal {}, {}, {}",
                compare_result,
                instr->left(),
                instr->right());
            auto one = GetSafeTempName();
            bbb.AppendCode("Move {}, {:#x}", one, uint32_t{1});
            bbb.AppendCode(
                "Select {}, {}, {}, {:#x}",
                instr->GetOutput(),
                compare_result,
                one,
                static_cast<uint32_t>(0));
            break;
          }
          bbb.AppendCall(
              instr->dst(),
              JITRT_RichCompareBool,
              instr->left(),
              instr->right(),
              static_cast<int>(instr->op()));
        }
        break;
      }
      case Opcode::kCopyDictWithoutKeys: {
        auto instr = static_cast<const CopyDictWithoutKeys*>(&i);
        bbb.AppendCall(
            instr->dst(),
            JITRT_CopyDictWithoutKeys,
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kIncref: {
        MakeIncref(bbb, i, false);
        break;
      }
      case Opcode::kXIncref: {
        MakeIncref(bbb, i, true);
        break;
      }
      case Opcode::kDecref: {
        MakeDecref(bbb, i, false);
        break;
      }
      case Opcode::kXDecref: {
        MakeDecref(bbb, i, true);
        break;
      }
      case Opcode::kBatchDecref: {
        auto instr = static_cast<const BatchDecref*>(&i);

        std::stringstream ss;
        ss << "BatchDecref ";
        auto nargs = instr->NumOperands();
        for (size_t i = 0; i < nargs; i++) {
          ss << (i == 0 ? "" : ", ") << *instr->GetOperand(i);
        }
        bbb.AppendCode(ss.str());
        break;
      }
      case Opcode::kDeopt: {
        AppendGuard(bbb, "AlwaysFail", static_cast<const DeoptBase&>(i));
        break;
      }
      case Opcode::kUnreachable: {
        bbb.AppendCode("Unreachable");
        break;
      }
      case Opcode::kDeoptPatchpoint: {
        const auto& instr = static_cast<const DeoptPatchpoint&>(i);
        std::size_t deopt_id = bbb.makeDeoptMetadata();
        std::stringstream ss;
        ss << "DeoptPatchpoint " << static_cast<void*>(instr.patcher()) << ", "
           << deopt_id;
        auto& regstates = instr.live_regs();
        for (const auto& reg_state : regstates) {
          ss << ", " << *reg_state.reg;
        }
        bbb.AppendCode(ss.str());
        break;
      }
      case Opcode::kRaiseAwaitableError: {
        const auto& instr = static_cast<const RaiseAwaitableError&>(i);
        bbb.AppendInvoke(
            format_awaitable_error,
            "__asm_tstate",
            instr.GetOperand(0),
            static_cast<int>(instr.with_prev_opcode()),
            static_cast<int>(instr.with_opcode()));
        AppendGuard(bbb, "AlwaysFail", instr);
        break;
      }
      case Opcode::kCheckErrOccurred: {
        const auto& instr = static_cast<const DeoptBase&>(i);
        auto exc_type = GetSafeTempName();
        bbb.AppendCode(
            "Load {}, __asm_tstate, {:#x}",
            exc_type,
            offsetof(PyThreadState, curexc_type));
        AppendGuard(bbb, "Zero", instr, exc_type);
        break;
      }
      case Opcode::kCheckExc:
      case Opcode::kCheckField:
      case Opcode::kCheckFreevar:
      case Opcode::kCheckNeg:
      case Opcode::kCheckVar:
      case Opcode::kGuard:
      case Opcode::kGuardIs: {
        const auto& instr = static_cast<const DeoptBase&>(i);
        std::string_view kind = "NotZero";
        if (instr.IsCheckNeg()) {
          kind = "NotNegative";
        } else if (instr.IsGuardIs()) {
          kind = "Is";
        }
        AppendGuard(bbb, kind, instr, instr.GetOperand(0)->name());
        break;
      }
      case Opcode::kGuardType: {
        const auto& instr = static_cast<const DeoptBase&>(i);
        AppendGuard(bbb, "HasType", instr, instr.GetOperand(0)->name());
        break;
      }
      case Opcode::kRefineType: {
        break;
      }
      case Opcode::kLoadGlobalCached: {
        ThreadedCompileSerialize guard;
        auto instr = static_cast<const LoadGlobalCached*>(&i);
        PyObject* globals = instr->globals();
        env_->code_rt->addReference(globals);
        PyObject* builtins = instr->builtins();
        env_->code_rt->addReference(builtins);
        PyObject* name =
            PyTuple_GET_ITEM(instr->code()->co_names, instr->name_idx());
        auto cache = env_->rt->findGlobalCache(builtins, globals, name);
        bbb.appendInstr(
            instr->dst(), Instruction::kMove, MemImm{cache.valuePtr()});
        break;
      }
      case Opcode::kLoadGlobal: {
        auto instr = static_cast<const LoadGlobal*>(&i);
        PyObject* builtins = instr->frameState()->builtins;
        env_->code_rt->addReference(builtins);
        PyObject* globals = instr->frameState()->globals;
        env_->code_rt->addReference(globals);
        PyObject* name = PyTuple_GET_ITEM(
            instr->frameState()->code->co_names, instr->name_idx());
        bbb.AppendCall(
            instr->GetOutput(), JITRT_LoadGlobal, globals, builtins, name);
        break;
      }
      case Opcode::kStoreAttr: {
        auto instr = static_cast<const StoreAttr*>(&i);

        std::string tmp_id = GetSafeTempName();

        PyCodeObject* code = instr->frameState()->code;
        auto ob_item =
            reinterpret_cast<PyTupleObject*>(code->co_names)->ob_item;
        bbb.AppendCall(
            instr->dst(),
            jit::StoreAttrCache::invoke,
            Runtime::get()->allocateStoreAttrCache(),
            instr->GetOperand(0),
            ob_item[instr->name_idx()],
            instr->GetOperand(1));
        break;
      }
      case Opcode::kVectorCall: {
        auto& instr = static_cast<const VectorCallBase&>(i);
        if (TranslateSpecializedCall(bbb, instr)) {
          break;
        }
        size_t flags = instr.isAwaited() ? Ci_Py_AWAITED_CALL_MARKER : 0;
        emitVectorCall(bbb, instr, flags, false);
        break;
      }
      case Opcode::kVectorCallKW: {
        auto& instr = static_cast<const VectorCallBase&>(i);
        size_t flags = instr.isAwaited() ? Ci_Py_AWAITED_CALL_MARKER : 0;
        emitVectorCall(bbb, instr, flags, true);
        break;
      }
      case Opcode::kVectorCallStatic: {
        auto& instr = static_cast<const VectorCallBase&>(i);
        if (TranslateSpecializedCall(bbb, instr)) {
          break;
        }
        size_t flags = Ci_Py_VECTORCALL_INVOKED_STATICALLY |
            (instr.isAwaited() ? Ci_Py_AWAITED_CALL_MARKER : 0);
        emitVectorCall(bbb, instr, flags, false);
        break;
      }
      case Opcode::kCallCFunc: {
        auto& instr = static_cast<const CallCFunc&>(i);

        std::stringstream ss;
        ss << "Call " << *instr.dst() << ", " << instr.funcAddr();
        for (size_t i = 0; i < instr.NumOperands(); i++) {
          ss << ", " << *instr.GetOperand(i);
        }

        bbb.AppendCode(ss.str());
        break;
      }
      case Opcode::kCallEx: {
        auto& instr = static_cast<const CallEx&>(i);
        auto rt_helper = instr.isAwaited() ? JITRT_CallFunctionExAwaited
                                           : JITRT_CallFunctionEx;
        bbb.AppendCall(
            instr.dst(), rt_helper, instr.func(), instr.pargs(), nullptr);
        break;
      }
      case Opcode::kCallExKw: {
        auto& instr = static_cast<const CallExKw&>(i);
        auto rt_helper = instr.isAwaited() ? JITRT_CallFunctionExAwaited
                                           : JITRT_CallFunctionEx;
        bbb.AppendCall(
            instr.dst(),
            rt_helper,
            instr.func(),
            instr.pargs(),
            instr.kwargs());
        break;
      }
      case Opcode::kCallMethod: {
        auto instr = static_cast<const CallMethod*>(&i);

        std::string s = fmt::format("Vectorcall {}", *instr->dst());
        size_t flags = instr->isAwaited() ? Ci_Py_AWAITED_CALL_MARKER : 0;
        format_to(
            s,
            ", {}, {}, {}, {}",
            reinterpret_cast<uint64_t>(JITRT_CallMethod),
            flags,
            *instr->func(),
            *instr->self());

        for (size_t i = 0, nargs = instr->NumArgs(); i < nargs; i++) {
          format_to(s, ", {}", *instr->arg(i));
        }

        bbb.AppendCode(s + ", 0"); /* kwnames */
        break;
      }

      case Opcode::kCallStatic: {
        auto instr = static_cast<const CallStatic*>(&i);
        auto nargs = instr->NumOperands();

        std::stringstream ss;
        ss << "Call " << instr->dst()->name() << ", "
           << reinterpret_cast<uint64_t>(instr->addr());

        for (size_t i = 0; i < nargs; i++) {
          Type src_type = instr->GetOperand(i)->type();
          if (src_type <= (TCBool | TCUInt8 | TCUInt16)) {
            std::string tmp = GetSafeTempName();
            bbb.AppendCode(
                "ConvertUnsigned {}:CUInt64, {}", tmp, instr->GetOperand(i));
            ss << ", " << tmp << ":CUInt64";
          } else if (src_type <= (TCInt8 | TCInt16)) {
            std::string tmp = GetSafeTempName();
            bbb.AppendCode("Convert {}:CInt64, {}", tmp, instr->GetOperand(i));
            ss << ", " << tmp << ":CInt64";
          } else {
            ss << ", " << instr->GetOperand(i)->name();
          }
        }

        bbb.AppendCode(ss.str());
        break;
      }
      case Opcode::kCallStaticRetVoid: {
        auto instr = static_cast<const CallStaticRetVoid*>(&i);
        auto nargs = instr->NumOperands();

        std::stringstream ss;
        ss << "Invoke " << reinterpret_cast<uint64_t>(instr->addr());

        for (size_t i = 0; i < nargs; i++) {
          ss << ", " << instr->GetOperand(i)->name();
        }

        bbb.AppendCode(ss.str());
        break;
      }
      case Opcode::kInvokeStaticFunction: {
        ThreadedCompileSerialize guard;

        auto instr = static_cast<const InvokeStaticFunction*>(&i);
        auto nargs = instr->NumOperands();
        PyFunctionObject* func = instr->func();

        std::stringstream ss;
        JIT_CHECK(
            !usesRuntimeFunc(func->func_code),
            "Can't statically invoke given function: %s",
            PyUnicode_AsUTF8(func->func_qualname));
        if (_PyJIT_IsCompiled((PyObject*)func)) {
          ss << fmt::format(
              "Call {}, {}",
              instr->dst(),
              reinterpret_cast<uint64_t>(
                  JITRT_GET_STATIC_ENTRY(func->vectorcall)));
        } else {
          void** indir = env_->rt->findFunctionEntryCache(func);
          env_->function_indirections.emplace(func, indir);
          std::string tmp_id = GetSafeTempName();
          bbb.AppendCode(
              "Load {}, {:#x}", tmp_id, reinterpret_cast<uint64_t>(indir));
          ss << "Call " << instr->dst()->name() << ", " << tmp_id;
        }

        for (size_t i = 0; i < nargs; i++) {
          ss << ", " << instr->GetOperand(i)->name();
        }

        bbb.AppendCode(ss.str());

        // functions that return primitives will signal error via edx/xmm1
        std::string_view err_indicator;
        Type ret_type = instr->ret_type();
        if (ret_type <= TCDouble) {
          err_indicator = "reg:xmm1";
        } else if (ret_type <= TPrimitive) {
          err_indicator = "reg:edx";
        } else {
          err_indicator = instr->GetOutput()->name();
        }
        AppendGuard(
            bbb, "NotZero", static_cast<const DeoptBase&>(i), err_indicator);
        break;
      }

      case Opcode::kInvokeMethod: {
        auto instr = static_cast<const InvokeMethod*>(&i);

        std::stringstream ss;
        size_t flags = instr->isAwaited() ? Ci_Py_AWAITED_CALL_MARKER : 0;
        if (instr->isClassmethod()) {
          ss << "Vectorcall " << *instr->dst() << ", "
             << reinterpret_cast<uint64_t>(JITRT_InvokeClassMethod) << ", "
             << flags << ", " << instr->slot();
        } else {
          ss << "Vectorcall " << *instr->dst() << ", "
             << reinterpret_cast<uint64_t>(JITRT_InvokeMethod) << ", " << flags
             << ", " << instr->slot();
        }

        auto nargs = instr->NumOperands();
        for (size_t i = 0; i < nargs; i++) {
          ss << ", " << *instr->GetOperand(i);
        }

        ss << ", 0"; /* kwnames */

        bbb.AppendCode(ss.str());
        break;
      }

      case Opcode::kLoadField: {
        auto instr = static_cast<const LoadField*>(&i);
        hir::Register* dest = instr->dst();
        Instruction* receiver = bbb.getDefInstr(instr->receiver());
        auto offset = static_cast<int32_t>(instr->offset());
        bbb.appendInstr(dest, Instruction::kMove, Ind{receiver, offset});
        break;
      }

      case Opcode::kLoadFieldAddress: {
        auto instr = static_cast<const LoadFieldAddress*>(&i);
        hir::Register* dest = instr->dst();
        Instruction* object = bbb.getDefInstr(instr->object());
        Instruction* offset = bbb.getDefInstr(instr->offset());
        bbb.appendInstr(dest, Instruction::kLea, Ind{object, offset});
        break;
      }

      case Opcode::kStoreField: {
        auto instr = static_cast<const StoreField*>(&i);
        bbb.AppendCode(
            "Store {}, {}, {:#x}",
            instr->value(),
            instr->receiver(),
            instr->offset());
        break;
      }

      case Opcode::kCast: {
        auto instr = static_cast<const Cast*>(&i);
        PyObject* (*func)(PyObject*, PyTypeObject*);
        if (instr->pytype() == &PyFloat_Type) {
          bbb.AppendCall(
              instr->dst(),
              instr->optional() ? JITRT_CastToFloatOptional : JITRT_CastToFloat,
              instr->value());
          break;
        } else if (instr->exact()) {
          func = instr->optional() ? JITRT_CastOptionalExact : JITRT_CastExact;
        } else {
          func = instr->optional() ? JITRT_CastOptional : JITRT_Cast;
        }

        bbb.AppendCall(instr->dst(), func, instr->value(), instr->pytype());
        break;
      }

      case Opcode::kTpAlloc: {
        auto instr = static_cast<const TpAlloc*>(&i);

        bbb.AppendCall(
            instr->dst(),
            instr->pytype()->tp_alloc,
            instr->pytype(),
            /*nitems=*/static_cast<Py_ssize_t>(0));
        break;
      }

      case Opcode::kMakeListTuple: {
        auto instr = static_cast<const MakeListTuple*>(&i);

        bbb.AppendCall(
            instr->dst(),
            instr->is_tuple() ? PyTuple_New : PyList_New,
            static_cast<Py_ssize_t>(instr->nvalues()));
        break;
      }
      case Opcode::kMatchClass: {
        const auto& instr = static_cast<const MatchClass&>(i);
        bbb.AppendCall(
            instr.GetOutput(),
            Ci_match_class,
            "__asm_tstate",
            instr.GetOperand(0),
            instr.GetOperand(1),
            instr.GetOperand(2),
            instr.GetOperand(3));
        break;
      }
      case Opcode::kMatchKeys: {
        auto instr = static_cast<const MatchKeys*>(&i);
        bbb.AppendCall(
            instr->dst(),
            Ci_match_keys,
            "__asm_tstate",
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kInitListTuple: {
        auto instr = static_cast<const InitListTuple*>(&i);
        auto is_tuple = instr->is_tuple();
        Instruction* base = bbb.getDefInstr(instr->GetOperand(0));
        if (!is_tuple && instr->NumOperands() > 1) {
          int32_t offset = offsetof(PyListObject, ob_item);
          base =
              bbb.appendInstr(Instruction::kMove, OutVReg{}, Ind{base, offset});
        }
        const size_t ob_item_offset =
            is_tuple ? offsetof(PyTupleObject, ob_item) : 0;
        for (size_t i = 1; i < instr->NumOperands(); i++) {
          int32_t offset = ob_item_offset + ((i - 1) * kPointerSize);
          bbb.appendInstr(
              Instruction::kMove, OutInd{base, offset}, instr->GetOperand(i));
        }
        break;
      }
      case Opcode::kLoadTupleItem: {
        auto instr = static_cast<const LoadTupleItem*>(&i);
        hir::Register* dest = instr->dst();
        Instruction* tuple = bbb.getDefInstr(instr->tuple());
        auto item_offset = static_cast<int32_t>(
            offsetof(PyTupleObject, ob_item) + instr->idx() * kPointerSize);
        bbb.appendInstr(dest, Instruction::kMove, Ind{tuple, item_offset});
        break;
      }
      case Opcode::kCheckSequenceBounds: {
        auto instr = static_cast<const CheckSequenceBounds*>(&i);
        auto type = instr->GetOperand(1)->type();
        std::string src = instr->GetOperand(1)->name();
        std::string tmp = GetSafeTempName();
        if (type <= (TCInt8 | TCInt16 | TCInt32)) {
          bbb.AppendCode("Convert {}:CInt64, {}:{}", tmp, src, type);
          src = tmp;
        } else if (type <= (TCUInt8 | TCUInt16 | TCUInt32)) {
          bbb.AppendCode("Convert {}:CUInt64, {}:{}", tmp, src, type);
          src = tmp;
        }
        bbb.AppendCall(
            instr->dst(), JITRT_CheckSequenceBounds, instr->GetOperand(0), src);
        break;
      }
      case Opcode::kLoadArrayItem: {
        auto instr = static_cast<const LoadArrayItem*>(&i);
        hir::Register* dest = instr->dst();
        Instruction* ob_item = bbb.getDefInstr(instr->ob_item());
        Instruction* idx = bbb.getDefInstr(instr->idx());
        // TODO(T139547908): x86-64 semantics bleeding into LIR generator.
        uint8_t multiplier = multiplierFromSize(instr->type().sizeInBytes());
        int32_t offset = instr->offset();
        // Might know the index at compile-time.
        auto ind = Ind{ob_item, idx, multiplier, offset};
        if (instr->idx()->type().hasIntSpec()) {
          auto scaled_offset = static_cast<int32_t>(
              instr->idx()->type().intSpec() * instr->type().sizeInBytes() +
              offset);
          ind = Ind{ob_item, scaled_offset};
        }
        bbb.appendInstr(dest, Instruction::kMove, ind);
        break;
      }
      case Opcode::kStoreArrayItem: {
        auto instr = static_cast<const StoreArrayItem*>(&i);
        auto type = instr->type();
        decltype(JITRT_SetI8_InArray)* func = nullptr;

        if (type <= TCInt8) {
          func = JITRT_SetI8_InArray;
        } else if (type <= TCUInt8) {
          func = JITRT_SetU8_InArray;
        } else if (type <= TCInt16) {
          func = JITRT_SetI16_InArray;
        } else if (type <= TCUInt16) {
          func = JITRT_SetU16_InArray;
        } else if (type <= TCInt32) {
          func = JITRT_SetI32_InArray;
        } else if (type <= TCUInt32) {
          func = JITRT_SetU32_InArray;
        } else if (type <= TCInt64) {
          func = JITRT_SetI64_InArray;
        } else if (type <= TCUInt64) {
          func = JITRT_SetU64_InArray;
        } else if (type <= TObject) {
          func = JITRT_SetObj_InArray;
        } else {
          JIT_CHECK(false, "unknown array type %s", type.toString().c_str());
        }

        bbb.AppendInvoke(func, instr->ob_item(), instr->value(), instr->idx());
        break;
      }
      case Opcode::kRepeatList: {
        auto instr = static_cast<const RepeatList*>(&i);
        bbb.AppendCall(
            instr->dst(), Ci_List_Repeat, instr->seq(), instr->num());
        break;
      }
      case Opcode::kRepeatTuple: {
        auto instr = static_cast<const RepeatTuple*>(&i);
        bbb.AppendCall(
            instr->dst(), Ci_Tuple_Repeat, instr->seq(), instr->num());
        break;
      }
      case Opcode::kMakeCheckedList: {
        auto instr = static_cast<const MakeCheckedList*>(&i);
        auto capacity = instr->GetCapacity();
        bbb.AppendCall(
            instr->GetOutput(),
            Ci_CheckedList_New,
            instr->type().typeSpec(),
            static_cast<Py_ssize_t>(capacity));
        break;
      }
      case Opcode::kMakeCheckedDict: {
        auto instr = static_cast<const MakeCheckedDict*>(&i);
        auto capacity = instr->GetCapacity();
        if (capacity == 0) {
          bbb.AppendCall(
              instr->GetOutput(), Ci_CheckedDict_New, instr->type().typeSpec());
        } else {
          bbb.AppendCall(
              instr->GetOutput(),
              Ci_CheckedDict_NewPresized,
              instr->type().typeSpec(),
              static_cast<Py_ssize_t>(capacity));
        }
        break;
      }
      case Opcode::kMakeDict: {
        auto instr = static_cast<const MakeDict*>(&i);
        auto capacity = instr->GetCapacity();
        if (capacity == 0) {
          bbb.AppendCall(instr->GetOutput(), PyDict_New);
        } else {
          bbb.AppendCall(
              instr->GetOutput(),
              _PyDict_NewPresized,
              static_cast<Py_ssize_t>(capacity));
        }
        break;
      }
      case Opcode::kMakeSet: {
        auto instr = static_cast<const MakeSet*>(&i);
        bbb.AppendCall(instr->GetOutput(), PySet_New, nullptr);
        break;
      }
      case Opcode::kDictUpdate: {
        bbb.AppendCall(
            i.GetOutput(),
            JITRT_DictUpdate,
            "__asm_tstate",
            i.GetOperand(0),
            i.GetOperand(1));
        break;
      }
      case Opcode::kDictMerge: {
        bbb.AppendCall(
            i.GetOutput(),
            JITRT_DictMerge,
            "__asm_tstate",
            i.GetOperand(0),
            i.GetOperand(1),
            i.GetOperand(2));
        break;
      }
      case Opcode::kMergeSetUnpack: {
        auto instr = static_cast<const MergeSetUnpack*>(&i);
        bbb.AppendCall(
            instr->GetOutput(),
            _PySet_Update,
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kSetDictItem: {
        auto instr = static_cast<const SetDictItem*>(&i);
        bbb.AppendCall(
            instr->GetOutput(),
            Ci_Dict_SetItemInternal,
            instr->GetOperand(0),
            instr->GetOperand(1),
            instr->GetOperand(2));
        break;
      }
      case Opcode::kSetSetItem: {
        auto instr = static_cast<const SetSetItem*>(&i);
        bbb.AppendCall(
            instr->GetOutput(),
            PySet_Add,
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kSetUpdate: {
        auto instr = static_cast<const SetUpdate*>(&i);
        bbb.AppendCall(
            instr->GetOutput(),
            _PySet_Update,
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kStoreSubscr: {
        auto instr = static_cast<const StoreSubscr*>(&i);
        bbb.AppendCall(
            instr->dst(),
            PyObject_SetItem,
            instr->container(),
            instr->index(),
            instr->value());

        break;
      }
      case Opcode::kDictSubscr: {
        auto instr = static_cast<const DictSubscr*>(&i);
        bbb.AppendCall(
            instr->GetOutput(),
            PyDict_Type.tp_as_mapping->mp_subscript,
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kInPlaceOp: {
        auto instr = static_cast<const InPlaceOp*>(&i);

        // NB: This needs to be in the order that the values appear in the
        // InPlaceOpKind enum
        static const binaryfunc helpers[] = {
            PyNumber_InPlaceAdd,
            PyNumber_InPlaceAnd,
            PyNumber_InPlaceFloorDivide,
            PyNumber_InPlaceLshift,
            PyNumber_InPlaceMatrixMultiply,
            PyNumber_InPlaceRemainder,
            PyNumber_InPlaceMultiply,
            PyNumber_InPlaceOr,
            nullptr, // Power is a ternaryfunc
            PyNumber_InPlaceRshift,
            PyNumber_InPlaceSubtract,
            PyNumber_InPlaceTrueDivide,
            PyNumber_InPlaceXor,
        };
        JIT_CHECK(
            static_cast<unsigned long>(instr->op()) < sizeof(helpers),
            "unsupported inplaceop");

        auto op_kind = static_cast<int>(instr->op());

        if (instr->op() != InPlaceOpKind::kPower) {
          bbb.AppendCall(
              instr->dst(), helpers[op_kind], instr->left(), instr->right());
        } else {
          bbb.AppendCall(
              instr->dst(),
              PyNumber_InPlacePower,
              instr->left(),
              instr->right(),
              Py_None);
        }
        break;
      }
      case Opcode::kBranch: {
        break;
      }
      case Opcode::kBuildSlice: {
        auto instr = static_cast<const BuildSlice*>(&i);

        bbb.AppendCall(
            instr->dst(),
            PySlice_New,
            instr->start(),
            instr->stop(),
            instr->step() != nullptr ? instr->step() : nullptr);

        break;
      }
      case Opcode::kGetIter: {
        auto instr = static_cast<const GetIter*>(&i);
        bbb.AppendCall(
            instr->GetOutput(), PyObject_GetIter, instr->GetOperand(0));
        break;
      }
      case Opcode::kGetLength: {
        auto instr = static_cast<const GetLength*>(&i);
        bbb.AppendCall(
            instr->GetOutput(), JITRT_GetLength, instr->GetOperand(0));
        break;
      }
      case Opcode::kPhi: {
        auto instr = static_cast<const Phi*>(&i);

        std::stringstream ss;
        ss << "Phi " << fmt::format("{}", instr->GetOutput());

        for (size_t i = 0; i < instr->NumOperands(); i++) {
          fmt::print(
              ss,
              ", {:#x}, {}",
              reinterpret_cast<uint64_t>(instr->basic_blocks().at(i)),
              // Phis don't support constant inputs yet
              instr->GetOperand(i)->name());
        }

        bbb.AppendCode(ss.str());
        break;
      }
      case Opcode::kInitFunction: {
        auto instr = static_cast<const InitFunction*>(&i);

        bbb.AppendInvoke(PyEntry_init, instr->GetOperand(0));
        break;
      }
      case Opcode::kMakeFunction: {
        auto instr = static_cast<const MakeFunction*>(&i);
        auto qualname = instr->GetOperand(0);
        auto code = instr->GetOperand(1);
        PyObject* globals = instr->frameState()->globals;
        env_->code_rt->addReference(globals);

        bbb.AppendCall(
            instr->GetOutput(),
            PyFunction_NewWithQualName,
            code,
            globals,
            qualname);
        break;
      }
      case Opcode::kSetFunctionAttr: {
        auto instr = static_cast<const SetFunctionAttr*>(&i);

        bbb.AppendCode(
            "Store {}, {}, {:#x}",
            instr->value(),
            instr->base(),
            instr->offset());
        break;
      }
      case Opcode::kListAppend: {
        auto instr = static_cast<const ListAppend*>(&i);

        bbb.AppendCall(
            instr->dst(),
            Ci_List_APPEND,
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kListExtend: {
        auto instr = static_cast<const ListExtend*>(&i);
        bbb.AppendCall(
            instr->dst(),
            __Invoke_PyList_Extend,
            "__asm_tstate",
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kMakeTupleFromList: {
        auto instr = static_cast<const MakeTupleFromList*>(&i);
        bbb.AppendCall(instr->dst(), PyList_AsTuple, instr->GetOperand(0));
        break;
      }
      case Opcode::kGetTuple: {
        auto instr = static_cast<const GetTuple*>(&i);

        bbb.AppendCall(instr->dst(), PySequence_Tuple, instr->GetOperand(0));
        break;
      }
      case Opcode::kInvokeIterNext: {
        auto instr = static_cast<const InvokeIterNext*>(&i);
        bbb.AppendCall(
            instr->GetOutput(), jit::invokeIterNext, instr->GetOperand(0));
        break;
      }
      case Opcode::kLoadEvalBreaker: {
        // NB: This corresponds to an atomic load with
        // std::memory_order_relaxed. It's correct on x86-64 but probably isn't
        // on other architectures.
        static_assert(
            sizeof(reinterpret_cast<PyInterpreterState*>(0)
                       ->ceval.eval_breaker._value) == 4,
            "Eval breaker is not a 4 byte value");
        hir::Register* dest = i.GetOutput();
        JIT_CHECK(dest->type() == TCInt32, "eval breaker output should be int");
        // tstate->interp->ceval.eval_breaker
        Instruction* tstate = env_->asm_tstate;
        Instruction* interp = bbb.appendInstr(
            Instruction::kMove,
            OutVReg{OperandBase::k64bit},
            Ind{tstate, offsetof(PyThreadState, interp)});
        bbb.appendInstr(
            dest,
            Instruction::kMove,
            Ind{interp, offsetof(PyInterpreterState, ceval.eval_breaker)});
        break;
      }
      case Opcode::kRunPeriodicTasks: {
        bbb.AppendCall(
            i.GetOutput(), eval_frame_handle_pending, "__asm_tstate");
        break;
      }
      case Opcode::kSnapshot: {
        // Snapshots are purely informative
        break;
      }
      case Opcode::kUseType: {
        // UseTypes are purely informative
        break;
      }
      case Opcode::kHintType: {
        // HintTypes are purely informative
        break;
      }
      case Opcode::kBeginInlinedFunction: {
        // TODO(T109706798): Support calling from generators and inlining
        // generators.
        // TODO(emacs): Link all shadow frame prev pointers in function
        // prologue, since they need not happen with every call -- just the
        // data pointers need to be reset with every call.
        // TODO(emacs): If we manage to optimize leaf calls to a series of
        // non-deopting instructions, remove BeginInlinedFunction and
        // EndInlinedFunction completely.
        if (py_debug) {
          bbb.AppendInvoke(assertShadowCallStackConsistent, "__asm_tstate");
        }
        auto instr = static_cast<const BeginInlinedFunction*>(&i);
        auto caller_shadow_frame = GetSafeTempName();
        bbb.AppendCode(
            "Lea {}, __native_frame_base, {}",
            caller_shadow_frame,
            shadowFrameOffsetBefore(instr));
        // There is already a shadow frame for the caller function.
        auto callee_shadow_frame = GetSafeTempName();
        bbb.AppendCode(
            "Lea {}, __native_frame_base, {}",
            callee_shadow_frame,
            shadowFrameOffsetOf(instr));
        bbb.AppendCode(
            "Store {}, {}, {}",
            caller_shadow_frame,
            callee_shadow_frame,
            SHADOW_FRAME_FIELD_OFF(prev));
        // Set code object data
        PyCodeObject* code = instr->code();
        env_->code_rt->addReference(reinterpret_cast<PyObject*>(code));
        PyObject* globals = instr->globals();
        env_->code_rt->addReference(reinterpret_cast<PyObject*>(globals));
        PyObject* builtins = instr->builtins();
        env_->code_rt->addReference(builtins);
        RuntimeFrameState* rtfs =
            env_->code_rt->allocateRuntimeFrameState(code, builtins, globals);
        uintptr_t data = _PyShadowFrame_MakeData(rtfs, PYSF_RTFS, PYSF_JIT);
        auto data_reg = GetSafeTempName();
        bbb.AppendCode("Move {}, {:#x}", data_reg, data);
        bbb.AppendCode(
            "Store {}, {}, {}",
            data_reg,
            callee_shadow_frame,
            SHADOW_FRAME_FIELD_OFF(data));
        // Set orig_data
        // This is only necessary when in normal-frame mode because the frame
        // is already materialized on function entry. It is lazily filled when
        // the frame is materialized in shadow-frame mode.
        if (func_->frameMode == jit::hir::FrameMode::kNormal) {
          bbb.AppendCode(
              "Store {}, {}, {}",
              data_reg,
              callee_shadow_frame,
              JIT_SHADOW_FRAME_FIELD_OFF(orig_data));
        }
        // Set our shadow frame as top of shadow stack
        bbb.AppendCode(
            "Store {}, __asm_tstate, {}",
            callee_shadow_frame,
            offsetof(PyThreadState, shadow_frame));
        if (py_debug) {
          bbb.AppendInvoke(assertShadowCallStackConsistent, "__asm_tstate");
        }
        break;
      }
      case Opcode::kEndInlinedFunction: {
        // TODO(T109706798): Support calling from generators and inlining
        // generators.
        if (py_debug) {
          bbb.AppendInvoke(assertShadowCallStackConsistent, "__asm_tstate");
        }
        // callee_shadow_frame <- tstate.shadow_frame
        auto callee_shadow_frame = GetSafeTempName();
        bbb.AppendCode(
            "Load {}, __asm_tstate, {}",
            callee_shadow_frame,
            offsetof(PyThreadState, shadow_frame));

        // Check if the callee has been materialized into a PyFrame. Use the
        // flags below.
        static_assert(
            PYSF_PYFRAME == 1 && _PyShadowFrame_NumPtrKindBits == 2,
            "Unexpected constants");
        auto shadow_frame_data = GetSafeTempName();
        bbb.AppendCode(
            "Load {}, {}, {}",
            shadow_frame_data,
            callee_shadow_frame,
            SHADOW_FRAME_FIELD_OFF(data));
        bbb.AppendCode("BitTest {}, 0", shadow_frame_data);

        // caller_shadow_frame <- callee_shadow_frame.prev
        auto caller_shadow_frame = GetSafeTempName();
        bbb.AppendCode(
            "Load {}, {}, {}",
            caller_shadow_frame,
            callee_shadow_frame,
            SHADOW_FRAME_FIELD_OFF(prev));
        // caller_shadow_frame -> tstate.shadow_frame
        bbb.AppendCode(
            "Store {}, __asm_tstate, {}",
            caller_shadow_frame,
            offsetof(PyThreadState, shadow_frame));

        // Unlink PyFrame if needed. Someone might have materialized all of the
        // PyFrames via PyEval_GetFrame or similar.
        auto done = GetSafeLabelName();
        bbb.AppendCode("BranchNC {}", done);
        // TODO(T109445584): Remove this unused label.
        bbb.AppendLabel(GetSafeLabelName());
        bbb.AppendInvoke(JITRT_UnlinkFrame, "__asm_tstate");
        bbb.AppendLabel(done);
        if (py_debug) {
          bbb.AppendInvoke(assertShadowCallStackConsistent, "__asm_tstate");
        }
        break;
      }
      case Opcode::kIsTruthy: {
        bbb.AppendCall(i.GetOutput(), PyObject_IsTrue, i.GetOperand(0));
        break;
      }
      case Opcode::kImportFrom: {
        auto& instr = static_cast<const ImportFrom&>(i);
        PyCodeObject* code = instr.frameState()->code;
        PyObject* name = PyTuple_GET_ITEM(code->co_names, instr.nameIdx());
        bbb.AppendCall(
            i.GetOutput(),
            _Py_DoImportFrom,
            "__asm_tstate",
            instr.module(),
            name);
        break;
      }
      case Opcode::kImportName: {
        auto instr = static_cast<const ImportName*>(&i);
        PyCodeObject* code = instr->frameState()->code;
        PyObject* name = PyTuple_GET_ITEM(code->co_names, instr->name_idx());
        bbb.AppendCall(
            i.GetOutput(),
            JITRT_ImportName,
            "__asm_tstate",
            name,
            instr->GetFromList(),
            instr->GetLevel());
        break;
      }
      case Opcode::kRaise: {
        const auto& instr = static_cast<const Raise&>(i);
        std::string exc = "0";
        std::string cause = "0";
        switch (instr.kind()) {
          case Raise::Kind::kReraise:
            break;
          case Raise::Kind::kRaiseWithExcAndCause:
            cause = instr.GetOperand(1)->name();
            // Fallthrough
          case Raise::Kind::kRaiseWithExc:
            exc = instr.GetOperand(0)->name();
        }
        bbb.AppendCode(
            "Call {}, {:#x}, __asm_tstate, {}, {}",
            GetSafeTempName(),
            reinterpret_cast<uint64_t>(&do_raise),
            exc,
            cause);
        AppendGuard(bbb, "AlwaysFail", instr);
        break;
      }
      case Opcode::kRaiseStatic: {
        const auto& instr = static_cast<const RaiseStatic&>(i);
        std::stringstream args;
        for (size_t i = 0; i < instr.NumOperands(); i++) {
          args << ", " << *instr.GetOperand(i);
        }
        bbb.AppendCode(
            "Invoke {:#x}, {:#x}, {:#x}{}",
            reinterpret_cast<uint64_t>(&PyErr_Format),
            reinterpret_cast<uint64_t>(instr.excType()),
            reinterpret_cast<uint64_t>(instr.fmt()),
            args.str());
        AppendGuard(bbb, "AlwaysFail", instr);
        break;
      }
      case Opcode::kFormatValue: {
        const auto& instr = static_cast<const FormatValue&>(i);
        bbb.AppendCall(
            instr.dst(),
            JITRT_FormatValue,
            "__asm_tstate",
            instr.GetOperand(0),
            instr.GetOperand(1),
            instr.conversion());
        break;
      }
      case Opcode::kBuildString: {
        const auto& instr = static_cast<const BuildString&>(i);

        // using vectorcall here although this is not strictly a vector call.
        // the callable is always null, and all the components to be
        // concatenated will be in the args argument.
        std::string s = fmt::format(
            "Vectorcall {}, {}, 0, 0",
            instr.dst(),
            reinterpret_cast<uint64_t>(JITRT_BuildString));
        for (size_t i = 0; i < instr.NumOperands(); i++) {
          s += fmt::format(", {}", instr.GetOperand(i));
        }

        s += ", 0";

        bbb.AppendCode(s);
        break;
      }
      case Opcode::kWaitHandleLoadWaiter: {
        const auto& instr = static_cast<const WaitHandleLoadWaiter&>(i);
        Instruction* base = bbb.getDefInstr(instr.reg());
        int32_t offset = offsetof(Ci_PyWaitHandleObject, wh_waiter);
        bbb.appendInstr(instr.dst(), Instruction::kMove, Ind{base, offset});
        break;
      }
      case Opcode::kWaitHandleLoadCoroOrResult: {
        const auto& instr = static_cast<const WaitHandleLoadCoroOrResult&>(i);
        Instruction* base = bbb.getDefInstr(instr.reg());
        int32_t offset = offsetof(Ci_PyWaitHandleObject, wh_coro_or_result);
        bbb.appendInstr(instr.dst(), Instruction::kMove, Ind{base, offset});
        break;
      }
      case Opcode::kWaitHandleRelease: {
        const auto& instr = static_cast<const WaitHandleRelease&>(i);
        std::string null_var = GetSafeTempName();
        bbb.AppendCode(
            "Store 0, {}, {}",
            instr.reg(),
            offsetof(Ci_PyWaitHandleObject, wh_coro_or_result));
        bbb.AppendCode(
            "Store 0, {}, {}",
            instr.reg(),
            offsetof(Ci_PyWaitHandleObject, wh_waiter));
        break;
      }
      case Opcode::kDeleteSubscr: {
        auto tmp = GetSafeTempName();
        const auto& instr = static_cast<const DeleteSubscr&>(i);
        bbb.AppendCode(
            "Call {}:CInt32, {:#x}, {}, {}",
            tmp,
            reinterpret_cast<uint64_t>(PyObject_DelItem),
            instr.GetOperand(0),
            instr.GetOperand(1));
        AppendGuard(bbb, "NotNegative", instr, tmp);
        break;
      }
      case Opcode::kUnpackExToTuple: {
        auto instr = static_cast<const UnpackExToTuple*>(&i);
        bbb.AppendCall(
            instr->dst(),
            JITRT_UnpackExToTuple,
            "__asm_tstate",
            instr->seq(),
            instr->before(),
            instr->after());
        break;
      }
      case Opcode::kGetAIter: {
        auto& instr = static_cast<const GetAIter&>(i);
        bbb.AppendCall(
            instr.dst(), Ci_GetAIter, "__asm_tstate", instr.GetOperand(0));
        break;
      }
      case Opcode::kGetANext: {
        auto& instr = static_cast<const GetAIter&>(i);
        bbb.AppendCall(
            instr.dst(), Ci_GetANext, "__asm_tstate", instr.GetOperand(0));
        break;
      }
    }

    if (auto db = i.asDeoptBase()) {
      switch (db->opcode()) {
        case Opcode::kCheckErrOccurred:
        case Opcode::kCheckExc:
        case Opcode::kCheckField:
        case Opcode::kCheckVar:
        case Opcode::kDeleteAttr:
        case Opcode::kDeleteSubscr:
        case Opcode::kDeopt:
        case Opcode::kDeoptPatchpoint:
        case Opcode::kGuard:
        case Opcode::kGuardIs:
        case Opcode::kGuardType:
        case Opcode::kInvokeStaticFunction:
        case Opcode::kRaiseAwaitableError:
        case Opcode::kRaise:
        case Opcode::kRaiseStatic: {
          break;
        }
        case Opcode::kCompare: {
          auto op = static_cast<const Compare&>(i).op();
          if (op == CompareOp::kIs || op == CompareOp::kIsNot) {
            // These are implemented using pointer equality and cannot
            // throw.
            break;
          }
          emitExceptionCheck(*db, bbb);
          break;
        }
        case Opcode::kPrimitiveBox: {
          auto& pb = static_cast<const PrimitiveBox&>(i);
          JIT_DCHECK(
              !(pb.value()->type() <= TCBool), "should not be able to deopt");
          emitExceptionCheck(*db, bbb);
          break;
        }
        default: {
          emitExceptionCheck(*db, bbb);
          break;
        }
      }
    }
  }

  // the last instruction must be kBranch, kCondBranch, or kReturn
  auto bbs = bbb.Generate();
  basic_blocks_.insert(basic_blocks_.end(), bbs.begin(), bbs.end());

  return {bbs.front(), bbs.back()};
}

std::string LIRGenerator::GetSafeTempName() {
  return fmt::format("__codegen_temp_{}", temp_id++);
}

std::string LIRGenerator::GetSafeLabelName() {
  return fmt::format("__codegen_label_{}", label_id++);
}

void LIRGenerator::FixPhiNodes(
    UnorderedMap<const hir::BasicBlock*, TranslatedBlock>& bb_map) {
  for (auto& bb : basic_blocks_) {
    bb->foreachPhiInstr([&bb_map](Instruction* instr) {
      auto num_inputs = instr->getNumInputs();
      for (size_t i = 0; i < num_inputs; i += 2) {
        auto o = instr->getInput(i);
        auto opnd = static_cast<Operand*>(o);
        auto hir_bb =
            reinterpret_cast<jit::hir::BasicBlock*>(opnd->getBasicBlock());
        opnd->setBasicBlock(bb_map.at(hir_bb).last);
      }
    });
  }
}

void LIRGenerator::FixOperands() {
  for (auto& pair : env_->operand_to_fix) {
    auto& name = pair.first;

    auto def_instr = map_get(env_->output_map, name, nullptr);

    if (def_instr == nullptr) {
      // the output has to be copy propagated.
      auto iter = env_->copy_propagation_map.find(name);
      const char* prop_name = nullptr;
      while (iter != env_->copy_propagation_map.end()) {
        prop_name = iter->second.c_str();
        iter = env_->copy_propagation_map.find(prop_name);
      }

      if (prop_name != nullptr) {
        def_instr = map_get(env_->output_map, prop_name, nullptr);
      }
    }

    JIT_DCHECK(
        def_instr != nullptr, "unable to find def instruction for '%s'.", name);

    auto& operands = pair.second;
    for (auto& operand : operands) {
      operand->setLinkedInstr(def_instr);
    }
  }
}

} // namespace jit::lir

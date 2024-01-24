// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "cinderx/Jit/lir/generator.h"

#include "Python.h"
#include "cinder/exports.h"
#include "cinderx/Interpreter/interpreter.h"
#include "cinderx/StaticPython/checked_dict.h"
#include "cinderx/StaticPython/checked_list.h"
#include "internal/pycore_import.h"
#include "internal/pycore_interp.h"
#include "internal/pycore_pyerrors.h"
#include "internal/pycore_pystate.h"
#include "internal/pycore_shadow_frame.h"
#include "listobject.h"
#include "pystate.h"

#include "cinderx/Jit/codegen/x86_64.h"
#include "cinderx/Jit/config.h"
#include "cinderx/Jit/containers.h"
#include "cinderx/Jit/deopt.h"
#include "cinderx/Jit/frame.h"
#include "cinderx/Jit/hir/analysis.h"
#include "cinderx/Jit/inline_cache.h"
#include "cinderx/Jit/jit_rt.h"
#include "cinderx/Jit/lir/block_builder.h"
#include "cinderx/Jit/log.h"
#include "cinderx/Jit/pyjit.h"
#include "cinderx/Jit/runtime_support.h"
#include "cinderx/Jit/threaded_compile.h"
#include "cinderx/Jit/util.h"

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <functional>
#include <sstream>

// XXX: this file needs to be revisited when we optimize HIR-to-LIR translation
// in codegen.cpp/h. Currently, this file is almost an identical copy from
// codegen.cpp with some interfaces changes so that it works with the new
// LIR.

using namespace jit::hir;

namespace jit::lir {

namespace {

constexpr size_t kRefcountOffset = offsetof(PyObject, ob_refcnt);

// _Py_RefTotal is only defined when Py_REF_DEBUG is defined.
void* kRefTotalAddr =
#ifdef Py_REF_DEBUG
    &_Py_RefTotal
#else
    nullptr
#endif
    ;

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

void emitVectorCall(
    BasicBlockBuilder& bbb,
    const VectorCallBase& hir_instr,
    size_t flags,
    bool kwnames) {
  Instruction* instr = bbb.appendInstr(
      hir_instr.dst(),
      Instruction::kVectorCall,
      // TODO(T140174965): This should be MemImm.
      Imm{reinterpret_cast<uint64_t>(_PyObject_Vectorcall)},
      Imm{flags});
  for (hir::Register* arg : hir_instr.GetOperands()) {
    instr->addOperands(VReg{bbb.getDefInstr(arg)});
  }
  if (!kwnames) {
    // TODO(T140174965): This should be MemImm.
    instr->addOperands(Imm{0});
  }
}

void finishYield(
    BasicBlockBuilder& bbb,
    Instruction* instr,
    const DeoptBase* hir_instr) {
  for (const RegState& rs : hir_instr->live_regs()) {
    instr->addOperands(VReg{bbb.getDefInstr(rs.reg)});
  }
  instr->addOperands(Imm{hir_instr->live_regs().size()});
  instr->addOperands(Imm{bbb.makeDeoptMetadata()});
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

int bytes_from_cint_type(Type type) {
  if (type <= TCInt8 || type <= TCUInt8) {
    return 1;
  } else if (type <= TCInt16 || type <= TCUInt16) {
    return 2;
  } else if (type <= TCInt32 || type <= TCUInt32) {
    return 3;
  } else if (type <= TCInt64 || type <= TCUInt64) {
    return 4;
  }
  JIT_ABORT("Bad primitive int type: ({})", type);
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

Instruction*
emitSubclassCheck(BasicBlockBuilder& bbb, hir::Register* obj, Type type) {
  // Fast path: a subset of builtin types that have Py_TPFLAGS
  uint64_t fptr = 0;
#define GET_FPTR(name)                                            \
  if (type <= T##name) {                                          \
    fptr = reinterpret_cast<uint64_t>(__Invoke_Py##name##_Check); \
  } else
  FOREACH_FAST_BUILTIN(GET_FPTR) {
    JIT_ABORT("Unsupported subclass check in CondBranchCheckType");
  }
#undef GET_FPTR
  return bbb.appendInstr(
      Instruction::kCall,
      OutVReg{OperandBase::k8bit},
      // TODO(T140174965): This should be MemImm.
      Imm{fptr},
      obj);
}

#undef FOREACH_FAST_BUILTIN

ssize_t shadowFrameOffsetBefore(const InlineBase* instr) {
  return -instr->inlineDepth() * ssize_t{kJITShadowFrameSize};
}

ssize_t shadowFrameOffsetOf(const InlineBase* instr) {
  return shadowFrameOffsetBefore(instr) - ssize_t{kJITShadowFrameSize};
}

// x86 encodes scales as size==2**X, so this does log2(num_bytes), but we have
// a limited set of inputs.
uint8_t multiplierFromSize(int num_bytes) {
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
  JIT_ABORT("Unexpected num_bytes {}", num_bytes);
}

} // namespace

LIRGenerator::LIRGenerator(
    const jit::hir::Function* func,
    jit::codegen::Environ* env)
    : func_(func), env_(env) {
  for (int i = 0, n = func->env.numLoadTypeAttrCaches(); i < n; i++) {
    load_type_attr_caches_.emplace_back(
        Runtime::get()->allocateLoadTypeAttrCache());
  }
  for (int i = 0, n = func->env.numLoadTypeMethodCaches(); i < n; i++) {
    load_type_method_caches_.emplace_back(
        Runtime::get()->allocateLoadTypeMethodCache());
  }
}

BasicBlock* LIRGenerator::GenerateEntryBlock() {
  auto block = lir_func_->allocateBasicBlock();
  auto bindVReg = [&](int phy_reg) {
    auto instr = block->allocateInstr(Instruction::kBind, nullptr);
    instr->output()->setVirtualRegister();
    instr->allocatePhyRegisterInput(phy_reg);
    return instr;
  };

  env_->asm_extra_args = bindVReg(jit::codegen::PhyLocation::R10);
  env_->asm_tstate = bindVReg(jit::codegen::PhyLocation::R11);
  if (func_->uses_runtime_func) {
    env_->asm_func = bindVReg(jit::codegen::PhyLocation::RDI);
  }

  return block;
}

BasicBlock* LIRGenerator::GenerateExitBlock() {
  auto block = lir_func_->allocateBasicBlock();
  auto instr = block->allocateInstr(Instruction::kMove, nullptr);
  instr->addOperands(OutPhyReg{PhyLocation::RDI}, VReg{env_->asm_tstate});
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
            instr.GetOutput(), instr.GetOperand(0));
      }
    }
  }
}

std::unique_ptr<jit::lir::Function> LIRGenerator::TranslateFunction() {
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

  resolvePhiOperands(bb_map);

  return function;
}

void LIRGenerator::appendGuardAlwaysFail(
    BasicBlockBuilder& bbb,
    const hir::DeoptBase& hir_instr) {
  auto deopt_id = bbb.makeDeoptMetadata();
  Instruction* instr = bbb.appendInstr(
      Instruction::kGuard,
      Imm{InstrGuardKind::kAlwaysFail},
      Imm{deopt_id},
      Imm{0},
      Imm{0});
  addLiveRegOperands(bbb, instr, hir_instr);
}

void LIRGenerator::addLiveRegOperands(
    BasicBlockBuilder& bbb,
    Instruction* instr,
    const hir::DeoptBase& hir_instr) {
  auto& regstates = hir_instr.live_regs();
  for (const auto& reg_state : regstates) {
    hir::Register* reg = reg_state.reg;
    instr->addOperands(VReg{bbb.getDefInstr(reg)});
  }
}

// Attempt to emit a type-specialized call, returning true if successful.
bool LIRGenerator::TranslateSpecializedCall(
    BasicBlockBuilder& bbb,
    const hir::VectorCallBase& hir_instr) {
  hir::Register* callable = hir_instr.func();
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
      if (hir_instr.numArgs() == 1) {
        bbb.appendCallInstruction(
            hir_instr.dst(), Ci_Builtin_Next_Core, hir_instr.arg(0), nullptr);
        return true;
      } else if (hir_instr.numArgs() == 2) {
        bbb.appendCallInstruction(
            hir_instr.dst(),
            Ci_Builtin_Next_Core,
            hir_instr.arg(0),
            hir_instr.arg(1));
        return true;
      }
    }
    switch (
        PyCFunction_GET_FLAGS(callee) &
        (METH_VARARGS | METH_FASTCALL | METH_NOARGS | METH_O | METH_KEYWORDS)) {
      case METH_NOARGS:
        if (hir_instr.numArgs() == 0) {
          bbb.appendCallInstruction(
              hir_instr.dst(),
              PyCFunction_GET_FUNCTION(callee),
              PyCFunction_GET_SELF(callee),
              nullptr);
          return true;
        }
        break;
      case METH_O:
        if (hir_instr.numArgs() == 1) {
          bbb.appendCallInstruction(
              hir_instr.dst(),
              PyCFunction_GET_FUNCTION(callee),
              PyCFunction_GET_SELF(callee),
              hir_instr.arg(0));
          return true;
        }
        break;
    }
  }

  auto func = [&]() {
    ThreadedCompileSerialize guard;
    return _PyVectorcall_Function(callee);
  }();
  if (func == nullptr ||
      func == reinterpret_cast<vectorcallfunc>(PyEntry_LazyInit)) {
    // Bail if the object doesn't support vectorcall, or if it's a function
    // that hasn't been initialized yet.
    return false;
  }

  Instruction* instr = bbb.appendInstr(
      hir_instr.dst(),
      Instruction::kVectorCall,
      // TODO(T140174965): This should be MemImm.
      Imm{reinterpret_cast<uint64_t>(func)},
      Imm{0});
  for (hir::Register* arg : hir_instr.GetOperands()) {
    instr->addOperands(VReg{bbb.getDefInstr(arg)});
  }
  instr->addOperands(Imm{0});
  return true;
}

void LIRGenerator::emitExceptionCheck(
    const jit::hir::DeoptBase& i,
    jit::lir::BasicBlockBuilder& bbb) {
  hir::Register* out = i.GetOutput();
  if (out->isA(TBottom)) {
    appendGuardAlwaysFail(bbb, i);
  } else {
    auto kind = out->isA(TCSigned) ? InstrGuardKind::kNotNegative
                                   : InstrGuardKind::kNotZero;
    appendGuard(bbb, kind, i, bbb.getDefInstr(out));
  }
}

void LIRGenerator::MakeIncref(
    BasicBlockBuilder& bbb,
    const hir::Instr& instr,
    bool xincref) {
  Register* obj = instr.GetOperand(0);

  // Don't generate anything for immortal objects.
  if (kImmortalInstances && !obj->type().couldBe(TMortalObject)) {
    return;
  }

  auto end_incref = bbb.allocateBlock(GetSafeLabelName());
  if (xincref) {
    auto cont = bbb.allocateBlock(GetSafeLabelName());
    bbb.appendBranch(Instruction::kCondBranch, obj, cont, end_incref);
    bbb.appendBlock(cont);
  }

  // If this could be an immortal object then we need to load the refcount as a
  // 32-bit integer to see if it overflows on increment, indicating that it's
  // immortal.  For mortal objects the refcount is a regular 64-bit integer.
  if (kImmortalInstances && obj->type().couldBe(TImmortalObject)) {
    auto mortal = bbb.allocateBlock(GetSafeLabelName());
    Instruction* r1 = bbb.appendInstr(
        OutVReg{OperandBase::k32bit},
        Instruction::kMove,
        Ind{bbb.getDefInstr(obj), kRefcountOffset});
    bbb.appendInstr(Instruction::kInc, r1);
    bbb.appendBranch(Instruction::kBranchE, end_incref);
    bbb.appendBlock(mortal);
    bbb.appendInstr(
           OutInd{bbb.getDefInstr(obj), kRefcountOffset},
           Instruction::kMove,
           r1)
        ->output()
        ->setDataType(Operand::k32bit);
  } else {
    Instruction* r1 = bbb.appendInstr(
        OutVReg{},
        Instruction::kMove,
        Ind{bbb.getDefInstr(obj), kRefcountOffset});
    bbb.appendInstr(Instruction::kInc, r1);
    bbb.appendInstr(
        OutInd{bbb.getDefInstr(obj), kRefcountOffset}, Instruction::kMove, r1);
  }

  if (kRefTotalAddr != 0) {
    auto r0 =
        bbb.appendInstr(OutVReg{}, Instruction::kMove, MemImm{kRefTotalAddr});
    bbb.appendInstr(Instruction::kInc, r0);
    bbb.appendInstr(OutMemImm{kRefTotalAddr}, Instruction::kMove, r0);
  }
  bbb.appendBlock(end_incref);
}

void LIRGenerator::MakeDecref(
    BasicBlockBuilder& bbb,
    const jit::hir::Instr& instr,
    bool xdecref) {
  Register* obj = instr.GetOperand(0);

  // Don't generate anything for immortal objects.
  if (kImmortalInstances && !obj->type().couldBe(TMortalObject)) {
    return;
  }

  auto end_decref = bbb.allocateBlock(GetSafeLabelName());
  if (xdecref) {
    auto cont = bbb.allocateBlock(GetSafeLabelName());
    bbb.appendBranch(Instruction::kCondBranch, obj, cont, end_decref);
    bbb.appendBlock(cont);
  }

  Instruction* r1 = bbb.appendInstr(
      OutVReg{},
      Instruction::kMove,
      Ind{bbb.getDefInstr(obj), kRefcountOffset});

  if (kImmortalInstances && obj->type().couldBe(TImmortalObject)) {
    auto mortal = bbb.allocateBlock(GetSafeLabelName());
    bbb.appendInstr(Instruction::kTest32, r1, r1);
    bbb.appendBranch(Instruction::kBranchS, end_decref);
    bbb.appendBlock(mortal);
  }

  if (kRefTotalAddr != 0) {
    auto r0 =
        bbb.appendInstr(OutVReg{}, Instruction::kMove, MemImm{kRefTotalAddr});
    bbb.appendInstr(Instruction::kDec, r0);
    bbb.appendInstr(OutMemImm{kRefTotalAddr}, Instruction::kMove, r0);
  }

  auto dealloc = bbb.allocateBlock(GetSafeLabelName());
  bbb.appendInstr(Instruction::kDec, r1);
  bbb.appendInstr(
      OutInd{bbb.getDefInstr(obj), kRefcountOffset}, Instruction::kMove, r1);
  bbb.appendBranch(Instruction::kBranchNZ, end_decref);
  bbb.appendBlock(dealloc);
  if (getConfig().multiple_code_sections) {
    dealloc->setSection(codegen::CodeSection::kCold);
  }

  bbb.appendInvokeInstruction(JITRT_Dealloc, obj);
  bbb.appendBlock(end_decref);
}

LIRGenerator::TranslatedBlock LIRGenerator::TranslateOneBasicBlock(
    const hir::BasicBlock* hir_bb) {
  BasicBlockBuilder bbb{env_, lir_func_};
  BasicBlock* entry_block = bbb.allocateBlock("__main__");
  bbb.switchBlock(entry_block);

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
        bbb.appendCallInstruction(
            instr->dst(), PyCell_New, instr->GetOperand(0));
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
        bbb.appendInstr(
            OutInd{
                bbb.getDefInstr(instr->GetOperand(0)),
                int32_t{offsetof(PyCellObject, ob_ref)}},
            Instruction::kMove,
            instr->GetOperand(1));
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
        bbb.appendCallInstruction(
            instr->dst(),
            JITRT_LoadFunctionIndirect,
            instr->funcptr(),
            instr->descr());
        break;
      }
      case Opcode::kIntConvert: {
        auto instr = static_cast<const IntConvert*>(&i);
        if (instr->type() <= TCUnsigned) {
          bbb.appendInstr(instr->dst(), Instruction::kZext, instr->src());
        } else {
          JIT_CHECK(
              instr->type() <= TCSigned,
              "Unexpected IntConvert type {}",
              instr->type());
          bbb.appendInstr(instr->dst(), Instruction::kSext, instr->src());
        }
        break;
      }
      case Opcode::kIntBinaryOp: {
        auto instr = static_cast<const IntBinaryOp*>(&i);
        auto op = Instruction::kNop;
        std::optional<Instruction::Opcode> extend;
        uint64_t helper = 0;
        switch (instr->op()) {
          case BinaryOpKind::kAdd:
            op = Instruction::kAdd;
            break;
          case BinaryOpKind::kAnd:
            op = Instruction::kAnd;
            break;
          case BinaryOpKind::kSubtract:
            op = Instruction::kSub;
            break;
          case BinaryOpKind::kXor:
            op = Instruction::kXor;
            break;
          case BinaryOpKind::kOr:
            op = Instruction::kOr;
            break;
          case BinaryOpKind::kMultiply:
            op = Instruction::kMul;
            break;
          case BinaryOpKind::kLShift:
            switch (bytes_from_cint_type(instr->GetOperand(0)->type())) {
              case 1:
              case 2:
                extend = Instruction::kSext;
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
                extend = Instruction::kSext;
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
                extend = Instruction::kZext;
              case 3:
                helper = reinterpret_cast<uint64_t>(JITRT_ShiftRightUnsigned32);
                break;
              case 4:
                helper = reinterpret_cast<uint64_t>(JITRT_ShiftRightUnsigned64);
                break;
            }
            break;
          case BinaryOpKind::kFloorDivide:
            op = Instruction::kDiv;
            break;
          case BinaryOpKind::kFloorDivideUnsigned:
            op = Instruction::kDivUn;
            break;
          case BinaryOpKind::kModulo:
            switch (bytes_from_cint_type(instr->GetOperand(0)->type())) {
              case 1:
              case 2:
                extend = Instruction::kSext;
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
                extend = Instruction::kZext;
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
                extend = Instruction::kSext;
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
                extend = Instruction::kZext;
              case 3:
                helper = reinterpret_cast<uint64_t>(JITRT_PowerUnsigned32);
                break;
              case 4:
                helper = reinterpret_cast<uint64_t>(JITRT_PowerUnsigned64);
                break;
            };
            break;
          default:
            JIT_ABORT("not implemented");
            break;
        }

        if (helper != 0) {
          Instruction* left = bbb.getDefInstr(instr->left());
          Instruction* right = bbb.getDefInstr(instr->right());
          if (extend.has_value()) {
            auto dt = OperandBase::k32bit;
            left = bbb.appendInstr(*extend, OutVReg{dt}, left);
            right = bbb.appendInstr(*extend, OutVReg{dt}, right);
          }
          bbb.appendInstr(
              instr->dst(),
              Instruction::kCall,
              // TODO(T140174965): This should be MemImm.
              Imm{reinterpret_cast<uint64_t>(helper)},
              left,
              right);
        } else if (
            instr->op() == BinaryOpKind::kFloorDivide ||
            instr->op() == BinaryOpKind::kFloorDivideUnsigned) {
          // Divides take an extra zero argument.
          bbb.appendInstr(
              instr->dst(), op, Imm{0}, instr->left(), instr->right());
        } else {
          bbb.appendInstr(instr->dst(), op, instr->left(), instr->right());
        }

        break;
      }
      case Opcode::kDoubleBinaryOp: {
        auto instr = static_cast<const DoubleBinaryOp*>(&i);

        if (instr->op() == BinaryOpKind::kPower) {
          bbb.appendCallInstruction(
              instr->dst(), JITRT_PowerDouble, instr->left(), instr->right());
          break;
        }

        auto op = Instruction::kNop;
        switch (instr->op()) {
          case BinaryOpKind::kAdd: {
            op = Instruction::kFadd;
            break;
          }
          case BinaryOpKind::kSubtract: {
            op = Instruction::kFsub;
            break;
          }
          case BinaryOpKind::kMultiply: {
            op = Instruction::kFmul;
            break;
          }
          case BinaryOpKind::kTrueDivide: {
            op = Instruction::kFdiv;
            break;
          }
          default: {
            JIT_ABORT("Invalid operation for DoubleBinaryOp");
            break;
          }
        }

        bbb.appendInstr(instr->dst(), op, instr->left(), instr->right());
        break;
      }
      case Opcode::kPrimitiveCompare: {
        auto instr = static_cast<const PrimitiveCompare*>(&i);
        Instruction::Opcode op;
        switch (instr->op()) {
          case PrimitiveCompareOp::kEqual:
            op = Instruction::kEqual;
            break;
          case PrimitiveCompareOp::kNotEqual:
            op = Instruction::kNotEqual;
            break;
          case PrimitiveCompareOp::kGreaterThanUnsigned:
            op = Instruction::kGreaterThanUnsigned;
            break;
          case PrimitiveCompareOp::kGreaterThan:
            op = Instruction::kGreaterThanSigned;
            break;
          case PrimitiveCompareOp::kLessThanUnsigned:
            op = Instruction::kLessThanUnsigned;
            break;
          case PrimitiveCompareOp::kLessThan:
            op = Instruction::kLessThanSigned;
            break;
          case PrimitiveCompareOp::kGreaterThanEqualUnsigned:
            op = Instruction::kGreaterThanEqualUnsigned;
            break;
          case PrimitiveCompareOp::kGreaterThanEqual:
            op = Instruction::kGreaterThanEqualSigned;
            break;
          case PrimitiveCompareOp::kLessThanEqualUnsigned:
            op = Instruction::kLessThanEqualUnsigned;
            break;
          case PrimitiveCompareOp::kLessThanEqual:
            op = Instruction::kLessThanEqualSigned;
            break;
          default:
            JIT_ABORT("Not implemented {}", static_cast<int>(instr->op()));
            break;
        }
        bbb.appendInstr(instr->dst(), op, instr->left(), instr->right());
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
        Instruction* src = bbb.getDefInstr(instr->value());
        Type src_type = instr->value()->type();
        uint64_t func = 0;

        if (src_type == TNullptr) {
          // special case for an uninitialized variable, we'll
          // load zero
          bbb.appendCallInstruction(
              instr->GetOutput(), JITRT_BoxI64, int64_t{0});
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
          src = bbb.appendInstr(
              Instruction::kZext, OutVReg{OperandBase::k32bit}, src);
          func = reinterpret_cast<uint64_t>(JITRT_BoxU32);
        } else if (src_type <= (TCInt8 | TCInt16)) {
          src = bbb.appendInstr(
              Instruction::kSext, OutVReg{OperandBase::k32bit}, src);
          func = reinterpret_cast<uint64_t>(JITRT_BoxI32);
        }

        JIT_CHECK(func != 0, "Unknown box type {}", src_type.toString());

        bbb.appendInstr(
            instr->GetOutput(),
            Instruction::kCall,
            // TODO(T140174965): This should be MemImm.
            Imm{func},
            src);

        break;
      }

      case Opcode::kIsNegativeAndErrOccurred: {
        // Emit code to do the following:
        //   dst = (src == -1 && tstate->curexc_type != nullptr) ? -1 : 0;

        auto instr = static_cast<const IsNegativeAndErrOccurred*>(&i);
        Type src_type = instr->reg()->type();

        // We do have to widen to at least 32 bits due to calling convention
        // always passing a minimum of 32 bits.
        Instruction* src = bbb.getDefInstr(instr->reg());
        if (src_type <= (TCBool | TCInt8 | TCUInt8 | TCInt16 | TCUInt16)) {
          src = bbb.appendInstr(
              Instruction::kSext, OutVReg{OperandBase::k32bit}, src);
        }

        // Because a failed unbox to unsigned smuggles the bit pattern for a
        // signed -1 in the unsigned value, we can likewise just treat unsigned
        // as signed for purposes of checking for -1 here.
        Instruction* is_not_negative = bbb.appendInstr(
            Instruction::kNotEqual,
            OutVReg{OperandBase::k8bit},
            src,
            Imm{static_cast<uint64_t>(-1)});

        bbb.appendInstr(instr->dst(), Instruction::kMove, Imm{0});

        auto check_err = bbb.allocateBlock(GetSafeLabelName());
        auto set_err = bbb.allocateBlock(GetSafeLabelName());
        auto done = bbb.allocateBlock(GetSafeLabelName());

        bbb.appendBranch(
            Instruction::kCondBranch, is_not_negative, done, check_err);
        bbb.switchBlock(check_err);

        constexpr int32_t kOffset = offsetof(PyThreadState, curexc_type);
        Instruction* curexc_type = bbb.appendInstr(
            Instruction::kMove, OutVReg{}, Ind{env_->asm_tstate, kOffset});

        Instruction* is_no_err_set = bbb.appendInstr(
            Instruction::kEqual,
            OutVReg{OperandBase::k8bit},
            curexc_type,
            MemImm{0});

        bbb.appendBranch(
            Instruction::kCondBranch, is_no_err_set, done, set_err);
        bbb.switchBlock(set_err);

        // Set to -1 in the error case.
        bbb.appendInstr(Instruction::kDec, instr->dst());
        bbb.switchBlock(done);
        break;
      }

      case Opcode::kPrimitiveUnbox: {
        auto instr = static_cast<const PrimitiveUnbox*>(&i);
        Type ty = instr->type();
        if (ty <= TCBool) {
          bbb.appendInstr(
              instr->dst(),
              Instruction::kEqual,
              instr->value(),
              Imm{reinterpret_cast<uint64_t>(Py_True), OperandBase::kObject});
        } else if (ty <= TCDouble) {
          // For doubles, we can directly load the offset into the destination.
          Instruction* value = bbb.getDefInstr(instr->value());
          int32_t offset = offsetof(PyFloatObject, ob_fval);
          bbb.appendInstr(instr->dst(), Instruction::kMove, Ind{value, offset});
        } else if (ty <= TCUInt64) {
          bbb.appendCallInstruction(
              instr->dst(), JITRT_UnboxU64, instr->value());
        } else if (ty <= TCUInt32) {
          bbb.appendCallInstruction(
              instr->dst(), JITRT_UnboxU32, instr->value());
        } else if (ty <= TCUInt16) {
          bbb.appendCallInstruction(
              instr->dst(), JITRT_UnboxU16, instr->value());
        } else if (ty <= TCUInt8) {
          bbb.appendCallInstruction(
              instr->dst(), JITRT_UnboxU8, instr->value());
        } else if (ty <= TCInt64) {
          bbb.appendCallInstruction(
              instr->dst(), JITRT_UnboxI64, instr->value());
        } else if (ty <= TCInt32) {
          bbb.appendCallInstruction(
              instr->dst(), JITRT_UnboxI32, instr->value());
        } else if (ty <= TCInt16) {
          bbb.appendCallInstruction(
              instr->dst(), JITRT_UnboxI16, instr->value());
        } else if (ty <= TCInt8) {
          bbb.appendCallInstruction(
              instr->dst(), JITRT_UnboxI8, instr->value());
        } else {
          JIT_ABORT("Cannot unbox type {}", ty.toString());
        }
        break;
      }
      case Opcode::kIndexUnbox: {
        auto instr = static_cast<const IndexUnbox*>(&i);
        bbb.appendCallInstruction(
            instr->dst(),
            PyNumber_AsSsize_t,
            instr->GetOperand(0),
            instr->exception());
        break;
      }
      case Opcode::kPrimitiveUnaryOp: {
        auto instr = static_cast<const PrimitiveUnaryOp*>(&i);
        switch (instr->op()) {
          case PrimitiveUnaryOpKind::kNegateInt:
            bbb.appendInstr(
                instr->GetOutput(), Instruction::kNegate, instr->value());
            break;
          case PrimitiveUnaryOpKind::kInvertInt:
            bbb.appendInstr(
                instr->GetOutput(), Instruction::kInvert, instr->value());
            break;
          case PrimitiveUnaryOpKind::kNotInt: {
            bbb.appendInstr(
                instr->GetOutput(),
                Instruction::kEqual,
                instr->value(),
                Imm{0});
            break;
          }
          default:
            JIT_ABORT("Not implemented unary op {}", (int)instr->op());
            break;
        }
        break;
      }
      case Opcode::kReturn: {
        // TODO support constant operand to Return
        bbb.appendInstr(Instruction::kReturn, i.GetOperand(0));
        break;
      }
      case Opcode::kSetCurrentAwaiter: {
        bbb.appendInvokeInstruction(
            JITRT_SetCurrentAwaiter, i.GetOperand(0), env_->asm_tstate);
        break;
      }
      case Opcode::kYieldValue: {
        auto hir_instr = static_cast<const YieldValue*>(&i);
        Instruction* instr = bbb.appendInstr(
            hir_instr->dst(),
            Instruction::kYieldValue,
            env_->asm_tstate,
            hir_instr->reg());
        finishYield(bbb, instr, hir_instr);
        break;
      }
      case Opcode::kInitialYield: {
        auto hir_instr = static_cast<const InitialYield*>(&i);
        Instruction* instr = bbb.appendInstr(
            hir_instr->dst(), Instruction::kYieldInitial, env_->asm_tstate);
        finishYield(bbb, instr, hir_instr);
        break;
      }
      case Opcode::kYieldAndYieldFrom:
      case Opcode::kYieldFrom:
      case Opcode::kYieldFromHandleStopAsyncIteration: {
        Instruction::Opcode op = [&] {
          if (opcode == Opcode::kYieldAndYieldFrom) {
            return Instruction::kYieldFromSkipInitialSend;
          } else if (opcode == Opcode::kYieldFrom) {
            return Instruction::kYieldFrom;
          } else {
            return Instruction::kYieldFromHandleStopAsyncIteration;
          }
        }();
        Instruction* instr = bbb.appendInstr(
            i.GetOutput(),
            op,
            env_->asm_tstate,
            i.GetOperand(0),
            i.GetOperand(1));
        finishYield(bbb, instr, static_cast<const DeoptBase*>(&i));
        break;
      }
      case Opcode::kAssign: {
        JIT_CHECK(false, "assign shouldn't be present");
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
        Instruction* eq_res_var = nullptr;
        if (type.isExact()) {
          Instruction* reg = bbb.getDefInstr(instr.reg());
          constexpr int32_t kOffset = offsetof(PyObject, ob_type);
          Instruction* type_var =
              bbb.appendInstr(Instruction::kMove, OutVReg{}, Ind{reg, kOffset});
          eq_res_var = bbb.appendInstr(
              Instruction::kEqual,
              OutVReg{OperandBase::k8bit},
              type_var,
              Imm{reinterpret_cast<uint64_t>(type.uniquePyType())});
        } else {
          eq_res_var = emitSubclassCheck(bbb, instr.GetOperand(0), type);
        }
        bbb.appendInstr(Instruction::kCondBranch, eq_res_var);
        break;
      }
      case Opcode::kDeleteAttr: {
        auto instr = static_cast<const DeleteAttr*>(&i);
        PyObject* name = instr->name();
        Instruction* call = bbb.appendInstr(
            Instruction::kCall,
            OutVReg{OperandBase::k32bit},
            // TODO(T140174965): This should be MemImm.
            Imm{reinterpret_cast<uint64_t>(PyObject_SetAttr)},
            instr->GetOperand(0),
            // TODO(T140174965): This should be MemImm.
            Imm{reinterpret_cast<uint64_t>(name)},
            Imm{0});
        appendGuard(bbb, InstrGuardKind::kNotNegative, *instr, call);
        break;
      }
      case Opcode::kLoadAttr: {
        auto instr = static_cast<const LoadAttr*>(&i);
        PyObject* name = instr->name();

        Instruction* move = bbb.appendInstr(
            Instruction::kMove,
            OutVReg{},
            // TODO(T140174965): This should be MemImm.
            Imm{reinterpret_cast<uint64_t>(name)});
        bbb.appendCallInstruction(
            instr->dst(),
            jit::LoadAttrCache::invoke,
            Runtime::get()->allocateLoadAttrCache(),
            instr->GetOperand(0),
            move);
        break;
      }
      case Opcode::kLoadAttrSpecial: {
        auto instr = static_cast<const LoadAttrSpecial*>(&i);
        bbb.appendCallInstruction(
            instr->GetOutput(),
            Cix_special_lookup,
            env_->asm_tstate,
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
        PyObject* name = instr->name();
        Instruction* move = bbb.appendInstr(
            Instruction::kMove,
            OutVReg{},
            Imm(reinterpret_cast<uint64_t>(name)));
        bbb.appendCallInstruction(
            instr->GetOutput(),
            jit::LoadTypeAttrCache::invoke,
            load_type_attr_caches_.at(instr->cache_id()),
            instr->receiver(),
            move);
        break;
      }
      case Opcode::kFillTypeMethodCache: {
        auto instr = static_cast<const FillTypeMethodCache*>(&i);
        PyCodeObject* code = instr->frameState()->code;
        PyObject* name = instr->name();
        auto cache_entry = load_type_method_caches_.at(instr->cache_id());
        if (g_collect_inline_cache_stats) {
          cache_entry->initCacheStats(
              PyUnicode_AsUTF8(code->co_filename),
              PyUnicode_AsUTF8(code->co_name));
        }
        Instruction* move = bbb.appendInstr(
            Instruction::kMove,
            OutVReg{},
            Imm(reinterpret_cast<uint64_t>(name)));
        bbb.appendCallInstruction(
            instr->GetOutput(),
            jit::LoadTypeMethodCache::lookupHelper,
            cache_entry,
            instr->receiver(),
            move);
        break;
      }
      case Opcode::kLoadTypeMethodCacheEntryType: {
        auto instr = static_cast<const LoadTypeMethodCacheEntryType*>(&i);
        LoadTypeMethodCache* cache =
            load_type_method_caches_.at(instr->cache_id());
        BorrowedRef<PyTypeObject>* addr = &cache->type;
        bbb.appendInstr(instr->dst(), Instruction::kMove, MemImm{addr});
        break;
      }
      case Opcode::kLoadTypeMethodCacheEntryValue: {
        auto instr = static_cast<const LoadTypeMethodCacheEntryValue*>(&i);
        LoadTypeMethodCache* cache =
            load_type_method_caches_.at(instr->cache_id());
        bbb.appendCallInstruction(
            instr->dst(),
            LoadTypeMethodCache::getValueHelper,
            cache,
            instr->receiver());
        break;
      }
      case Opcode::kLoadMethod: {
        auto instr = static_cast<const LoadMethod*>(&i);

        PyCodeObject* code = instr->frameState()->code;
        PyObject* name = instr->name();
        Instruction* move = bbb.appendInstr(
            Instruction::kMove,
            OutVReg{},
            Imm{reinterpret_cast<uint64_t>(name)});
        auto cache_entry = Runtime::get()->allocateLoadMethodCache();
        if (g_collect_inline_cache_stats) {
          cache_entry->initCacheStats(
              PyUnicode_AsUTF8(code->co_filename),
              PyUnicode_AsUTF8(code->co_name));
        }
        bbb.appendCallInstruction(
            instr->dst(),
            LoadMethodCache::lookupHelper,
            cache_entry,
            instr->receiver(),
            move);

        break;
      }
      case Opcode::kLoadModuleMethod: {
        auto instr = static_cast<const LoadModuleMethod*>(&i);

        PyCodeObject* code = instr->frameState()->code;
        PyObject* name = PyTuple_GET_ITEM(code->co_names, instr->name_idx());

        Instruction* move = bbb.appendInstr(
            Instruction::kMove,
            OutVReg{},
            Imm{reinterpret_cast<uint64_t>(name)});

        auto cache_entry = Runtime::get()->allocateLoadModuleMethodCache();
        bbb.appendCallInstruction(
            instr->dst(),
            LoadModuleMethodCache::lookupHelper,
            cache_entry,
            instr->receiver(),
            move);
        break;
      }
      case Opcode::kGetSecondOutput: {
        bbb.appendInstr(
            i.GetOutput(), Instruction::kLoadSecondCallResult, i.GetOperand(0));
        break;
      }
      case Opcode::kLoadMethodSuper: {
        auto instr = static_cast<const LoadMethodSuper*>(&i);
        PyObject* name = instr->name();
        Instruction* move = bbb.appendInstr(
            Instruction::kMove,
            OutVReg{},
            Imm{reinterpret_cast<uint64_t>(name)});

        bbb.appendCallInstruction(
            instr->dst(),
            JITRT_GetMethodFromSuper,
            instr->global_super(),
            instr->type(),
            instr->receiver(),
            move,
            instr->no_args_in_super_call() ? true : false);
        break;
      }
      case Opcode::kLoadAttrSuper: {
        auto instr = static_cast<const LoadAttrSuper*>(&i);
        PyObject* name = instr->name();

        Instruction* move = bbb.appendInstr(
            Instruction::kMove,
            OutVReg{},
            Imm{reinterpret_cast<uint64_t>(name)});

        bbb.appendCallInstruction(
            instr->dst(),
            JITRT_GetAttrFromSuper,
            instr->global_super(),
            instr->type(),
            instr->receiver(),
            move,
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
          bbb.appendCallInstruction(
              bin_op->dst(), helpers[op_kind], bin_op->left(), bin_op->right());
        } else {
          bbb.appendCallInstruction(
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
          bbb.appendCallInstruction(
              instr->dst(),
              PyLong_Type.tp_as_number->nb_power,
              instr->left(),
              instr->right(),
              Py_None);
        } else {
          bbb.appendCallInstruction(
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
        bbb.appendCallInstruction(
            unary_op->dst(), helpers[op_kind], unary_op->operand());
        break;
      }
      case Opcode::kIsInstance: {
        auto instr = static_cast<const IsInstance*>(&i);
        bbb.appendCallInstruction(
            instr->dst(),
            PyObject_IsInstance,
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kCompare: {
        auto instr = static_cast<const Compare*>(&i);
        if (instr->op() == CompareOp::kIn) {
          bbb.appendCallInstruction(
              instr->dst(),
              JITRT_SequenceContains,
              instr->right(),
              instr->left());
          break;
        }
        if (instr->op() == CompareOp::kNotIn) {
          bbb.appendCallInstruction(
              instr->dst(),
              JITRT_SequenceNotContains,
              instr->right(),
              instr->left());
          break;
        }
        int op = static_cast<int>(instr->op());
        JIT_CHECK(op >= Py_LT, "invalid compare op {}", op);
        JIT_CHECK(op <= Py_GE, "invalid compare op {}", op);
        bbb.appendCallInstruction(
            instr->dst(),
            PyObject_RichCompare,
            instr->left(),
            instr->right(),
            op);
        break;
      }
      case Opcode::kLongCompare: {
        auto instr = static_cast<const LongCompare*>(&i);

        bbb.appendCallInstruction(
            instr->dst(),
            PyLong_Type.tp_richcompare,
            instr->left(),
            instr->right(),
            static_cast<int>(instr->op()));
        break;
      }
      case Opcode::kUnicodeCompare: {
        auto instr = static_cast<const UnicodeCompare*>(&i);

        bbb.appendCallInstruction(
            instr->dst(),
            PyUnicode_Type.tp_richcompare,
            instr->left(),
            instr->right(),
            static_cast<int>(instr->op()));
        break;
      }
      case Opcode::kUnicodeConcat: {
        auto instr = static_cast<const UnicodeConcat*>(&i);

        bbb.appendCallInstruction(
            instr->dst(),
            PyUnicode_Concat,
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kUnicodeRepeat: {
        auto instr = static_cast<const UnicodeRepeat*>(&i);

        bbb.appendCallInstruction(
            instr->dst(),
            PyUnicode_Type.tp_as_sequence->sq_repeat,
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kUnicodeSubscr: {
        auto instr = static_cast<const UnicodeSubscr*>(&i);

        bbb.appendCallInstruction(
            instr->dst(),
            PyUnicode_Type.tp_as_sequence->sq_item,
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kCompareBool: {
        auto instr = static_cast<const CompareBool*>(&i);

        if (instr->op() == CompareOp::kIn) {
          if (instr->right()->type() <= TUnicodeExact) {
            bbb.appendCallInstruction(
                instr->dst(),
                PyUnicode_Contains,
                instr->right(),
                instr->left());
          } else {
            bbb.appendCallInstruction(
                instr->dst(),
                PySequence_Contains,
                instr->right(),
                instr->left());
          }
        } else if (instr->op() == CompareOp::kNotIn) {
          bbb.appendCallInstruction(
              instr->dst(),
              JITRT_NotContainsBool,
              instr->right(),
              instr->left());
        } else if (
            (instr->op() == CompareOp::kEqual ||
             instr->op() == CompareOp::kNotEqual) &&
            (instr->left()->type() <= TUnicodeExact ||
             instr->right()->type() <= TUnicodeExact)) {
          bbb.appendCallInstruction(
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
          bbb.appendCallInstruction(
              instr->dst(),
              PyObject_RichCompareBool,
              instr->left(),
              instr->right(),
              static_cast<int>(instr->op()));
        } else {
          bbb.appendCallInstruction(
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
        bbb.appendCallInstruction(
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

        Instruction* lir = bbb.appendInstr(Instruction::kBatchDecref);
        for (hir::Register* arg : instr->GetOperands()) {
          lir->addOperands(VReg{bbb.getDefInstr(arg)});
        }

        break;
      }
      case Opcode::kDeopt: {
        appendGuardAlwaysFail(bbb, static_cast<const DeoptBase&>(i));
        break;
      }
      case Opcode::kUnreachable: {
        bbb.appendInstr(Instruction::kUnreachable);
        break;
      }
      case Opcode::kDeoptPatchpoint: {
        const auto& instr = static_cast<const DeoptPatchpoint&>(i);
        std::size_t deopt_id = bbb.makeDeoptMetadata();
        auto& regstates = instr.live_regs();
        Instruction* lir = bbb.appendInstr(
            Instruction::kDeoptPatchpoint,
            MemImm{instr.patcher()},
            Imm{deopt_id});
        for (const auto& reg_state : regstates) {
          lir->addOperands(VReg{bbb.getDefInstr(reg_state.reg)});
        }
        break;
      }
      case Opcode::kRaiseAwaitableError: {
        const auto& instr = static_cast<const RaiseAwaitableError&>(i);
        bbb.appendInvokeInstruction(
            Cix_format_awaitable_error,
            env_->asm_tstate,
            instr.GetOperand(0),
            static_cast<int>(instr.with_prev_opcode()),
            static_cast<int>(instr.with_opcode()));
        appendGuardAlwaysFail(bbb, instr);
        break;
      }
      case Opcode::kCheckErrOccurred: {
        const auto& instr = static_cast<const DeoptBase&>(i);
        constexpr int32_t kOffset = offsetof(PyThreadState, curexc_type);
        Instruction* load = bbb.appendInstr(
            Instruction::kMove, OutVReg{}, Ind{env_->asm_tstate, kOffset});
        appendGuard(bbb, InstrGuardKind::kZero, instr, load);
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
        auto kind = InstrGuardKind::kNotZero;
        if (instr.IsCheckNeg()) {
          kind = InstrGuardKind::kNotNegative;
        } else if (instr.IsGuardIs()) {
          kind = InstrGuardKind::kIs;
        }
        appendGuard(bbb, kind, instr, bbb.getDefInstr(instr.GetOperand(0)));
        break;
      }
      case Opcode::kGuardType: {
        const auto& instr = static_cast<const DeoptBase&>(i);
        Instruction* value = bbb.getDefInstr(instr.GetOperand(0));
        appendGuard(bbb, InstrGuardKind::kHasType, instr, value);
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
        PyObject* name = instr->name();
        bbb.appendCallInstruction(
            instr->GetOutput(), JITRT_LoadGlobal, globals, builtins, name);
        break;
      }
      case Opcode::kStoreAttr: {
        auto instr = static_cast<const StoreAttr*>(&i);

        PyObject* name = instr->name();
        bbb.appendCallInstruction(
            instr->dst(),
            jit::StoreAttrCache::invoke,
            Runtime::get()->allocateStoreAttrCache(),
            instr->GetOperand(0),
            name,
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
        size_t flags = (instr.isAwaited() ? Ci_Py_AWAITED_CALL_MARKER : 0);
        emitVectorCall(bbb, instr, flags, false);
        break;
      }
      case Opcode::kCallCFunc: {
        auto& hir_instr = static_cast<const CallCFunc&>(i);
        Instruction* instr = bbb.appendInstr(
            hir_instr.dst(), Instruction::kCall, Imm{hir_instr.funcAddr()});
        for (hir::Register* arg : hir_instr.GetOperands()) {
          instr->addOperands(VReg{bbb.getDefInstr(arg)});
        }
        break;
      }
      case Opcode::kCallEx: {
        auto& instr = static_cast<const CallEx&>(i);
        auto rt_helper = instr.isAwaited() ? JITRT_CallFunctionExAwaited
                                           : JITRT_CallFunctionEx;
        bbb.appendCallInstruction(
            instr.dst(), rt_helper, instr.func(), instr.pargs(), nullptr);
        break;
      }
      case Opcode::kCallExKw: {
        auto& instr = static_cast<const CallExKw&>(i);
        auto rt_helper = instr.isAwaited() ? JITRT_CallFunctionExAwaited
                                           : JITRT_CallFunctionEx;
        bbb.appendCallInstruction(
            instr.dst(),
            rt_helper,
            instr.func(),
            instr.pargs(),
            instr.kwargs());
        break;
      }
      case Opcode::kCallMethod: {
        auto& hir_instr = static_cast<const CallMethod&>(i);
        size_t flags = hir_instr.isAwaited() ? Ci_Py_AWAITED_CALL_MARKER : 0;
        Instruction* instr = bbb.appendInstr(
            hir_instr.dst(),
            Instruction::kVectorCall,
            // TODO(T140174965): This should be MemImm.
            Imm{reinterpret_cast<uint64_t>(JITRT_CallMethod)},
            Imm{flags});
        for (hir::Register* arg : hir_instr.GetOperands()) {
          instr->addOperands(VReg{bbb.getDefInstr(arg)});
        }
        // kwnames
        // TODO(T140174965): This should be MemImm.
        instr->addOperands(Imm{0});
        break;
      }

      case Opcode::kCallStatic: {
        auto& hir_instr = static_cast<const CallStatic&>(i);
        std::vector<Instruction*> args;
        // Generate the argument conversions before the call.
        for (hir::Register* reg_arg : hir_instr.GetOperands()) {
          Instruction* arg = bbb.getDefInstr(reg_arg);
          Type src_type = reg_arg->type();
          if (src_type <= (TCBool | TCUInt8 | TCUInt16)) {
            arg = bbb.appendInstr(
                Instruction::kZext, OutVReg{OperandBase::k64bit}, arg);
          } else if (src_type <= (TCInt8 | TCInt16)) {
            arg = bbb.appendInstr(
                Instruction::kSext, OutVReg{OperandBase::k64bit}, arg);
          }
          args.push_back(arg);
        }
        Instruction* instr = bbb.appendInstr(
            hir_instr.dst(),
            Instruction::kCall,
            // TODO(T140174965): This should be MemImm.
            Imm{reinterpret_cast<uint64_t>(hir_instr.addr())});
        for (const auto& arg : args) {
          instr->addOperands(VReg{arg});
        }
        break;
      }
      case Opcode::kCallStaticRetVoid: {
        auto& hir_instr = static_cast<const CallStaticRetVoid&>(i);
        Instruction* instr = bbb.appendInstr(
            Instruction::kCall,
            // TODO(T140174965): This should be MemImm.
            Imm{reinterpret_cast<uint64_t>(hir_instr.addr())});
        for (hir::Register* arg : hir_instr.GetOperands()) {
          instr->addOperands(VReg{bbb.getDefInstr(arg)});
        }
        break;
      }
      case Opcode::kInvokeStaticFunction: {
        ThreadedCompileSerialize guard;

        auto instr = static_cast<const InvokeStaticFunction*>(&i);
        auto nargs = instr->NumOperands();
        PyFunctionObject* func = instr->func();

        std::stringstream ss;
        Instruction* lir;
        if (_PyJIT_IsCompiled(func)) {
          lir = bbb.appendInstr(
              instr->dst(),
              Instruction::kCall,
              Imm{reinterpret_cast<uint64_t>(
                  JITRT_GET_STATIC_ENTRY(func->vectorcall))});
        } else {
          void** indir = env_->rt->findFunctionEntryCache(func);
          env_->function_indirections.emplace(func, indir);
          Instruction* move = bbb.appendInstr(
              OutVReg{OperandBase::k64bit}, Instruction::kMove, MemImm{indir});

          lir = bbb.appendInstr(instr->dst(), Instruction::kCall, move);
        }

        for (size_t i = 0; i < nargs; i++) {
          lir->addOperands(VReg{bbb.getDefInstr(instr->GetOperand(i))});
        }
        // functions that return primitives will signal error via edx/xmm1
        auto kind = InstrGuardKind::kNotZero;
        Type ret_type = instr->ret_type();
        if (ret_type <= TCDouble) {
          appendGuard(
              bbb,
              kind,
              *instr,
              PhyReg{PhyLocation::XMM1, OperandBase::kDouble});
        } else if (ret_type <= TPrimitive) {
          appendGuard(
              bbb, kind, *instr, PhyReg{PhyLocation::RDX, OperandBase::k32bit});
        } else {
          appendGuard(bbb, kind, *instr, instr->GetOutput());
        }
        break;
      }

      case Opcode::kInvokeMethod: {
        auto& hir_instr = static_cast<const InvokeMethod&>(i);
        size_t flags = hir_instr.isAwaited() ? Ci_Py_AWAITED_CALL_MARKER : 0;
        auto func = hir_instr.isClassmethod()
            ? reinterpret_cast<uint64_t>(JITRT_InvokeClassMethod)
            : reinterpret_cast<uint64_t>(JITRT_InvokeMethod);
        Instruction* instr = bbb.appendInstr(
            hir_instr.dst(),
            Instruction::kVectorCall,
            // TODO(T140174965): This should be MemImm.
            Imm{func},
            Imm{flags},
            Imm{static_cast<uint64_t>(hir_instr.slot())});
        for (hir::Register* arg : hir_instr.GetOperands()) {
          instr->addOperands(VReg{bbb.getDefInstr(arg)});
        }
        // kwnames
        // TODO(T140174965): This should be MemImm.
        instr->addOperands(Imm{0});
        break;
      }

      case Opcode::kInvokeMethodStatic: {
        auto& hir_instr = static_cast<const InvokeMethodStatic&>(i);
        auto slot = hir_instr.slot();
        Instruction* self_reg = bbb.getDefInstr(hir_instr.self());

        Instruction* type;
        if (!hir_instr.isClassmethod()) {
          type = bbb.appendInstr(
              OutVReg{},
              Instruction::kMove,
              Ind{self_reg, (int32_t)offsetof(PyObject, ob_type)});
        } else {
          type = self_reg;
        }

        Instruction* load_vtable = bbb.appendInstr(
            OutVReg{},
            Instruction::kMove,
            Ind{type, (int32_t)offsetof(PyTypeObject, tp_cache)});

        Instruction* load_state = bbb.appendInstr(
            OutVReg{},
            Instruction::kMove,
            Ind{load_vtable,
                (int32_t)(offsetof(_PyType_VTable, vt_entries) + slot * sizeof(_PyType_VTableEntry) + offsetof(_PyType_VTableEntry, vte_state))});
        Instruction* load_entry = bbb.appendInstr(
            OutVReg{},
            Instruction::kMove,
            Ind{load_vtable,
                (int32_t)(offsetof(_PyType_VTable, vt_entries) + slot * sizeof(_PyType_VTableEntry) + offsetof(_PyType_VTableEntry, vte_entry))});

        Instruction* instr = bbb.appendInstr(
            hir_instr.dst(), Instruction::kCall, load_entry, load_state);
        for (hir::Register* arg : hir_instr.GetOperands()) {
          instr->addOperands(VReg{bbb.getDefInstr(arg)});
        }

        auto kind = InstrGuardKind::kNotZero;
        Type ret_type = hir_instr.ret_type();
        if (ret_type <= TCDouble) {
          appendGuard(
              bbb,
              kind,
              hir_instr,
              PhyReg{PhyLocation::XMM1, OperandBase::kDouble});
        } else if (ret_type <= TPrimitive) {
          appendGuard(
              bbb,
              kind,
              hir_instr,
              PhyReg{PhyLocation::RDX, OperandBase::k32bit});
        } else {
          appendGuard(bbb, kind, hir_instr, hir_instr.GetOutput());
        }
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
        Instruction* lir = bbb.appendInstr(
            OutInd{
                bbb.getDefInstr(instr->receiver()),
                static_cast<int32_t>(instr->offset())},
            Instruction::kMove,
            instr->value());
        lir->output()->setDataType(lir->getInput(0)->dataType());
        break;
      }

      case Opcode::kCast: {
        auto instr = static_cast<const Cast*>(&i);
        PyObject* (*func)(PyObject*, PyTypeObject*);
        if (instr->pytype() == &PyFloat_Type) {
          bbb.appendCallInstruction(
              instr->dst(),
              instr->optional() ? JITRT_CastToFloatOptional : JITRT_CastToFloat,
              instr->value());
          break;
        } else if (instr->exact()) {
          func = instr->optional() ? JITRT_CastOptionalExact : JITRT_CastExact;
        } else {
          func = instr->optional() ? JITRT_CastOptional : JITRT_Cast;
        }

        bbb.appendCallInstruction(
            instr->dst(), func, instr->value(), instr->pytype());
        break;
      }

      case Opcode::kTpAlloc: {
        auto instr = static_cast<const TpAlloc*>(&i);

        bbb.appendCallInstruction(
            instr->dst(),
            instr->pytype()->tp_alloc,
            instr->pytype(),
            /*nitems=*/static_cast<Py_ssize_t>(0));
        break;
      }

      case Opcode::kMakeList: {
        auto instr = static_cast<const MakeList*>(&i);
        Instruction* call = bbb.appendCallInstruction(
            instr->dst(),
            PyList_New,
            static_cast<Py_ssize_t>(instr->nvalues()));
        if (instr->nvalues() > 0) {
          // TODO(T174544781): need to check for NULL before initializing,
          // currently that check only happens after assigning these values.
          Instruction* load = bbb.appendInstr(
              Instruction::kMove,
              OutVReg{OperandBase::k64bit},
              Ind{call, offsetof(PyListObject, ob_item)});
          for (size_t i = 0; i < instr->nvalues(); i++) {
            bbb.appendInstr(
                OutInd{load, static_cast<int32_t>(i * kPointerSize)},
                Instruction::kMove,
                instr->GetOperand(i));
          }
        }
        break;
      }
      case Opcode::kMakeTuple: {
        auto instr = static_cast<const MakeTuple*>(&i);
        Instruction* tuple = bbb.appendCallInstruction(
            instr->dst(),
            PyTuple_New,
            static_cast<Py_ssize_t>(instr->nvalues()));
        // TODO(T174544781): need to check for 0 before initializing, currently
        // that check only happens after assigning these values.
        const size_t ob_item_offset = offsetof(PyTupleObject, ob_item);
        for (size_t i = 0; i < instr->NumOperands(); i++) {
          bbb.appendInstr(
              OutInd{
                  tuple,
                  static_cast<int32_t>(ob_item_offset + i * kPointerSize)},
              Instruction::kMove,
              instr->GetOperand(i));
        }
        break;
      }
      case Opcode::kMatchClass: {
        const auto& instr = static_cast<const MatchClass&>(i);
        bbb.appendCallInstruction(
            instr.GetOutput(),
            Cix_match_class,
            env_->asm_tstate,
            instr.GetOperand(0),
            instr.GetOperand(1),
            instr.GetOperand(2),
            instr.GetOperand(3));
        break;
      }
      case Opcode::kMatchKeys: {
        auto instr = static_cast<const MatchKeys*>(&i);
        bbb.appendCallInstruction(
            instr->dst(),
            Cix_match_keys,
            env_->asm_tstate,
            instr->GetOperand(0),
            instr->GetOperand(1));
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
        if (type <= (TCInt8 | TCInt16 | TCInt32) ||
            type <= (TCUInt8 | TCUInt16 | TCUInt32)) {
          Instruction* lir = bbb.appendInstr(
              Instruction::kSext, OutVReg{}, instr->GetOperand(1));
          bbb.appendCallInstruction(
              instr->dst(),
              JITRT_CheckSequenceBounds,
              instr->GetOperand(0),
              lir);
        } else {
          bbb.appendCallInstruction(
              instr->dst(),
              JITRT_CheckSequenceBounds,
              instr->GetOperand(0),
              instr->GetOperand(1));
        }
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
          JIT_ABORT("Unknown array type {}", type.toString());
        }

        bbb.appendInvokeInstruction(
            func, instr->ob_item(), instr->value(), instr->idx());
        break;
      }
      case Opcode::kLoadSplitDictItem: {
        auto instr = static_cast<const LoadSplitDictItem*>(&i);
        Register* dict = instr->GetOperand(0);
        // Users of LoadSplitDictItem are required to verify that dict has a
        // split table, so it's safe to load and acess ma_values with no
        // additional checks here.
        Instruction* ma_values = bbb.appendInstr(
            OutVReg{},
            Instruction::kMove,
            Ind{bbb.getDefInstr(dict),
                static_cast<int32_t>(offsetof(PyDictObject, ma_values))});
        bbb.appendInstr(
            instr->GetOutput(),
            Instruction::kMove,
            Ind{ma_values,
                static_cast<int32_t>(instr->itemIdx() * sizeof(PyObject*))});
        break;
      }
      case Opcode::kMakeCheckedList: {
        auto instr = static_cast<const MakeCheckedList*>(&i);
        auto capacity = instr->nvalues();
        bbb.appendCallInstruction(
            instr->GetOutput(),
            Ci_CheckedList_New,
            instr->type().typeSpec(),
            static_cast<Py_ssize_t>(capacity));
        if (instr->nvalues() > 0) {
          Instruction* ob_item = bbb.appendInstr(
              OutVReg{},
              Instruction::kMove,
              Ind{bbb.getDefInstr(instr->dst()),
                  static_cast<int32_t>(offsetof(PyListObject, ob_item))});
          for (size_t i = 0; i < instr->nvalues(); i++) {
            bbb.appendInstr(
                OutInd{ob_item, static_cast<int32_t>(i * kPointerSize)},
                Instruction::kMove,
                instr->GetOperand(i));
          }
        }
        break;
      }
      case Opcode::kMakeCheckedDict: {
        auto instr = static_cast<const MakeCheckedDict*>(&i);
        auto capacity = instr->GetCapacity();
        if (capacity == 0) {
          bbb.appendCallInstruction(
              instr->GetOutput(), Ci_CheckedDict_New, instr->type().typeSpec());
        } else {
          bbb.appendCallInstruction(
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
          bbb.appendCallInstruction(instr->GetOutput(), PyDict_New);
        } else {
          bbb.appendCallInstruction(
              instr->GetOutput(),
              _PyDict_NewPresized,
              static_cast<Py_ssize_t>(capacity));
        }
        break;
      }
      case Opcode::kMakeSet: {
        auto instr = static_cast<const MakeSet*>(&i);
        bbb.appendCallInstruction(instr->GetOutput(), PySet_New, nullptr);
        break;
      }
      case Opcode::kDictUpdate: {
        bbb.appendCallInstruction(
            i.GetOutput(),
            JITRT_DictUpdate,
            env_->asm_tstate,
            i.GetOperand(0),
            i.GetOperand(1));
        break;
      }
      case Opcode::kDictMerge: {
        bbb.appendCallInstruction(
            i.GetOutput(),
            JITRT_DictMerge,
            env_->asm_tstate,
            i.GetOperand(0),
            i.GetOperand(1),
            i.GetOperand(2));
        break;
      }
      case Opcode::kMergeSetUnpack: {
        auto instr = static_cast<const MergeSetUnpack*>(&i);
        bbb.appendCallInstruction(
            instr->GetOutput(),
            _PySet_Update,
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kSetDictItem: {
        auto instr = static_cast<const SetDictItem*>(&i);
        bbb.appendCallInstruction(
            instr->GetOutput(),
            Ci_DictOrChecked_SetItem,
            instr->GetOperand(0),
            instr->GetOperand(1),
            instr->GetOperand(2));
        break;
      }
      case Opcode::kSetSetItem: {
        auto instr = static_cast<const SetSetItem*>(&i);
        bbb.appendCallInstruction(
            instr->GetOutput(),
            PySet_Add,
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kSetUpdate: {
        auto instr = static_cast<const SetUpdate*>(&i);
        bbb.appendCallInstruction(
            instr->GetOutput(),
            _PySet_Update,
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kStoreSubscr: {
        auto instr = static_cast<const StoreSubscr*>(&i);
        bbb.appendCallInstruction(
            instr->dst(),
            PyObject_SetItem,
            instr->container(),
            instr->index(),
            instr->value());

        break;
      }
      case Opcode::kDictSubscr: {
        auto instr = static_cast<const DictSubscr*>(&i);
        bbb.appendCallInstruction(
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
          bbb.appendCallInstruction(
              instr->dst(), helpers[op_kind], instr->left(), instr->right());
        } else {
          bbb.appendCallInstruction(
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

        bbb.appendCallInstruction(
            instr->dst(),
            PySlice_New,
            instr->start(),
            instr->stop(),
            instr->step() != nullptr ? instr->step() : nullptr);

        break;
      }
      case Opcode::kGetIter: {
        auto instr = static_cast<const GetIter*>(&i);
        bbb.appendCallInstruction(
            instr->GetOutput(), PyObject_GetIter, instr->GetOperand(0));
        break;
      }
      case Opcode::kGetLength: {
        auto instr = static_cast<const GetLength*>(&i);
        bbb.appendCallInstruction(
            instr->GetOutput(), JITRT_GetLength, instr->GetOperand(0));
        break;
      }
      case Opcode::kPhi: {
        auto instr = static_cast<const Phi*>(&i);
        bbb.appendInstr(instr->GetOutput(), Instruction::kPhi);
        // The phi's operands will get filled out later, once we have LIR
        // definitions for all HIR values.
        break;
      }
      case Opcode::kMakeFunction: {
        auto instr = static_cast<const MakeFunction*>(&i);
        auto qualname = instr->GetOperand(0);
        auto code = instr->GetOperand(1);
        PyObject* globals = instr->frameState()->globals;
        env_->code_rt->addReference(globals);

        bbb.appendCallInstruction(
            instr->GetOutput(),
            PyFunction_NewWithQualName,
            code,
            globals,
            qualname);
        break;
      }
      case Opcode::kSetFunctionAttr: {
        auto instr = static_cast<const SetFunctionAttr*>(&i);

        bbb.appendInstr(
            OutInd{
                bbb.getDefInstr(instr->base()),
                static_cast<int32_t>(instr->offset())},
            Instruction::kMove,
            instr->value());
        break;
      }
      case Opcode::kListAppend: {
        auto instr = static_cast<const ListAppend*>(&i);

        bbb.appendCallInstruction(
            instr->dst(),
            Ci_ListOrCheckedList_Append,
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kListExtend: {
        auto instr = static_cast<const ListExtend*>(&i);
        bbb.appendCallInstruction(
            instr->dst(),
            __Invoke_PyList_Extend,
            env_->asm_tstate,
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kMakeTupleFromList: {
        auto instr = static_cast<const MakeTupleFromList*>(&i);
        bbb.appendCallInstruction(
            instr->dst(), PyList_AsTuple, instr->GetOperand(0));
        break;
      }
      case Opcode::kGetTuple: {
        auto instr = static_cast<const GetTuple*>(&i);

        bbb.appendCallInstruction(
            instr->dst(), PySequence_Tuple, instr->GetOperand(0));
        break;
      }
      case Opcode::kInvokeIterNext: {
        auto instr = static_cast<const InvokeIterNext*>(&i);
        bbb.appendCallInstruction(
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
        bbb.appendCallInstruction(
            i.GetOutput(), Cix_eval_frame_handle_pending, env_->asm_tstate);
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
        if (kPyDebug) {
          bbb.appendInvokeInstruction(
              assertShadowCallStackConsistent, env_->asm_tstate);
        }
        auto instr = static_cast<const BeginInlinedFunction*>(&i);
        Instruction* caller_shadow_frame = bbb.appendInstr(
            OutVReg{},
            Instruction::kLea,
            PhyRegStack{PhyLocation(
                static_cast<int32_t>(shadowFrameOffsetBefore(instr)))});
        // There is already a shadow frame for the caller function.
        Instruction* callee_shadow_frame = bbb.appendInstr(
            OutVReg{},
            Instruction::kLea,
            PhyRegStack{
                PhyLocation(static_cast<int32_t>(shadowFrameOffsetOf(instr)))});
        bbb.appendInstr(
            OutInd{callee_shadow_frame, SHADOW_FRAME_FIELD_OFF(prev)},
            Instruction::kMove,
            caller_shadow_frame);
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
        Instruction* data_reg =
            bbb.appendInstr(OutVReg{}, Instruction::kMove, data);
        bbb.appendInstr(
            OutInd{callee_shadow_frame, SHADOW_FRAME_FIELD_OFF(data)},
            Instruction::kMove,
            data_reg);
        // Set orig_data
        // This is only necessary when in normal-frame mode because the frame
        // is already materialized on function entry. It is lazily filled when
        // the frame is materialized in shadow-frame mode.
        if (func_->frameMode == jit::FrameMode::kNormal) {
          bbb.appendInstr(
              OutInd{
                  callee_shadow_frame, JIT_SHADOW_FRAME_FIELD_OFF(orig_data)},
              Instruction::kMove,
              data_reg);
        }
        // Set our shadow frame as top of shadow stack
        bbb.appendInstr(
            OutInd{env_->asm_tstate, offsetof(PyThreadState, shadow_frame)},
            Instruction::kMove,
            callee_shadow_frame);
        if (kPyDebug) {
          bbb.appendInvokeInstruction(
              assertShadowCallStackConsistent, env_->asm_tstate);
        }
        break;
      }
      case Opcode::kEndInlinedFunction: {
        // TODO(T109706798): Support calling from generators and inlining
        // generators.
        if (kPyDebug) {
          bbb.appendInvokeInstruction(
              assertShadowCallStackConsistent, env_->asm_tstate);
        }
        // callee_shadow_frame <- tstate.shadow_frame
        Instruction* callee_shadow_frame = bbb.appendInstr(
            OutVReg{},
            Instruction::kMove,
            Ind{env_->asm_tstate, offsetof(PyThreadState, shadow_frame)});

        // Check if the callee has been materialized into a PyFrame. Use the
        // flags below.
        static_assert(
            PYSF_PYFRAME == 1 && _PyShadowFrame_NumPtrKindBits == 2,
            "Unexpected constants");
        Instruction* shadow_frame_data = bbb.appendInstr(
            OutVReg{},
            Instruction::kMove,
            Ind{callee_shadow_frame, SHADOW_FRAME_FIELD_OFF(data)});
        bbb.appendInstr(Instruction::kBitTest, shadow_frame_data, Imm{0});

        // caller_shadow_frame <- callee_shadow_frame.prev
        Instruction* caller_shadow_frame = bbb.appendInstr(
            OutVReg{},
            Instruction::kMove,
            Ind{callee_shadow_frame, SHADOW_FRAME_FIELD_OFF(prev)});
        // caller_shadow_frame -> tstate.shadow_frame
        bbb.appendInstr(
            OutInd{env_->asm_tstate, offsetof(PyThreadState, shadow_frame)},
            Instruction::kMove,
            caller_shadow_frame);

        // Unlink PyFrame if needed. Someone might have materialized all of the
        // PyFrames via PyEval_GetFrame or similar.
        auto done_block = bbb.allocateBlock(GetSafeLabelName());
        bbb.appendBranch(Instruction::kBranchNC, done_block);
        // TODO(T109445584): Remove this unused label.
        bbb.appendLabel(GetSafeLabelName());
        bbb.appendInvokeInstruction(JITRT_UnlinkFrame, env_->asm_tstate);
        bbb.appendBlock(done_block);
        if (kPyDebug) {
          bbb.appendInvokeInstruction(
              assertShadowCallStackConsistent, env_->asm_tstate);
        }
        break;
      }
      case Opcode::kIsTruthy: {
        bbb.appendCallInstruction(
            i.GetOutput(), PyObject_IsTrue, i.GetOperand(0));
        break;
      }
      case Opcode::kImportFrom: {
        auto& instr = static_cast<const ImportFrom&>(i);
        PyObject* name = instr.name();
        bbb.appendCallInstruction(
            i.GetOutput(),
            _PyImport_ImportFrom,
            env_->asm_tstate,
            instr.module(),
            name);
        break;
      }
      case Opcode::kImportName: {
        auto instr = static_cast<const ImportName*>(&i);
        PyObject* name = instr->name();
        bbb.appendCallInstruction(
            i.GetOutput(),
            JITRT_ImportName,
            env_->asm_tstate,
            name,
            instr->GetFromList(),
            instr->GetLevel());
        break;
      }
      case Opcode::kRaise: {
        const auto& instr = static_cast<const Raise&>(i);
        hir::Register* exc = nullptr;
        hir::Register* cause = nullptr;

        switch (instr.kind()) {
          case Raise::Kind::kReraise:
            break;
          case Raise::Kind::kRaiseWithExcAndCause:
            cause = instr.GetOperand(1);
            // Fallthrough
          case Raise::Kind::kRaiseWithExc:
            exc = instr.GetOperand(0);
            break;
        }
        bbb.appendCallInstruction(
            OutVReg{OperandBase::k32bit},
            Cix_do_raise,
            env_->asm_tstate,
            exc,
            cause);
        appendGuardAlwaysFail(bbb, instr);
        break;
      }
      case Opcode::kRaiseStatic: {
        const auto& instr = static_cast<const RaiseStatic&>(i);
        Instruction* lir = bbb.appendInstr(
            Instruction::kCall,
            reinterpret_cast<uint64_t>(PyErr_Format),
            // TODO(T140174965): This should be MemImm.
            Imm{reinterpret_cast<uint64_t>(instr.excType())},
            // TODO(T140174965): This should be MemImm.
            Imm{reinterpret_cast<uint64_t>(instr.fmt())});
        for (size_t i = 0; i < instr.NumOperands(); i++) {
          lir->addOperands(VReg{bbb.getDefInstr(instr.GetOperand(i))});
        }

        appendGuardAlwaysFail(bbb, instr);
        break;
      }
      case Opcode::kFormatValue: {
        const auto& instr = static_cast<const FormatValue&>(i);
        bbb.appendCallInstruction(
            instr.dst(),
            JITRT_FormatValue,
            env_->asm_tstate,
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

        Instruction* lir = bbb.appendInstr(
            instr.dst(),
            Instruction::kVectorCall,
            JITRT_BuildString,
            nullptr,
            nullptr);
        for (size_t i = 0; i < instr.NumOperands(); i++) {
          lir->addOperands(VReg{bbb.getDefInstr(instr.GetOperand(i))});
        }
        lir->addOperands(Imm{0});

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
        bbb.appendInstr(
            OutInd{
                bbb.getDefInstr(instr.reg()),
                static_cast<int32_t>(
                    offsetof(Ci_PyWaitHandleObject, wh_coro_or_result))},
            Instruction::kMove,
            0);
        bbb.appendInstr(
            OutInd{
                bbb.getDefInstr(instr.reg()),
                static_cast<int32_t>(
                    offsetof(Ci_PyWaitHandleObject, wh_waiter))},
            Instruction::kMove,
            0);
        break;
      }
      case Opcode::kDeleteSubscr: {
        const auto& instr = static_cast<const DeleteSubscr&>(i);
        Instruction* call = bbb.appendInstr(
            Instruction::kCall,
            OutVReg{OperandBase::k32bit},
            // TODO(T140174965): This should be MemImm.
            Imm{reinterpret_cast<uint64_t>(PyObject_DelItem)},
            instr.GetOperand(0),
            instr.GetOperand(1));
        appendGuard(bbb, InstrGuardKind::kNotNegative, instr, call);
        break;
      }
      case Opcode::kUnpackExToTuple: {
        auto instr = static_cast<const UnpackExToTuple*>(&i);
        bbb.appendCallInstruction(
            instr->dst(),
            JITRT_UnpackExToTuple,
            env_->asm_tstate,
            instr->seq(),
            instr->before(),
            instr->after());
        break;
      }
      case Opcode::kGetAIter: {
        auto& instr = static_cast<const GetAIter&>(i);
        bbb.appendCallInstruction(
            instr.dst(), Ci_GetAIter, env_->asm_tstate, instr.GetOperand(0));
        break;
      }
      case Opcode::kGetANext: {
        auto& instr = static_cast<const GetAIter&>(i);
        bbb.appendCallInstruction(
            instr.dst(), Ci_GetANext, env_->asm_tstate, instr.GetOperand(0));
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
        case Opcode::kInvokeMethodStatic:
        case Opcode::kInvokeStaticFunction:
        case Opcode::kRaiseAwaitableError:
        case Opcode::kRaise:
        case Opcode::kRaiseStatic: {
          break;
        }
        case Opcode::kCompare: {
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

std::string LIRGenerator::GetSafeLabelName() {
  return fmt::format("__codegen_label_{}", label_id++);
}

void LIRGenerator::resolvePhiOperands(
    UnorderedMap<const hir::BasicBlock*, TranslatedBlock>& bb_map) {
  // This is creating a different builder than the first pass, but that's okay
  // because the state is really in `env_` which is unchanged.
  BasicBlockBuilder bbb{env_, lir_func_};

  for (auto& block : basic_blocks_) {
    block->foreachPhiInstr([&](Instruction* instr) {
      auto hir_instr = static_cast<const Phi*>(instr->origin());
      for (size_t i = 0; i < hir_instr->NumOperands(); ++i) {
        hir::BasicBlock* hir_block = hir_instr->basic_blocks().at(i);
        hir::Register* hir_value = hir_instr->GetOperand(i);
        instr->allocateLabelInput(bb_map.at(hir_block).last);
        instr->allocateLinkedInput(bbb.getDefInstr(hir_value));
      }
    });
  }
}

} // namespace jit::lir

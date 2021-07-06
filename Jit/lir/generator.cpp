// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/lir/generator.h"
#include "Jit/hir/analysis.h"
#include "Jit/jit_rt.h"
#include "Jit/lir/block_builder.h"

#include "Jit/deopt.h"
#include "Jit/log.h"
#include "Jit/pyjit.h"
#include "Jit/runtime_support.h"
#include "Jit/threaded_compile.h"

#include "Jit/codegen/x86_64.h"

#include "Python.h"
#include "internal/pycore_pyerrors.h"
#include "internal/pycore_pystate.h"
#include "listobject.h"

#include <functional>
#include <sstream>

// XXX: this file needs to be revisited when we optimize HIR-to-LIR translation
// in codegen.cpp/h. Currently, this file is almost an identical copy from
// codegen.cpp with some interfaces changes so that it works with the new
// LIR.

using namespace jit::hir;

template <>
struct fmt::formatter<Register*> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(Register* const& reg, FormatContext& ctx) {
    if (reg->type().hasIntSpec()) {
      return fmt::format_to(
          ctx.out(),
          "{}:{}",
          reg->type().intSpec(),
          reg->type().unspecialized());
    } else if (reg->type().hasDoubleSpec()) {
      return fmt::format_to(
          ctx.out(),
          "{}:{}",
          reg->type().doubleSpec(),
          reg->type().unspecialized());
    } else if (reg->type() <= TInternal) {
      return fmt::format_to(
          ctx.out(), "{}:{}", reg->name(), reg->type().toString());
    } else {
      return fmt::format_to(ctx.out(), "{}", reg->name());
    }
  }
};

template <>
struct fmt::formatter<PyObject*> : fmt::formatter<void*> {};

namespace jit {
namespace lir {

// These functions call their counterparts and convert its output from int (32
// bits) to uint64_t (64 bits). This is solely because the code generator cannot
// support an operand size other than 64 bits at this moment. A future diff will
// make it support different operand sizes so that this function can be removed.

extern "C" uint64_t
_Invoke_PySlice_New(PyObject* start, PyObject* stop, PyObject* step) {
  return reinterpret_cast<uint64_t>(PySlice_New(start, stop, step));
}

extern "C" PyObject* __Invoke_PyList_Extend(
    PyThreadState* tstate,
    PyListObject* list,
    PyObject* iterable,
    PyObject* func) {
  PyObject* none_val = _PyList_Extend(list, iterable);
  bool with_call = func != nullptr;
  if (none_val == nullptr) {
    if (with_call && _PyErr_ExceptionMatches(tstate, PyExc_TypeError)) {
      check_args_iterable(tstate, func, iterable);
    }
  }

  return none_val;
}

extern "C" uint64_t __Invoke_PyTuple_Check(PyObject* iterable) {
  int is_tuple = PyTuple_Check(iterable);
  return is_tuple == 0 ? 0 : 1;
}

extern "C" uint64_t __Invoke_PyDict_MergeEx(
    PyThreadState* tstate,
    PyObject* a,
    PyObject* b,
    PyObject* func) {
  int result = _PyDict_MergeEx(a, b, func == nullptr ? 1 : 2);
  if (result < 0) {
    if (func == nullptr) {
      // BUILD_MAP_UNPACK
      if (_PyErr_ExceptionMatches(tstate, PyExc_AttributeError)) {
        _PyErr_Format(
            tstate,
            PyExc_TypeError,
            "'%.200s' object is not a mapping",
            b->ob_type->tp_name);
      }
    } else {
      // BUILD_MAP_UNPACK_WITH_CALL
      format_kwargs_error(tstate, func, b);
    }

    return 0;
  }
  return reinterpret_cast<uint64_t>(Py_None);
}

BasicBlock* LIRGenerator::GenerateEntryBlock() {
  auto block = lir_func_->allocateBasicBlock();
  auto bindVReg = [&](const std::string& name, int phy_reg) {
    auto instr = block->allocateInstr(Instruction::kBind, nullptr);
    instr->output()->setVirtualRegister();
    instr->allocatePhyRegisterInput(phy_reg);
    env_->output_map.emplace(name, instr);
  };

  bindVReg("__asm_extra_args", jit::codegen::PhyLocation::R10);
  bindVReg("__asm_tstate", jit::codegen::PhyLocation::R11);
  if (func_->uses_runtime_func) {
    bindVReg("__asm_func", jit::codegen::PhyLocation::RAX);
  }

  return block;
}

BasicBlock* LIRGenerator::GenerateExitBlock() {
  auto block = lir_func_->allocateBasicBlock();
  auto instr = block->allocateInstr(Instruction::kMove, nullptr);
  instr->output()->setPhyRegister(jit::codegen::PhyLocation::RDI);
  instr->allocateLinkedInput(map_get(env_->output_map, "__asm_tstate"));
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
          (hir::isPassthrough(instr) || instr.IsGuardIs())) {
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

  std::unordered_map<const hir::BasicBlock*, TranslatedBlock> bb_map;
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
        auto condbranch = static_cast<const CondBranch*>(hir_term);
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

std::string LIRGenerator::MakeGuard(
    const std::string& kind,
    const DeoptBase& instr,
    const std::string& guard_var) {
  auto deopt_meta = jit::DeoptMetadata::fromInstr(
      instr, env_->optimizable_load_call_methods_, env_->code_rt);
  auto id = env_->rt->addDeoptMetadata(std::move(deopt_meta));

  std::stringstream ss;
  ss << "Guard " << kind << ", " << id;

  JIT_CHECK(
      guard_var.empty() == (kind == "AlwaysFail"),
      "MakeGuard expects a register name to guard iff the kind is not "
      "AlwaysFail");
  if (!guard_var.empty()) {
    ss << ", " << guard_var;
  }
  if (instr.IsGuardIs()) {
    const auto& guard = static_cast<const GuardIs&>(instr);
    ss << ", " << static_cast<void*>(guard.target());
  }
  auto& regstates = instr.live_regs();
  for (const auto& reg_state : regstates) {
    ss << ", " << *reg_state.reg;
  }

  return ss.str();
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
        std::string call = fmt::format(
            "Call {}, {}, {}, 0",
            instr.dst()->name(),
            reinterpret_cast<uint64_t>(_PyBuiltin_Next),
            instr.arg(0));
        bbb.AppendCode(call);
        return true;
      } else if (instr.numArgs() == 2) {
        std::string call = fmt::format(
            "Call {}, {}, {}, {}",
            instr.dst()->name(),
            reinterpret_cast<uint64_t>(_PyBuiltin_Next),
            instr.arg(0),
            instr.arg(1));
        bbb.AppendCode(call);
        return true;
      }
    }
    switch (
        PyCFunction_GET_FLAGS(callee) &
        (METH_VARARGS | METH_FASTCALL | METH_NOARGS | METH_O | METH_KEYWORDS)) {
      case METH_NOARGS:
        if (instr.numArgs() == 0) {
          std::string call = fmt::format(
              "Call {}, {}, {}, 0",
              instr.dst()->name(),
              reinterpret_cast<uint64_t>(PyCFunction_GET_FUNCTION(callee)),
              reinterpret_cast<uint64_t>(PyCFunction_GET_SELF(callee)));
          bbb.AppendCode(call);
          return true;
        }
        break;
      case METH_O:
        if (instr.numArgs() == 1) {
          std::string call = fmt::format(
              "Call {}, {}, {}, {}",
              instr.dst()->name(),
              reinterpret_cast<uint64_t>(PyCFunction_GET_FUNCTION(callee)),
              reinterpret_cast<uint64_t>(PyCFunction_GET_SELF(callee)),
              instr.arg(0));
          bbb.AppendCode(call);
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

  std::string call = fmt::format(
      "Vectorcall {}, {}, 0, {}",
      instr.dst()->name(),
      reinterpret_cast<uint64_t>(func),
      reinterpret_cast<uint64_t>(callee));
  for (size_t i = 0, num_args = instr.numArgs(); i < num_args; i++) {
    call += fmt::format(", {}", instr.arg(i));
  }
  call += ", 0";
  bbb.AppendCode(call);
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
  auto out = i.GetOutput();
  std::string kind = out->type() <= TCSigned ? "NotNegative" : "NotNull";
  bbb.AppendCode(MakeGuard(kind, i, out->name()));
}

void LIRGenerator::MakeIncref(
    BasicBlockBuilder& bbb,
    const hir::Instr& instr,
    bool xincref) {
  auto end_incref = GetSafeLabelName();
  auto obj = instr.GetOperand(0);
  if (xincref) {
    auto cont = GetSafeLabelName();
    bbb.AppendCode(
        "JumpIf {}, {}, {}\n"
        "{}:",
        obj,
        cont,
        end_incref,
        cont);
  }

#ifdef Py_DEBUG
  auto r0 = GetSafeTempName();
  bbb.AppendCode(
      "Load {}, {:#x}\n"
      "Inc {}\n"
      "Store {}, {:#x}",
      r0,
      reinterpret_cast<uint64_t>(&_Py_RefTotal),
      r0,
      r0,
      reinterpret_cast<uint64_t>(&_Py_RefTotal));
#endif

  auto r1 = GetSafeTempName();
  auto cond_incref = GetSafeLabelName();

  bbb.AppendCode(
      "Load {}, {}, {:#x}\n"
#ifdef Py_IMMORTAL_INSTANCES
      "BitTest {}, {}\n"
      "BranchC {}\n"
#endif
      "{}:\n"
      "Inc {}\n"
      "Store {}, {}, {:#x}\n"
      "{}:",
      r1, // Load
      obj,
      GET_STRUCT_MEMBER_OFFSET(PyObject, ob_refcnt),
#ifdef Py_IMMORTAL_INSTANCES
      r1, // BitTest
      kImmortalBitPos,
      end_incref, // BranchC
#endif
      cond_incref, // label
      r1, // Inc
      r1, // Store
      obj,
      GET_STRUCT_MEMBER_OFFSET(PyObject, ob_refcnt),
      end_incref // label
  );
}

void LIRGenerator::MakeDecref(
    BasicBlockBuilder& bbb,
    const jit::hir::Instr& instr,
    bool xdecref) {
  auto end_decref = GetSafeLabelName();
  auto obj = instr.GetOperand(0);
  if (xdecref) {
    auto cont = GetSafeLabelName();
    bbb.AppendCode(
        "JumpIf {}, {}, {}\n"
        "{}:",
        obj,
        cont,
        end_decref,
        cont);
  }

#ifdef Py_DEBUG
  auto r0 = GetSafeTempName();
  bbb.AppendCode(
      "Load {}, {:#x}\n"
      "Dec {}\n"
      "Store {}, {:#x}",
      r0,
      reinterpret_cast<uint64_t>(&_Py_RefTotal),
      r0,
      r0,
      reinterpret_cast<uint64_t>(&_Py_RefTotal));
#endif

  auto r1 = GetSafeTempName();
  auto r2 = GetSafeTempName();
  auto cond_decref = GetSafeLabelName();
  auto dealloc = GetSafeLabelName();

  bbb.AppendCode(
      "Load {}, {}, {:#x}\n"
#ifdef Py_IMMORTAL_INSTANCES
      "BitTest {}, {}\n"
      "BranchC {}\n"
#endif
      "{}:\n"
      "Sub {}, {}, 1\n"
      "Store {}, {}, {:#x}\n"
      "BranchNZ {}\n"
      "{}:\n"
      "Invoke {:#x}, {}\n"
      "{}:",
      r1, // Load
      obj,
      GET_STRUCT_MEMBER_OFFSET(PyObject, ob_refcnt),
#ifdef Py_IMMORTAL_INSTANCES
      r1, // BitTest
      kImmortalBitPos,
      end_decref, // BranchC
#endif
      cond_decref, // label
      r2, // Sub
      r1,
      r2, // Store
      obj,
      GET_STRUCT_MEMBER_OFFSET(PyObject, ob_refcnt),
      end_decref, // BranchNZ
      dealloc, // label
      reinterpret_cast<uint64_t>(JITRT_Dealloc), // Invoke
      obj,
      end_decref // label
  );
}

// Checks if a type has reasonable == semantics, that is that
// object identity implies equality when compared by Python.  This
// is true for most types, but not true for floats where nan is
// not equal to nan.  But it is true for container types containing
// those floats where PyObject_RichCompareBool is used and it short
// circuits on object identity.
bool isTypeWithReasonablePointerEq(Type t) {
  return t <= TArrayExact || t <= TBytesExact || t <= TDictExact ||
      t <= TListExact || t <= TSetExact || t <= TTupleExact ||
      t <= TTypeExact || t <= TLongExact || t <= TBool || t <= TFunc ||
      t <= TGen || t <= TNoneType || t <= TSlice;
}

static void append_yield_live_regs(
    std::stringstream& ss,
    const YieldBase* yield) {
  for (const auto& reg : yield->liveUnownedRegs()) {
    ss << ", " << reg->name();
  }
  for (const auto& reg : yield->liveOwnedRegs()) {
    ss << ", " << reg->name();
  }
  ss << ", " << yield->liveOwnedRegs().size();
}

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

LIRGenerator::TranslatedBlock LIRGenerator::TranslateOneBasicBlock(
    const hir::BasicBlock* hir_bb) {
  BasicBlockBuilder bbb(env_, lir_func_);

  for (auto& i : *hir_bb) {
    auto opcode = i.opcode();
    bbb.setCurrentInstr(&i);
    switch (opcode) {
      case Opcode::kLoadArg: {
        auto instr = static_cast<const LoadArg*>(&i);
        if (instr->arg_idx() >= env_->arg_locations.size()) {
          bbb.AppendCode(
              "Load {}, __asm_extra_args, {}",
              instr->dst(),
              (instr->arg_idx() - env_->arg_locations.size()) * kPointerSize);
        } else {
          bbb.AppendCode("LoadArg {} {}", instr->dst(), instr->arg_idx());
        }
        break;
      }
      case Opcode::kLoadCurrentFunc: {
        bbb.AppendCode("Move {}, __asm_func", i.GetOutput());
        break;
      }
      case Opcode::kMakeCell: {
        auto instr = static_cast<const MakeCell*>(&i);
        bbb.AppendCode(
            "Call {}, {:#x}, {}",
            instr->dst(),
            reinterpret_cast<uint64_t>(PyCell_New),
            instr->val());
        break;
      }
      case Opcode::kStealCellItem:
      case Opcode::kLoadCellItem: {
        bbb.AppendCode(
            "Load {}, {}, {}",
            i.GetOutput(),
            i.GetOperand(0),
            GET_STRUCT_MEMBER_OFFSET(PyCellObject, ob_ref));
        break;
      }
      case Opcode::kSetCellItem: {
        auto instr = static_cast<const SetCellItem*>(&i);
        bbb.AppendCode(
            "Store {}, {}, {}",
            instr->src(),
            instr->cell(),
            GET_STRUCT_MEMBER_OFFSET(PyCellObject, ob_ref));
        break;
      }
      case Opcode::kLoadConst: {
        auto instr = static_cast<const LoadConst*>(&i);
        Type ty = instr->type();
        if (ty <= TCDouble) {
          auto tmp_name = GetSafeTempName();
          double_t spec_value = ty.doubleSpec();
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
          uint64_t v = *reinterpret_cast<uint64_t*>(&spec_value);
#pragma GCC diagnostic pop
          // This loads the bits of the double into memory
          bbb.AppendCode("Move {}:{}, {:#x}", tmp_name, TCUInt64, v);
          // This moves the value into a floating point register
          bbb.AppendCode(
              "Move {}:{}, {}",
              instr->dst()->name(),
              ty.unspecialized(),
              tmp_name);
        } else {
          intptr_t spec_value = ty.hasIntSpec()
              ? ty.intSpec()
              : reinterpret_cast<intptr_t>(ty.asObject());
          bbb.AppendCode(
              "Move {}:{}, {:#x}",
              instr->dst()->name(),
              ty.unspecialized(),
              spec_value);
        }
        break;
      }
      case Opcode::kLoadVarObjectSize: {
        const size_t kSizeOffset = offsetof(PyVarObject, ob_size);
        bbb.AppendCode(
            "Load {}, {}, {}", i.GetOutput(), i.GetOperand(0), kSizeOffset);
        break;
      }
      case Opcode::kLoadFunctionIndirect: {
        // format will pass this down as a constant
        auto instr = static_cast<const LoadFunctionIndirect*>(&i);
        bbb.AppendCode(
            "Call {} {:#x}, {}, {}",
            instr->dst(),
            reinterpret_cast<uint64_t>(JITRT_LoadFunctionIndirect),
            reinterpret_cast<uint64_t>(instr->funcptr()),
            reinterpret_cast<uint64_t>(instr->descr()));
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
      case Opcode::kPrimitiveBox: {
        auto instr = static_cast<const PrimitiveBox*>(&i);
        std::string src = instr->value()->name();
        Type src_type = instr->value()->type();
        std::string tmp = GetSafeTempName();
        uint64_t func = 0;

        if (src_type == TNullptr) {
          // special case for an uninitialized variable, we'll
          // load zero
          bbb.AppendCode(
              "Call {}, {:#x}, 0",
              instr->GetOutput(),
              reinterpret_cast<uint64_t>(JITRT_BoxI64));
          break;
        } else if (src_type <= (TCUInt64 | TNullptr)) {
          func = reinterpret_cast<uint64_t>(JITRT_BoxU64);
        } else if (src_type <= (TCInt64 | TNullptr)) {
          func = reinterpret_cast<uint64_t>(JITRT_BoxI64);
        } else if (src_type <= (TCUInt32 | TNullptr)) {
          func = reinterpret_cast<uint64_t>(JITRT_BoxU32);
        } else if (src_type <= (TCInt32 | TNullptr)) {
          func = reinterpret_cast<uint64_t>(JITRT_BoxI32);
        } else if (src_type <= (TCDouble)) {
          func = reinterpret_cast<uint64_t>(JITRT_BoxDouble);
        } else if (src_type <= (TCBool | TCUInt8 | TCUInt16 | TNullptr)) {
          bbb.AppendCode(
              "ConvertUnsigned {}:CUInt32, {}:{}", tmp, src, src_type);
          src = tmp;
          func = reinterpret_cast<uint64_t>(
              (src_type <= TCBool) ? JITRT_BoxBool : JITRT_BoxU32);
          src_type = TCUInt32;
        } else if (src_type <= (TCInt8 | TCInt16 | TNullptr)) {
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
        uint64_t func = 0;

        // Because a failed unbox to unsigned smuggles the bit pattern for a
        // signed -1 in the unsigned value, we can likewise just treat unsigned
        // as signed for purposes of checking for -1 here.
        if (src_type <= (TCInt64 | TCUInt64)) {
          func = reinterpret_cast<uint64_t>(JITRT_IsNegativeAndErrOccurred_64);
        } else {
          func = reinterpret_cast<uint64_t>(JITRT_IsNegativeAndErrOccurred_32);
          // We do have to widen to at least 32 bits due to calling convention
          // always passing a minimum of 32 bits.
          if (src_type <= (TCBool | TCInt8 | TCUInt8 | TCInt16 | TCUInt16)) {
            std::string tmp_name = GetSafeTempName();
            bbb.AppendCode(
                "Convert {}:CInt32, {}:{}", tmp_name, src_name, src_type);
            src_name = tmp_name;
            src_type = TCInt32;
          }
        }
        bbb.AppendCode(
            "Call {}, {:#x}, {}:{}", instr->dst(), func, src_name, src_type);
        break;
      }

      case Opcode::kPrimitiveUnbox: {
        auto instr = static_cast<const PrimitiveUnbox*>(&i);
        Type ty = instr->type();
        uint64_t func = 0;
        if (ty <= TCBool) {
          uint64_t true_addr = reinterpret_cast<uint64_t>(Py_True);
          bbb.AppendCode(
              "Equal {} {} {:#x}", instr->dst(), instr->value(), true_addr);
        } else if (ty <= TCUInt64) {
          func = reinterpret_cast<uint64_t>(JITRT_UnboxU64);
        } else if (ty <= TCUInt32) {
          func = reinterpret_cast<uint64_t>(JITRT_UnboxU32);
        } else if (ty <= TCUInt16) {
          func = reinterpret_cast<uint64_t>(JITRT_UnboxU16);
        } else if (ty <= TCUInt8) {
          func = reinterpret_cast<uint64_t>(JITRT_UnboxU8);
        } else if (ty <= TCInt64) {
          func = reinterpret_cast<uint64_t>(JITRT_UnboxI64);
        } else if (ty <= TCInt32) {
          func = reinterpret_cast<uint64_t>(JITRT_UnboxI32);
        } else if (ty <= TCInt16) {
          func = reinterpret_cast<uint64_t>(JITRT_UnboxI16);
        } else if (ty <= TCInt8) {
          func = reinterpret_cast<uint64_t>(JITRT_UnboxI8);
        } else {
          Py_UNREACHABLE();
        }

        if (func) {
          bbb.AppendCode(
              "Call {}, {:#x}, {}", instr->dst(), func, instr->value());
        }

        break;
      }
      case Opcode::kPrimitiveUnaryOp: {
        auto instr = static_cast<const PrimitiveUnaryOp*>(&i);
        std::string op;
        switch (instr->op()) {
          case PrimitiveUnaryOpKind::kNegateInt:
            op = "Negate";
            break;
          case PrimitiveUnaryOpKind::kInvertInt:
            op = "Invert";
            break;
          default:
            JIT_CHECK(false, "not implemented unary op %d", (int)instr->op());
            break;
        }
        bbb.AppendCode("{} {}, {}", op, instr->GetOutput(), instr->value());
        break;
      }
      case Opcode::kReturn: {
        // TODO support constant operand to Return
        Register* reg = i.GetOperand(0);
        bbb.AppendCode(
            "Return {}:{}", reg->name(), reg->type().unspecialized());
        break;
      }
      case Opcode::kYieldValue: {
        auto instr = static_cast<const YieldValue*>(&i);
        std::stringstream ss;
        ss << "YieldValue " << instr->dst()->name() << ", __asm_tstate,"
           << instr->reg()->name();
        append_yield_live_regs(ss, instr);
        bbb.AppendCode(ss.str());
        break;
      }
      case Opcode::kInitialYield: {
        auto instr = static_cast<const InitialYield*>(&i);
        std::stringstream ss;
        ss << "YieldInitial " << instr->dst()->name() << ", __asm_tstate, ";
        append_yield_live_regs(ss, instr);
        bbb.AppendCode(ss.str());
        break;
      }
      case Opcode::kYieldFrom: {
        auto instr = static_cast<const YieldFrom*>(&i);
        std::stringstream ss;
        ss << (instr->skipInitialYield() ? "YieldFromSkipInitialSend "
                                         : "YieldFrom ")
           << instr->dst()->name() << ", __asm_tstate, "
           << instr->sendValue()->name() << ", " << instr->iter()->name();
        append_yield_live_regs(ss, instr);
        bbb.AppendCode(ss.str());
        break;
      }
      case Opcode::kAssign: {
        auto instr = static_cast<const Assign*>(&i);
        bbb.AppendCode("Assign {}, {}", instr->dst(), instr->reg());
        break;
      }
      case Opcode::kCondBranch:
      case Opcode::kCondBranchIterNotDone: {
        auto instr = static_cast<const CondBranch*>(&i);

        auto tmp = instr->reg()->name();

        if (instr->opcode() == Opcode::kCondBranchIterNotDone) {
          tmp = GetSafeTempName();
          auto iter_done_addr =
              reinterpret_cast<uint64_t>(&jit::g_iterDoneSentinel);
          bbb.AppendCode("Sub {}, {}, {}", tmp, instr->reg(), iter_done_addr);
        }

        bbb.AppendCode(
            "CondBranch {}, {}, {}",
            tmp,
            instr->true_bb()->id,
            instr->false_bb()->id);
        break;
      }
      case Opcode::kCondBranchCheckType: {
        auto& instr = static_cast<const CondBranchCheckType&>(i);
        JIT_CHECK(instr.type().isExact(), "only exact type checking supported");
        auto type_var = GetSafeTempName();
        auto eq_res_var = GetSafeTempName();
        bbb.AppendCode(
            "Load {}, {}, {}",
            type_var,
            instr.reg(),
            GET_STRUCT_MEMBER_OFFSET(PyObject, ob_type));
        bbb.AppendCode(
            "Equal {}, {}, {:#x}",
            eq_res_var,
            type_var,
            reinterpret_cast<uint64_t>(instr.type().uniquePyType()));
        bbb.AppendCode(
            "CondBranch {}, {}, {}",
            eq_res_var,
            instr.true_bb()->id,
            instr.false_bb()->id);
        break;
      }
      case Opcode::kLoadAttr: {
        auto instr = static_cast<const LoadAttr*>(&i);
        std::string tmp_id = GetSafeTempName();
        auto func = reinterpret_cast<uint64_t>(&jit::LoadAttrCache::invoke);
        auto cache = env_->code_rt->AllocateLoadAttrCache();
        PyObject* name = PyTuple_GET_ITEM(
            GetHIRFunction()->code->co_names, instr->name_idx());

        bbb.AppendCode(
            "Move {0}, {1:#x}\n"
            "Call {2}, {3:#x}, {4:#x}, {5}, {0}",
            tmp_id,
            reinterpret_cast<uint64_t>(name),
            instr->dst(),
            func,
            reinterpret_cast<uint64_t>(cache),
            instr->receiver());
        break;
      }
      case Opcode::kLoadAttrSpecial: {
        auto instr = static_cast<const LoadAttrSpecial*>(&i);
        bbb.AppendCode(
            "Call {}, {}, __asm_tstate, {}, {}",
            instr->GetOutput(),
            reinterpret_cast<intptr_t>(special_lookup),
            instr->GetOperand(0),
            reinterpret_cast<intptr_t>(instr->id()));
        break;
      }
      case Opcode::kLoadTypeAttrCacheItem: {
        auto instr = static_cast<const LoadTypeAttrCacheItem*>(&i);
        auto cache = env_->code_rt->getLoadTypeAttrCache(instr->cache_id());
        auto addr =
            reinterpret_cast<uint64_t>(&(cache->items[instr->item_idx()]));
        bbb.AppendCode("Load {}, {:#x}", instr->GetOutput(), addr);
        break;
      }
      case Opcode::kFillTypeAttrCache: {
        auto instr = static_cast<const FillTypeAttrCache*>(&i);
        auto cache = reinterpret_cast<uint64_t>(
            env_->code_rt->getLoadTypeAttrCache(instr->cache_id()));
        auto func = reinterpret_cast<uint64_t>(&jit::LoadTypeAttrCache::invoke);
        std::string tmp_id = GetSafeTempName();
        PyObject* name = PyTuple_GET_ITEM(
            GetHIRFunction()->code->co_names, instr->name_idx());
        bbb.AppendCode(
            "Move {}, {:#x}", tmp_id, reinterpret_cast<uint64_t>(name));
        bbb.AppendCode(
            "Call {}, {:#x}, {:#x}, {}, {}",
            instr->GetOutput(),
            func,
            cache,
            instr->receiver(),
            tmp_id);
        break;
      }
      case Opcode::kLoadMethod: {
        auto instr = static_cast<const LoadMethod*>(&i);

        std::string tmp_id = GetSafeTempName();
        PyObject* name = PyTuple_GET_ITEM(
            GetHIRFunction()->code->co_names, instr->name_idx());

        bbb.AppendCode(
            "Move {}, {:#x}", tmp_id, reinterpret_cast<uint64_t>(name));

        if (env_->optimizable_load_call_methods_.count(instr)) {
          auto func = reinterpret_cast<uint64_t>(JITRT_GetMethod);
          auto cache_entry = env_->code_rt->AllocateLoadMethodCache();
          bbb.AppendCode(
              "Call {}, {:#x}, {}, {}, {:#x}\n",
              instr->dst(),
              func,
              instr->receiver(),
              tmp_id,
              reinterpret_cast<uint64_t>(cache_entry));
        } else {
          auto func = reinterpret_cast<uint64_t>(PyObject_GetAttr);
          bbb.AppendCode(
              "Call {}, {:#x}, {}, {}\n",
              instr->dst(),
              func,
              instr->receiver(),
              tmp_id);
        }
        break;
      }
      case Opcode::kLoadMethodSuper: {
        auto instr = static_cast<const LoadMethodSuper*>(&i);
        std::string tmp_id = GetSafeTempName();
        PyObject* name = PyTuple_GET_ITEM(
            GetHIRFunction()->code->co_names, instr->name_idx());
        bbb.AppendCode(
            "Move {}, {:#x}", tmp_id, reinterpret_cast<uint64_t>(name));

        if (env_->optimizable_load_call_methods_.count(instr)) {
          auto func = reinterpret_cast<uint64_t>(JITRT_GetMethodFromSuper);
          bbb.AppendCode(
              "Call {}, {:#x}, {}, {}, {}, {}, {}\n",
              instr->dst(),
              func,
              instr->global_super(),
              instr->type(),
              instr->receiver(),
              tmp_id,
              instr->no_args_in_super_call() ? 1 : 0);
        } else {
          auto func = reinterpret_cast<uint64_t>(JITRT_GetAttrFromSuper);
          bbb.AppendCode(
              "Call {}, {:#x}, {}, {}, {}, {}, {}\n",
              instr->dst(),
              func,
              instr->global_super(),
              instr->type(),
              instr->receiver(),
              tmp_id,
              instr->no_args_in_super_call() ? 1 : 0);
        }
        break;
      }
      case Opcode::kLoadAttrSuper: {
        auto instr = static_cast<const LoadAttrSuper*>(&i);
        std::string tmp_id = GetSafeTempName();
        PyObject* name = PyTuple_GET_ITEM(
            GetHIRFunction()->code->co_names, instr->name_idx());

        bbb.AppendCode(
            "Move {}, {:#x}", tmp_id, reinterpret_cast<uint64_t>(name));

        auto func = reinterpret_cast<uint64_t>(JITRT_GetAttrFromSuper);
        bbb.AppendCode(
            "Call {}, {:#x}, {}, {}, {}, {}, {}\n",
            instr->dst(),
            func,
            instr->global_super(),
            instr->type(),
            instr->receiver(),
            tmp_id,
            instr->no_args_in_super_call() ? 1 : 0);

        break;
      }
      case Opcode::kBinaryOp: {
        auto bin_op = static_cast<const BinaryOp*>(&i);

        // NB: This needs to be in the order that the values appear in the
        // BinaryOpKind enum
        static const uint64_t helpers[] = {
            reinterpret_cast<uint64_t>(PyNumber_Add),
            reinterpret_cast<uint64_t>(PyNumber_And),
            reinterpret_cast<uint64_t>(PyNumber_FloorDivide),
            reinterpret_cast<uint64_t>(PyNumber_Lshift),
            reinterpret_cast<uint64_t>(PyNumber_MatrixMultiply),
            reinterpret_cast<uint64_t>(PyNumber_Remainder),
            reinterpret_cast<uint64_t>(PyNumber_Multiply),
            reinterpret_cast<uint64_t>(PyNumber_Or),
            reinterpret_cast<uint64_t>(PyNumber_Power),
            reinterpret_cast<uint64_t>(PyNumber_Rshift),
            reinterpret_cast<uint64_t>(PyObject_GetItem),
            reinterpret_cast<uint64_t>(PyNumber_Subtract),
            reinterpret_cast<uint64_t>(PyNumber_TrueDivide),
            reinterpret_cast<uint64_t>(PyNumber_Xor),
        };
        JIT_CHECK(
            static_cast<unsigned long>(bin_op->op()) < sizeof(helpers),
            "unsupported binop");
        auto op_kind = static_cast<int>(bin_op->op());

        if (bin_op->op() != BinaryOpKind::kPower) {
          bbb.AppendCode(
              "Call {}, {:#x}, {}, {}",
              bin_op->dst(),
              helpers[op_kind],
              bin_op->left(),
              bin_op->right());
        } else {
          bbb.AppendCode(
              "Call {}, {:#x}, {}, {}, {:#x}",
              bin_op->dst(),
              helpers[op_kind],
              bin_op->left(),
              bin_op->right(),
              reinterpret_cast<uint64_t>(Py_None));
        }
        break;
      }
      case Opcode::kUnaryOp: {
        auto unary_op = static_cast<const UnaryOp*>(&i);

        // NB: This needs to be in the order that the values appear in the
        // UnaryOpKind enum
        static const uint64_t helpers[] = {
            reinterpret_cast<uint64_t>(JITRT_UnaryNot),
            reinterpret_cast<uint64_t>(PyNumber_Negative),
            reinterpret_cast<uint64_t>(PyNumber_Positive),
            reinterpret_cast<uint64_t>(PyNumber_Invert),
        };
        JIT_CHECK(
            static_cast<unsigned long>(unary_op->op()) < sizeof(helpers),
            "unsupported unaryop");

        auto op_kind = static_cast<int>(unary_op->op());
        bbb.AppendCode(
            "Call {}, {:#x}, {}",
            unary_op->dst(),
            helpers[op_kind],
            unary_op->operand());
        break;
      }
      case Opcode::kIsInstance: {
        auto instr = static_cast<const IsInstance*>(&i);
        bbb.AppendCode(
            "Call {}, {:#x}, {}, {}",
            instr->dst(),
            reinterpret_cast<uint64_t>(PyObject_IsInstance),
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kCompare: {
        auto instr = static_cast<const Compare*>(&i);

        bbb.AppendCode(
            "Call {}, {:#x}, __asm_tstate, {}, {}, {}",
            instr->dst(),
            reinterpret_cast<uint64_t>(cmp_outcome),
            static_cast<int>(instr->op()),
            instr->left(),
            instr->right());
        break;
      }
      case Opcode::kCompareBool: {
        auto instr = static_cast<const Compare*>(&i);

        if (instr->op() == CompareOp::kIn) {
          if (instr->right()->type() <= TUnicodeExact) {
            bbb.AppendCode(
                "Call {}, {:#x}, {}, {}",
                instr->dst(),
                reinterpret_cast<uint64_t>(PyUnicode_Contains),
                instr->right(),
                instr->left());
          } else {
            bbb.AppendCode(
                "Call {}, {:#x}, {}, {}",
                instr->dst(),
                reinterpret_cast<uint64_t>(PySequence_Contains),
                instr->right(),
                instr->left());
          }
        } else if (instr->op() == CompareOp::kNotIn) {
          bbb.AppendCode(
              "Call {}, {:#x}, {}, {}",
              instr->dst(),
              reinterpret_cast<uint64_t>(JITRT_NotContains),
              instr->right(),
              instr->left());
        } else if (
            (instr->op() == CompareOp::kEqual ||
             instr->op() == CompareOp::kNotEqual) &&
            (instr->left()->type() <= TUnicodeExact ||
             instr->right()->type() <= TUnicodeExact)) {
          bbb.AppendCode(
              "Call {}, {:#x}, {}, {}, {}",
              instr->dst(),
              reinterpret_cast<uint64_t>(JITRT_UnicodeEquals),
              instr->left(),
              instr->right(),
              static_cast<int>(instr->op()));
        } else if (
            (instr->op() == CompareOp::kEqual ||
             instr->op() == CompareOp::kNotEqual) &&
            (isTypeWithReasonablePointerEq(instr->left()->type()) ||
             isTypeWithReasonablePointerEq(instr->right()->type()))) {
          bbb.AppendCode(
              "Call {}, {:#x}, {}, {}, {}",
              instr->dst(),
              reinterpret_cast<uint64_t>(PyObject_RichCompareBool),
              instr->left(),
              instr->right(),
              static_cast<int>(instr->op()));
        } else {
          bbb.AppendCode(
              "Call {}, {:#x}, {}, {}, {}",
              instr->dst(),
              reinterpret_cast<uint64_t>(JITRT_RichCompareBool),
              instr->left(),
              instr->right(),
              static_cast<int>(instr->op()));
        }
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

      case Opcode::kDeopt: {
        bbb.AppendCode(
            MakeGuard("AlwaysFail", static_cast<const DeoptBase&>(i)));
        break;
      }
      case Opcode::kRaiseAwaitableError: {
        const auto& instr = static_cast<const RaiseAwaitableError&>(i);
        bbb.AppendCode(
            "Invoke {}, __asm_tstate, {}, {}",
            reinterpret_cast<intptr_t>(format_awaitable_error),
            instr.GetOperand(0),
            instr.with_opcode());
        bbb.AppendCode(MakeGuard("AlwaysFail", instr));
        break;
      }
      case Opcode::kCheckNone:
      case Opcode::kCheckExc:
      case Opcode::kCheckNeg:
      case Opcode::kCheckVar:
      case Opcode::kCheckField:
      case Opcode::kGuard:
      case Opcode::kGuardIs: {
        const auto& instr = static_cast<const DeoptBase&>(i);
        std::string kind = "NotNull";
        if (instr.IsCheckNone()) {
          kind = "NotNone";
        } else if (instr.IsCheckNeg()) {
          kind = "NotNegative";
        } else if (instr.IsGuardIs()) {
          kind = "Is";
        }
        bbb.AppendCode(MakeGuard(kind, instr, instr.GetOperand(0)->name()));
        break;
      }
      case Opcode::kRefineType: {
        break;
      }
      case Opcode::kLoadGlobalCached: {
        ThreadedCompileSerialize guard;
        auto instr = static_cast<const LoadGlobalCached*>(&i);
        PyObject* globals = env_->code_rt->GetGlobals();
        PyObject* name = PyTuple_GET_ITEM(
            GetHIRFunction()->code->co_names, instr->name_idx());
        auto cache = env_->rt->findGlobalCache(globals, name);
        bbb.AppendCode(
            "Load {}, {:#x}",
            instr->GetOutput(),
            reinterpret_cast<uint64_t>(cache.valuePtr()));
        break;
      }
      case Opcode::kLoadGlobal: {
        auto instr = static_cast<const LoadGlobal*>(&i);
        PyObject* builtins = env_->code_rt->GetBuiltins();
        PyObject* globals = env_->code_rt->GetGlobals();
        PyObject* name = PyTuple_GET_ITEM(
            GetHIRFunction()->code->co_names, instr->name_idx());
        bbb.AppendCode(
            "Call {}, {:#x}, {}, {}, {}",
            instr->GetOutput(),
            reinterpret_cast<uint64_t>(JITRT_LoadGlobal),
            globals,
            builtins,
            name);
        break;
      }
      case Opcode::kStoreAttr: {
        auto instr = static_cast<const StoreAttr*>(&i);

        std::string tmp_id = GetSafeTempName();

        PyCodeObject* code = GetHIRFunction()->code;
        auto ob_item =
            reinterpret_cast<PyTupleObject*>(code->co_names)->ob_item;
        StoreAttrCache* cache = env_->code_rt->allocateStoreAttrCache();
        bbb.AppendCode(
            "Call {}, {:#x}, {:#x}, {}, {:#x}, {}",
            instr->dst(),
            reinterpret_cast<uint64_t>(&jit::StoreAttrCache::invoke),
            reinterpret_cast<uint64_t>(cache),
            instr->receiver(),
            reinterpret_cast<uint64_t>(ob_item[instr->name_idx()]),
            instr->value());

        break;
      }
      case Opcode::kVectorCall: {
        auto& instr = static_cast<const VectorCallBase&>(i);
        if (TranslateSpecializedCall(bbb, instr)) {
          break;
        }
        size_t flags = instr.isAwaited() ? _Py_AWAITED_CALL_MARKER : 0;
        emitVectorCall(bbb, instr, flags, false);
        break;
      }
      case Opcode::kVectorCallKW: {
        auto& instr = static_cast<const VectorCallBase&>(i);
        size_t flags = instr.isAwaited() ? _Py_AWAITED_CALL_MARKER : 0;
        emitVectorCall(bbb, instr, flags, true);
        break;
      }
      case Opcode::kVectorCallStatic: {
        auto& instr = static_cast<const VectorCallBase&>(i);
        if (TranslateSpecializedCall(bbb, instr)) {
          break;
        }
        size_t flags = _Py_VECTORCALL_INVOKED_STATICALLY |
            (instr.isAwaited() ? _Py_AWAITED_CALL_MARKER : 0);
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
        bbb.AppendCode(
            "Call {}, {:#x}, {}, {}, 0",
            instr.dst()->name(),
            reinterpret_cast<uint64_t>(rt_helper),
            instr.func()->name(),
            instr.pargs()->name());
        break;
      }
      case Opcode::kCallExKw: {
        auto& instr = static_cast<const CallExKw&>(i);
        auto rt_helper = instr.isAwaited() ? JITRT_CallFunctionExAwaited
                                           : JITRT_CallFunctionEx;
        bbb.AppendCode(
            "Call {}, {:#x}, {}, {}, {}",
            instr.dst()->name(),
            reinterpret_cast<uint64_t>(rt_helper),
            instr.func()->name(),
            instr.pargs()->name(),
            instr.kwargs()->name());
        break;
      }
      case Opcode::kCallMethod: {
        auto instr = static_cast<const CallMethod*>(&i);

        std::string s = fmt::format("Vectorcall {}", *instr->dst());
        size_t flags = instr->isAwaited() ? _Py_AWAITED_CALL_MARKER : 0;
        if (env_->optimizable_load_call_methods_.count(instr)) {
          format_to(
              s,
              ", {}, {}, {}, {}",
              reinterpret_cast<uint64_t>(JITRT_CallMethod),
              flags,
              *instr->func(),
              *instr->self());
        } else {
          format_to(
              s,
              ", {}, {}, {}",
              reinterpret_cast<uint64_t>(_PyObject_Vectorcall),
              flags,
              *instr->func());
        }
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
          ss << ", " << instr->GetOperand(i)->name();
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
        int prim_ret_type = _PyClassLoader_ResolvePrimitiveType(
            _PyClassLoader_GetReturnTypeDescr(func));

        std::stringstream ss;
        JIT_CHECK(
            !usesRuntimeFunc(func->func_code),
            "Can't statically invoke given function");
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

        // functions that return primitives will signal error via edx
        bbb.AppendCode(MakeGuard(
            "NotNull",
            static_cast<const DeoptBase&>(i),
            prim_ret_type != TYPED_OBJECT ? "reg:edx"
                                          : instr->GetOutput()->name()));
        break;
      }

      case Opcode::kInvokeMethod: {
        auto instr = static_cast<const InvokeMethod*>(&i);

        std::stringstream ss;
        size_t flags = instr->isAwaited() ? _Py_AWAITED_CALL_MARKER : 0;
        ss << "Vectorcall " << *instr->dst() << ", "
           << reinterpret_cast<uint64_t>(JITRT_InvokeMethod) << ", " << flags
           << ", " << instr->slot();

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
        bbb.AppendCode(
            "Load {}, {}, {}",
            instr->GetOutput(),
            instr->receiver(),
            instr->offset());
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
        uint64_t func;
        auto instr = static_cast<const Cast*>(&i);
        if (instr->optional()) {
          func = reinterpret_cast<uint64_t>(JITRT_CastOptional);
        } else {
          func = reinterpret_cast<uint64_t>(JITRT_Cast);
        }

        bbb.AppendCode(
            "Call {}, {:#x}, {}, {:#x}\n",
            instr->dst(),
            func,
            instr->value(),
            reinterpret_cast<uint64_t>(instr->pytype()));
        break;
      }

      case Opcode::kMakeListTuple: {
        auto instr = static_cast<const MakeListTuple*>(&i);

        bbb.AppendCode(
            "Call {}, {:#x}, {}",
            instr->dst(),
            instr->is_tuple() ? reinterpret_cast<uint64_t>(PyTuple_New)
                              : reinterpret_cast<uint64_t>(PyList_New),
            instr->nvalues());
        break;
      }
      case Opcode::kInitListTuple: {
        auto instr = static_cast<const InitListTuple*>(&i);
        auto is_tuple = instr->is_tuple();

        std::string base = instr->GetOperand(0)->name();

        std::string tmp_id = GetSafeTempName();
        if (!is_tuple && instr->NumOperands() > 1) {
          bbb.AppendCode(
              "Load {}, {}, {}",
              tmp_id,
              base,
              GET_STRUCT_MEMBER_OFFSET(PyListObject, ob_item));
          base = std::move(tmp_id);
        }

        const size_t ob_item_offset =
            is_tuple ? GET_STRUCT_MEMBER_OFFSET(PyTupleObject, ob_item) : 0;
        for (size_t i = 1; i < instr->NumOperands(); i++) {
          bbb.AppendCode(
              "Store {}, {}, {}",
              instr->GetOperand(i),
              base,
              ob_item_offset + ((i - 1) * kPointerSize));
        }
        break;
      }
      case Opcode::kLoadTupleItem: {
        auto instr = static_cast<const LoadTupleItem*>(&i);

        const size_t item_offset =
            GET_STRUCT_MEMBER_OFFSET(PyTupleObject, ob_item) +
            instr->idx() * kPointerSize;
        bbb.AppendCode(
            "Load {} {} {}", instr->GetOutput(), instr->tuple(), item_offset);
        break;
      }
      case Opcode::kCheckSequenceBounds: {
        auto instr = static_cast<const CheckSequenceBounds*>(&i);
        bbb.AppendCode(
            "Call {}, {:#x}, {}, {}",
            instr->dst(),
            reinterpret_cast<uint64_t>(_PySequence_CheckBounds),
            instr->array(),
            instr->idx());
        break;
      }
      case Opcode::kLoadArrayItem: {
        auto instr = static_cast<const LoadArrayItem*>(&i);
        auto type = instr->type();
        uint64_t func = 0;

        if (type <= TCInt8) {
          func = reinterpret_cast<uint64_t>(JITRT_GetI8_FromArray);
        } else if (type <= TCUInt8) {
          func = reinterpret_cast<uint64_t>(JITRT_GetU8_FromArray);
        } else if (type <= TCInt16) {
          func = reinterpret_cast<uint64_t>(JITRT_GetI16_FromArray);
        } else if (type <= TCUInt16) {
          func = reinterpret_cast<uint64_t>(JITRT_GetU16_FromArray);
        } else if (type <= TCInt32) {
          func = reinterpret_cast<uint64_t>(JITRT_GetI32_FromArray);
        } else if (type <= TCUInt32) {
          func = reinterpret_cast<uint64_t>(JITRT_GetU32_FromArray);
        } else if (type <= TCInt64) {
          func = reinterpret_cast<uint64_t>(JITRT_GetI64_FromArray);
        } else if (type <= TCUInt64) {
          func = reinterpret_cast<uint64_t>(JITRT_GetU64_FromArray);
        } else if (type <= TObject) {
          func = reinterpret_cast<uint64_t>(JITRT_GetObj_FromArray);
        }
        JIT_CHECK(func != 0, "unknown array type %s", type.toString().c_str());

        bbb.AppendCode(
            "Call {}, {:#x}, {}, {}, {:#x}",
            instr->dst(),
            func,
            instr->ob_item(),
            instr->idx(),
            instr->offset());
        break;
      }
      case Opcode::kStoreArrayItem: {
        auto instr = static_cast<const StoreArrayItem*>(&i);
        auto type = instr->type();
        uint64_t func = 0;

        if (type <= TCInt8) {
          func = reinterpret_cast<uint64_t>(JITRT_SetI8_InArray);
        } else if (type <= TCUInt8) {
          func = reinterpret_cast<uint64_t>(JITRT_SetU8_InArray);
        } else if (type <= TCInt16) {
          func = reinterpret_cast<uint64_t>(JITRT_SetI16_InArray);
        } else if (type <= TCUInt16) {
          func = reinterpret_cast<uint64_t>(JITRT_SetU16_InArray);
        } else if (type <= TCInt32) {
          func = reinterpret_cast<uint64_t>(JITRT_SetI32_InArray);
        } else if (type <= TCUInt32) {
          func = reinterpret_cast<uint64_t>(JITRT_SetU32_InArray);
        } else if (type <= TCInt64) {
          func = reinterpret_cast<uint64_t>(JITRT_SetI64_InArray);
        } else if (type <= TCUInt64) {
          func = reinterpret_cast<uint64_t>(JITRT_SetU64_InArray);
        } else if (type <= TObject) {
          func = reinterpret_cast<uint64_t>(JITRT_SetObj_InArray);
        }
        JIT_CHECK(func != 0, "unknown array type %s", type.toString().c_str());

        bbb.AppendCode(
            "Invoke {:#x}, {}, {}, {}",
            func,
            instr->ob_item(),
            instr->value(),
            instr->idx());
        break;
      }
      case Opcode::kRepeatList: {
        auto instr = static_cast<const RepeatList*>(&i);
        auto func = reinterpret_cast<uint64_t>(_PyList_Repeat);
        bbb.AppendCode(
            "Call {}, {:#x}, {}, {}\n",
            instr->dst(),
            func,
            instr->seq(),
            instr->num());
        break;
      }
      case Opcode::kRepeatTuple: {
        auto instr = static_cast<const RepeatTuple*>(&i);
        auto func = reinterpret_cast<uint64_t>(_PyTuple_Repeat);
        bbb.AppendCode(
            "Call {}, {:#x}, {}, {}\n",
            instr->dst(),
            func,
            instr->seq(),
            instr->num());
        break;
      }
      case Opcode::kMakeCheckedDict: {
        auto instr = static_cast<const MakeCheckedDict*>(&i);
        auto capacity = instr->GetCapacity();
        if (capacity == 0) {
          bbb.AppendCode(
              "Call {}, {:#x}, {:#x}",
              instr->GetOutput(),
              reinterpret_cast<uint64_t>(_PyCheckedDict_New),
              reinterpret_cast<uint64_t>(instr->type().typeSpec()));
        } else {
          bbb.AppendCode(
              "Call {}, {:#x}, {:#x}, {}",
              instr->GetOutput(),
              reinterpret_cast<uint64_t>(_PyCheckedDict_NewPresized),
              reinterpret_cast<uint64_t>(instr->type().typeSpec()),
              capacity);
        }
        break;
      }
      case Opcode::kMakeDict: {
        auto instr = static_cast<const MakeDict*>(&i);
        auto capacity = instr->GetCapacity();
        if (capacity == 0) {
          bbb.AppendCode(
              "Call {}, {:#x}",
              instr->GetOutput(),
              reinterpret_cast<uint64_t>(PyDict_New));
        } else {
          bbb.AppendCode(
              "Call {}, {:#x}, {}",
              instr->GetOutput(),
              reinterpret_cast<uint64_t>(_PyDict_NewPresized),
              capacity);
        }
        break;
      }
      case Opcode::kMakeSet: {
        auto instr = static_cast<const MakeSet*>(&i);
        bbb.AppendCode(
            "Call {}, {:#x}, 0",
            instr->GetOutput(),
            reinterpret_cast<uint64_t>(PySet_New));
        break;
      }
      case Opcode::kMergeDictUnpack: {
        auto instr = static_cast<const MergeDictUnpack*>(&i);
        bbb.AppendCode(
            "Call {}, {:#x}, __asm_tstate, {}, {}, {}",
            instr->GetOutput(),
            reinterpret_cast<uint64_t>(__Invoke_PyDict_MergeEx),
            instr->GetOperand(0),
            instr->GetOperand(1),
            instr->GetOperand(2));
        break;
      }
      case Opcode::kMergeSetUnpack: {
        auto instr = static_cast<const MergeSetUnpack*>(&i);
        bbb.AppendCode(
            "Call {}, {:#x}, {}, {}",
            instr->GetOutput(),
            reinterpret_cast<uint64_t>(_PySet_Update),
            instr->GetOperand(0),
            instr->GetOperand(1));
        break;
      }
      case Opcode::kSetDictItem: {
        auto instr = static_cast<const SetDictItem*>(&i);
        bbb.AppendCode(
            "Call {}, {:#x}, {}, {}, {}",
            instr->GetOutput(),
            reinterpret_cast<uint64_t>(_PyDict_SetItem),
            instr->GetDict(),
            instr->GetKey(),
            instr->GetValue());
        break;
      }
      case Opcode::kSetSetItem: {
        auto instr = static_cast<const SetSetItem*>(&i);
        bbb.AppendCode(
            "Call {}, {:#x}, {}, {}",
            instr->GetOutput(),
            reinterpret_cast<uint64_t>(PySet_Add),
            instr->GetSet(),
            instr->GetKey());
        break;
      }
      case Opcode::kStoreSubscr: {
        auto instr = static_cast<const StoreSubscr*>(&i);
        bbb.AppendCode(
            "Call {}, {:#x}, {}, {}, {}",
            instr->dst(),
            reinterpret_cast<uint64_t>(PyObject_SetItem),
            instr->container(),
            instr->index(),
            instr->value());

        break;
      }
      case Opcode::kInPlaceOp: {
        auto instr = static_cast<const InPlaceOp*>(&i);

        // NB: This needs to be in the order that the values appear in the
        // InPlaceOpKind enum
        static const uint64_t helpers[] = {
            reinterpret_cast<uint64_t>(PyNumber_InPlaceAdd),
            reinterpret_cast<uint64_t>(PyNumber_InPlaceAnd),
            reinterpret_cast<uint64_t>(PyNumber_InPlaceFloorDivide),
            reinterpret_cast<uint64_t>(PyNumber_InPlaceLshift),
            reinterpret_cast<uint64_t>(PyNumber_InPlaceMatrixMultiply),
            reinterpret_cast<uint64_t>(PyNumber_InPlaceRemainder),
            reinterpret_cast<uint64_t>(PyNumber_InPlaceMultiply),
            reinterpret_cast<uint64_t>(PyNumber_InPlaceOr),
            reinterpret_cast<uint64_t>(PyNumber_InPlacePower),
            reinterpret_cast<uint64_t>(PyNumber_InPlaceRshift),
            reinterpret_cast<uint64_t>(PyNumber_InPlaceSubtract),
            reinterpret_cast<uint64_t>(PyNumber_InPlaceTrueDivide),
            reinterpret_cast<uint64_t>(PyNumber_InPlaceXor),
        };
        JIT_CHECK(
            static_cast<unsigned long>(instr->op()) < sizeof(helpers),
            "unsupported inplaceop");

        auto op_kind = static_cast<int>(instr->op());

        if (instr->op() != InPlaceOpKind::kPower) {
          bbb.AppendCode(
              "Call {} {:#x}, {}, {}",
              instr->dst(),
              helpers[op_kind],
              instr->left(),
              instr->right());
        } else {
          bbb.AppendCode(
              "Call {} {:#x}, {}, {}, {:#x}",
              instr->dst(),
              helpers[op_kind],
              instr->left(),
              instr->right(),
              reinterpret_cast<uint64_t>(Py_None));
        }
        break;
      }
      case Opcode::kBranch: {
        break;
      }
      case Opcode::kBuildSlice: {
        auto instr = static_cast<const BuildSlice*>(&i);

        bbb.AppendCode(
            "Call {}, {:#x}, {}, {}, {}",
            instr->dst(),
            reinterpret_cast<uint64_t>(_Invoke_PySlice_New),
            instr->start(),
            instr->stop(),
            instr->step() != nullptr ? instr->step()->name() : "0x0");

        break;
      }
      case Opcode::kGetIter: {
        auto instr = static_cast<const GetIter*>(&i);

        bbb.AppendCode(
            "Call {}, {:#x}, {}",
            instr->GetOutput(),
            reinterpret_cast<uint64_t>(PyObject_GetIter),
            instr->GetOperand(0));

        break;
      }
      case Opcode::kPhi: {
        auto instr = static_cast<const Phi*>(&i);

        std::stringstream ss;
        fmt::print(ss, "Phi {}", instr->GetOutput());
        // ss << "Phi " << *instr->GetOutput();

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

        bbb.AppendCode(
            "Invoke {:#x}, {}",
            reinterpret_cast<uint64_t>(PyEntry_init),
            instr->func());
        break;
      }
      case Opcode::kMakeFunction: {
        auto instr = static_cast<const MakeFunction*>(&i);
        auto code = instr->codeobj();
        auto qualname = instr->qualname();
        PyObject* globals = GetHIRFunction()->globals;

        bbb.AppendCode(
            "Call {}, {:#x}, {}, {:#x}, {}",
            instr->GetOutput(),
            reinterpret_cast<uint64_t>(PyFunction_NewWithQualName),
            code,
            reinterpret_cast<uint64_t>(globals),
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

        bbb.AppendCode(
            "Call {}, {:#x}, {}, {}",
            instr->dst(),
            reinterpret_cast<uint64_t>(PyList_Append),
            instr->list(),
            instr->item());
        break;
      }
      case Opcode::kListExtend: {
        auto instr = static_cast<const ListExtend*>(&i);
        bbb.AppendCode(
            "Call {}, {:#x}, __asm_tstate, {}, {}, {}",
            instr->dst(),
            reinterpret_cast<uint64_t>(__Invoke_PyList_Extend),
            instr->list(),
            instr->iterable(),
            instr->func());
        break;
      }
      case Opcode::kMakeTupleFromList: {
        auto instr = static_cast<const MakeTupleFromList*>(&i);
        bbb.AppendCode(
            "Call {}, {:#x}, {}",
            instr->dst(),
            reinterpret_cast<uint64_t>(PyList_AsTuple),
            instr->list());
        break;
      }
      case Opcode::kGetTuple: {
        auto instr = static_cast<const GetTuple*>(&i);

        bbb.AppendCode(
            "Call {}, {:#x}, {}",
            instr->dst(),
            reinterpret_cast<uint64_t>(PySequence_Tuple),
            instr->iterable());
        break;
      }
      case Opcode::kCheckTuple: {
        auto instr = static_cast<const CheckTuple*>(&i);

        bbb.AppendCode(
            "Call {}, {:#x}, {}",
            instr->dst(),
            reinterpret_cast<uint64_t>(__Invoke_PyTuple_Check),
            instr->iterable());
        break;
      }
      case Opcode::kInvokeIterNext: {
        auto instr = static_cast<const InvokeIterNext*>(&i);
        bbb.AppendCode(
            "Call {}, {:#x}, {}",
            instr->GetOutput(),
            reinterpret_cast<uint64_t>(jit::invokeIterNext),
            instr->GetOperand(0));
        break;
      }
      case Opcode::kLoadEvalBreaker: {
        // NB: This corresponds to an atomic load with
        // std::memory_order_relaxed. It's correct on x86-64 but probably isn't
        // on other architectures.
        static_assert(
            sizeof(_PyRuntime.ceval.eval_breaker._value) == 4,
            "Eval breaker is not a 4 byte value");
        auto eval_breaker =
            reinterpret_cast<uint64_t>(&_PyRuntime.ceval.eval_breaker._value);
        JIT_CHECK(
            i.GetOutput()->type() == TCInt32,
            "eval breaker output should be int");
        bbb.AppendCode("Load {}, {:#x}", i.GetOutput(), eval_breaker);
        break;
      }
      case Opcode::kRunPeriodicTasks: {
        auto func = reinterpret_cast<uint64_t>(&jit::runPeriodicTasks);
        bbb.AppendCode("Call {}, {:#x}", i.GetOutput(), func);
        break;
      }
      case Opcode::kSnapshot: {
        // Snapshots are purely informative
        break;
      }
      case Opcode::kIsTruthy: {
        auto func = reinterpret_cast<uint64_t>(&PyObject_IsTrue);
        bbb.AppendCode(
            "Call {}, {:#x}, {}", i.GetOutput(), func, i.GetOperand(0));
        break;
      }
      case Opcode::kImportFrom: {
        auto& instr = static_cast<const ImportFrom&>(i);
        PyObject* name =
            PyTuple_GET_ITEM(GetHIRFunction()->code->co_names, instr.nameIdx());
        bbb.AppendCode(
            "Call {}, {:#x}, __asm_tstate, {}, {}",
            i.GetOutput(),
            reinterpret_cast<uint64_t>(&_Py_DoImportFrom),
            instr.module(),
            name);
        break;
      }
      case Opcode::kImportName: {
        auto instr = static_cast<const ImportName*>(&i);
        PyObject* name = PyTuple_GET_ITEM(
            GetHIRFunction()->code->co_names, instr->name_idx());
        bbb.AppendCode(
            "Call {}, {:#x}, __asm_tstate, {}, {}, {}",
            i.GetOutput(),
            reinterpret_cast<uint64_t>(&JITRT_ImportName),
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
            "Invoke {:#x}, __asm_tstate, {}, {}",
            reinterpret_cast<uint64_t>(&_Py_DoRaise),
            exc,
            cause);
        bbb.AppendCode(MakeGuard("AlwaysFail", instr));
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
        bbb.AppendCode(MakeGuard("AlwaysFail", instr));
        break;
      }
      case Opcode::kFormatValue: {
        const auto& instr = static_cast<const FormatValue&>(i);
        bbb.AppendCode(
            "Call {}, {:#x}, __asm_tstate, {}, {}, {}",
            instr.dst(),
            reinterpret_cast<uint64_t>(JITRT_FormatValue),
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
        bbb.AppendCode(
            "Load {}, {}, {}",
            instr.GetOutput()->name(),
            instr.reg(),
            GET_STRUCT_MEMBER_OFFSET(PyWaitHandleObject, wh_waiter));
        break;
      }
      case Opcode::kWaitHandleLoadCoroOrResult: {
        const auto& instr = static_cast<const WaitHandleLoadCoroOrResult&>(i);
        bbb.AppendCode(
            "Load {}, {}, {}",
            instr.GetOutput()->name(),
            instr.reg(),
            GET_STRUCT_MEMBER_OFFSET(PyWaitHandleObject, wh_coro_or_result));
        break;
      }
      case Opcode::kWaitHandleRelease: {
        const auto& instr = static_cast<const WaitHandleRelease&>(i);
        std::string null_var = GetSafeTempName();
        bbb.AppendCode(
            "Store 0, {}, {}",
            instr.reg(),
            GET_STRUCT_MEMBER_OFFSET(PyWaitHandleObject, wh_coro_or_result));
        bbb.AppendCode(
            "Store 0, {}, {}",
            instr.reg(),
            GET_STRUCT_MEMBER_OFFSET(PyWaitHandleObject, wh_waiter));
        break;
      }
      case Opcode::kDeleteSubscr: {
        auto tmp = GetSafeTempName();
        const auto& instr = static_cast<const DeleteSubscr&>(i);
        bbb.AppendCode(
            "Call {}:CInt32, {:#x}, {}, {}",
            tmp,
            reinterpret_cast<uint64_t>(PyObject_DelItem),
            instr.container(),
            instr.sub());
        bbb.AppendCode(MakeGuard("NotNegative", instr, tmp));
        break;
      }
      case Opcode::kUnpackExToTuple: {
        auto instr = static_cast<const UnpackExToTuple*>(&i);
        bbb.AppendCode(
            "Call {}, {:#x}, __asm_tstate, {}, {}, {}",
            instr->dst(),
            reinterpret_cast<uint64_t>(JITRT_UnpackExToTuple),
            instr->seq(),
            instr->before(),
            instr->after());
        break;
      }
      case Opcode::kIsErrStopAsyncIteration: {
        auto instr = static_cast<const IsErrStopAsyncIteration*>(&i);
        bbb.AppendCode(
            "Call {}, {:#x}, __asm_tstate, {:#x}",
            instr->dst(),
            reinterpret_cast<uint64_t>(_PyErr_ExceptionMatches),
            reinterpret_cast<uint64_t>(PyExc_StopAsyncIteration));
        break;
      }
      case Opcode::kClearError: {
        bbb.AppendCode(
            "Invoke {:#x}, __asm_tstate",
            reinterpret_cast<uint64_t>(_PyErr_Clear));
        break;
      }
    }

    if (auto db = dynamic_cast<const DeoptBase*>(&i)) {
      switch (db->opcode()) {
        case Opcode::kCheckExc:
        case Opcode::kCheckField:
        case Opcode::kCheckNone:
        case Opcode::kCheckVar:
        case Opcode::kDeleteSubscr:
        case Opcode::kDeopt:
        case Opcode::kGuard:
        case Opcode::kGuardIs:
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
    std::unordered_map<const hir::BasicBlock*, TranslatedBlock>& bb_map) {
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
  for (auto pair : env_->operand_to_fix) {
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

} // namespace lir
} // namespace jit

// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/hir/printer.h"

#include "Python.h"

#include "Jit/hir/hir.h"
#include "Jit/jit_rt.h"
#include "Jit/util.h"

#include <fmt/format.h>

#include <algorithm>
#include <sstream>
#include <vector>

namespace jit {
namespace hir {

void HIRPrinter::Indent() {
  indent_level_ += 1;
}

std::ostream& HIRPrinter::Indented(std::ostream& os) {
  os << line_prefix_;
  for (int i = 0; i < indent_level_; i++) {
    os << "  ";
  }
  return os;
}

void HIRPrinter::Dedent() {
  indent_level_ -= 1;
}

void HIRPrinter::Print(std::ostream& os, const Function& func) {
  fmt::print(
      os, "fun {} {{\n", func.fullname.empty() ? "<unknown>" : func.fullname);
  Indent();
  Print(os, func.cfg);
  Dedent();
  os << "}" << std::endl;
}

void HIRPrinter::Print(std::ostream& os, const CFG& cfg) {
  std::vector<BasicBlock*> blocks = cfg.GetRPOTraversal();
  auto last_block = blocks.back();
  for (auto block : blocks) {
    Print(os, *block);
    if (block != last_block) {
      os << std::endl;
    }
  }
}

void HIRPrinter::Print(std::ostream& os, const BasicBlock& block) {
  Indented(os);
  fmt::print(os, "bb {}", block.id);
  auto& in_edges = block.in_edges();
  if (!in_edges.empty()) {
    std::vector<const Edge*> edges(in_edges.begin(), in_edges.end());
    std::sort(edges.begin(), edges.end(), [](auto& e1, auto& e2) {
      return e1->from()->id < e2->from()->id;
    });
    os << " (preds ";
    auto sep = "";
    for (auto edge : edges) {
      fmt::print(os, "{}{}", sep, edge->from()->id);
      sep = ", ";
    }
    os << ")";
  }
  os << " {\n";
  Indent();
  for (auto& instr : block) {
    if (instr.IsSnapshot() && !show_snapshots_) {
      continue;
    }
    Print(os, instr);
    os << std::endl;
  }
  Dedent();
  Indented(os) << "}" << std::endl;
}

static void print_reg_states(
    std::ostream& os,
    const std::vector<RegState>& reg_states) {
  auto rss = reg_states;
  std::sort(rss.begin(), rss.end(), [](RegState& a, RegState& b) {
    return a.reg->id() < b.reg->id();
  });
  os << fmt::format("<{}>", rss.size());
  if (!rss.empty()) {
    os << " ";
  }
  auto sep = "";
  for (auto& reg_state : rss) {
    const char* prefix = "?";
    switch (reg_state.value_kind) {
      case ValueKind::kSigned: {
        prefix = "s";
        break;
      }
      case ValueKind::kUnsigned: {
        prefix = "uns";
        break;
      }
      case ValueKind::kBool: {
        prefix = "bool";
        break;
      }
      case ValueKind::kDouble:
        prefix = "double";
        break;
      case ValueKind::kObject: {
        switch (reg_state.ref_kind) {
          case RefKind::kUncounted: {
            prefix = "unc";
            break;
          }
          case RefKind::kBorrowed: {
            prefix = "b";
            break;
          }
          case RefKind::kOwned: {
            prefix = "o";
            break;
          }
        }
        break;
      }
    }
    os << fmt::format("{}{}:{}", sep, prefix, reg_state.reg->name());
    sep = " ";
  }
}

static PyCodeObject* get_code(const Instr& instr) {
  if (instr.IsLoadGlobalCached()) {
    return static_cast<const LoadGlobalCached&>(instr).code();
  }
  if (auto deopt = dynamic_cast<const DeoptBase*>(&instr)) {
    return deopt->frameState()->code;
  }
  if (instr.IsLoadArg()) {
    // LoadArg does not have a FrameState because there are no snapshots, but
    // there will only ever be LoadArgs in the current (top-level) function,
    // even with an HIR inliner.
    auto block = instr.block();
    if (block == nullptr) {
      return nullptr;
    }
    auto cfg = block->cfg;
    if (cfg == nullptr) {
      return nullptr;
    }
    auto func = cfg->func;
    if (func == nullptr) {
      return nullptr;
    }
    return func->code;
  }
  return nullptr;
}

static std::string format_name_impl(int idx, PyObject* names) {
  auto name = PyUnicode_AsUTF8(PyTuple_GET_ITEM(names, idx));

  std::string ret = fmt::format("{}; \"", idx);
  for (; *name != '\0'; ++name) {
    switch (*name) {
      case '"':
      case '\\':
        ret += '\\';
        ret += *name;
        break;
      case '\n':
        ret += "\\n";
        break;
      default:
        ret += *name;
        break;
    }
  }
  ret += "\"";
  return ret;
}

static std::string format_name(const Instr& instr, int idx) {
  auto code = get_code(instr);
  if (idx < 0 || code == nullptr) {
    return fmt::format("{}", idx);
  }

  return format_name_impl(idx, code->co_names);
}

static std::string format_load_super(const LoadSuperBase& load) {
  auto code = get_code(load);
  if (code == nullptr) {
    return fmt::format("{} {}", load.name_idx(), load.no_args_in_super_call());
  }
  return fmt::format(
      "{}, {}",
      format_name_impl(load.name_idx(), code->co_names),
      load.no_args_in_super_call());
}

static std::string format_varname(const Instr& instr, int idx) {
  auto code = get_code(instr);
  if (idx < 0 || code == nullptr) {
    return fmt::format("{}", idx);
  }

  auto names = JITRT_GetVarnameTuple(code, &idx);
  return format_name_impl(idx, names);
}

static std::string format_immediates(const Instr& instr) {
  switch (instr.opcode()) {
    case Opcode::kAssign:
    case Opcode::kBuildString:
    case Opcode::kCheckExc:
    case Opcode::kCheckNeg:
    case Opcode::kCheckNone:
    case Opcode::kCheckSequenceBounds:
    case Opcode::kCheckTuple:
    case Opcode::kClearError:
    case Opcode::kDecref:
    case Opcode::kDeleteSubscr:
    case Opcode::kDeopt:
    case Opcode::kGetIter:
    case Opcode::kGetTuple:
    case Opcode::kGuard:
    case Opcode::kIncref:
    case Opcode::kInitFunction:
    case Opcode::kInvokeIterNext:
    case Opcode::kIsErrStopAsyncIteration:
    case Opcode::kIsInstance:
    case Opcode::kIsNegativeAndErrOccurred:
    case Opcode::kIsTruthy:
    case Opcode::kListAppend:
    case Opcode::kListExtend:
    case Opcode::kLoadCellItem:
    case Opcode::kLoadCurrentFunc:
    case Opcode::kLoadEvalBreaker:
    case Opcode::kLoadVarObjectSize:
    case Opcode::kMakeCell:
    case Opcode::kMakeFunction:
    case Opcode::kMakeSet:
    case Opcode::kMakeTupleFromList:
    case Opcode::kMergeDictUnpack:
    case Opcode::kMergeSetUnpack:
    case Opcode::kRaise:
    case Opcode::kRepeatList:
    case Opcode::kRepeatTuple:
    case Opcode::kRunPeriodicTasks:
    case Opcode::kSetCurrentAwaiter:
    case Opcode::kSetCellItem:
    case Opcode::kSetDictItem:
    case Opcode::kSetSetItem:
    case Opcode::kSnapshot:
    case Opcode::kStealCellItem:
    case Opcode::kStoreArrayItem:
    case Opcode::kStoreSubscr:
    case Opcode::kWaitHandleLoadCoroOrResult:
    case Opcode::kWaitHandleLoadWaiter:
    case Opcode::kWaitHandleRelease:
    case Opcode::kXDecref:
    case Opcode::kXIncref: {
      return "";
    }
    case Opcode::kLoadArrayItem: {
      const auto& load = static_cast<const LoadArrayItem&>(instr);
      return load.offset() == 0 ? "" : fmt::format("Offset[{}]", load.offset());
    }
    case Opcode::kReturn: {
      const auto& ret = static_cast<const Return&>(instr);
      return ret.type() != TObject ? ret.type().toString() : "";
    }
    case Opcode::kCallEx: {
      const auto& call = static_cast<const CallEx&>(instr);
      return call.isAwaited() ? "awaited" : "";
    }
    case Opcode::kCallExKw: {
      const auto& call = static_cast<const CallExKw&>(instr);
      return call.isAwaited() ? "awaited" : "";
    }
    case Opcode::kBinaryOp: {
      const auto& bin_op = static_cast<const BinaryOp&>(instr);
      return GetBinaryOpName(bin_op.op());
    }
    case Opcode::kUnaryOp: {
      const auto& unary_op = static_cast<const UnaryOp&>(instr);
      return GetUnaryOpName(unary_op.op());
    }
    case Opcode::kBranch: {
      const auto& branch = static_cast<const Branch&>(instr);
      return fmt::format("{}", branch.target()->id);
    }
    case Opcode::kVectorCall:
    case Opcode::kVectorCallStatic:
    case Opcode::kVectorCallKW: {
      const auto& call = static_cast<const VectorCallBase&>(instr);
      return fmt::format(
          "{}{}", call.numArgs(), call.isAwaited() ? ", awaited" : "");
    }
    case Opcode::kCallCFunc: {
      const auto& call = static_cast<const CallCFunc&>(instr);
      return call.funcName();
    }
    case Opcode::kCallMethod: {
      const auto& call = static_cast<const CallMethod&>(instr);
      return fmt::format(
          "{}{}", call.NumOperands(), call.isAwaited() ? ", awaited" : "");
    }
    case Opcode::kCallStatic: {
      const auto& call = static_cast<const CallStatic&>(instr);
      return fmt::format("{}", call.NumOperands());
    }
    case Opcode::kCallStaticRetVoid: {
      const auto& call = static_cast<const CallStatic&>(instr);
      return fmt::format("{}", call.NumOperands());
    }
    case Opcode::kInvokeStaticFunction: {
      const auto& call = static_cast<const InvokeStaticFunction&>(instr);
      return fmt::format(
          "{}.{}, {}, {}",
          PyUnicode_AsUTF8(call.func()->func_module),
          PyUnicode_AsUTF8(call.func()->func_qualname),
          call.NumOperands(),
          call.ret_type());
    }
    case Opcode::kInvokeMethod: {
      const auto& call = static_cast<const InvokeMethod&>(instr);
      return fmt::format(
          "{}{}", call.NumOperands(), call.isAwaited() ? ", awaited" : "");
    }
    case Opcode::kLoadField: {
      const auto& call = static_cast<const LoadField&>(instr);
#ifdef Py_TRACE_REFS
      // Keep these stable from the offset of ob_refcnt, in trace refs
      // we have 2 extra next/prev pointers linking all objects together
      return fmt::format("{}", call.offset() - (sizeof(PyObject*) * 2));
#else
      return fmt::format("{}", call.offset());
#endif
    }
    case Opcode::kStoreField: {
      const auto& call = static_cast<const StoreField&>(instr);
      return fmt::format("{}", call.offset());
    }
    case Opcode::kCast: {
      const auto& cast = static_cast<const Cast&>(instr);
      if (cast.optional()) {
        return fmt::format("Optional[{}]", cast.pytype()->tp_name);
      } else {
        return fmt::format("{}", cast.pytype()->tp_name);
      }
    }
    case Opcode::kTpAlloc: {
      const auto& tp_alloc = static_cast<const TpAlloc&>(instr);
      return fmt::format("{}", tp_alloc.pytype()->tp_name);
    }
    case Opcode::kCompare: {
      const auto& cmp = static_cast<const Compare&>(instr);
      return GetCompareOpName(cmp.op());
    }
    case Opcode::kCompareBool: {
      const auto& cmp = static_cast<const Compare&>(instr);
      return GetCompareOpName(cmp.op());
    }
    case Opcode::kIntConvert: {
      const auto& conv = static_cast<const IntConvert&>(instr);
      return conv.type().toString();
    }
    case Opcode::kPrimitiveUnaryOp: {
      const auto& unary = static_cast<const PrimitiveUnaryOp&>(instr);
      return GetPrimitiveUnaryOpName(unary.op());
    }
    case Opcode::kCondBranch:
    case Opcode::kCondBranchIterNotDone:
    case Opcode::kCondBranchCheckType: {
      const auto& cond = static_cast<const CondBranchBase&>(instr);
      auto targets =
          fmt::format("{}, {}", cond.true_bb()->id, cond.false_bb()->id);
      if (cond.IsCondBranchCheckType()) {
        Type type = static_cast<const CondBranchCheckType&>(cond).type();
        return fmt::format("{}, {}", targets, type);
      }
      return targets;
    }
    case Opcode::kDoubleBinaryOp: {
      const auto& bin_op = static_cast<const DoubleBinaryOp&>(instr);
      return GetBinaryOpName(bin_op.op());
    }
    case Opcode::kLoadArg: {
      const auto& load = static_cast<const LoadArg&>(instr);
      return format_varname(load, load.arg_idx());
    }
    case Opcode::kLoadAttr: {
      const auto& load = static_cast<const LoadAttr&>(instr);
      return format_name(load, load.name_idx());
    }
    case Opcode::kLoadAttrSpecial: {
      const auto& load = static_cast<const LoadAttrSpecial&>(instr);
      _Py_Identifier* id = load.id();
      return fmt::format("\"{}\"", id->string);
    }
    case Opcode::kLoadMethod: {
      const auto& load = static_cast<const LoadMethod&>(instr);
      return format_name(load, load.name_idx());
    }
    case Opcode::kLoadMethodSuper: {
      return format_load_super(static_cast<const LoadSuperBase&>(instr));
    }
    case Opcode::kLoadAttrSuper: {
      return format_load_super(static_cast<const LoadSuperBase&>(instr));
    }
    case Opcode::kLoadConst: {
      const auto& load = static_cast<const LoadConst&>(instr);
      return fmt::format("{}", load.type());
    }
    case Opcode::kLoadFunctionIndirect: {
      const auto& load = static_cast<const LoadFunctionIndirect&>(instr);
      PyObject* func = *load.funcptr();
      const char* name;
      if (PyFunction_Check(func)) {
        name = PyUnicode_AsUTF8(((PyFunctionObject*)func)->func_name);
      } else {
        name = Py_TYPE(func)->tp_name;
      }
      return fmt::format("{}", name);
    }
    case Opcode::kIntBinaryOp: {
      const auto& bin_op = static_cast<const IntBinaryOp&>(instr);
      return GetBinaryOpName(bin_op.op());
    }
    case Opcode::kPrimitiveCompare: {
      const auto& cmp = static_cast<const PrimitiveCompare&>(instr);
      return GetPrimitiveCompareOpName(cmp.op());
    }
    case Opcode::kPrimitiveBox: {
      const auto& box = static_cast<const PrimitiveBox&>(instr);
      return fmt::format("{}", box.is_signed());
    }
    case Opcode::kPrimitiveUnbox: {
      const auto& unbox = static_cast<const PrimitiveUnbox&>(instr);
      return fmt::format("{}", unbox.type());
    }
    case Opcode::kLoadGlobalCached: {
      const auto& load = static_cast<const LoadGlobalCached&>(instr);
      return format_name(load, load.name_idx());
    }
    case Opcode::kLoadGlobal: {
      const auto& load = static_cast<const LoadGlobal&>(instr);
      return format_name(load, load.name_idx());
    }
    case Opcode::kMakeListTuple: {
      const auto& makelt = static_cast<const MakeListTuple&>(instr);
      return fmt::format(
          "{}, {}", makelt.is_tuple() ? "tuple" : "list", makelt.nvalues());
    }
    case Opcode::kInitListTuple: {
      const auto& initlt = static_cast<const InitListTuple&>(instr);
      return fmt::format(
          "{}, {}", initlt.is_tuple() ? "tuple" : "list", initlt.num_args());
    }
    case Opcode::kLoadTupleItem: {
      const auto& loaditem = static_cast<const LoadTupleItem&>(instr);
      return fmt::format("{}", loaditem.idx());
    }
    case Opcode::kMakeCheckedDict: {
      const auto& makedict = static_cast<const MakeCheckedDict&>(instr);
      return fmt::format("{} {}", makedict.type(), makedict.GetCapacity());
    }
    case Opcode::kMakeCheckedList: {
      const auto& makelist = static_cast<const MakeCheckedList&>(instr);
      return fmt::format("{} {}", makelist.type(), makelist.GetCapacity());
    }
    case Opcode::kMakeDict: {
      const auto& makedict = static_cast<const MakeDict&>(instr);
      return fmt::format("{}", makedict.GetCapacity());
    }
    case Opcode::kPhi: {
      const auto& phi = static_cast<const Phi&>(instr);
      std::stringstream ss;
      bool first = true;
      for (auto& bb : phi.basic_blocks()) {
        if (first) {
          first = false;
        } else {
          ss << ", ";
        }
        ss << bb->id;
      }

      return ss.str();
    }
    case Opcode::kStoreAttr: {
      const auto& store = static_cast<const StoreAttr&>(instr);
      return format_name(store, store.name_idx());
    }
    case Opcode::kInPlaceOp: {
      const auto& inplace_op = static_cast<const InPlaceOp&>(instr);
      return GetInPlaceOpName(inplace_op.op());
    }
    case Opcode::kBuildSlice: {
      const auto& build_slice = static_cast<const BuildSlice&>(instr);
      return fmt::format("{}", build_slice.NumOperands());
    }
    case Opcode::kLoadTypeAttrCacheItem: {
      const auto& i = static_cast<const LoadTypeAttrCacheItem&>(instr);
      return fmt::format("{}, {}", i.cache_id(), i.item_idx());
    }
    case Opcode::kFillTypeAttrCache: {
      const auto& ftac = static_cast<const FillTypeAttrCache&>(instr);
      return fmt::format("{}, {}", ftac.cache_id(), ftac.name_idx());
    }
    case Opcode::kSetFunctionAttr: {
      const auto& set_fn_attr = static_cast<const SetFunctionAttr&>(instr);
      return fmt::format("{}", functionFieldName(set_fn_attr.field()));
    }
    case Opcode::kCheckField: {
      const auto& cf = static_cast<const CheckField&>(instr);
      return fmt::format("{}", cf.field_idx());
    }
    case Opcode::kCheckVar: {
      const auto& cv = static_cast<const CheckVar&>(instr);
      return format_varname(cv, cv.name_idx());
    }
    case Opcode::kGuardIs: {
      const auto& gs = static_cast<const GuardIs&>(instr);
      return fmt::format("{}", getStablePointer(gs.target()));
    }
    case Opcode::kRaiseAwaitableError: {
      const auto& ra = static_cast<const RaiseAwaitableError&>(instr);
      if (ra.with_opcode() == BEFORE_ASYNC_WITH) {
        return "BEFORE_ASYNC_WITH";
      }
      if (ra.with_opcode() == WITH_CLEANUP_START) {
        return "WITH_CLEANUP_START";
      }
      return fmt::format("invalid:{}", ra.with_opcode());
    }
    case Opcode::kRaiseStatic: {
      const auto& pyerr = static_cast<const RaiseStatic&>(instr);
      std::ostringstream os;
      print_reg_states(os, pyerr.live_regs());
      return fmt::format(
          "{}, \"{}\", <{}>",
          PyExceptionClass_Name(pyerr.excType()),
          pyerr.fmt(),
          os.str());
    }
    case Opcode::kInitialYield:
    case Opcode::kYieldValue:
    case Opcode::kYieldFrom: {
      std::ostringstream os;
      auto sep = "";
      for (auto reg : dynamic_cast<const YieldBase*>(&instr)->liveOwnedRegs()) {
        os << fmt::format("{}o:{}", sep, reg->name());
        sep = ", ";
      }
      for (auto reg :
           dynamic_cast<const YieldBase*>(&instr)->liveUnownedRegs()) {
        os << fmt::format("{}u:{}", sep, reg->name());
        sep = ", ";
      }
      return os.str();
    }
    case Opcode::kImportFrom: {
      const auto& import_from = static_cast<const ImportFrom&>(instr);
      return format_name(import_from, import_from.nameIdx());
    }
    case Opcode::kImportName: {
      const auto& import_name = static_cast<const ImportName&>(instr);
      return format_name(import_name, import_name.name_idx());
    }
    case Opcode::kRefineType: {
      const auto& rt = static_cast<const RefineType&>(instr);
      return rt.type().toString();
    }
    case Opcode::kFormatValue: {
      int conversion = static_cast<const FormatValue&>(instr).conversion();
      switch (conversion) {
        case FVC_NONE:
          return "None";
        case FVC_STR:
          return "Str";
        case FVC_REPR:
          return "Repr";
        case FVC_ASCII:
          return "ASCII";
      }
      JIT_CHECK(false, "Unknown conversion type.");
    }
    case Opcode::kUnpackExToTuple: {
      const auto& i = static_cast<const UnpackExToTuple&>(instr);
      return fmt::format("{}, {}", i.before(), i.after());
    }
  }
  JIT_CHECK(false, "invalid opcode %d", static_cast<int>(instr.opcode()));
}

void HIRPrinter::Print(std::ostream& os, const Instr& instr) {
  Indented(os);
  if (Register* dst = instr.GetOutput()) {
    os << dst->name();
    if (dst->type() != TTop) {
      os << ":" << dst->type();
    }
    os << " = ";
  }
  os << instr.opname();

  auto immed = format_immediates(instr);
  if (!immed.empty()) {
    os << "<" << immed << ">";
  }
  for (size_t i = 0, n = instr.NumOperands(); i < n; ++i) {
    auto op = instr.GetOperand(i);
    if (op != nullptr) {
      os << " " << op->name();
    } else {
      os << " nullptr";
    }
  }

  auto fs = get_frame_state(instr);
  auto db = dynamic_cast<const DeoptBase*>(&instr);
  if (db != nullptr && db->live_regs().size() > 0) {
    os << " {" << std::endl;
    Indent();
    if (!db->descr().empty()) {
      Indented(os) << fmt::format("Descr '{}'\n", db->descr());
    }
    if (Register* guilty_reg = db->guiltyReg()) {
      Indented(os) << fmt::format("GuiltyReg {}\n", *guilty_reg);
    }
    if (db->live_regs().size() > 0) {
      Indented(os) << "LiveValues";
      print_reg_states(os, db->live_regs());
      os << std::endl;
    }
    if (fs != nullptr) {
      Print(os, *fs);
    }
    Dedent();
    Indented(os) << "}";
  } else if (fs != nullptr) {
    os << " {" << std::endl;
    Indent();
    Print(os, *fs);
    Dedent();
    Indented(os) << "}";
  }
}

void HIRPrinter::Print(std::ostream& os, const FrameState& state) {
  Indented(os) << "NextInstrOffset " << state.next_instr_offset << std::endl;

  auto nlocals = state.locals.size();
  if (nlocals > 0) {
    Indented(os) << "Locals<" << nlocals << ">";
    for (auto reg : state.locals) {
      if (reg == nullptr) {
        os << " <null>";
      } else {
        os << " " << reg->name();
      }
    }
    os << std::endl;
  }

  auto ncells = state.cells.size();
  if (ncells > 0) {
    Indented(os) << "Cells<" << ncells << ">";
    for (auto reg : state.cells) {
      if (reg == nullptr) {
        os << " <null>";
      } else {
        os << " " << reg->name();
      }
    }
    os << std::endl;
  }

  auto opstack_size = state.stack.size();
  if (opstack_size > 0) {
    Indented(os) << "Stack<" << opstack_size << ">";
    for (std::size_t i = 0; i < opstack_size; i++) {
      os << " " << state.stack.at(i)->name();
    }
    os << std::endl;
  }

  auto& bs = state.block_stack;
  if (bs.size() > 0) {
    Indented(os) << "BlockStack {" << std::endl;
    Indent();
    for (std::size_t i = 0; i < bs.size(); i++) {
      auto& entry = bs.at(i);
      Indented(os) << fmt::format(
          "Opcode {} HandlerOff {} StackLevel {}\n",
          entry.opcode,
          entry.handler_off,
          entry.stack_level);
    }
    Dedent();
    Indented(os) << "}" << std::endl;
  }
}

} // namespace hir
} // namespace jit

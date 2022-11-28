// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/hir/parser.h"

#include "classloader.h"
#include "pycore_tuple.h"

#include "Jit/hir/hir.h"
#include "Jit/log.h"
#include "Jit/ref.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace jit::hir {

#define NEW_INSTR(type, ...)              \
  auto instr = type::create(__VA_ARGS__); \
  instruction = instr;

void HIRParser::expect(std::string_view expected) {
  std::string_view actual = GetNextToken();
  if (expected != actual) {
    JIT_LOG("Expected \"%s\", but got \"%s\"", expected, actual);
    std::abort();
  }
}

Register* HIRParser::allocateRegister(std::string_view name) {
  JIT_CHECK(
      name[0] == 'v', "invalid register name (must be v[0-9]+): %s", name);
  auto opt_id = parseInt<int>(name.substr(1));
  JIT_CHECK(
      opt_id.has_value(), "Cannot parse register '%s' into an integer", name);
  auto id = *opt_id;

  auto reg = env_->getRegister(id);
  if (reg == nullptr) {
    reg = env_->addRegister(std::make_unique<Register>(id));
  }

  max_reg_id_ = std::max(max_reg_id_, id);
  return reg;
}

Register* HIRParser::ParseRegister() {
  std::string_view name = GetNextToken();
  return allocateRegister(name);
}

HIRParser::ListOrTuple HIRParser::parseListOrTuple() {
  std::string_view kind = GetNextToken();
  if (kind == "list") {
    return ListOrTuple::List;
  }
  if (kind == "tuple") {
    return ListOrTuple::Tuple;
  }
  JIT_CHECK(false, "Invalid kind %s, expected list or tuple", kind);
}

Instr*
HIRParser::parseInstr(std::string_view opcode, Register* dst, int bb_index) {
  Instr* instruction = nullptr;
  if (opcode == "Branch") {
    NEW_INSTR(Branch, nullptr);
    expect("<");
    branches_.emplace(instr, GetNextInteger());
    expect(">");
  } else if (
      opcode == "VectorCall" || opcode == "VectorCallStatic" ||
      opcode == "VectorCallKW") {
    expect("<");
    int num_args = GetNextInteger();
    bool is_awaited = false;
    if (peekNextToken() == ",") {
      expect(",");
      expect("awaited");
      is_awaited = true;
    }
    expect(">");
    auto func = ParseRegister();
    std::vector<Register*> args(num_args);
    std::generate(
        args.begin(),
        args.end(),
        std::bind(std::mem_fun(&HIRParser::ParseRegister), this));

    if (opcode == "VectorCall") {
      instruction = newInstr<VectorCall>(num_args + 1, dst, is_awaited);
    } else if (opcode == "VectorCallStatic") {
      instruction = newInstr<VectorCallStatic>(num_args + 1, dst, is_awaited);
    } else if (opcode == "VectorCallKW") {
      instruction = newInstr<VectorCallKW>(num_args + 1, dst, is_awaited);
    } else {
      JIT_CHECK(false, "Unhandled opcode {}", opcode);
    }

    instruction->SetOperand(0, func);
    for (int i = 0; i < num_args; i++) {
      instruction->SetOperand(i + 1, args[i]);
    }
  } else if (opcode == "FormatValue") {
    expect("<");
    auto tok = GetNextToken();
    auto conversion = [&] {
      if (tok == "None") {
        return FVC_NONE;
      } else if (tok == "Str") {
        return FVC_STR;
      } else if (tok == "Repr") {
        return FVC_REPR;
      } else if (tok == "ASCII") {
        return FVC_ASCII;
      }
      JIT_CHECK(false, "Bad FormatValue conversion type: %s", tok);
    }();
    expect(">");
    Register* fmt_spec = ParseRegister();
    Register* val = ParseRegister();
    instruction = newInstr<FormatValue>(dst, fmt_spec, val, conversion);
  } else if (opcode == "CallEx") {
    bool is_awaited = false;
    if (peekNextToken() == "<") {
      expect("<");
      expect("awaited");
      expect(">");
      is_awaited = true;
    }
    Register* func = ParseRegister();
    Register* pargs = ParseRegister();
    instruction = newInstr<CallEx>(dst, func, pargs, is_awaited);
  } else if (opcode == "CallExKw") {
    bool is_awaited = false;
    if (peekNextToken() == "<") {
      expect("<");
      expect("awaited");
      expect(">");
      is_awaited = true;
    }
    Register* func = ParseRegister();
    Register* pargs = ParseRegister();
    Register* kwargs = ParseRegister();
    instruction = newInstr<CallExKw>(dst, func, pargs, kwargs, is_awaited);
  } else if (opcode == "ImportFrom") {
    expect("<");
    int name_idx = GetNextInteger();
    expect(">");
    Register* module = ParseRegister();
    instruction = newInstr<ImportFrom>(dst, module, name_idx);
  } else if (opcode == "ImportName") {
    expect("<");
    int name_idx = GetNextInteger();
    expect(">");
    Register* fromlist = ParseRegister();
    Register* level = ParseRegister();
    instruction = newInstr<ImportName>(dst, name_idx, fromlist, level);
  } else if (opcode == "InitListTuple") {
    expect("<");
    auto kind = parseListOrTuple();
    expect(",");
    int num_args = GetNextInteger();
    expect(">");

    auto target = ParseRegister();
    std::vector<Register*> args(num_args);
    std::generate(
        args.begin(),
        args.end(),
        std::bind(std::mem_fun(&HIRParser::ParseRegister), this));

    NEW_INSTR(InitListTuple, num_args + 1, kind == ListOrTuple::Tuple);
    instr->SetOperand(0, target);
    for (int i = 0; i < num_args; i++) {
      instr->SetOperand(i + 1, args[i]);
    }
  } else if (opcode == "MakeListTuple") {
    expect("<");
    auto kind = parseListOrTuple();
    expect(",");
    int nvalues = GetNextInteger();
    expect(">");

    instruction =
        newInstr<MakeListTuple>(kind == ListOrTuple::Tuple, dst, nvalues);
  } else if (opcode == "MakeSet") {
    NEW_INSTR(MakeSet, dst);
  } else if (opcode == "SetSetItem") {
    auto receiver = ParseRegister();
    auto item = ParseRegister();
    NEW_INSTR(SetSetItem, dst, receiver, item);
  } else if (opcode == "SetUpdate") {
    auto receiver = ParseRegister();
    auto item = ParseRegister();
    NEW_INSTR(SetUpdate, dst, receiver, item);
  } else if (opcode == "LoadArg") {
    expect("<");
    int idx = GetNextNameIdx();
    Type ty = TObject;
    if (peekNextToken() == ",") {
      expect(",");
      ty = Type::parse(env_, GetNextToken());
    }
    expect(">");
    NEW_INSTR(LoadArg, dst, idx, ty);
  } else if (opcode == "LoadMethod") {
    expect("<");
    int idx = GetNextNameIdx();
    expect(">");
    auto receiver = ParseRegister();
    instruction = newInstr<LoadMethod>(dst, receiver, idx);
  } else if (opcode == "LoadTupleItem") {
    expect("<");
    int idx = GetNextNameIdx();
    expect(">");
    auto receiver = ParseRegister();
    NEW_INSTR(LoadTupleItem, dst, receiver, idx);
  } else if (opcode == "CallMethod") {
    expect("<");
    int num_args = GetNextInteger();
    bool is_awaited = false;
    if (peekNextToken() == ",") {
      expect(",");
      expect("awaited");
      is_awaited = true;
    }
    expect(">");
    std::vector<Register*> args(num_args);
    std::generate(
        args.begin(),
        args.end(),
        std::bind(std::mem_fun(&HIRParser::ParseRegister), this));
    instruction = newInstr<CallMethod>(args.size(), dst, is_awaited);
    for (std::size_t i = 0; i < args.size(); i++) {
      instruction->SetOperand(i, args[i]);
    }
  } else if (opcode == "CondBranch") {
    expect("<");
    auto true_bb = GetNextInteger();
    expect(",");
    auto false_bb = GetNextInteger();
    expect(">");
    auto var = ParseRegister();
    NEW_INSTR(CondBranch, var, nullptr, nullptr);
    cond_branches_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(instr),
        std::forward_as_tuple(true_bb, false_bb));
  } else if (opcode == "CondBranchCheckType") {
    expect("<");
    auto true_bb = GetNextInteger();
    expect(",");
    auto false_bb = GetNextInteger();
    expect(",");
    Type ty = Type::parse(env_, GetNextToken());
    expect(">");
    auto var = ParseRegister();
    NEW_INSTR(CondBranchCheckType, var, ty, nullptr, nullptr);
    cond_branches_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(instr),
        std::forward_as_tuple(true_bb, false_bb));
  } else if (opcode == "Decref") {
    auto var = ParseRegister();
    NEW_INSTR(Decref, var);
  } else if (opcode == "Incref") {
    auto var = ParseRegister();
    NEW_INSTR(Incref, var);
  } else if (opcode == "LoadAttr") {
    expect("<");
    int idx = GetNextNameIdx();
    expect(">");
    auto receiver = ParseRegister();
    instruction = newInstr<LoadAttr>(dst, receiver, idx);
  } else if (opcode == "LoadConst") {
    expect("<");
    Type ty = Type::parse(env_, GetNextToken());
    expect(">");
    NEW_INSTR(LoadConst, dst, ty);
  } else if (opcode == "LoadGlobal") {
    expect("<");
    int name_idx = GetNextNameIdx();
    expect(">");
    instruction = newInstr<LoadGlobal>(dst, name_idx);
  } else if (opcode == "LoadGlobalCached") {
    expect("<");
    int name_idx = GetNextNameIdx();
    expect(">");
    instruction = LoadGlobalCached::create(
        dst,
        /*code=*/nullptr,
        /*builtins=*/nullptr,
        /*globals=*/nullptr,
        name_idx);
  } else if (opcode == "StoreAttr") {
    expect("<");
    int idx = GetNextNameIdx();
    expect(">");
    auto receiver = ParseRegister();
    auto value = ParseRegister();
    instruction = newInstr<StoreAttr>(dst, receiver, value, idx);
  } else if (opcode == "GetLength") {
    auto container = ParseRegister();
    NEW_INSTR(GetLength, dst, container, FrameState{});
  } else if (opcode == "DeleteSubscr") {
    auto container = ParseRegister();
    auto sub = ParseRegister();
    newInstr<DeleteSubscr>(container, sub);
  } else if (opcode == "DictSubscr") {
    auto dict = ParseRegister();
    auto key = ParseRegister();
    NEW_INSTR(DictSubscr, dst, dict, key, FrameState{});
  } else if (opcode == "StoreSubscr") {
    auto receiver = ParseRegister();
    auto index = ParseRegister();
    auto value = ParseRegister();
    NEW_INSTR(StoreSubscr, dst, receiver, index, value);
  } else if (opcode == "Assign") {
    auto src = ParseRegister();
    NEW_INSTR(Assign, dst, src);
  } else if (opcode == "BinaryOp") {
    expect("<");
    BinaryOpKind op = ParseBinaryOpName(GetNextToken());
    uint8_t readonly_flags = 0;
    if (peekNextToken() == ",") {
      expect(",");
      readonly_flags = GetNextInteger();
    }
    expect(">");
    auto left = ParseRegister();
    auto right = ParseRegister();
    instruction = newInstr<BinaryOp>(dst, op, readonly_flags, left, right);
  } else if (opcode == "LongBinaryOp") {
    expect("<");
    BinaryOpKind op = ParseBinaryOpName(GetNextToken());
    expect(">");
    auto left = ParseRegister();
    auto right = ParseRegister();
    instruction = newInstr<LongBinaryOp>(dst, op, left, right);
  } else if (opcode == "IntBinaryOp") {
    expect("<");
    BinaryOpKind op = ParseBinaryOpName(GetNextToken());
    expect(">");
    auto left = ParseRegister();
    auto right = ParseRegister();
    NEW_INSTR(IntBinaryOp, dst, op, left, right);
  } else if (opcode == "Compare") {
    expect("<");
    CompareOp op = ParseCompareOpName(GetNextToken());
    uint8_t readonly_flags = 0;
    if (peekNextToken() == ",") {
      expect(",");
      readonly_flags = GetNextInteger();
    }
    expect(">");
    auto left = ParseRegister();
    auto right = ParseRegister();
    instruction = newInstr<Compare>(dst, op, readonly_flags, left, right);
  } else if (opcode == "LongCompare") {
    expect("<");
    CompareOp op = ParseCompareOpName(GetNextToken());
    expect(">");
    auto left = ParseRegister();
    auto right = ParseRegister();
    NEW_INSTR(LongCompare, dst, op, left, right);
  } else if (opcode == "UnicodeCompare") {
    expect("<");
    CompareOp op = ParseCompareOpName(GetNextToken());
    expect(">");
    auto left = ParseRegister();
    auto right = ParseRegister();
    NEW_INSTR(UnicodeCompare, dst, op, left, right);
  } else if (opcode == "UnicodeConcat") {
    auto left = ParseRegister();
    auto right = ParseRegister();
    NEW_INSTR(UnicodeConcat, dst, left, right, FrameState{});
  } else if (opcode == "UnicodeRepeat") {
    auto left = ParseRegister();
    auto right = ParseRegister();
    NEW_INSTR(UnicodeRepeat, dst, left, right, FrameState{});
  } else if (opcode == "IntConvert") {
    expect("<");
    Type type = Type::parse(env_, GetNextToken());
    expect(">");
    auto src = ParseRegister();
    NEW_INSTR(IntConvert, dst, src, type);
  } else if (opcode == "PrimitiveCompare") {
    expect("<");
    PrimitiveCompareOp op = ParsePrimitiveCompareOpName(GetNextToken());
    expect(">");
    auto left = ParseRegister();
    auto right = ParseRegister();
    NEW_INSTR(PrimitiveCompare, dst, op, left, right);
  } else if (opcode == "PrimitiveUnaryOp") {
    expect("<");
    PrimitiveUnaryOpKind op = ParsePrimitiveUnaryOpName(GetNextToken());
    expect(">");
    auto operand = ParseRegister();
    NEW_INSTR(PrimitiveUnaryOp, dst, op, operand);
  } else if (opcode == "PrimitiveUnbox") {
    expect("<");
    Type type = Type::parse(env_, GetNextToken());
    expect(">");
    auto operand = ParseRegister();
    NEW_INSTR(PrimitiveUnbox, dst, operand, type);
  } else if (opcode == "PrimitiveBoxBool") {
    auto operand = ParseRegister();
    NEW_INSTR(PrimitiveBoxBool, dst, operand);
  } else if (opcode == "PrimitiveBox") {
    expect("<");
    Type type = Type::parse(env_, GetNextToken());
    expect(">");
    auto operand = ParseRegister();
    instruction = newInstr<PrimitiveBox>(dst, operand, type);
  } else if (opcode == "InPlaceOp") {
    expect("<");
    InPlaceOpKind op = ParseInPlaceOpName(GetNextToken());
    expect(">");
    auto left = ParseRegister();
    auto right = ParseRegister();
    instruction = newInstr<InPlaceOp>(dst, op, left, right);
  } else if (opcode == "UnaryOp") {
    expect("<");
    UnaryOpKind op = ParseUnaryOpName(GetNextToken());
    uint8_t readonly_flags = 0;
    if (peekNextToken() == ",") {
      expect(",");
      readonly_flags = GetNextInteger();
    }
    expect(">");
    auto operand = ParseRegister();
    instruction = newInstr<UnaryOp>(dst, op, readonly_flags, operand);
  } else if (opcode == "RaiseAwaitableError") {
    expect("<");
    int prev_opcode = GetNextInteger();
    expect(",");
    int opcode = GetNextInteger();
    expect(">");
    auto type_reg = ParseRegister();
    NEW_INSTR(RaiseAwaitableError, type_reg, prev_opcode, opcode, FrameState{});
  } else if (opcode == "Return") {
    Type type = TObject;
    if (peekNextToken() == "<") {
      GetNextToken();
      type = Type::parse(env_, GetNextToken());
      expect(">");
    }
    auto var = ParseRegister();
    NEW_INSTR(Return, var, type);
  } else if (opcode == "YieldValue") {
    Register* value = ParseRegister();
    instruction = newInstr<YieldValue>(dst, value);
  } else if (opcode == "InitialYield") {
    instruction = newInstr<InitialYield>(dst);
  } else if (opcode == "GetIter") {
    uint8_t readonly_flags = 0;
    if (peekNextToken() == "<") {
      GetNextToken();
      readonly_flags = GetNextInteger();
      expect(">");
    }
    auto iterable = ParseRegister();
    instruction = newInstr<GetIter>(dst, iterable, readonly_flags);
  } else if (opcode == "GetLoadMethodInstance") {
    expect("<");
    int num_args = GetNextInteger();
    expect(">");

    std::vector<Register*> args(num_args);
    std::generate(
        args.begin(),
        args.end(),
        std::bind(std::mem_fun(&HIRParser::ParseRegister), this));

    NEW_INSTR(GetLoadMethodInstance, num_args, dst, args);
  } else if (opcode == "LoadTypeAttrCacheItem") {
    expect("<");
    int cache_id = GetNextInteger();
    int item_idx = GetNextInteger();
    expect(">");
    NEW_INSTR(LoadTypeAttrCacheItem, dst, cache_id, item_idx);
  } else if (opcode == "FillTypeAttrCache") {
    expect("<");
    int cache_id = GetNextInteger();
    int name_idx = GetNextInteger();
    expect(">");
    auto receiver = ParseRegister();
    instruction =
        newInstr<FillTypeAttrCache>(dst, receiver, name_idx, cache_id);
  } else if (opcode == "LoadArrayItem") {
    auto ob_item = ParseRegister();
    auto idx = ParseRegister();
    auto array_unused = ParseRegister();
    NEW_INSTR(LoadArrayItem, dst, ob_item, idx, array_unused, 0, TObject);
  } else if (opcode == "Phi") {
    expect("<");
    PhiInfo info{dst};
    while (true) {
      info.inputs.emplace_back(PhiInput{GetNextInteger(), nullptr});
      if (peekNextToken() == ">") {
        GetNextToken();
        break;
      }
      expect(",");
    }
    for (auto& input : info.inputs) {
      input.value = ParseRegister();
    }
    phis_[bb_index].emplace_back(std::move(info));
  } else if (opcode == "Guard") {
    auto operand = ParseRegister();
    instruction = newInstr<Guard>(operand);
  } else if (opcode == "GuardType") {
    expect("<");
    Type ty = Type::parse(env_, GetNextToken());
    expect(">");
    auto operand = ParseRegister();
    instruction = newInstr<GuardType>(dst, ty, operand);
  } else if (opcode == "GuardIs") {
    expect("<");
    // Since we print raw pointer values for GuardIs, we should parse values
    // as pointers as well. However, since pointers to memory aren't stable,
    // we cannot currently turn them into meaningful values, and since we can't
    // execute parsed HIR code yet, we only support Py_None as the target object
    // for now.
    expect("Py_None");
    expect(">");
    auto operand = ParseRegister();
    NEW_INSTR(GuardIs, dst, Py_None, operand);
  } else if (opcode == "IsTruthy") {
    auto src = ParseRegister();
    instruction = newInstr<IsTruthy>(dst, src);
  } else if (opcode == "UseType") {
    expect("<");
    Type ty = Type::parse(env_, GetNextToken());
    expect(">");
    auto operand = ParseRegister();
    NEW_INSTR(UseType, operand, ty);
  } else if (opcode == "HintType") {
    ProfiledTypes types;
    expect("<");
    int num_args = GetNextInteger();
    expect(",");
    while (true) {
      std::vector<Type> single_profile;
      expect("<");
      while (true) {
        Type ty = Type::parse(env_, GetNextToken());
        single_profile.emplace_back(ty);
        if (peekNextToken() == ">") {
          GetNextToken();
          break;
        }
        expect(",");
      }
      types.emplace_back(single_profile);
      if (peekNextToken() == ">") {
        GetNextToken();
        break;
      }
      expect(",");
    }
    std::vector<Register*> args(num_args);
    std::generate(
        args.begin(),
        args.end(),
        std::bind(std::mem_fun(&HIRParser::ParseRegister), this));
    NEW_INSTR(HintType, num_args, types, args);
  } else if (opcode == "RefineType") {
    expect("<");
    Type ty = Type::parse(env_, GetNextToken());
    expect(">");
    auto operand = ParseRegister();
    NEW_INSTR(RefineType, dst, ty, operand);
  } else if (opcode == "CheckExc") {
    auto operand = ParseRegister();
    instruction = newInstr<CheckExc>(dst, operand);
  } else if (opcode == "CheckVar") {
    expect("<");
    BorrowedRef<> name = GetNextUnicode();
    expect(">");
    auto operand = ParseRegister();
    instruction = newInstr<CheckVar>(dst, operand, name);
  } else if (opcode == "Snapshot") {
    auto snapshot = Snapshot::create();
    if (peekNextToken() == "{") {
      snapshot->setFrameState(parseFrameState());
    }
    instruction = snapshot;
  } else if (opcode == "Deopt") {
    instruction = newInstr<Deopt>();
  } else if (opcode == "Unreachable") {
    instruction = Unreachable::create();
  } else if (opcode == "MakeDict") {
    expect("<");
    auto capacity = GetNextInteger();
    expect(">");
    instruction = newInstr<MakeDict>(dst, capacity);
  } else if (opcode == "InvokeStaticFunction") {
    expect("<");
    auto name = GetNextToken();
    auto mod_name =
        Ref<>::steal(PyUnicode_FromStringAndSize(name.data(), name.size()));
    JIT_CHECK(mod_name != nullptr, "failed to allocate mod name");
    auto dot = Ref<>::steal(PyUnicode_FromString("."));
    JIT_CHECK(dot != nullptr, "failed to allocate mod name");

    auto names = Ref<PyListObject>::steal(PyUnicode_Split(mod_name, dot, -1));
    JIT_CHECK(names != nullptr, "unknown func");
    auto type_descr =
        Ref<>::steal(_PyTuple_FromArray(names->ob_item, Py_SIZE(names.get())));
    JIT_CHECK(type_descr != nullptr, "unknown func");
    PyObject* container = nullptr;
    auto func = Ref<PyFunctionObject>::steal(
        _PyClassLoader_ResolveFunction(type_descr, &container));
    JIT_CHECK(func != nullptr, "unknown func");
    Py_XDECREF(container);

    expect(",");
    auto argcount = GetNextInteger();
    expect(",");
    Type ty = Type::parse(env_, GetNextToken());
    expect(">");

    instruction = newInstr<InvokeStaticFunction>(argcount, dst, func, ty);
  } else if (opcode == "LoadCurrentFunc") {
    NEW_INSTR(LoadCurrentFunc, dst);
  } else if (opcode == "RepeatList") {
    Register* list = ParseRegister();
    Register* count = ParseRegister();
    instruction = newInstr<RepeatList>(dst, list, count);
  } else {
    JIT_CHECK(0, "Unknown opcode: %s", opcode);
  }

  return instruction;
}

std::vector<Register*> HIRParser::parseRegisterVector() {
  expect("<");
  int num_items = GetNextInteger();
  expect(">");
  std::vector<Register*> registers;
  for (int i = 0; i < num_items; i++) {
    auto name = GetNextToken();
    if (name == "<null>") {
      registers.emplace_back(nullptr);
    } else {
      registers.emplace_back(allocateRegister(name));
    }
  }
  return registers;
}

std::vector<RegState> HIRParser::parseRegStates() {
  expect("<");
  int num_vals = GetNextInteger();
  expect(">");
  std::vector<RegState> reg_states;
  for (int i = 0; i < num_vals; i++) {
    auto rs = GetNextRegState();
    reg_states.emplace_back(rs);
  }
  return reg_states;
}

FrameState HIRParser::parseFrameState() {
  FrameState fs;
  expect("{");
  auto token = GetNextToken();
  while (token != "}") {
    if (token == "NextInstrOffset") {
      fs.next_instr_offset = BCOffset{GetNextInteger()};
    } else if (token == "Locals") {
      fs.locals = parseRegisterVector();
    } else if (token == "Cells") {
      fs.cells = parseRegisterVector();
    } else if (token == "Stack") {
      for (Register* r : parseRegisterVector()) {
        fs.stack.push(r);
      }
    } else if (token == "BlockStack") {
      expect("{");
      while (peekNextToken() != "}") {
        ExecutionBlock block;
        expect("Opcode");
        block.opcode = GetNextInteger();
        expect("HandlerOff");
        block.handler_off = BCOffset{GetNextInteger()};
        expect("StackLevel");
        block.stack_level = GetNextInteger();
        fs.block_stack.push(block);
      }
      expect("}");
    } else {
      JIT_CHECK(false, "unexpected token in FrameState: %s", token);
    }
    token = GetNextToken();
  }
  return fs;
}

BasicBlock* HIRParser::ParseBasicBlock(CFG& cfg) {
  if (peekNextToken() != "bb") {
    return nullptr;
  }

  expect("bb");
  int id = GetNextInteger();
  auto bb = cfg.AllocateBlock();
  bb->id = id;

  if (peekNextToken() == "(") {
    // Skip over optional "(preds 1, 2, 3)".
    while (GetNextToken() != ")") {
    }
  }
  expect("{");

  while (peekNextToken() != "}") {
    Register* dst = nullptr;
    if (peekNextToken(1) == "=") {
      dst = ParseRegister();
      expect("=");
    }
    std::string_view token = GetNextToken();
    auto* instr = parseInstr(token, dst, id);
    if (instr != nullptr) {
      bb->Append(instr);
    }
  }
  expect("}");

  index_to_bb_.emplace(id, bb);
  return bb;
}

static bool is_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n';
}

static bool is_single_char_token(char c) {
  return c == '=' || c == '<' || c == '>' || c == ',' || c == '{' || c == '}' ||
      c == '(' || c == ')' || c == ';';
}

std::unique_ptr<Function> HIRParser::ParseHIR(const char* hir) {
  tokens_.clear();
  phis_.clear();
  branches_.clear();
  cond_branches_.clear();
  index_to_bb_.clear();

  const char* p = hir;
  while (true) {
    while (is_whitespace(*p)) {
      p++;
    }

    if (*p == '\0') {
      break;
    }

    if (*p == '"') {
      std::string token;
      for (p++; *p != '"'; p++) {
        JIT_CHECK(*p != '\0', "End of input during string literal");
        if (*p != '\\') {
          token += *p;
          continue;
        }
        p++;
        switch (*p) {
          case 'n':
            token += '\n';
            break;
          case '"':
          case '\\':
            token += *p;
            break;
          default:
            JIT_CHECK(false, "Bad escape sequence \\%c", *p);
        }
      }
      p++;
      tokens_.emplace_back(std::move(token));
    }

    if (is_single_char_token(*p)) {
      tokens_.emplace_back(p, 1);
      p++;
      continue;
    }

    auto q = p;
    while (!is_whitespace(*q) && !is_single_char_token(*q) && *q != '\0') {
      q++;
    }

    tokens_.emplace_back(p, q - p);
    p = q;
  }

  token_iter_ = tokens_.begin();

  expect("fun");

  auto hir_func = std::make_unique<Function>();
  env_ = &hir_func->env;
  hir_func->fullname = GetNextToken();

  expect("{");

  while (auto bb = ParseBasicBlock(hir_func->cfg)) {
    if (hir_func->cfg.entry_block == nullptr) {
      hir_func->cfg.entry_block = bb;
    }
  }

  realizePhis();

  for (auto& it : branches_) {
    it.first->set_target(index_to_bb_[it.second]);
  }

  for (auto& it : cond_branches_) {
    it.first->set_true_bb(index_to_bb_[it.second.first]);
    it.first->set_false_bb(index_to_bb_[it.second.second]);
  }

  expect("}");

  hir_func->env.setNextRegisterId(max_reg_id_ + 1);
  return hir_func;
}

void HIRParser::realizePhis() {
  for (auto& pair : phis_) {
    auto block = index_to_bb_[pair.first];
    auto& front = block->front();

    for (auto& phi : pair.second) {
      std::unordered_map<BasicBlock*, Register*> inputs;
      for (auto& info : phi.inputs) {
        inputs.emplace(index_to_bb_[info.bb], info.value);
      }
      (Phi::create(phi.dst, inputs))->InsertBefore(front);
    }
  }
}

// Parse an integer, followed by an optional ; and string name (which are
// ignored).
int HIRParser::GetNextNameIdx() {
  auto idx = GetNextInteger();
  if (peekNextToken() == ";") {
    // Ignore ; and name.
    GetNextToken();
    GetNextToken();
  }
  return idx;
}

BorrowedRef<> HIRParser::GetNextUnicode() {
  std::string_view str = GetNextToken();
  auto raw_obj = PyUnicode_FromStringAndSize(str.data(), str.size());
  JIT_CHECK(raw_obj != nullptr, "Failed to create string %s", str);
  PyUnicode_InternInPlace(&raw_obj);
  auto obj = Ref<>::steal(raw_obj);
  JIT_CHECK(obj != nullptr, "Failed to intern string %s", str);
  return env_->addReference(std::move(obj));
}

RegState HIRParser::GetNextRegState() {
  auto token = GetNextToken();
  auto end = token.find(':');
  JIT_CHECK(end != std::string::npos, "Invalid reg state: %s", token);
  RegState rs;
  rs.reg = allocateRegister(token.substr(end + 1));
  switch (token[0]) {
    case 'b':
      rs.ref_kind = RefKind::kBorrowed;
      break;
    case 'o':
      rs.ref_kind = RefKind::kOwned;
      break;
    case 'u':
      rs.ref_kind = RefKind::kUncounted;
      break;
    default:
      JIT_CHECK(false, "unknown ref kind: %c", token[0]);
      break;
  }

  return rs;
}

} // namespace jit::hir

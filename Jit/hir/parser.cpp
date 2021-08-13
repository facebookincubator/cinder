// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/hir/parser.h"

#include "classloader.h"
#include "pycore_tupleobject.h"

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

namespace jit {
namespace hir {

#define NEW_INSTR(type, ...)              \
  auto instr = type::create(__VA_ARGS__); \
  instruction = instr;

void HIRParser::expect(const char* expected) {
  const char* actual = GetNextToken();
  if (strcmp(expected, actual) != 0) {
    JIT_LOG("Expected \"%s\", but got \"%s\"", expected, actual);
    std::abort();
  }
}

Register* HIRParser::allocateRegister(const char* name) {
  JIT_CHECK(
      name[0] == 'v', "invalid register name (must be v[0-9]+): %s", name);
  int id = atoi(name + 1);
  auto reg = env_->getRegister(id);
  if (reg == nullptr) {
    reg = env_->addRegister(std::make_unique<Register>(id));
  }

  max_reg_id_ = std::max(max_reg_id_, id);
  return reg;
}

Register* HIRParser::ParseRegister() {
  const char* name = GetNextToken();
  return allocateRegister(name);
}

HIRParser::ListOrTuple HIRParser::parseListOrTuple() {
  const char* kind = GetNextToken();
  if (strcmp(kind, "list") == 0) {
    return ListOrTuple::List;
  }
  if (strcmp(kind, "tuple") == 0) {
    return ListOrTuple::Tuple;
  }
  JIT_CHECK(false, "Invalid kind %s, expected list or tuple", kind);
}

Instr* HIRParser::parseInstr(const char* opcode, Register* dst, int bb_index) {
  Instr* instruction = nullptr;
  if (strcmp(opcode, "Branch") == 0) {
    NEW_INSTR(Branch, nullptr);
    expect("<");
    branches_.emplace(instr, GetNextInteger());
    expect(">");
  } else if (
      strcmp(opcode, "VectorCall") == 0 ||
      strcmp(opcode, "VectorCallStatic") == 0 ||
      strcmp(opcode, "VectorCallKW") == 0) {
    expect("<");
    int num_args = GetNextInteger();
    bool is_awaited = false;
    if (strcmp(peekNextToken(), ",") == 0) {
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

    if (strcmp(opcode, "VectorCall") == 0) {
      instruction = newInstr<VectorCall>(num_args + 1, dst, is_awaited);
    } else if (strcmp(opcode, "VectorCallStatic") == 0) {
      instruction = newInstr<VectorCallStatic>(num_args + 1, dst, is_awaited);
    } else if (strcmp(opcode, "VectorCallKW") == 0) {
      instruction = newInstr<VectorCallKW>(num_args + 1, dst, is_awaited);
    } else {
      JIT_CHECK(false, "Unhandled opcode {}", opcode);
    }

    instruction->SetOperand(0, func);
    for (int i = 0; i < num_args; i++) {
      instruction->SetOperand(i + 1, args[i]);
    }
  } else if (strcmp(opcode, "FormatValue") == 0) {
    expect("<");
    auto tok = GetNextToken();
    auto conversion = [&] {
      if (strcmp(tok, "None") == 0) {
        return FVC_NONE;
      } else if (strcmp(tok, "Str") == 0) {
        return FVC_STR;
      } else if (strcmp(tok, "Repr") == 0) {
        return FVC_REPR;
      } else if (strcmp(tok, "ASCII") == 0) {
        return FVC_ASCII;
      }
      JIT_CHECK(false, "Bad FormatValue conversion type: %s", tok);
    }();
    expect(">");
    Register* fmt_spec = ParseRegister();
    Register* val = ParseRegister();
    instruction = newInstr<FormatValue>(dst, fmt_spec, val, conversion);
  } else if (strcmp(opcode, "CallEx") == 0) {
    bool is_awaited = false;
    if (strcmp(peekNextToken(), "<") == 0) {
      expect("<");
      expect("awaited");
      expect(">");
      is_awaited = true;
    }
    Register* func = ParseRegister();
    Register* pargs = ParseRegister();
    instruction = newInstr<CallEx>(dst, func, pargs, is_awaited);
  } else if (strcmp(opcode, "CallExKw") == 0) {
    bool is_awaited = false;
    if (strcmp(peekNextToken(), "<") == 0) {
      expect("<");
      expect("awaited");
      expect(">");
      is_awaited = true;
    }
    Register* func = ParseRegister();
    Register* pargs = ParseRegister();
    Register* kwargs = ParseRegister();
    instruction = newInstr<CallExKw>(dst, func, pargs, kwargs, is_awaited);
  } else if (strcmp(opcode, "ImportFrom") == 0) {
    expect("<");
    int name_idx = GetNextInteger();
    expect(">");
    Register* module = ParseRegister();
    instruction = newInstr<ImportFrom>(dst, module, name_idx);
  } else if (strcmp(opcode, "ImportName") == 0) {
    expect("<");
    int name_idx = GetNextInteger();
    expect(">");
    Register* fromlist = ParseRegister();
    Register* level = ParseRegister();
    instruction = newInstr<ImportName>(dst, name_idx, fromlist, level);
  } else if (strcmp(opcode, "InitListTuple") == 0) {
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
  } else if (strcmp(opcode, "MakeListTuple") == 0) {
    expect("<");
    auto kind = parseListOrTuple();
    expect(",");
    int nvalues = GetNextInteger();
    expect(">");

    instruction =
        newInstr<MakeListTuple>(kind == ListOrTuple::Tuple, dst, nvalues);
  } else if (strcmp(opcode, "LoadArg") == 0) {
    expect("<");
    int idx = GetNextNameIdx();
    expect(">");
    NEW_INSTR(LoadArg, dst, idx);
  } else if (strcmp(opcode, "_AddLocal") == 0) {
    // Pseudo instruction, allocates a local
    const char* name = GetNextToken();
    allocateRegister(name);
  } else if (strcmp(opcode, "LoadMethod") == 0) {
    expect("<");
    int idx = GetNextNameIdx();
    expect(">");
    auto receiver = ParseRegister();
    instruction = newInstr<LoadMethod>(dst, receiver, idx);
  } else if (strcmp(opcode, "CallMethod") == 0) {
    expect("<");
    int num_args = GetNextInteger();
    bool is_awaited = false;
    if (strcmp(peekNextToken(), ",") == 0) {
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
  } else if (strcmp(opcode, "CondBranch") == 0) {
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
  } else if (strcmp(opcode, "Decref") == 0) {
    auto var = ParseRegister();
    NEW_INSTR(Decref, var);
  } else if (strcmp(opcode, "Incref") == 0) {
    auto var = ParseRegister();
    NEW_INSTR(Incref, var);
  } else if (strcmp(opcode, "LoadAttr") == 0) {
    expect("<");
    int idx = GetNextNameIdx();
    expect(">");
    auto receiver = ParseRegister();
    instruction = newInstr<LoadAttr>(dst, receiver, idx);
  } else if (strcmp(opcode, "LoadConst") == 0) {
    expect("<");
    Type ty = Type::parse(GetNextToken());
    expect(">");
    NEW_INSTR(LoadConst, dst, ty);
  } else if (strcmp(opcode, "LoadGlobal") == 0) {
    expect("<");
    int name_idx = GetNextNameIdx();
    expect(">");
    instruction = newInstr<LoadGlobal>(dst, name_idx);
  } else if (strcmp(opcode, "LoadGlobalCached") == 0) {
    expect("<");
    int name_idx = GetNextNameIdx();
    expect(">");
    instruction = LoadGlobalCached::create(dst, name_idx);
  } else if (strcmp(opcode, "StoreAttr") == 0) {
    expect("<");
    int idx = GetNextNameIdx();
    expect(">");
    auto receiver = ParseRegister();
    auto value = ParseRegister();
    instruction = newInstr<StoreAttr>(dst, receiver, idx, value);
  } else if (strcmp(opcode, "DeleteSubscr") == 0) {
    auto container = ParseRegister();
    auto sub = ParseRegister();
    newInstr<DeleteSubscr>(container, sub);
  } else if (strcmp(opcode, "StoreSubscr") == 0) {
    auto receiver = ParseRegister();
    auto index = ParseRegister();
    auto value = ParseRegister();
    NEW_INSTR(StoreSubscr, dst, receiver, index, value);
  } else if (strcmp(opcode, "Assign") == 0) {
    auto src = ParseRegister();
    NEW_INSTR(Assign, dst, src);
  } else if (strcmp(opcode, "BinaryOp") == 0) {
    expect("<");
    BinaryOpKind op = ParseBinaryOpName(GetNextToken());
    expect(">");
    auto left = ParseRegister();
    auto right = ParseRegister();
    instruction = newInstr<BinaryOp>(dst, op, left, right);
  } else if (strcmp(opcode, "PrimitiveCompareOp") == 0) {
    expect("<");
    PrimitiveCompareOp op = ParsePrimitiveCompareOpName(GetNextToken());
    expect(">");
    auto left = ParseRegister();
    auto right = ParseRegister();
    NEW_INSTR(PrimitiveCompare, dst, op, left, right);
  } else if (strcmp(opcode, "PrimitiveUnaryOp") == 0) {
    expect("<");
    PrimitiveUnaryOpKind op = ParsePrimitiveUnaryOpName(GetNextToken());
    expect(">");
    auto operand = ParseRegister();
    NEW_INSTR(PrimitiveUnaryOp, dst, op, operand);
  } else if (strcmp(opcode, "InPlaceOp") == 0) {
    expect("<");
    InPlaceOpKind op = ParseInPlaceOpName(GetNextToken());
    expect(">");
    auto left = ParseRegister();
    auto right = ParseRegister();
    instruction = newInstr<InPlaceOp>(dst, op, left, right);
  } else if (strcmp(opcode, "UnaryOp") == 0) {
    expect("<");
    UnaryOpKind op = ParseUnaryOpName(GetNextToken());
    expect(">");
    auto operand = ParseRegister();
    instruction = newInstr<UnaryOp>(dst, op, operand);
  } else if (strcmp(opcode, "RaiseAwaitableError") == 0) {
    expect("<");
    auto tok = GetNextToken();
    auto opcode = [&] {
      if (strcmp(tok, "BEFORE_ASYNC_WITH") == 0) {
        return BEFORE_ASYNC_WITH;
      }
      if (strcmp(tok, "WITH_CLEANUP_START") != 0) {
        return WITH_CLEANUP_START;
      }
      JIT_CHECK(false, "Bad RaiseAwaitableError opcode: %s", tok);
    }();
    expect(">");
    auto type_reg = ParseRegister();
    NEW_INSTR(RaiseAwaitableError, type_reg, opcode, FrameState{});
  } else if (strcmp(opcode, "Return") == 0) {
    Type type = TObject;
    if (strcmp(peekNextToken(), "<") == 0) {
      GetNextToken();
      type = Type::parse(GetNextToken());
      expect(">");
    }
    auto var = ParseRegister();
    NEW_INSTR(Return, var);
  } else if (
      strcmp(opcode, "YieldValue") == 0 ||
      strcmp(opcode, "InitialYield") == 0) {
    std::vector<Register*> live_owned_registers;
    if (strcmp(peekNextToken(), "<") == 0) {
      GetNextToken();
      while (true) {
        live_owned_registers.push_back(ParseRegister());
        auto token = peekNextToken();
        if (strcmp(token, ">") == 0) {
          GetNextToken();
          break;
        }
        expect(",");
      }
    }
    if (strcmp(opcode, "InitialYield") == 0) {
      instruction = InitialYield::create(dst);
    } else {
      auto reg = ParseRegister();
      instruction = YieldValue::create(dst, reg);
    }
    YieldBase* yield_base = dynamic_cast<YieldBase*>(instruction);
    JIT_CHECK(yield_base, "Not a yield opcode");
    for (auto reg : live_owned_registers) {
      yield_base->emplaceLiveOwnedReg(reg);
    }
  } else if (strcmp(opcode, "GetIter") == 0) {
    auto iterable = ParseRegister();
    instruction = newInstr<GetIter>(dst, iterable);
  } else if (strcmp(opcode, "LoadTypeAttrCacheItem") == 0) {
    expect("<");
    int cache_id = GetNextInteger();
    int item_idx = GetNextInteger();
    expect(">");
    NEW_INSTR(LoadTypeAttrCacheItem, dst, cache_id, item_idx);
  } else if (strcmp(opcode, "FillTypeAttrCache") == 0) {
    expect("<");
    int cache_id = GetNextInteger();
    int name_idx = GetNextInteger();
    expect(">");
    auto receiver = ParseRegister();
    instruction =
        newInstr<FillTypeAttrCache>(dst, receiver, name_idx, cache_id);
  } else if (strcmp(opcode, "Phi") == 0) {
    expect("<");
    PhiInfo info{dst};
    while (true) {
      info.inputs.emplace_back(PhiInput{GetNextInteger(), nullptr});
      auto token = peekNextToken();
      if (strcmp(token, ">") == 0) {
        GetNextToken();
        break;
      }
      expect(",");
    }
    for (auto& input : info.inputs) {
      input.value = ParseRegister();
    }
    phis_[bb_index].emplace_back(std::move(info));
  } else if (strcmp(opcode, "Guard") == 0) {
    auto operand = ParseRegister();
    instruction = newInstr<Guard>(operand);
  } else if (strcmp(opcode, "CheckExc") == 0) {
    auto operand = ParseRegister();
    instruction = newInstr<CheckExc>(dst, operand);
  } else if (strcmp(opcode, "CheckVar") == 0) {
    expect("<");
    int name_idx = GetNextNameIdx();
    expect(">");
    auto operand = ParseRegister();
    instruction = newInstr<CheckVar>(dst, operand, name_idx);
  } else if (strcmp(opcode, "Snapshot") == 0) {
    auto snapshot = Snapshot::create();
    if (strcmp(peekNextToken(), "{") == 0) {
      expect("{");
      snapshot->setFrameState(parseFrameState());
    }
    instruction = snapshot;
  } else if (strcmp(opcode, "MakeDict") == 0) {
    expect("<");
    auto capacity = GetNextInteger();
    expect(">");
    instruction = newInstr<MakeDict>(dst, capacity);
  } else if (strcmp(opcode, "InvokeStaticFunction") == 0) {
    expect("<");
    auto name = GetNextToken();
    auto mod_name = Ref<>::steal(PyUnicode_FromString(name));
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
    Type ty = Type::parse(GetNextToken());
    expect(">");

    instruction = newInstr<InvokeStaticFunction>(argcount, dst, func, ty);
  } else if (strcmp(opcode, "IsErrStopAsyncIteration") == 0) {
    NEW_INSTR(IsErrStopAsyncIteration, dst);
  } else if (strcmp(opcode, "ClearError") == 0) {
    NEW_INSTR(ClearError);
  } else if (strcmp(opcode, "LoadCurrentFunc") == 0) {
    NEW_INSTR(LoadCurrentFunc, dst);
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
    if (strcmp(name, "<null>") == 0) {
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
  auto token = GetNextToken();
  while (strcmp(token, "}") != 0) {
    if (strcmp(token, "NextInstrOffset") == 0) {
      fs.next_instr_offset = GetNextInteger();
    } else if (strcmp(token, "Locals") == 0) {
      fs.locals = parseRegisterVector();
    } else if (strcmp(token, "Cells") == 0) {
      fs.cells = parseRegisterVector();
    } else if (strcmp(token, "Stack") == 0) {
      expect("<");
      int num_items = GetNextInteger();
      expect(">");
      for (int i = 0; i < num_items; i++) {
        auto reg = ParseRegister();
        fs.stack.push(reg);
      }
    } else if (strcmp(token, "BlockStack") == 0) {
      expect("{");
      while (strcmp(peekNextToken(), "}") != 0) {
        ExecutionBlock block;
        expect("Opcode");
        block.opcode = GetNextInteger();
        expect("HandlerOff");
        block.handler_off = GetNextInteger();
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
  if (strcmp(peekNextToken(), "bb") != 0) {
    return nullptr;
  }

  expect("bb");
  int id = GetNextInteger();
  auto bb = cfg.AllocateBlock();
  bb->id = id;

  if (strcmp(peekNextToken(), "(") == 0) {
    // Skip over optional "(preds 1, 2, 3)".
    while (strcmp(GetNextToken(), ")") != 0) {
    }
  }
  expect("{");

  while (strcmp(peekNextToken(), "}") != 0) {
    Register* dst = nullptr;
    if (strcmp(peekNextToken(1), "=") == 0) {
      dst = ParseRegister();
      expect("=");
    }
    const char* token = GetNextToken();
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
        JIT_CHECK(*p != '\0', "End up input during string literal");
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
  if (strcmp(peekNextToken(), ";") == 0) {
    // Ignore ; and name.
    GetNextToken();
    GetNextToken();
  }
  return idx;
}

RegState HIRParser::GetNextRegState() {
  auto token = GetNextToken();
  auto end = strchr(token, ':');
  JIT_CHECK(end != NULL, "invalid reg state: %s", token);
  RegState rs;
  rs.reg = allocateRegister(end + 1);
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

} // namespace hir
} // namespace jit

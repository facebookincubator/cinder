// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/hir/builder.h"

#include <algorithm>
#include <deque>
#include <memory>
#include <set>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Jit/bitvector.h"
#include "Jit/bytecode.h"
#include "Jit/hir/hir.h"
#include "Jit/hir/optimization.h"
#include "Jit/pyjit.h"
#include "Jit/ref.h"
#include "Jit/threaded_compile.h"

#include "Python.h"
#include "ceval.h"
#include "classloader.h"
#include "opcode.h"
#include "structmember.h"

namespace jit {
namespace hir {

using jit::BytecodeInstruction;

Register* TempAllocator::Allocate() {
  Register* reg = env_->AllocateRegister();
  cache_.emplace_back(reg);
  return reg;
}

// Get the i-th temporary
Register* TempAllocator::GetOrAllocate(std::size_t idx) {
  if (idx < cache_.size()) {
    Register* reg = cache_[idx];
    return reg;
  }
  return Allocate();
}

// Return the maximum number of temporaries that were allocated
int TempAllocator::MaxAllocated() const {
  return cache_.size();
}

// TODO(mpage) - Remove this and replace all calls with a call to
// env_->AllocateRegister()
Register* TempAllocator::allocateNextTruthy() {
  return env_->AllocateRegister();
}

// Opcodes that we know how to translate into HIR
const std::unordered_set<int> kSupportedOpcodes = {
    BEFORE_ASYNC_WITH,
    BEGIN_FINALLY,
    BINARY_ADD,
    BINARY_AND,
    BINARY_FLOOR_DIVIDE,
    BINARY_LSHIFT,
    BINARY_MATRIX_MULTIPLY,
    BINARY_MODULO,
    BINARY_MULTIPLY,
    BINARY_OR,
    BINARY_POWER,
    BINARY_RSHIFT,
    BINARY_SUBSCR,
    BINARY_SUBTRACT,
    BINARY_TRUE_DIVIDE,
    BINARY_XOR,
    BUILD_CONST_KEY_MAP,
    BUILD_LIST,
    BUILD_LIST_UNPACK,
    BUILD_MAP,
    BUILD_MAP_UNPACK,
    BUILD_MAP_UNPACK_WITH_CALL,
    BUILD_SET,
    BUILD_SET_UNPACK,
    BUILD_SLICE,
    BUILD_STRING,
    BUILD_TUPLE,
    BUILD_TUPLE_UNPACK,
    BUILD_TUPLE_UNPACK_WITH_CALL,
    CALL_FINALLY,
    CALL_FUNCTION,
    CALL_FUNCTION_EX,
    CALL_FUNCTION_KW,
    CALL_METHOD,
    CAST,
    CHECK_ARGS,
    COMPARE_OP,
    CONVERT_PRIMITIVE,
    DELETE_FAST,
    DELETE_SUBSCR,
    DUP_TOP,
    DUP_TOP_TWO,
    END_ASYNC_FOR,
    END_FINALLY,
    EXTENDED_ARG,
    FAST_LEN,
    FORMAT_VALUE,
    FOR_ITER,
    GET_AITER,
    GET_ANEXT,
    GET_AWAITABLE,
    GET_ITER,
    GET_YIELD_FROM_ITER,
    IMPORT_FROM,
    IMPORT_NAME,
    INPLACE_ADD,
    INPLACE_AND,
    INPLACE_FLOOR_DIVIDE,
    INPLACE_LSHIFT,
    INPLACE_MATRIX_MULTIPLY,
    INPLACE_MODULO,
    INPLACE_MULTIPLY,
    INPLACE_OR,
    INPLACE_POWER,
    INPLACE_RSHIFT,
    INPLACE_SUBTRACT,
    INPLACE_TRUE_DIVIDE,
    INPLACE_XOR,
    PRIMITIVE_BINARY_OP,
    PRIMITIVE_BOX,
    INT_COMPARE_OP,
    PRIMITIVE_LOAD_CONST,
    INT_LOAD_CONST_OLD,
    PRIMITIVE_UNARY_OP,
    PRIMITIVE_UNBOX,
    INVOKE_FUNCTION,
    INVOKE_METHOD,
    JUMP_ABSOLUTE,
    JUMP_FORWARD,
    JUMP_IF_FALSE_OR_POP,
    JUMP_IF_NONZERO_OR_POP,
    JUMP_IF_TRUE_OR_POP,
    JUMP_IF_ZERO_OR_POP,
    LIST_APPEND,
    LOAD_ATTR,
    LOAD_ATTR_SUPER,
    LOAD_CLOSURE,
    LOAD_CONST,
    LOAD_DEREF,
    LOAD_FAST,
    LOAD_FIELD,
    LOAD_GLOBAL,
    LOAD_ITERABLE_ARG,
    LOAD_LOCAL,
    LOAD_METHOD,
    LOAD_METHOD_SUPER,
    MAKE_FUNCTION,
    MAP_ADD,
    NOP,
    POP_BLOCK,
    POP_EXCEPT,
    POP_FINALLY,
    POP_JUMP_IF_FALSE,
    POP_JUMP_IF_NONZERO,
    POP_JUMP_IF_TRUE,
    POP_JUMP_IF_ZERO,
    POP_TOP,
    RAISE_VARARGS,
    REFINE_TYPE,
    RETURN_INT,
    RETURN_VALUE,
    ROT_FOUR,
    ROT_THREE,
    ROT_TWO,
    SEQUENCE_GET,
    SEQUENCE_REPEAT,
    SEQUENCE_SET,
    SETUP_ASYNC_WITH,
    SETUP_FINALLY,
    SETUP_WITH,
    SET_ADD,
    STORE_ATTR,
    STORE_DEREF,
    STORE_FAST,
    STORE_FIELD,
    STORE_LOCAL,
    STORE_SUBSCR,
    UNARY_INVERT,
    UNARY_NEGATIVE,
    UNARY_NOT,
    UNARY_POSITIVE,
    UNPACK_EX,
    UNPACK_SEQUENCE,
    WITH_CLEANUP_FINISH,
    WITH_CLEANUP_START,
    YIELD_FROM,
    YIELD_VALUE,
};

static bool can_translate(PyCodeObject* code) {
  static const std::unordered_set<std::string> kBannedNames{
      "eval", "exec", "locals"};
  PyObject* names = code->co_names;
  std::unordered_set<Py_ssize_t> banned_name_ids;
  auto name_at = [&](Py_ssize_t i) {
    return PyUnicode_AsUTF8(PyTuple_GET_ITEM(names, i));
  };
  for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(names); i++) {
    if (kBannedNames.count(name_at(i))) {
      banned_name_ids.insert(i);
    }
  }
  for (auto& bci : BytecodeInstructionBlock{code}) {
    auto opcode = bci.opcode();
    int oparg = bci.oparg();
    if (!kSupportedOpcodes.count(opcode)) {
      JIT_DLOG("Unsupported opcode: %d", opcode);
      return false;
    } else if (opcode == LOAD_GLOBAL && banned_name_ids.count(oparg)) {
      JIT_DLOG("'%s' unsupported", name_at(oparg));
      return false;
    }
  }
  return true;
}

void HIRBuilder::AllocateRegistersForLocals(
    Environment* env,
    FrameState& state) {
  auto nlocals = code_->co_nlocals;
  state.locals.clear();
  state.locals.reserve(nlocals);
  for (int i = 0; i < nlocals; i++) {
    state.locals.emplace_back(env->AllocateRegister());
  }
}

void HIRBuilder::AllocateRegistersForCells(
    Environment* env,
    FrameState& state) {
  Py_ssize_t ncells = PyTuple_GET_SIZE(code_->co_cellvars) +
      PyTuple_GET_SIZE(code_->co_freevars);
  state.cells.clear();
  state.cells.reserve(ncells);
  for (int i = 0; i < ncells; i++) {
    state.cells.emplace_back(env->AllocateRegister());
  }
}

// Holds the current state of translation for a given basic block
struct HIRBuilder::TranslationContext {
  TranslationContext() = default;
  TranslationContext(BasicBlock* b, const FrameState& fs)
      : block(b), frame(fs) {}

  template <typename T, typename... Args>
  T* emit(Args&&... args) {
    auto instr = block->append<T>(std::forward<Args>(args)...);
    instr->setBytecodeOffset(frame.instr_offset());
    return instr;
  }

  template <typename T, typename... Args>
  T* emitChecked(Args&&... args) {
    auto instr = emit<T>(std::forward<Args>(args)...);
    auto out = instr->GetOutput();
    emit<CheckExc>(out, out, frame);
    return instr;
  }

  template <typename T, typename... Args>
  T* emitVariadic(
      TempAllocator& temps,
      std::size_t num_operands,
      Args&&... args) {
    Register* out = temps.Allocate();
    auto call = emit<T>(num_operands, out, std::forward<Args>(args)...);
    for (auto i = num_operands; i > 0; i--) {
      Register* operand = frame.stack.pop();
      call->SetOperand(i - 1, operand);
    }
    call->setFrameState(frame);
    frame.stack.push(out);
    return call;
  }

  void setCurrentInstr(const jit::BytecodeInstruction& cur_bci) {
    frame.next_instr_offset = cur_bci.NextInstrOffset();
  }

  void snapshot() {
    auto terminator = block->GetTerminator();
    if ((terminator != nullptr) && terminator->IsSnapshot()) {
      auto snapshot = static_cast<Snapshot*>(terminator);
      snapshot->setFrameState(frame);
    } else {
      emit<Snapshot>(frame);
    }
  }

  BasicBlock* block{nullptr};
  FrameState frame;
};

void HIRBuilder::addInitialYield(TranslationContext& tc) {
  auto out = temps_.Allocate();
  tc.emitChecked<InitialYield>(out);
}

Type prim_type_to_type(int prim_type) {
  switch (prim_type) {
    case TYPED_BOOL:
      return TCBool;
    case TYPED_CHAR:
    case TYPED_INT8:
      return TCInt8;
    case TYPED_INT16:
      return TCInt16;
    case TYPED_INT32:
      return TCInt32;
    case TYPED_INT64:
      return TCInt64;
    case TYPED_UINT8:
      return TCUInt8;
    case TYPED_UINT16:
      return TCUInt16;
    case TYPED_UINT32:
      return TCUInt32;
    case TYPED_UINT64:
      return TCUInt64;
    case TYPED_OBJECT:
      return TOptObject;
    case TYPED_DOUBLE:
      return TCDouble;
    case TYPED_ERROR:
      return TCInt32;
    default:
      JIT_CHECK(
          false, "non-primitive or unsupported Python type: %d", prim_type);
      break;
  }
}

Type resolve_type_descr(PyObject* descr) {
  int optional;
  Ref<PyTypeObject> type = THREADED_COMPILE_SERIALIZED_CALL(
      Ref<PyTypeObject>::steal(_PyClassLoader_ResolveType(descr, &optional)));

  JIT_CHECK(type != NULL, "bad type descr %s", repr(descr));

  int prim_type = _PyClassLoader_GetTypeCode(type);

  if (prim_type == TYPED_OBJECT) {
    return Type::fromType(type) | (optional ? TNoneType : TBottom);
  } else {
    return prim_type_to_type(prim_type);
  }
}

// Add LoadArg instructions for each function argument. This ensures that the
// corresponding variables are always assigned and allows for a uniform
// treatment of registers that correspond to arguments (vs locals) during
// definite assignment analysis.
void HIRBuilder::addLoadArgs(TranslationContext& tc, int num_args) {
  if (code_->co_flags & CO_STATICALLY_COMPILED) {
    _Py_CODEUNIT* rawcode = code_->co_rawcode;
    JIT_CHECK(
        _Py_OPCODE(rawcode[0]) == CHECK_ARGS, "expected CHECK_ARGS as 1st arg");
    PyObject* checks =
        PyTuple_GET_ITEM(code_->co_consts, _Py_OPARG(rawcode[0]));

    for (int i = 0; i < num_args; i++) {
      Register* dst = tc.frame.locals[i];
      Type type = TObject;
      // Arguments in CPython are the first N locals
      for (Py_ssize_t cur_check = 0; cur_check < PyTuple_GET_SIZE(checks) / 2;
           cur_check++) {
        long local = PyLong_AsLong(PyTuple_GET_ITEM(checks, cur_check * 2));
        if (local == i) {
          PyObject* type_descr = PyTuple_GET_ITEM(checks, cur_check * 2 + 1);
          int prim_type = THREADED_COMPILE_SERIALIZED_CALL(
              _PyClassLoader_ResolvePrimitiveType(type_descr));
          JIT_CHECK(prim_type != -1, "unknown type %s", repr(type_descr));
          if (prim_type != TYPED_OBJECT) {
            type = prim_type_to_type(prim_type);
          }
          break;
        }
      }
      JIT_CHECK(dst != nullptr, "No register for argument %d", i);
      tc.emit<LoadArg>(dst, i, type);
    }
  } else {
    for (int i = 0; i < num_args; i++) {
      // Arguments in CPython are the first N locals
      Register* dst = tc.frame.locals[i];
      JIT_CHECK(dst != nullptr, "No register for argument %d", i);
      tc.emit<LoadArg>(dst, i);
    }
  }
}

// Add a LoadClosureCell instruction for each freevar and a MakeCell for
// each cellvar.
void HIRBuilder::addInitializeCells(
    TranslationContext& tc,
    Register* cur_func) {
  Py_ssize_t ncellvars = PyTuple_GET_SIZE(code_->co_cellvars);
  Py_ssize_t nfreevars = PyTuple_GET_SIZE(code_->co_freevars);

  Register* null_reg = ncellvars > 0 ? temps_.Allocate() : nullptr;
  for (int i = 0; i < ncellvars; i++) {
    int arg = CO_CELL_NOT_AN_ARG;
    auto dst = tc.frame.cells[i];
    JIT_CHECK(dst != nullptr, "No register for cell %d", i);
    Register* cell_contents = null_reg;
    if (code_->co_cell2arg != NULL &&
        (arg = code_->co_cell2arg[i]) != CO_CELL_NOT_AN_ARG) {
      // cell is for argument local number `arg`
      JIT_CHECK(
          static_cast<unsigned>(arg) < tc.frame.locals.size(),
          "co_cell2arg says cell %d is local %d but locals size is %ld",
          i,
          arg,
          tc.frame.locals.size());
      cell_contents = tc.frame.locals[arg];
    }
    tc.emit<MakeCell>(dst, cell_contents, tc.frame);
    if (arg != CO_CELL_NOT_AN_ARG) {
      // Clear the local once we have it in a cell.
      tc.frame.locals[arg] = null_reg;
    }
  }

  if (nfreevars == 0) {
    return;
  }

  JIT_CHECK(cur_func != nullptr, "No cur_func in function with freevars");
  Register* func_closure = temps_.Allocate();
  tc.emit<LoadField>(
      func_closure, cur_func, offsetof(PyFunctionObject, func_closure), TTuple);
  for (int i = 0; i < nfreevars; i++) {
    auto cell_idx = i + ncellvars;
    Register* dst = tc.frame.cells[cell_idx];
    JIT_CHECK(dst != nullptr, "No register for cell %ld", cell_idx);
    tc.emit<LoadTupleItem>(dst, func_closure, i);
  }
}

static bool should_snapshot(
    const BytecodeInstruction& bci,
    bool is_in_async_for_header_block) {
  switch (bci.opcode()) {
    // These instructions conditionally alter the operand stack based on which
    // branch is taken, thus we cannot safely take a snapshot in the same basic
    // block. They're also control instructions, so snapshotting in the same
    // basic block doesn't make sense anyway.
    case FOR_ITER:
    case JUMP_IF_FALSE_OR_POP:
    case JUMP_IF_NONZERO_OR_POP:
    case JUMP_IF_TRUE_OR_POP:
    case JUMP_IF_ZERO_OR_POP:
    // These are all control instructions. Taking a snapshot after them in the
    // same basic block doesn't make sense, as control immediately transfers
    // to another basic block.
    case BEGIN_FINALLY:
    case CALL_FINALLY:
    case END_FINALLY:
    case JUMP_ABSOLUTE:
    case JUMP_FORWARD:
    case POP_JUMP_IF_FALSE:
    case POP_JUMP_IF_TRUE:
    case POP_JUMP_IF_ZERO:
    case POP_JUMP_IF_NONZERO:
    case RETURN_INT:
    case RETURN_VALUE:
    case RAISE_VARARGS:
    // These instructions only modify frame state and are always safe to
    // replay. We don't snapshot these in order to limit the amount of
    // unnecessary metadata in the lowered IR.
    case NOP:
    case DUP_TOP:
    case DUP_TOP_TWO:
    case EXTENDED_ARG:
    case LOAD_CLOSURE:
    case LOAD_CONST:
    case LOAD_FAST:
    case LOAD_LOCAL:
    case CONVERT_PRIMITIVE:
    case PRIMITIVE_LOAD_CONST:
    case INT_LOAD_CONST_OLD:
    case POP_FINALLY:
    case POP_TOP:
    case ROT_FOUR:
    case ROT_THREE:
    case ROT_TWO:
    case STORE_FAST:
    case PRIMITIVE_BOX:
    case PRIMITIVE_UNBOX:
    case PRIMITIVE_UNARY_OP:
    case CHECK_ARGS:
    case STORE_LOCAL: {
      return false;
    }
    // The `is` and `is not` comparison operators are implemented using pointer
    // equality. They are always safe to replay.
    case COMPARE_OP: {
      auto op = static_cast<CompareOp>(bci.oparg());
      return (op != CompareOp::kIs) && (op != CompareOp::kIsNot);
    }
    // In an async-for header block YIELD_FROM controls whether we end the loop
    case YIELD_FROM: {
      return !is_in_async_for_header_block;
    }
    // Take a snapshot after translating all other bytecode instructions. This
    // may generate unnecessary deoptimization metadata but will always be
    // correct.
    default: {
      return true;
    }
  }
}

static FrameMode getFrameMode(BorrowedRef<PyCodeObject> code) {
  /* check for code specific flags */
  if (code->co_flags & CO_NO_FRAME) {
    return FrameMode::kNone;
  } else if (code->co_flags & CO_NORMAL_FRAME) {
    return FrameMode::kNormal;
  }

  if (_PyJIT_NoFrame()) {
    return FrameMode::kNone;
  }
  if (_PyJIT_ShadowFrame()) {
    return FrameMode::kShadow;
  }
  return FrameMode::kNormal;
}

// Compute basic block boundaries and allocate corresponding HIR blocks
HIRBuilder::BlockMap HIRBuilder::createBlocks(
    Function& irfunc,
    const BytecodeInstructionBlock& bc_block) {
  BlockMap block_map;

  // Mark the beginning of each basic block in the bytecode
  std::set<Py_ssize_t> block_starts = {0};
  auto maybe_add_next_instr = [&](const BytecodeInstruction& bc_instr) {
    Py_ssize_t next_instr_idx = bc_instr.NextInstrIndex();
    if (next_instr_idx < bc_block.size()) {
      block_starts.insert(next_instr_idx);
    }
  };
  for (auto bc_instr : bc_block) {
    auto opcode = bc_instr.opcode();
    if (bc_instr.IsBranch()) {
      maybe_add_next_instr(bc_instr);
      auto target = bc_instr.GetJumpTargetAsIndex();
      block_starts.insert(target);
    } else if (
        // We always split after YIELD_FROM to handle the case where it's the
        // top of an async-for loop and so generate a HIR conditional jump.
        (opcode == BEGIN_FINALLY) || (opcode == END_FINALLY) ||
        (opcode == POP_FINALLY) || (opcode == RAISE_VARARGS) ||
        (opcode == RETURN_VALUE) || (opcode == YIELD_FROM)) {
      maybe_add_next_instr(bc_instr);
    } else {
      JIT_CHECK(!bc_instr.IsTerminator(), "Terminator should split block");
    }
  }

  // Allocate blocks
  auto it = block_starts.begin();
  while (it != block_starts.end()) {
    Py_ssize_t start_idx = *it;
    ++it;
    Py_ssize_t end_idx;
    if (it != block_starts.end()) {
      end_idx = *it;
    } else {
      end_idx = bc_block.size();
    }
    auto block = irfunc.cfg.AllocateBlock();
    block_map.blocks[start_idx * sizeof(_Py_CODEUNIT)] = block;
    block_map.bc_blocks.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(block),
        std::forward_as_tuple(bc_block.bytecode(), start_idx, end_idx));
  }

  return block_map;
}

BasicBlock* HIRBuilder::getBlockAtOff(Py_ssize_t off) {
  auto it = block_map_.blocks.find(off);
  JIT_DCHECK(it != block_map_.blocks.end(), "No block for offset %ld", off);
  return it->second;
}

std::unique_ptr<Function> HIRBuilder::BuildHIR(
    BorrowedRef<PyFunctionObject> func) {
  return BuildHIR(func->func_code, func->func_globals, funcFullname(func));
}

// This performs an abstract interpretation over the bytecode for func in order
// to translate it from a stack to register machine. The translation proceeds
// in two passes over the bytecode. First, basic block boundaries are
// enumerated and a mapping from block start offset to basic block is
// created. Next, basic blocks are filled in by simulating the effect that each
// instruction has on the stack.
//
// The correctness of the translation depends on the invariant that the depth
// the operand stack is be constant at each program point.  All of the CPython
// bytecode that we currently support maintain this invariant. However, there
// are a few bytecodes that do not (e.g. SETUP_FINALLY). We will need to deal
// with that if we ever want to support compiling them.
std::unique_ptr<Function> HIRBuilder::BuildHIR(
    BorrowedRef<PyCodeObject> code,
    BorrowedRef<PyDictObject> globals,
    const std::string& fullname) {
  JIT_DCHECK(PyCode_Check(code), "didn't supply a code object");
  code_ = code;
  if (!can_translate(code_)) {
    JIT_DLOG("Can't translate all opcodes in %s", fullname);
    return nullptr;
  }

  auto irfunc = std::make_unique<Function>();
  irfunc->fullname = fullname;
  irfunc->frameMode = getFrameMode(code);
  irfunc->setCode(code);
  irfunc->globals.reset(globals);
  irfunc->builtins.reset(PyEval_GetBuiltins());
  globals_ = irfunc->globals;
  builtins_ = irfunc->builtins;
  temps_ = TempAllocator(&irfunc->env);
  if (code_->co_flags & CO_STATICALLY_COMPILED) {
    irfunc->return_type =
        resolve_type_descr(_PyClassLoader_GetCodeReturnTypeDescr(code));
  }

  BytecodeInstructionBlock bc_instrs{code_};
  block_map_ = createBlocks(*irfunc, bc_instrs);

  // Ensure that the entry block isn't a loop header
  BasicBlock* entry_block = getBlockAtOff(0);
  for (const auto& bci : bc_instrs) {
    if (bci.IsBranch() && bci.GetJumpTarget() == 0) {
      entry_block = irfunc->cfg.AllocateBlock();
      break;
    }
  }
  irfunc->cfg.entry_block = entry_block;

  // Insert LoadArg, LoadClosureCell, and MakeCell/MakeNullCell instructions
  // for the entry block
  TranslationContext entry_tc{entry_block, FrameState{}};
  AllocateRegistersForLocals(&irfunc->env, entry_tc.frame);
  AllocateRegistersForCells(&irfunc->env, entry_tc.frame);

  addLoadArgs(entry_tc, irfunc->numArgs());
  Register* cur_func = nullptr;
  if (irfunc->uses_runtime_func) {
    cur_func = temps_.Allocate();
    entry_tc.emit<LoadCurrentFunc>(cur_func);
  }
  addInitializeCells(entry_tc, cur_func);

  if (code_->co_flags & kCoFlagsAnyGenerator) {
    // InitialYield must be after args are loaded so they can be spilled to
    // the suspendable state. It must also come before anything which can
    // deopt as generator deopt assumes we're running from state stored
    // in a generator object.
    addInitialYield(entry_tc);
  }

  BasicBlock* first_block = getBlockAtOff(0);
  if (entry_block != first_block) {
    entry_block->append<Branch>(first_block);
  }

  entry_tc.block = first_block;
  translate(*irfunc, bc_instrs, entry_tc);

  irfunc->cfg.RemoveTrampolineBlocks();
  irfunc->cfg.removeUnreachableBlocks();

  return irfunc;
}

void HIRBuilder::translate(
    Function& irfunc,
    const jit::BytecodeInstructionBlock& bc_instrs,
    const TranslationContext& tc,
    FinallyCompleter complete_finally) {
  std::deque<TranslationContext> queue = {tc};
  std::unordered_set<BasicBlock*> processed;
  std::unordered_set<BasicBlock*> loop_headers;

  while (!queue.empty()) {
    auto tc = std::move(queue.front());
    queue.pop_front();
    if (processed.count(tc.block)) {
      continue;
    }
    processed.emplace(tc.block);

    // Translate remaining instructions into HIR
    auto& bc_block = map_get(block_map_.bc_blocks, tc.block);
    tc.frame.next_instr_offset = bc_block.startOffset();
    tc.snapshot();

    auto is_in_async_for_header_block = [&tc, &bc_instrs]() {
      if (tc.frame.block_stack.isEmpty()) {
        return false;
      }
      const ExecutionBlock& block_top = tc.frame.block_stack.top();
      return block_top.isAsyncForHeaderBlock(bc_instrs);
    };

    for (auto bc_it = bc_block.begin(); bc_it != bc_block.end(); ++bc_it) {
      BytecodeInstruction bc_instr = *bc_it;
      tc.setCurrentInstr(bc_instr);

      // Translate instruction
      switch (bc_instr.opcode()) {
        case NOP: {
          break;
        }
        case BINARY_ADD:
        case BINARY_AND:
        case BINARY_FLOOR_DIVIDE:
        case BINARY_LSHIFT:
        case BINARY_MATRIX_MULTIPLY:
        case BINARY_MODULO:
        case BINARY_MULTIPLY:
        case BINARY_OR:
        case BINARY_POWER:
        case BINARY_RSHIFT:
        case BINARY_SUBSCR:
        case BINARY_SUBTRACT:
        case BINARY_TRUE_DIVIDE:
        case BINARY_XOR: {
          emitBinaryOp(tc, bc_instr);
          break;
        }
        case INPLACE_ADD:
        case INPLACE_AND:
        case INPLACE_FLOOR_DIVIDE:
        case INPLACE_LSHIFT:
        case INPLACE_MATRIX_MULTIPLY:
        case INPLACE_MODULO:
        case INPLACE_MULTIPLY:
        case INPLACE_OR:
        case INPLACE_POWER:
        case INPLACE_RSHIFT:
        case INPLACE_SUBTRACT:
        case INPLACE_TRUE_DIVIDE:
        case INPLACE_XOR: {
          emitInPlaceOp(tc, bc_instr);
          break;
        }
        case UNARY_NOT:
        case UNARY_NEGATIVE:
        case UNARY_POSITIVE:
        case UNARY_INVERT: {
          emitUnaryOp(tc, bc_instr);
          break;
        }
        case BUILD_LIST:
        case BUILD_TUPLE:
          emitMakeListTuple(tc, bc_instr);
          break;
        case BUILD_LIST_UNPACK:
        case BUILD_TUPLE_UNPACK:
        case BUILD_TUPLE_UNPACK_WITH_CALL:
          emitMakeListTupleUnpack(tc, bc_instr);
          break;
        case BUILD_MAP: {
          emitBuildMap(tc, bc_instr);
          break;
        }
        case BUILD_MAP_UNPACK:
          emitBuildMapUnpack(tc, bc_instr, false);
          break;
        case BUILD_MAP_UNPACK_WITH_CALL:
          emitBuildMapUnpack(tc, bc_instr, true);
          break;
        case BUILD_SET: {
          emitBuildSet(tc, bc_instr);
          break;
        }
        case BUILD_SET_UNPACK: {
          emitBuildSetUnpack(tc, bc_instr);
          break;
        }
        case BUILD_CONST_KEY_MAP: {
          emitBuildConstKeyMap(tc, bc_instr);
          break;
        }
        case CALL_FUNCTION:
        case CALL_FUNCTION_EX:
        case CALL_FUNCTION_KW:
        case CALL_METHOD:
        case INVOKE_FUNCTION:
        case INVOKE_METHOD: {
          emitAnyCall(irfunc.cfg, tc, bc_it, bc_instrs);
          break;
        }
        case COMPARE_OP: {
          emitCompareOp(tc, bc_instr);
          break;
        }
        case LOAD_ATTR: {
          emitLoadAttr(tc, bc_instr);
          break;
        }
        case LOAD_METHOD: {
          emitLoadMethod(tc, bc_instr);
          break;
        }
        case LOAD_METHOD_SUPER: {
          emitLoadMethodOrAttrSuper(tc, bc_instr, true);
          break;
        }
        case LOAD_ATTR_SUPER: {
          emitLoadMethodOrAttrSuper(tc, bc_instr, false);
          break;
        }
        case LOAD_CLOSURE: {
          tc.frame.stack.push(tc.frame.cells[bc_instr.oparg()]);
          break;
        }
        case LOAD_DEREF: {
          emitLoadDeref(tc, bc_instr);
          break;
        }
        case STORE_DEREF: {
          emitStoreDeref(tc, bc_instr);
          break;
        }
        case LOAD_CONST: {
          emitLoadConst(tc, bc_instr);
          break;
        }
        case LOAD_FAST: {
          emitLoadFast(tc, bc_instr);
          break;
        }
        case LOAD_LOCAL: {
          emitLoadLocal(tc, bc_instr);
          break;
        }
        case CONVERT_PRIMITIVE: {
          emitConvertPrimitive(tc, bc_instr);
          break;
        }
        case PRIMITIVE_LOAD_CONST: {
          emitPrimitiveLoadConst(tc, bc_instr);
          break;
        }
        case INT_LOAD_CONST_OLD: {
          emitIntLoadConstOld(tc, bc_instr);
          break;
        }
        case PRIMITIVE_BOX: {
          emitPrimitiveBox(tc, bc_instr);
          break;
        }
        case PRIMITIVE_UNBOX: {
          emitPrimitiveUnbox(tc, bc_instr);
          break;
        }
        case PRIMITIVE_BINARY_OP: {
          emitPrimitiveBinaryOp(tc, bc_instr);
          break;
        }
        case INT_COMPARE_OP: {
          emitIntCompare(tc, bc_instr);
          break;
        }
        case PRIMITIVE_UNARY_OP: {
          emitPrimitiveUnaryOp(tc, bc_instr);
          break;
        }
        case FAST_LEN: {
          emitFastLen(irfunc.cfg, tc, bc_instr);
          break;
        }
        case REFINE_TYPE: {
          emitRefineType(tc, bc_instr);
          break;
        }
        case SEQUENCE_GET: {
          emitSequenceGet(tc, bc_instr);
          break;
        }
        case SEQUENCE_SET: {
          emitSequenceSet(tc, bc_instr);
          break;
        }
        case SEQUENCE_REPEAT: {
          emitSequenceRepeat(irfunc.cfg, tc, bc_instr);
          break;
        }
        case LOAD_GLOBAL: {
          emitLoadGlobal(tc, bc_instr);
          break;
        }
        case JUMP_ABSOLUTE:
        case JUMP_FORWARD: {
          auto target_off = bc_instr.GetJumpTarget();
          auto target = getBlockAtOff(target_off);
          if ((bc_instr.opcode() == JUMP_ABSOLUTE) &&
              (target_off <= bc_instr.offset())) {
            loop_headers.emplace(target);
          }
          tc.emit<Branch>(target);
          break;
        }
        case JUMP_IF_FALSE_OR_POP:
        case JUMP_IF_NONZERO_OR_POP:
        case JUMP_IF_TRUE_OR_POP:
        case JUMP_IF_ZERO_OR_POP: {
          emitJumpIf(tc, bc_instr);
          break;
        }
        case POP_BLOCK: {
          popBlock(irfunc.cfg, tc);
          break;
        }
        case POP_JUMP_IF_FALSE:
        case POP_JUMP_IF_TRUE: {
          auto target_off = bc_instr.GetJumpTarget();
          auto target = getBlockAtOff(target_off);
          if (target_off <= bc_instr.offset()) {
            loop_headers.emplace(target);
          }
          emitPopJumpIf(tc, bc_instr);
          break;
        }
        case POP_TOP: {
          tc.frame.stack.pop();
          break;
        }
        case RETURN_INT: {
          Type type = prim_type_to_type(bc_instr.oparg());
          JIT_CHECK(
              type <= irfunc.return_type,
              "bad return type %s, expected %s",
              type,
              irfunc.return_type);
          Register* reg = tc.frame.stack.pop();
          tc.emit<Return>(reg, type);
          break;
        }
        case RETURN_VALUE: {
          Register* reg = tc.frame.stack.pop();
          // TODO add irfunc.return_type to Return instr here to validate that
          // all values flowing to return are of correct type; will require
          // consistency of static compiler and JIT types, see T86480663
          JIT_CHECK(
              tc.frame.block_stack.isEmpty(),
              "Returning with non-empty block stack");
          tc.emit<Return>(reg);
          break;
        }
        case BEGIN_FINALLY: {
          emitBeginFinally(irfunc, tc, bc_instrs, bc_instr, queue);
          break;
        }
        case CALL_FINALLY: {
          emitCallFinally(irfunc, tc, bc_instrs, bc_instr, queue);
          break;
        }
        case END_ASYNC_FOR: {
          emitEndAsyncFor(tc, bc_instr);
          break;
        }
        case END_FINALLY: {
          emitEndFinally(tc, bc_instr, complete_finally);
          break;
        }
        case POP_FINALLY: {
          emitPopFinally(tc, bc_instr, complete_finally);
          break;
        }
        case SETUP_FINALLY: {
          emitSetupFinally(tc, bc_instr);
          break;
        }
        case STORE_ATTR: {
          emitStoreAttr(tc, bc_instr);
          break;
        }
        case STORE_FAST: {
          emitStoreFast(tc, bc_instr);
          break;
        }
        case STORE_LOCAL: {
          emitStoreLocal(tc, bc_instr);
          break;
        }
        case STORE_SUBSCR: {
          emitStoreSubscr(tc);
          break;
        }
        case BUILD_SLICE: {
          emitBuildSlice(tc, bc_instr);
          break;
        }
        case GET_AITER: {
          emitGetAIter(tc);
          break;
        }
        case GET_ANEXT: {
          emitGetANext(tc);
          break;
        }
        case GET_ITER: {
          emitGetIter(tc);
          break;
        }
        case GET_YIELD_FROM_ITER: {
          emitGetYieldFromIter(irfunc.cfg, tc);
          break;
        }
        case MAKE_FUNCTION: {
          emitMakeFunction(tc, bc_instr);
          break;
        }
        case LIST_APPEND: {
          emitListAppend(tc, bc_instr);
          break;
        }
        case LOAD_ITERABLE_ARG: {
          emitLoadIterableArg(irfunc.cfg, tc, bc_instr);
          break;
        }
        case DUP_TOP: {
          auto& stack = tc.frame.stack;
          stack.push(stack.top());
          break;
        }
        case DUP_TOP_TWO: {
          auto& stack = tc.frame.stack;
          Register* top = stack.top();
          Register* snd = stack.top(1);
          stack.push(snd);
          stack.push(top);
          break;
        }
        case ROT_TWO: {
          auto& stack = tc.frame.stack;
          Register* top = stack.pop();
          Register* snd = stack.pop();
          stack.push(top);
          stack.push(snd);
          break;
        }
        case ROT_THREE: {
          auto& stack = tc.frame.stack;
          Register* top = stack.pop();
          Register* snd = stack.pop();
          Register* thd = stack.pop();
          stack.push(top);
          stack.push(thd);
          stack.push(snd);
          break;
        }
        case ROT_FOUR: {
          auto& stack = tc.frame.stack;
          Register* r1 = stack.pop();
          Register* r2 = stack.pop();
          Register* r3 = stack.pop();
          Register* r4 = stack.pop();
          stack.push(r1);
          stack.push(r4);
          stack.push(r3);
          stack.push(r2);
          break;
        }
        case FOR_ITER: {
          emitForIter(tc, bc_instr);
          break;
        }
        case LOAD_FIELD: {
          emitLoadField(tc, bc_instr);
          break;
        }
        case CAST: {
          emitCast(tc, bc_instr);
          break;
        }
        case CHECK_ARGS: {
          // check args is handled in the prologue
          break;
        }
        case STORE_FIELD: {
          emitStoreField(tc, bc_instr);
          break;
        }
        case POP_JUMP_IF_ZERO:
        case POP_JUMP_IF_NONZERO: {
          emitPopJumpIf(tc, bc_instr);
          break;
        }
        case IMPORT_FROM: {
          emitImportFrom(tc, bc_instr);
          break;
        }
        case IMPORT_NAME: {
          emitImportName(tc, bc_instr);
          break;
        }
        case RAISE_VARARGS: {
          emitRaiseVarargs(tc, bc_instr);
          break;
        }
        case YIELD_VALUE: {
          emitYieldValue(tc);
          break;
        }
        case YIELD_FROM: {
          if (is_in_async_for_header_block()) {
            emitAsyncForHeaderYieldFrom(tc, bc_instr);
          } else {
            emitYieldFrom(tc, temps_.Allocate());
          }
          break;
        }
        case GET_AWAITABLE: {
          Py_ssize_t idx = bc_instr.index();
          int prev_op = idx ? bc_instrs.at(idx - 1).opcode() : 0;
          emitGetAwaitable(irfunc.cfg, tc, prev_op);
          break;
        }
        case BUILD_STRING: {
          emitBuildString(tc, bc_instr);
          break;
        }
        case FORMAT_VALUE: {
          emitFormatValue(tc, bc_instr);
          break;
        }
        case MAP_ADD: {
          emitMapAdd(tc, bc_instr);
          break;
        }
        case SET_ADD: {
          emitSetAdd(tc, bc_instr);
          break;
        }
        case UNPACK_EX: {
          emitUnpackEx(tc, bc_instr);
          break;
        }
        case UNPACK_SEQUENCE: {
          emitUnpackSequence(irfunc.cfg, tc, bc_instr);
          break;
        }
        case DELETE_SUBSCR: {
          Register* sub = tc.frame.stack.pop();
          Register* container = tc.frame.stack.pop();
          tc.emit<DeleteSubscr>(container, sub, tc.frame);
          break;
        }
        case DELETE_FAST: {
          int var_idx = bc_instr.oparg();
          Register* var = tc.frame.locals[var_idx];
          tc.emit<LoadConst>(var, TNullptr);
          break;
        }
        case BEFORE_ASYNC_WITH: {
          emitBeforeAsyncWith(tc);
          break;
        }
        case SETUP_ASYNC_WITH: {
          emitSetupAsyncWith(tc, bc_instr);
          break;
        }
        case SETUP_WITH: {
          emitSetupWith(tc, bc_instr);
          break;
        }
        case WITH_CLEANUP_START: {
          emitWithCleanupStart(tc);
          break;
        }
        case WITH_CLEANUP_FINISH: {
          emitWithCleanupFinish(tc);
          break;
        }
        default: {
          // NOTREACHED
          JIT_CHECK(false, "unhandled opcode: %d", bc_instr.opcode());
          break;
        }
      }

      if (should_snapshot(bc_instr, is_in_async_for_header_block())) {
        tc.snapshot();
      }
    }
    // Insert jumps for blocks that fall through.
    auto last_instr = tc.block->GetTerminator();
    if ((last_instr == nullptr) || !last_instr->IsTerminator()) {
      auto off = bc_block.endOffset();
      last_instr = tc.emit<Branch>(getBlockAtOff(off));
    }

    // Make sure any values left on the stack are in the registers that we
    // expect
    BlockCanonicalizer bc;
    bc.Run(tc.block, temps_, tc.frame.stack);

    // Add successors to be processed
    //
    // These bytecodes alter the operand stack along one branch and leave it
    // untouched along the other. Thus, they must be special cased.
    BytecodeInstruction last_bc_instr = bc_block.lastInstr();
    switch (last_bc_instr.opcode()) {
      case BEGIN_FINALLY:
      case CALL_FINALLY:
      case END_FINALLY:
      case POP_FINALLY: {
        // Opcodes for handling finally blocks are handled specially because
        // CPython does not guarantee a constant stack depth when entering a
        // finally block. We work around the issue by "tail duplicating" the
        // finally block at each "call site" (BEGIN_FINALLY or CALL_FINALLY) by
        // recursing into the compiler with a fresh set of basic blocks.  The
        // callee then links the finally block back to us and queues the
        // appropriate block for processing. See the various `emit` functions
        // for these opcodes for the implementation.
        break;
      }
      case FOR_ITER: {
        auto condbr = static_cast<CondBranchIterNotDone*>(last_instr);
        auto new_frame = tc.frame;
        // Sentinel value signaling iteration is complete and the iterator
        // itself
        new_frame.stack.discard(2);
        queue.emplace_back(condbr->true_bb(), tc.frame);
        queue.emplace_back(condbr->false_bb(), new_frame);
        break;
      }
      case JUMP_IF_FALSE_OR_POP:
      case JUMP_IF_ZERO_OR_POP: {
        auto condbr = static_cast<CondBranch*>(last_instr);
        auto new_frame = tc.frame;
        new_frame.stack.pop();
        queue.emplace_back(condbr->true_bb(), new_frame);
        queue.emplace_back(condbr->false_bb(), tc.frame);
        break;
      }
      case JUMP_IF_NONZERO_OR_POP:
      case JUMP_IF_TRUE_OR_POP: {
        auto condbr = static_cast<CondBranch*>(last_instr);
        auto new_frame = tc.frame;
        new_frame.stack.pop();
        queue.emplace_back(condbr->true_bb(), tc.frame);
        queue.emplace_back(condbr->false_bb(), new_frame);
        break;
      }
      default: {
        if (last_bc_instr.opcode() == YIELD_FROM &&
            is_in_async_for_header_block()) {
          JIT_CHECK(
              last_instr->IsCondBranch(),
              "Async-for header should end with CondBranch");
          auto condbr = static_cast<CondBranch*>(last_instr);
          FrameState new_frame = tc.frame;
          new_frame.stack.pop();
          queue.emplace_back(condbr->true_bb(), tc.frame);
          queue.emplace_back(condbr->false_bb(), std::move(new_frame));
          break;
        }
        for (std::size_t i = 0; i < last_instr->numEdges(); i++) {
          auto succ = last_instr->successor(i);
          queue.emplace_back(succ, tc.frame);
        }
        break;
      }
    }
  }

  for (auto block : loop_headers) {
    insertEvalBreakerCheckForLoop(irfunc.cfg, block);
  }
}

void BlockCanonicalizer::InsertCopies(
    Register* reg,
    TempAllocator& temps,
    Instr& terminator,
    std::vector<Register*>& alloced) {
  if (done_.count(reg)) {
    return;
  } else if (processing_.count(reg)) {
    // We've detected a cycle. Move the register to a new home
    // in order to break the cycle.
    auto tmp = temps.Allocate();
    auto mov = Assign::create(tmp, reg);
    mov->copyBytecodeOffset(terminator);
    mov->InsertBefore(terminator);
    moved_[reg] = tmp;
    alloced.emplace_back(tmp);
    return;
  }

  auto orig_reg = reg;
  for (auto dst : copies_[reg]) {
    auto it = copies_.find(dst);
    if (it != copies_.end()) {
      // The destination also needs to be moved. So deal with it first.
      processing_.insert(reg);
      InsertCopies(dst, temps, terminator, alloced);
      processing_.erase(reg);
      // It's possible that the register we were processing was moved
      // because it participated in a cycle
      auto it2 = moved_.find(reg);
      if (it2 != moved_.end()) {
        reg = it2->second;
      }
    }
    auto mov = Assign::create(dst, reg);
    mov->copyBytecodeOffset(terminator);
    mov->InsertBefore(terminator);
  }

  done_.insert(orig_reg);
}

void BlockCanonicalizer::Run(
    BasicBlock* block,
    TempAllocator& temps,
    OperandStack& stack) {
  if (stack.isEmpty()) {
    return;
  }

  processing_.clear();
  copies_.clear();
  moved_.clear();

  // Compute the desired stack layout
  std::vector<Register*> dsts;
  dsts.reserve(stack.size());
  for (std::size_t i = 0; i < stack.size(); i++) {
    auto reg = temps.GetOrAllocate(i);
    dsts.emplace_back(reg);
  }

  // Compute the minimum number of copies that need to happen
  std::vector<Register*> need_copy;
  auto term = block->GetTerminator();
  std::vector<Register*> alloced;
  for (std::size_t i = 0; i < stack.size(); i++) {
    auto src = stack.at(i);
    auto dst = dsts[i];
    if (src != dst) {
      need_copy.emplace_back(src);
      copies_[src].emplace_back(dst);

      if (term->Uses(src)) {
        term->ReplaceUsesOf(src, dst);
      } else if (term->Uses(dst)) {
        auto tmp = temps.Allocate();
        alloced.emplace_back(tmp);
        auto mov = Assign::create(tmp, dst);
        mov->InsertBefore(*term);
        term->ReplaceUsesOf(dst, tmp);
      }
    }
  }
  if (need_copy.empty()) {
    return;
  }

  for (auto reg : need_copy) {
    InsertCopies(reg, temps, *term, alloced);
  }

  // Put the stack in canonical form
  for (std::size_t i = 0; i < stack.size(); i++) {
    stack.atPut(i, dsts[i]);
  }
}

static inline BinaryOpKind get_bin_op_kind(
    const jit::BytecodeInstruction& bc_instr) {
  switch (bc_instr.opcode()) {
    case BINARY_ADD: {
      return BinaryOpKind::kAdd;
    }
    case BINARY_AND: {
      return BinaryOpKind::kAnd;
    }
    case BINARY_FLOOR_DIVIDE: {
      return BinaryOpKind::kFloorDivide;
    }
    case BINARY_LSHIFT: {
      return BinaryOpKind::kLShift;
    }
    case BINARY_MATRIX_MULTIPLY: {
      return BinaryOpKind::kMatrixMultiply;
    }
    case BINARY_MODULO: {
      return BinaryOpKind::kModulo;
    }
    case BINARY_MULTIPLY: {
      return BinaryOpKind::kMultiply;
    }
    case BINARY_OR: {
      return BinaryOpKind::kOr;
    }
    case BINARY_POWER: {
      return BinaryOpKind::kPower;
    }
    case BINARY_RSHIFT: {
      return BinaryOpKind::kRShift;
    }
    case BINARY_SUBSCR: {
      return BinaryOpKind::kSubscript;
    }
    case BINARY_SUBTRACT: {
      return BinaryOpKind::kSubtract;
    }
    case BINARY_TRUE_DIVIDE: {
      return BinaryOpKind::kTrueDivide;
    }
    case BINARY_XOR: {
      return BinaryOpKind::kXor;
    }
    default: {
      JIT_CHECK(false, "unhandled binary op %d", bc_instr.opcode());
      // NOTREACHED
      break;
    }
  }
}

void HIRBuilder::emitAnyCall(
    CFG& cfg,
    TranslationContext& tc,
    jit::BytecodeInstructionBlock::Iterator& bc_it,
    const jit::BytecodeInstructionBlock& bc_instrs) {
  BytecodeInstruction bc_instr = *bc_it;
  int idx = bc_instr.index();
  bool is_awaited = code_->co_flags & CO_COROUTINE &&
      // We only need to be followed by GET_AWAITABLE to know we are awaited,
      // but we also need to ensure the following LOAD_CONST and YIELD_FROM are
      // inside this BytecodeInstructionBlock. This may not be the case if the
      // 'await' is shared as in 'await (x if y else z)'.
      bc_it.remainingInstrs() >= 3 &&
      bc_instrs.at(idx + 1).opcode() == GET_AWAITABLE;
  JIT_CHECK(
      !is_awaited ||
          (bc_instrs.at(idx + 2).opcode() == LOAD_CONST &&
           bc_instrs.at(idx + 3).opcode() == YIELD_FROM),
      "GET_AWAITABLE should always be followed by LOAD_CONST and "
      "YIELD_FROM");
  bool call_used_is_awaited = true;
  switch (bc_instr.opcode()) {
    case CALL_FUNCTION: {
      emitCallFunction(tc, bc_instr, is_awaited);
      break;
    }
    case CALL_FUNCTION_EX: {
      emitCallEx(tc, bc_instr, is_awaited);
      break;
    }
    case CALL_FUNCTION_KW: {
      emitCallKWArgs(tc, bc_instr, is_awaited);
      break;
    }
    case CALL_METHOD: {
      emitCallMethod(tc, bc_instr, is_awaited);
      break;
    }
    case INVOKE_FUNCTION: {
      call_used_is_awaited = emitInvokeFunction(tc, bc_instr, is_awaited);
      break;
    }
    case INVOKE_METHOD: {
      call_used_is_awaited = emitInvokeMethod(tc, bc_instr, is_awaited);
      break;
    }
    default: {
      JIT_CHECK(false, "Unhandled call opcode");
    }
  }
  if (is_awaited && call_used_is_awaited) {
    Register* out = temps_.Allocate();
    TranslationContext await_block{cfg.AllocateBlock(), tc.frame};
    TranslationContext post_await_block{cfg.AllocateBlock(), tc.frame};

    emitDispatchEagerCoroResult(
        cfg, tc, out, await_block.block, post_await_block.block);

    tc.block = await_block.block;

    ++bc_it;
    emitGetAwaitable(cfg, tc, bc_instr.opcode());

    ++bc_it;
    emitLoadConst(tc, *bc_it);

    ++bc_it;
    emitYieldFrom(tc, out);
    tc.emit<Branch>(post_await_block.block);

    tc.block = post_await_block.block;
  }
}

void HIRBuilder::emitBinaryOp(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& stack = tc.frame.stack;
  Register* right = stack.pop();
  Register* left = stack.pop();
  Register* result = temps_.Allocate();
  BinaryOpKind op_kind = get_bin_op_kind(bc_instr);
  tc.emit<BinaryOp>(op_kind, result, left, right, tc.frame);
  stack.push(result);
}

static inline InPlaceOpKind get_inplace_op_kind(
    const jit::BytecodeInstruction& bc_instr) {
  switch (bc_instr.opcode()) {
    case INPLACE_ADD: {
      return InPlaceOpKind::kAdd;
    }
    case INPLACE_AND: {
      return InPlaceOpKind::kAnd;
    }
    case INPLACE_FLOOR_DIVIDE: {
      return InPlaceOpKind::kFloorDivide;
    }
    case INPLACE_LSHIFT: {
      return InPlaceOpKind::kLShift;
    }
    case INPLACE_MATRIX_MULTIPLY: {
      return InPlaceOpKind::kMatrixMultiply;
    }
    case INPLACE_MODULO: {
      return InPlaceOpKind::kModulo;
    }
    case INPLACE_MULTIPLY: {
      return InPlaceOpKind::kMultiply;
    }
    case INPLACE_OR: {
      return InPlaceOpKind::kOr;
    }
    case INPLACE_POWER: {
      return InPlaceOpKind::kPower;
    }
    case INPLACE_RSHIFT: {
      return InPlaceOpKind::kRShift;
    }
    case INPLACE_SUBTRACT: {
      return InPlaceOpKind::kSubtract;
    }
    case INPLACE_TRUE_DIVIDE: {
      return InPlaceOpKind::kTrueDivide;
    }
    case INPLACE_XOR: {
      return InPlaceOpKind::kXor;
    }
    default: {
      JIT_CHECK(false, "unhandled inplace op %d", bc_instr.opcode());
      // NOTREACHED
      break;
    }
  }
}

void HIRBuilder::emitInPlaceOp(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& stack = tc.frame.stack;
  Register* right = stack.pop();
  Register* left = stack.pop();
  Register* result = temps_.Allocate();
  InPlaceOpKind op_kind = get_inplace_op_kind(bc_instr);
  tc.emit<InPlaceOp>(op_kind, result, left, right, tc.frame);
  stack.push(result);
}

static inline UnaryOpKind get_unary_op_kind(
    const jit::BytecodeInstruction& bc_instr) {
  switch (bc_instr.opcode()) {
    case UNARY_NOT:
      return UnaryOpKind::kNot;

    case UNARY_NEGATIVE:
      return UnaryOpKind::kPositive;

    case UNARY_POSITIVE:
      return UnaryOpKind::kNegate;

    case UNARY_INVERT:
      return UnaryOpKind::kInvert;

    default:
      JIT_CHECK(false, "unhandled unary op %d", bc_instr.opcode());
      // NOTREACHED
      break;
  }
}

void HIRBuilder::emitUnaryOp(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* operand = tc.frame.stack.pop();
  Register* result = temps_.Allocate();
  UnaryOpKind op_kind = get_unary_op_kind(bc_instr);
  tc.emit<UnaryOp>(op_kind, result, operand, tc.frame);
  tc.frame.stack.push(result);
}

void HIRBuilder::emitCallFunction(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr,
    bool is_awaited) {
  std::size_t num_operands = static_cast<std::size_t>(bc_instr.oparg()) + 1;
  tc.emitVariadic<VectorCall>(temps_, num_operands, is_awaited);
}

void HIRBuilder::emitCallEx(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr,
    bool is_awaited) {
  Register* dst = temps_.Allocate();
  OperandStack& stack = tc.frame.stack;
  if (bc_instr.oparg() & 0x1) {
    Register* kwargs = stack.pop();
    Register* pargs = stack.pop();
    Register* func = stack.pop();
    CallExKw* call = tc.emit<CallExKw>(dst, func, pargs, kwargs, is_awaited);
    call->setFrameState(tc.frame);
  } else {
    Register* pargs = stack.pop();
    Register* func = stack.pop();
    CallEx* call = tc.emit<CallEx>(dst, func, pargs, is_awaited);
    call->setFrameState(tc.frame);
  }
  stack.push(dst);
}

void HIRBuilder::emitCallKWArgs(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr,
    bool is_awaited) {
  std::size_t num_operands = static_cast<std::size_t>(bc_instr.oparg()) + 2;
  tc.emitVariadic<VectorCallKW>(temps_, num_operands, is_awaited);
}

void HIRBuilder::emitCallMethod(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr,
    bool is_awaited) {
  std::size_t num_operands = static_cast<std::size_t>(bc_instr.oparg()) + 2;
  tc.emitVariadic<CallMethod>(temps_, num_operands, is_awaited);
}

void HIRBuilder::emitBuildSlice(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  std::size_t num_operands = static_cast<std::size_t>(bc_instr.oparg());
  tc.emitVariadic<BuildSlice>(temps_, num_operands);
}

void HIRBuilder::emitListAppend(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto item = tc.frame.stack.pop();
  auto list = tc.frame.stack.peek(bc_instr.oparg());
  auto dst = temps_.Allocate();
  tc.emit<ListAppend>(dst, list, item, tc.frame);
}

void HIRBuilder::emitLoadIterableArg(
    CFG& cfg,
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto iterable = tc.frame.stack.pop();
  Register* tuple;
  if (iterable->type() != TTupleExact) {
    auto is_tuple = temps_.Allocate();
    tc.emit<CheckTuple>(is_tuple, iterable);

    TranslationContext tuple_path{cfg.AllocateBlock(), tc.frame};
    tuple_path.snapshot();
    TranslationContext non_tuple_path{cfg.AllocateBlock(), tc.frame};
    non_tuple_path.snapshot();
    tc.emit<CondBranch>(is_tuple, tuple_path.block, non_tuple_path.block);
    tc.block = cfg.AllocateBlock();
    tc.snapshot();

    tuple = temps_.Allocate();

    tuple_path.emit<Assign>(tuple, iterable);
    tuple_path.emit<Branch>(tc.block);

    non_tuple_path.emit<GetTuple>(tuple, iterable, tc.frame);
    non_tuple_path.emit<Branch>(tc.block);
  } else {
    tuple = iterable;
  }

  auto tmp = temps_.Allocate();
  auto tup_idx = temps_.Allocate();
  auto element = temps_.Allocate();
  tc.emit<LoadConst>(tmp, Type::fromCInt(bc_instr.oparg(), TCInt64));
  tc.emitChecked<PrimitiveBox>(tup_idx, tmp, true);
  tc.emit<BinaryOp>(
      BinaryOpKind::kSubscript, element, tuple, tup_idx, tc.frame);
  tc.frame.stack.push(element);
  tc.frame.stack.push(tuple);
}

bool HIRBuilder::tryEmitDirectMethodCall(
    PyMethodDef* method,
    TranslationContext& tc,
    long nargs) {
  if (method->ml_flags & METH_TYPED) {
    emitInvokeTypedMethod(tc, method, nargs);
    return true;
  } else if (
      (method->ml_flags == METH_NOARGS && nargs == 1) ||
      (method->ml_flags == METH_O && nargs == 2)) {
    auto& stack = tc.frame.stack;
    // this isn't strongly typed, but we have the correct number of args
    // to directly invoke it
    Register* out = temps_.Allocate();
    auto staticCall =
        tc.emit<CallStatic>(nargs, out, (void*)method->ml_meth, TObject);
    for (auto i = nargs; i > 0; i--) {
      Register* operand = stack.pop();
      staticCall->SetOperand(i - 1, operand);
    }
    tc.emit<CheckExc>(out, out, tc.frame);
    stack.push(out);
    return true;
  }

  return false;
}

PyMethodDef* get_methoddef(PyObject* func) {
  if (Py_TYPE(func) == &PyMethodDescr_Type) {
    return ((PyMethodDescrObject*)func)->d_method;
  } else if (PyCFunction_Check(func)) {
    return ((PyCFunctionObject*)func)->m_ml;
  }
  return nullptr;
}

Type get_methoddef_ret_type(PyMethodDef* def) {
  _PyTypedMethodDef* typed_def = (_PyTypedMethodDef*)def->ml_meth;
  if (typed_def->tmd_ret & _Py_SIG_TYPE_PARAM) {
    return TObject;
  } else if (typed_def->tmd_ret != _Py_SIG_VOID) {
    return prim_type_to_type(_Py_SIG_TYPE_MASK(typed_def->tmd_ret));
  }

  return TBottom;
}

bool get_static_func_ret_type(PyObject* func, Type* ret_type) {
  if (PyFunction_Check(func)) {
    PyCodeObject* code = (PyCodeObject*)((PyFunctionObject*)func)->func_code;
    if (code->co_flags & CO_STATICALLY_COMPILED) {
      *ret_type = resolve_type_descr(
          _PyClassLoader_GetReturnTypeDescr((PyFunctionObject*)func));
      return true;
    }
  }

  PyMethodDef* def = get_methoddef(func);
  if (def != nullptr && def->ml_flags & METH_TYPED) {
    *ret_type = get_methoddef_ret_type(def);
    return true;
  }

  return false;
}

bool HIRBuilder::emitInvokeFunction(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr,
    bool is_awaited) {
  PyObject* descr = PyTuple_GET_ITEM(code_->co_consts, bc_instr.oparg());
  PyObject* target = PyTuple_GET_ITEM(descr, 0);
  long nargs = PyLong_AsLong(PyTuple_GET_ITEM(descr, 1));

  ThreadedCompileSerialize guard;

  PyObject* container;
  PyObject* func = _PyClassLoader_ResolveFunction(target, &container);
  JIT_CHECK(func != NULL, "unknown function %s", repr(target));

  Type ret_type = TObject;
  bool is_static_func = get_static_func_ret_type(func, &ret_type);

  Register* funcreg = temps_.Allocate();
  bool is_container_immutable = true;
  if (_PyClassLoader_IsImmutable(container)) {
    if (is_static_func && PyFunction_Check(func)) {
      if (_PyJIT_CompileFunction(reinterpret_cast<PyFunctionObject*>(func)) ==
          PYJIT_RESULT_RETRY) {
        JIT_LOG(
            "Warning: recursive compile of '%s' failed as it is already being "
            "compiled",
            funcFullname(reinterpret_cast<PyFunctionObject*>(func)));
      }

      // Direct invoke is safe whether we succeeded in JIT-compiling or not,
      // it'll just have an extra indirection if not JIT compiled.
      Register* out = temps_.Allocate();
      auto call = tc.emit<InvokeStaticFunction>(
          nargs, out, (PyFunctionObject*)func, ret_type);
      for (auto i = nargs - 1; i >= 0; i--) {
        Register* operand = tc.frame.stack.pop();
        call->SetOperand(i, operand);
      }
      call->setFrameState(tc.frame);

      tc.frame.stack.push(out);

      Py_DECREF(func);
      Py_XDECREF(container);
      return false;
    } else if (
        is_static_func &&
        tryEmitDirectMethodCall(get_methoddef(func), tc, nargs)) {
      return false;
    }

    tc.emit<LoadConst>(funcreg, Type::fromObject(func));
  } else {
    PyObject** funcptr = _PyClassLoader_GetIndirectPtr(target, func, container);
    JIT_CHECK(funcptr != NULL, "function lookup failed %s", repr(target));

    tc.emit<LoadFunctionIndirect>(funcptr, target, funcreg, tc.frame);
    // We can't invoke statically for indirect calls, we don't
    // know if they've been patched
    is_container_immutable = false;
  }

  Py_DECREF(func);
  Py_XDECREF(container);

  auto arg_regs = std::vector<Register*>(nargs, nullptr);

  for (auto i = nargs - 1; i >= 0; i--) {
    arg_regs[i] = tc.frame.stack.pop();
  }

  // If we have a static func but we couldn't emit a direct static call, we have
  // to box any primitive args
  if (is_static_func) {
    if (PyFunction_Check(func)) {
      auto prim_args_info =
          Ref<_PyTypedArgsInfo>::steal(_PyClassLoader_GetTypedArgsInfo(
              (PyCodeObject*)((PyFunctionObject*)func)->func_code, 1));

      for (Py_ssize_t i = 0; i < Py_SIZE(prim_args_info.get()); i++) {
        int argnum = prim_args_info->tai_args[i].tai_argnum;
        Register* reg = arg_regs.at(argnum);
        tc.emit<PrimitiveBox>(
            reg, reg, prim_args_info->tai_args[i].tai_primitive_type);
      }
    } else {
      PyMethodDef* method = get_methoddef(func);
      JIT_DCHECK(method->ml_flags & METH_TYPED, "expected type method def");
      _PyTypedMethodDef* def = (_PyTypedMethodDef*)method->ml_meth;
      for (Py_ssize_t i = 0; def->tmd_sig[i] != NULL; i++) {
        const _Py_SigElement* elem = def->tmd_sig[i];
        if (elem->se_argtype & _Py_SIG_TYPE_PARAM) {
          // all type parameters are currently reference types
          continue;
        }

        Type t = prim_type_to_type(_Py_SIG_TYPE_MASK(elem->se_argtype));
        if (t <= TInternal) {
          Register* reg = arg_regs.at(i);
          tc.emit<PrimitiveBox>(reg, reg, _Py_SIG_TYPE_MASK(elem->se_argtype));
        }
      }
    }
  }

  Register* out = temps_.Allocate();
  VectorCallBase* call;
  if (is_container_immutable) {
    call = tc.emit<VectorCallStatic>(nargs + 1, out, is_awaited);
  } else {
    call = tc.emit<VectorCall>(nargs + 1, out, is_awaited);
  }
  for (auto i = 0; i < nargs; i++) {
    call->SetOperand(i + 1, arg_regs.at(i));
  }
  call->SetOperand(0, funcreg);
  call->setFrameState(tc.frame);

  // Since we are not doing a direct invoke, we will get a boxed int back; if
  // the function is supposed to return a primitive int, we need to unbox it
  // because later code in the function will expect the primitive.
  if (ret_type <= TInternal) {
    tc.emit<PrimitiveUnbox>(out, out, ret_type);
  }

  tc.frame.stack.push(out);

  return true;
}

void HIRBuilder::emitCompareOp(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& stack = tc.frame.stack;
  Register* right = stack.pop();
  Register* left = stack.pop();
  Register* result = temps_.Allocate();
  CompareOp op = static_cast<CompareOp>(bc_instr.oparg());
  tc.emit<Compare>(op, result, left, right, tc.frame);
  stack.push(result);
}

void HIRBuilder::emitJumpIf(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* var = tc.frame.stack.top();

  Py_ssize_t true_offset, false_offset;
  bool check_truthy = true;
  switch (bc_instr.opcode()) {
    case JUMP_IF_NONZERO_OR_POP:
      check_truthy = false;
    case JUMP_IF_TRUE_OR_POP: {
      true_offset = bc_instr.oparg();
      false_offset = bc_instr.NextInstrOffset();
      break;
    }
    case JUMP_IF_ZERO_OR_POP:
      check_truthy = false;
    case JUMP_IF_FALSE_OR_POP: {
      false_offset = bc_instr.oparg();
      true_offset = bc_instr.NextInstrOffset();
      break;
    }
    default: {
      // NOTREACHED
      JIT_CHECK(
          false,
          "trying to translate non-jump-if bytecode: %d",
          bc_instr.opcode());
      break;
    }
  }

  BasicBlock* true_block = getBlockAtOff(true_offset);
  BasicBlock* false_block = getBlockAtOff(false_offset);

  if (check_truthy) {
    Register* tval = temps_.allocateNextTruthy();
    // Registers that hold the result of `IsTruthy` are guaranteed to never be
    // the home of a value left on the stack at the end of a basic block, so we
    // don't need to worry about potentially storing a PyObject in them.
    tc.emit<IsTruthy>(tval, var, tc.frame);
    tc.emit<CondBranch>(tval, true_block, false_block);
  } else {
    tc.emit<CondBranch>(var, true_block, false_block);
  }
}

void HIRBuilder::emitLoadAttr(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* receiver = tc.frame.stack.pop();
  Register* result = temps_.Allocate();
  tc.emit<LoadAttr>(result, receiver, bc_instr.oparg(), tc.frame);
  tc.frame.stack.push(result);
}

void HIRBuilder::emitLoadMethod(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* receiver = tc.frame.stack.top();
  Register* result = temps_.Allocate();
  tc.emit<LoadMethod>(result, receiver, bc_instr.oparg(), tc.frame);
  tc.frame.stack.push(result);
}

void HIRBuilder::emitLoadMethodOrAttrSuper(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr,
    bool load_method) {
  Register* receiver = tc.frame.stack.pop();
  Register* type = tc.frame.stack.pop();
  Register* global_super = tc.frame.stack.pop();
  Register* result = temps_.Allocate();
  PyObject* oparg = PyTuple_GET_ITEM(code_->co_consts, bc_instr.oparg());
  int name_idx = PyLong_AsLong(PyTuple_GET_ITEM(oparg, 0));
  bool no_args_in_super_call = PyTuple_GET_ITEM(oparg, 1) == Py_True;
  if (load_method) {
    tc.frame.stack.push(receiver);
    tc.emit<LoadMethodSuper>(
        result,
        global_super,
        type,
        receiver,
        name_idx,
        no_args_in_super_call,
        tc.frame);
  } else {
    tc.emit<LoadAttrSuper>(
        result,
        global_super,
        type,
        receiver,
        name_idx,
        no_args_in_super_call,
        tc.frame);
  }
  tc.frame.stack.push(result);
}

void HIRBuilder::emitLoadDeref(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  int idx = bc_instr.oparg();
  Register* src = tc.frame.cells[idx];
  Register* dst = temps_.Allocate();
  auto frame_idx = tc.frame.locals.size() + idx;
  tc.emit<LoadCellItem>(dst, src);
  tc.emit<CheckVar>(dst, dst, frame_idx, tc.frame);
  tc.frame.stack.push(dst);
}

void HIRBuilder::emitStoreDeref(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* old = temps_.Allocate();
  Register* dst = tc.frame.cells[bc_instr.oparg()];
  Register* src = tc.frame.stack.pop();
  tc.emit<StealCellItem>(old, dst);
  tc.emit<SetCellItem>(dst, src, old);
}

void HIRBuilder::emitLoadConst(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* tmp = temps_.Allocate();
  JIT_CHECK(
      bc_instr.oparg() < PyTuple_Size(code_->co_consts),
      "LOAD_CONST index out of bounds");
  tc.emit<LoadConst>(
      tmp,
      Type::fromObject(PyTuple_GET_ITEM(code_->co_consts, bc_instr.oparg())));
  tc.frame.stack.push(tmp);
}

void HIRBuilder::emitLoadFast(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  int var_idx = bc_instr.oparg();
  Register* var = tc.frame.locals[var_idx];
  tc.emit<CheckVar>(var, var, var_idx, tc.frame);
  tc.frame.stack.push(var);
}

void HIRBuilder::emitLoadLocal(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  PyObject* index_and_descr =
      PyTuple_GET_ITEM(code_->co_consts, bc_instr.oparg());
  int index = PyLong_AsLong(PyTuple_GET_ITEM(index_and_descr, 0));

  auto var = tc.frame.locals[index];
  tc.frame.stack.push(var);
}

void HIRBuilder::emitStoreLocal(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* src = tc.frame.stack.pop();
  PyObject* index_and_descr =
      PyTuple_GET_ITEM(code_->co_consts, bc_instr.oparg());
  int index = PyLong_AsLong(PyTuple_GET_ITEM(index_and_descr, 0));
  auto dst = tc.frame.locals[index];
  moveOverwrittenStackRegisters(tc, dst);
  tc.emit<Assign>(dst, src);
}

void HIRBuilder::emitConvertPrimitive(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* val = tc.frame.stack.pop();
  Register* out = temps_.Allocate();
  Type to_type = prim_type_to_type(bc_instr.oparg() >> 4);
  tc.emit<IntConvert>(out, val, to_type);
  tc.frame.stack.push(out);
}

void HIRBuilder::emitPrimitiveLoadConst(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* tmp = temps_.Allocate();
  int index = bc_instr.oparg();
  JIT_CHECK(
      index < PyTuple_Size(code_->co_consts),
      "PRIMITIVE_LOAD_CONST index out of bounds");
  PyObject* num_and_type = PyTuple_GET_ITEM(code_->co_consts, index);
  JIT_CHECK(
      PyTuple_Size(num_and_type) == 2,
      "wrong size for PRIMITIVE_LOAD_CONST arg tuple")
  PyObject* num = PyTuple_GET_ITEM(num_and_type, 0);
  Type size =
      prim_type_to_type(PyLong_AsSsize_t(PyTuple_GET_ITEM(num_and_type, 1)));
  Type type = TBottom;
  if (size == TCDouble) {
    type = Type::fromCDouble(PyFloat_AsDouble(num));
  } else if (size <= TCBool) {
    type = Type::fromCBool(num == Py_True);
  } else {
    type = (size <= TCUnsigned)
        ? Type::fromCUInt(PyLong_AsUnsignedLong(num), size)
        : Type::fromCInt(PyLong_AsLong(num), size);
  }
  tc.emit<LoadConst>(tmp, type);
  tc.frame.stack.push(tmp);
}

void HIRBuilder::emitIntLoadConstOld(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* tmp = temps_.Allocate();
  tc.emit<LoadConst>(tmp, Type::fromCInt(bc_instr.oparg(), TCInt64));
  tc.frame.stack.push(tmp);
}

void HIRBuilder::emitPrimitiveBox(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* tmp = temps_.Allocate();
  Register* src = tc.frame.stack.pop();
  tc.emitChecked<PrimitiveBox>(tmp, src, bc_instr.oparg());
  tc.frame.stack.push(tmp);
}

void HIRBuilder::emitPrimitiveUnbox(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* tmp = temps_.Allocate();
  Register* src = tc.frame.stack.pop();
  tc.emit<PrimitiveUnbox>(tmp, src, prim_type_to_type(bc_instr.oparg()));
  auto did_unbox_work = temps_.Allocate();
  tc.emit<IsNegativeAndErrOccurred>(did_unbox_work, tmp, tc.frame);
  tc.frame.stack.push(tmp);
}

static inline BinaryOpKind get_primitive_bin_op_kind(
    const jit::BytecodeInstruction& bc_instr) {
  switch (bc_instr.oparg()) {
    case PRIM_OP_ADD_DBL:
    case PRIM_OP_ADD_INT: {
      return BinaryOpKind::kAdd;
    }
    case PRIM_OP_AND_INT: {
      return BinaryOpKind::kAnd;
    }
    case PRIM_OP_DIV_INT: {
      return BinaryOpKind::kFloorDivide;
    }
    case PRIM_OP_DIV_UN_INT: {
      return BinaryOpKind::kFloorDivideUnsigned;
    }
    case PRIM_OP_LSHIFT_INT: {
      return BinaryOpKind::kLShift;
    }
    case PRIM_OP_MOD_INT: {
      return BinaryOpKind::kModulo;
    }
    case PRIM_OP_MOD_UN_INT: {
      return BinaryOpKind::kModuloUnsigned;
    }
    case PRIM_OP_MUL_DBL:
    case PRIM_OP_MUL_INT: {
      return BinaryOpKind::kMultiply;
    }
    case PRIM_OP_OR_INT: {
      return BinaryOpKind::kOr;
    }
    case PRIM_OP_RSHIFT_INT: {
      return BinaryOpKind::kRShift;
    }
    case PRIM_OP_RSHIFT_UN_INT: {
      return BinaryOpKind::kRShiftUnsigned;
    }
    case PRIM_OP_SUB_DBL:
    case PRIM_OP_SUB_INT: {
      return BinaryOpKind::kSubtract;
    }
    case PRIM_OP_XOR_INT: {
      return BinaryOpKind::kXor;
    }
    case PRIM_OP_DIV_DBL: {
      return BinaryOpKind::kTrueDivide;
    }
    default: {
      JIT_CHECK(false, "unhandled binary op %d", bc_instr.oparg());
      // NOTREACHED
      break;
    }
  }
}

static inline bool is_double_binop(int oparg) {
  switch (oparg) {
    case PRIM_OP_ADD_INT:
    case PRIM_OP_AND_INT:
    case PRIM_OP_DIV_INT:
    case PRIM_OP_DIV_UN_INT:
    case PRIM_OP_LSHIFT_INT:
    case PRIM_OP_MOD_INT:
    case PRIM_OP_MOD_UN_INT:
    case PRIM_OP_MUL_INT:
    case PRIM_OP_OR_INT:
    case PRIM_OP_RSHIFT_INT:
    case PRIM_OP_RSHIFT_UN_INT:
    case PRIM_OP_SUB_INT:
    case PRIM_OP_XOR_INT: {
      return false;
    }
    case PRIM_OP_ADD_DBL:
    case PRIM_OP_SUB_DBL:
    case PRIM_OP_DIV_DBL:
    case PRIM_OP_MUL_DBL: {
      return true;
    }
    default: {
      JIT_CHECK(false, "Invalid binary op %d", oparg);
      // NOTREACHED
      break;
    }
  }
}

static inline Type element_type_from_seq_type(int seq_type) {
  switch (seq_type) {
    case SEQ_ARRAY_INT8:
      return TCInt8;
    case SEQ_ARRAY_INT16:
      return TCInt16;
    case SEQ_ARRAY_INT32:
      return TCInt32;
    case SEQ_ARRAY_INT64:
      return TCInt64;
    case SEQ_ARRAY_UINT8:
      return TCUInt8;
    case SEQ_ARRAY_UINT16:
      return TCUInt16;
    case SEQ_ARRAY_UINT32:
      return TCUInt32;
    case SEQ_ARRAY_UINT64:
      return TCUInt64;
    case SEQ_LIST:
    case SEQ_LIST_INEXACT:
    case SEQ_TUPLE:
      return TObject;
    default:
      JIT_CHECK(false, "invalid sequence type: (%d)", seq_type);
      // NOTREACHED
      break;
  }
}

void HIRBuilder::emitPrimitiveBinaryOp(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& stack = tc.frame.stack;
  Register* right = stack.pop();
  Register* left = stack.pop();
  Register* result = temps_.Allocate();

  BinaryOpKind op_kind = get_primitive_bin_op_kind(bc_instr);

  if (is_double_binop(bc_instr.oparg())) {
    tc.emit<DoubleBinaryOp>(op_kind, result, left, right);
  } else {
    tc.emit<IntBinaryOp>(op_kind, result, left, right);
  }

  stack.push(result);
}

void HIRBuilder::emitIntCompare(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& stack = tc.frame.stack;
  Register* right = stack.pop();
  Register* left = stack.pop();
  Register* result = temps_.Allocate();
  IntCompareOp op;
  switch (bc_instr.oparg()) {
    case PRIM_OP_EQ_INT:
      op = IntCompareOp::kEqual;
      break;
    case PRIM_OP_NE_INT:
      op = IntCompareOp::kNotEqual;
      break;
    case PRIM_OP_LT_INT:
      op = IntCompareOp::kLessThan;
      break;
    case PRIM_OP_LE_INT:
      op = IntCompareOp::kLessThanEqual;
      break;
    case PRIM_OP_GT_INT:
      op = IntCompareOp::kGreaterThan;
      break;
    case PRIM_OP_GE_INT:
      op = IntCompareOp::kGreaterThanEqual;
      break;
    case PRIM_OP_LT_UN_INT:
      op = IntCompareOp::kLessThanUnsigned;
      break;
    case PRIM_OP_LE_UN_INT:
      op = IntCompareOp::kLessThanEqualUnsigned;
      break;
    case PRIM_OP_GT_UN_INT:
      op = IntCompareOp::kGreaterThanUnsigned;
      break;
    case PRIM_OP_GE_UN_INT:
      op = IntCompareOp::kGreaterThanEqualUnsigned;
      break;
    default:
      JIT_CHECK(false, "unsupported comparison");
      break;
  }
  tc.emit<IntCompare>(op, result, left, right);
  stack.push(result);
}

void HIRBuilder::emitPrimitiveUnaryOp(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* value = tc.frame.stack.pop();
  Register* result = temps_.Allocate();
  PrimitiveUnaryOpKind op;
  switch (bc_instr.oparg()) {
    case PRIM_OP_NEG_INT:
      op = PrimitiveUnaryOpKind::kNegateInt;
      break;
    case PRIM_OP_INV_INT:
      op = PrimitiveUnaryOpKind::kInvertInt;
      break;
    default:
      JIT_CHECK(false, "unsupported unary op");
      break;
  }
  tc.emit<PrimitiveUnaryOp>(op, result, value);
  tc.frame.stack.push(result);
}

void HIRBuilder::emitFastLen(
    CFG& cfg,
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto result = temps_.Allocate();
  Register* collection;
  auto oparg = bc_instr.oparg();
  int inexact = oparg & FAST_LEN_INEXACT;
  std::size_t offset = 0;
  auto type = TBottom;

  oparg &= ~FAST_LEN_INEXACT;
  if (oparg == FAST_LEN_LIST) {
    type = TListExact;
    offset = GET_STRUCT_MEMBER_OFFSET(PyVarObject, ob_size);
  } else if (oparg == FAST_LEN_TUPLE) {
    type = TTupleExact;
    offset = GET_STRUCT_MEMBER_OFFSET(PyVarObject, ob_size);
  } else if (oparg == FAST_LEN_ARRAY) {
    type = TArrayExact;
    offset = GET_STRUCT_MEMBER_OFFSET(PyVarObject, ob_size);
  } else if (oparg == FAST_LEN_DICT) {
    type = TDictExact;
    offset = GET_STRUCT_MEMBER_OFFSET(PyDictObject, ma_used);
  } else if (oparg == FAST_LEN_SET) {
    type = TSetExact;
    offset = GET_STRUCT_MEMBER_OFFSET(PySetObject, used);
  } else if (oparg == FAST_LEN_STR) {
    type = TUnicodeExact;
    // Note: In debug mode, the interpreter has an assert that
    // ensures the string is "ready", check PyUnicode_GET_LENGTH
    offset = GET_STRUCT_MEMBER_OFFSET(PyASCIIObject, length);
  }
  JIT_CHECK(offset > 0, "Bad oparg for FAST_LEN");

  if (inexact) {
    TranslationContext deopt_path{cfg.AllocateBlock(), tc.frame};
    deopt_path.frame.next_instr_offset = bc_instr.offset();
    deopt_path.snapshot();
    deopt_path.emit<Deopt>();
    collection = tc.frame.stack.pop();
    BasicBlock* fast_path = cfg.AllocateBlock();
    tc.emit<CondBranchCheckType>(collection, type, fast_path, deopt_path.block);
    tc.block = fast_path;
  } else {
    collection = tc.frame.stack.pop();
  }

  tc.emit<LoadField>(result, collection, offset, TCInt64);
  tc.frame.stack.push(result);
}

void HIRBuilder::emitRefineType(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  int oparg = bc_instr.oparg();
  JIT_CHECK(
      oparg < PyTuple_Size(code_->co_consts),
      "REFINE_TYPE index out of bounds");
  PyObject* type_descr = PyTuple_GET_ITEM(code_->co_consts, oparg);
  int optional;
  auto pytype = THREADED_COMPILE_SERIALIZED_CALL(Ref<PyTypeObject>::steal(
      _PyClassLoader_ResolveType(type_descr, &optional)));
  Type type = Type::fromType(pytype);
  if (optional) {
    type |= TNoneType;
  }
  Register* dst = tc.frame.stack.top();
  tc.emit<RefineType>(type, dst, dst);
}

void HIRBuilder::emitSequenceGet(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& stack = tc.frame.stack;
  auto idx = stack.pop();
  auto sequence = stack.pop();
  auto oparg = bc_instr.oparg();
  if (oparg == SEQ_LIST_INEXACT) {
    auto type = temps_.Allocate();
    tc.emit<LoadField>(type, sequence, offsetof(PyObject, ob_type), TType);
    tc.emit<GuardIs>((PyObject*)&PyList_Type, type, type);
    tc.emit<RefineType>(TListExact, sequence, sequence);
  }

  Register* adjusted_idx;
  int unchecked = oparg & SEQ_SUBSCR_UNCHECKED;
  if (!unchecked) {
    adjusted_idx = temps_.Allocate();
    tc.emit<CheckSequenceBounds>(adjusted_idx, sequence, idx, tc.frame);
  } else {
    adjusted_idx = idx;
    oparg &= ~SEQ_SUBSCR_UNCHECKED;
  }
  auto ob_item = temps_.Allocate();
  auto result = temps_.Allocate();
  int offset;
  if (_Py_IS_TYPED_ARRAY(oparg)) {
    offset = GET_STRUCT_MEMBER_OFFSET(PyStaticArrayObject, ob_item);
  } else if (oparg == SEQ_LIST || oparg == SEQ_LIST_INEXACT) {
    offset = GET_STRUCT_MEMBER_OFFSET(PyListObject, ob_item);
  } else {
    JIT_CHECK(false, "Unsupported oparg for SEQUENCE_GET: %d", oparg);
  }
  tc.emit<LoadField>(ob_item, sequence, offset, TCPtr);

  auto type = element_type_from_seq_type(oparg);
  tc.emit<LoadArrayItem>(result, ob_item, adjusted_idx, sequence, type);
  stack.push(result);
}

void HIRBuilder::emitSequenceRepeat(
    CFG& cfg,
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& stack = tc.frame.stack;
  Register* num;
  Register* seq;
  auto result = temps_.Allocate();
  int oparg = bc_instr.oparg();
  int seq_inexact = oparg & SEQ_REPEAT_INEXACT_SEQ;
  int num_inexact = oparg & SEQ_REPEAT_INEXACT_NUM;
  int primitive_num = oparg & SEQ_REPEAT_PRIMITIVE_NUM;
  oparg &= ~SEQ_REPEAT_FLAGS;

  JIT_DCHECK(
      oparg == SEQ_LIST || oparg == SEQ_TUPLE,
      "Bad oparg for SEQUENCE_REPEAT: %d",
      oparg);

  if (seq_inexact || num_inexact) {
    TranslationContext deopt_path{cfg.AllocateBlock(), tc.frame};
    deopt_path.frame.next_instr_offset = bc_instr.offset();
    deopt_path.snapshot();
    deopt_path.emit<Deopt>();
    // Stack pops must wait until after we snapshot, so if we deopt they are
    // still on stack.
    num = stack.pop();
    if (num_inexact) {
      BasicBlock* fast_path = cfg.AllocateBlock();
      tc.emit<CondBranchCheckType>(
          num, TLongExact, fast_path, deopt_path.block);
      tc.block = fast_path;
    }
    seq = stack.pop();
    if (seq_inexact) {
      BasicBlock* fast_path = cfg.AllocateBlock();
      tc.emit<CondBranchCheckType>(
          seq,
          (oparg == SEQ_LIST) ? TListExact : TTupleExact,
          fast_path,
          deopt_path.block);
      tc.block = fast_path;
    }
  } else {
    num = stack.pop();
    seq = stack.pop();
  }

  if (!primitive_num) {
    auto unboxed_num = temps_.Allocate();
    tc.emit<PrimitiveUnbox>(unboxed_num, num, TCInt64);
    num = unboxed_num;
  }

  if (oparg == SEQ_LIST) {
    tc.emit<RepeatList>(result, seq, num);
  } else {
    tc.emit<RepeatTuple>(result, seq, num);
  }

  stack.push(result);
}

void HIRBuilder::emitSequenceSet(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& stack = tc.frame.stack;
  auto idx = stack.pop();
  auto sequence = stack.pop();
  auto value = stack.pop();
  auto adjusted_idx = temps_.Allocate();
  auto oparg = bc_instr.oparg();
  if (oparg == SEQ_LIST_INEXACT) {
    auto type = temps_.Allocate();
    tc.emit<LoadField>(type, sequence, offsetof(PyObject, ob_type), TType);
    tc.emit<GuardIs>((PyObject*)&PyList_Type, type, type);
    tc.emit<RefineType>(TListExact, sequence, sequence);
  }
  tc.emit<CheckSequenceBounds>(adjusted_idx, sequence, idx, tc.frame);
  auto ob_item = temps_.Allocate();
  int offset;
  if (_Py_IS_TYPED_ARRAY(oparg)) {
    offset = GET_STRUCT_MEMBER_OFFSET(PyStaticArrayObject, ob_item);
  } else if (oparg == SEQ_LIST || oparg == SEQ_LIST_INEXACT) {
    offset = GET_STRUCT_MEMBER_OFFSET(PyListObject, ob_item);
  } else {
    JIT_CHECK(false, "Unsupported oparg for SEQUENCE_SET: %d", oparg);
  }
  tc.emit<LoadField>(ob_item, sequence, offset, TCPtr);
  tc.emit<StoreArrayItem>(
      ob_item,
      adjusted_idx,
      value,
      sequence,
      element_type_from_seq_type(oparg));
}

void HIRBuilder::emitLoadGlobal(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto name_idx = bc_instr.oparg();
  Register* result = temps_.Allocate();

  auto try_fast_path = [&] {
    if (!_PyDict_CanWatch(builtins_) || !_PyDict_CanWatch(globals_)) {
      return false;
    }
    PyObject* value = THREADED_COMPILE_SERIALIZED_CALL(
        loadGlobal(globals_, builtins_, code_->co_names, name_idx));
    if (value == nullptr) {
      return false;
    }
    tc.emit<LoadGlobalCached>(result, name_idx);
    auto guard_is = tc.emit<GuardIs>(value, result, result);
    BorrowedRef<> name = PyTuple_GET_ITEM(code_->co_names, name_idx);
    guard_is->setDescr(fmt::format("LOAD_GLOBAL: {}", PyUnicode_AsUTF8(name)));
    return true;
  };

  if (!try_fast_path()) {
    tc.emit<LoadGlobal>(result, name_idx, tc.frame);
  }

  tc.frame.stack.push(result);
}

void HIRBuilder::emitMakeFunction(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  int oparg = bc_instr.oparg();
  Register* func = temps_.Allocate();
  Register* qualname = tc.frame.stack.pop();
  Register* codeobj = tc.frame.stack.pop();

  // make a function
  tc.emit<MakeFunction>(func, qualname, codeobj, tc.frame);

  if (oparg & 0x08) {
    Register* closure = tc.frame.stack.pop();
    tc.emit<SetFunctionAttr>(closure, func, FunctionAttr::kClosure);
  }
  if (oparg & 0x04) {
    Register* annotations = tc.frame.stack.pop();
    tc.emit<SetFunctionAttr>(annotations, func, FunctionAttr::kAnnotations);
  }
  if (oparg & 0x02) {
    Register* kwdefaults = tc.frame.stack.pop();
    tc.emit<SetFunctionAttr>(kwdefaults, func, FunctionAttr::kKwDefaults);
  }
  if (oparg & 0x01) {
    Register* defaults = tc.frame.stack.pop();
    tc.emit<SetFunctionAttr>(defaults, func, FunctionAttr::kDefaults);
  }

  tc.emit<InitFunction>(func);
  tc.frame.stack.push(func);
}

void HIRBuilder::emitMakeListTuple(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  bool is_tuple = (bc_instr.opcode() == BUILD_TUPLE);
  auto num_elems = static_cast<size_t>(bc_instr.oparg());
  auto dst = temps_.Allocate();
  tc.emit<MakeListTuple>(is_tuple, dst, num_elems, tc.frame);
  auto init_lt = tc.emit<InitListTuple>(num_elems + 1, is_tuple);
  init_lt->SetOperand(0, dst);
  for (size_t i = num_elems; i > 0; i--) {
    auto opnd = tc.frame.stack.pop();
    init_lt->SetOperand(i, opnd);
  }
  auto new_dst = temps_.Allocate();
  tc.emit<Assign>(new_dst, dst);
  tc.frame.stack.push(new_dst);
}

void HIRBuilder::emitMakeListTupleUnpack(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* list = temps_.Allocate();
  tc.emit<MakeListTuple>(false, list, 0, tc.frame);

  bool with_call = bc_instr.opcode() == BUILD_TUPLE_UNPACK_WITH_CALL;
  int oparg = bc_instr.oparg();
  Register* func =
      with_call ? tc.frame.stack.peek(oparg + 1) : temps_.Allocate();

  for (int i = oparg; i > 0; i--) {
    Register* iterable = tc.frame.stack.peek(i);
    Register* none = temps_.Allocate();
    tc.emit<ListExtend>(none, list, iterable, func, tc.frame);
  }

  Register* retval = list;
  bool is_tuple = bc_instr.opcode() != BUILD_LIST_UNPACK;
  if (is_tuple) {
    Register* tuple = temps_.Allocate();
    tc.emit<MakeTupleFromList>(tuple, list, tc.frame);
    retval = tuple;
  }

  tc.frame.stack.discard(oparg);
  tc.frame.stack.push(retval);
}

void HIRBuilder::emitBuildMap(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto dict_size = bc_instr.oparg();
  Register* dict = temps_.Allocate();
  tc.emit<MakeDict>(dict, dict_size, tc.frame);
  // Fill dict
  auto& stack = tc.frame.stack;
  for (auto i = stack.size() - dict_size * 2, end = stack.size(); i < end;
       i += 2) {
    auto key = stack.at(i);
    auto value = stack.at(i + 1);
    auto result = temps_.Allocate();
    tc.emit<SetDictItem>(result, dict, key, value, tc.frame);
  }
  stack.discard(dict_size * 2);
  stack.push(dict);
}

void HIRBuilder::emitBuildMapUnpack(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr,
    bool with_call) {
  Register* sum = temps_.Allocate();
  tc.emit<MakeDict>(sum, 0, tc.frame);

  int oparg = bc_instr.oparg();
  auto& stack = tc.frame.stack;
  Register* func = with_call ? stack.peek(oparg + 2) : temps_.Allocate();

  for (int i = oparg; i > 0; i--) {
    auto arg = stack.peek(i);
    auto result = temps_.Allocate();
    tc.emit<MergeDictUnpack>(result, sum, arg, func, tc.frame);
  }

  stack.discard(oparg);
  stack.push(sum);
}

void HIRBuilder::emitBuildSet(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* set = temps_.Allocate();
  tc.emit<MakeSet>(set, tc.frame);

  int oparg = bc_instr.oparg();
  for (int i = oparg; i > 0; i--) {
    auto item = tc.frame.stack.peek(i);

    auto result = temps_.Allocate();
    tc.emit<SetSetItem>(result, set, item, tc.frame);
  }

  tc.frame.stack.discard(oparg);

  tc.frame.stack.push(set);
}

void HIRBuilder::emitBuildSetUnpack(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* set = temps_.Allocate();
  tc.emit<MakeSet>(set, tc.frame);

  int oparg = bc_instr.oparg();
  for (int i = oparg; i > 0; i--) {
    auto iterable = tc.frame.stack.peek(i);

    auto result = temps_.Allocate();
    tc.emit<MergeSetUnpack>(result, set, iterable, tc.frame);
  }

  tc.frame.stack.discard(oparg);
  tc.frame.stack.push(set);
}

void HIRBuilder::emitBuildConstKeyMap(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto dict_size = bc_instr.oparg();
  Register* dict = temps_.Allocate();
  tc.emit<MakeDict>(dict, dict_size, tc.frame);
  // Fill dict
  auto& stack = tc.frame.stack;
  Register* keys = stack.pop();
  // ceval.c checks the type and size of the keys tuple before proceeding; we
  // intentionally skip that here.
  for (auto i = 0; i < dict_size; ++i) {
    Register* key = temps_.Allocate();
    tc.emit<LoadTupleItem>(key, keys, i);
    Register* value = stack.at(stack.size() - dict_size + i);
    Register* result = temps_.Allocate();
    tc.emit<SetDictItem>(result, dict, key, value, tc.frame);
  }
  stack.discard(dict_size);
  stack.push(dict);
}

void HIRBuilder::emitPopJumpIf(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* var = tc.frame.stack.pop();
  Py_ssize_t true_offset, false_offset;
  switch (bc_instr.opcode()) {
    case POP_JUMP_IF_ZERO:
    case POP_JUMP_IF_FALSE: {
      true_offset = bc_instr.NextInstrOffset();
      false_offset = bc_instr.oparg();
      break;
    }
    case POP_JUMP_IF_NONZERO:
    case POP_JUMP_IF_TRUE: {
      true_offset = bc_instr.oparg();
      false_offset = bc_instr.NextInstrOffset();
      break;
    }
    default: {
      // NOTREACHED
      JIT_CHECK(
          false,
          "trying to translate non pop-jump bytecode: %d",
          bc_instr.opcode());
      break;
    }
  }

  BasicBlock* true_block = getBlockAtOff(true_offset);
  BasicBlock* false_block = getBlockAtOff(false_offset);

  if (bc_instr.opcode() == POP_JUMP_IF_FALSE ||
      bc_instr.opcode() == POP_JUMP_IF_TRUE) {
    Register* tval = temps_.allocateNextTruthy();
    tc.emit<IsTruthy>(tval, var, tc.frame);
    tc.emit<CondBranch>(tval, true_block, false_block);
  } else {
    tc.emit<CondBranch>(var, true_block, false_block);
  }
}

void HIRBuilder::emitStoreAttr(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* receiver = tc.frame.stack.pop();
  Register* value = tc.frame.stack.pop();
  Register* result = temps_.Allocate();
  tc.emit<StoreAttr>(result, receiver, bc_instr.oparg(), value, tc.frame);
}

void HIRBuilder::moveOverwrittenStackRegisters(
    TranslationContext& tc,
    Register* dst) {
  // If we're about to overwrite a register that is on the stack, move it to a
  // new register.
  Register* tmp = nullptr;
  auto& stack = tc.frame.stack;
  for (std::size_t i = 0, stack_size = stack.size(); i < stack_size; i++) {
    if (stack.at(i) == dst) {
      if (tmp == nullptr) {
        tmp = temps_.Allocate();
        tc.emit<Assign>(tmp, dst);
      }
      stack.atPut(i, tmp);
    }
  }
}
void HIRBuilder::emitStoreFast(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* src = tc.frame.stack.pop();
  Register* dst = tc.frame.locals[bc_instr.oparg()];
  JIT_DCHECK(dst != nullptr, "no register");
  moveOverwrittenStackRegisters(tc, dst);
  tc.emit<Assign>(dst, src);
}

void HIRBuilder::emitStoreSubscr(TranslationContext& tc) {
  auto& stack = tc.frame.stack;
  Register* sub = stack.pop();
  Register* container = stack.pop();
  Register* value = stack.pop();
  Register* result = temps_.Allocate();
  tc.emit<StoreSubscr>(result, container, sub, value, tc.frame);
}

void HIRBuilder::emitGetIter(TranslationContext& tc) {
  Register* iterable = tc.frame.stack.pop();
  Register* result = temps_.Allocate();
  tc.emit<GetIter>(result, iterable, tc.frame);
  tc.frame.stack.push(result);
}

void HIRBuilder::emitForIter(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* iterator = tc.frame.stack.top();
  Register* next_val = temps_.Allocate();
  tc.emit<InvokeIterNext>(next_val, iterator, tc.frame);
  tc.frame.stack.push(next_val);
  BasicBlock* footer = getBlockAtOff(bc_instr.GetJumpTarget());
  BasicBlock* body = getBlockAtOff(bc_instr.NextInstrOffset());
  tc.emit<CondBranchIterNotDone>(next_val, body, footer);
}

void HIRBuilder::emitGetYieldFromIter(CFG& cfg, TranslationContext& tc) {
  Register* iter_in = tc.frame.stack.pop();

  bool in_coro = code_->co_flags & (CO_COROUTINE | CO_ITERABLE_COROUTINE);
  BasicBlock* done_block = cfg.AllocateBlock();
  BasicBlock* next_block = cfg.AllocateBlock();
  BasicBlock* nop_block = cfg.AllocateBlock();
  BasicBlock* is_coro_block = in_coro ? nop_block : cfg.AllocateBlock();

  tc.emit<CondBranchCheckType>(
      iter_in, Type::fromTypeExact(&PyCoro_Type), is_coro_block, next_block);

  if (!in_coro) {
    tc.block = is_coro_block;
    tc.emit<RaiseStatic>(
        0,
        PyExc_TypeError,
        "cannot 'yield from' a coroutine object in a non-coroutine generator",
        tc.frame);
  }

  tc.block = next_block;

  BasicBlock* slow_path = cfg.AllocateBlock();
  Register* iter_out = temps_.Allocate();
  tc.emit<CondBranchCheckType>(iter_in, TGen, nop_block, slow_path);

  tc.block = slow_path;
  tc.emit<GetIter>(iter_out, iter_in, tc.frame);
  tc.emit<Branch>(done_block);

  tc.block = nop_block;
  tc.emit<Assign>(iter_out, iter_in);
  tc.emit<Branch>(done_block);

  tc.block = done_block;
  tc.frame.stack.push(iter_out);
}

void HIRBuilder::emitUnpackEx(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  int oparg = bc_instr.oparg();
  int arg_before = oparg & 0xff;
  int arg_after = oparg >> 8;

  auto& stack = tc.frame.stack;
  Register* seq = stack.pop();

  Register* tuple = temps_.Allocate();
  tc.emit<UnpackExToTuple>(tuple, seq, arg_before, arg_after, tc.frame);

  int total_args = arg_before + arg_after + 1;
  for (int i = total_args - 1; i >= 0; i--) {
    Register* item = temps_.Allocate();
    tc.emit<LoadTupleItem>(item, tuple, i);
    stack.push(item);
  }
}

void HIRBuilder::emitUnpackSequence(
    CFG& cfg,
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& stack = tc.frame.stack;
  Register* seq = stack.top();

  TranslationContext deopt_path{cfg.AllocateBlock(), tc.frame};
  deopt_path.frame.next_instr_offset = bc_instr.offset();
  deopt_path.snapshot();
  Deopt* deopt = deopt_path.emit<Deopt>();
  deopt->setGuiltyReg(seq);
  deopt->setDescr("UNPACK_SEQUENCE");

  BasicBlock* fast_path = cfg.AllocateBlock();
  stack.pop();
  tc.emit<CondBranchCheckType>(seq, TTupleExact, fast_path, deopt_path.block);
  tc.block = fast_path;

  Register* seq_size = temps_.Allocate();
  Register* target_size = temps_.Allocate();
  Register* is_equal = temps_.Allocate();
  tc.emit<LoadVarObjectSize>(seq_size, seq);
  tc.emit<LoadConst>(target_size, Type::fromCInt(bc_instr.oparg(), TCInt64));
  tc.emit<IntCompare>(IntCompareOp::kEqual, is_equal, seq_size, target_size);
  fast_path = cfg.AllocateBlock();
  tc.emit<CondBranch>(is_equal, fast_path, deopt_path.block);
  tc.block = fast_path;

  for (int idx = bc_instr.oparg() - 1; idx >= 0; --idx) {
    Register* item = temps_.Allocate();
    tc.emit<LoadTupleItem>(item, seq, idx);
    stack.push(item);
  }
}

void HIRBuilder::emitFinallyBlock(
    Function& irfunc,
    TranslationContext& tc,
    const BytecodeInstructionBlock& bc_instrs,
    std::deque<TranslationContext>& queue,
    Py_ssize_t finally_off,
    BasicBlock* ret_block) {
  // Create a new set of basic blocks to house the finally block and jump there
  BlockMap new_block_map = createBlocks(irfunc, bc_instrs);
  BasicBlock* finally_block = map_get(new_block_map.blocks, finally_off);
  tc.emit<Branch>(finally_block);
  BlockCanonicalizer().Run(tc.block, temps_, tc.frame.stack);

  // Recurse into translate() to duplicate the finally block.  `comp` will be
  // invoked in the callee to link the finally block back to us.
  std::swap(new_block_map, block_map_);
  auto comp = [&](TranslationContext& ftc,
                  const jit::BytecodeInstruction& bci) {
    BasicBlock* succ = ret_block;
    if (succ == nullptr || bci.opcode() == POP_FINALLY) {
      // Resume execution at the next instruction after the finally block
      succ = map_get(new_block_map.blocks, bci.NextInstrOffset());
    }
    ftc.emit<Branch>(succ);
    BlockCanonicalizer().Run(ftc.block, temps_, ftc.frame.stack);
    queue.emplace_back(succ, ftc.frame);
  };
  TranslationContext new_tc{finally_block, tc.frame};
  translate(irfunc, bc_instrs, new_tc, comp);
  std::swap(new_block_map, block_map_);
}

void HIRBuilder::emitBeginFinally(
    Function& irfunc,
    TranslationContext& tc,
    const BytecodeInstructionBlock& bc_instrs,
    const jit::BytecodeInstruction& bc_instr,
    std::deque<TranslationContext>& queue) {
  Register* null = temps_.Allocate();
  tc.emit<LoadConst>(null, TNullptr);
  tc.frame.stack.push(null);

  Py_ssize_t finally_off = bc_instr.NextInstrOffset();
  emitFinallyBlock(irfunc, tc, bc_instrs, queue, finally_off, nullptr);
}

void HIRBuilder::emitCallFinally(
    Function& irfunc,
    TranslationContext& tc,
    const BytecodeInstructionBlock& bc_instrs,
    const jit::BytecodeInstruction& bc_instr,
    std::deque<TranslationContext>& queue) {
  Register* ret_off = temps_.Allocate();
  tc.emit<LoadConst>(
      ret_off, Type::fromCInt(bc_instr.NextInstrOffset(), TCInt64));
  tc.frame.stack.push(ret_off);

  BasicBlock* succ = getBlockAtOff(bc_instr.NextInstrOffset());
  Py_ssize_t finally_off = bc_instr.GetJumpTarget();
  emitFinallyBlock(irfunc, tc, bc_instrs, queue, finally_off, succ);
}

void HIRBuilder::emitEndFinally(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr,
    FinallyCompleter complete_finally) {
  // Normally the interpreter will find either 1 value (when no
  // exception is active) or 6 values (when an exception is active) at
  // the top of the stack. We are guaranteed to only ever encounter 1
  // value at the top of the stack, as we deoptimize when an exception
  // is active.
  //
  // In the interpreter case, the single value is either `nullptr` (if
  // the finally block was entered via fallthrough) or an integer (if
  // the finally block was entered via `CALL_FINALLY`).
  tc.frame.stack.pop();
  complete_finally(tc, bc_instr);
}

void HIRBuilder::emitPopFinally(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr,
    FinallyCompleter complete_finally) {
  if (bc_instr.oparg() == 0) {
    // If oparg is 0, TOS is `nullptr` (if the finally block was entered via
    // `BEGIN_FINALLY`) or an integer (if the finally block was entered via
    // `CALL_FINALLY`). Both can be discarded, since execution always continues
    // at the next instruction.
    tc.frame.stack.pop();
  } else {
    // If oparg is 1, the return value is additionally pushed on the stack
    Register* res = tc.frame.stack.pop();
    tc.frame.stack.pop();
    tc.frame.stack.push(res);
  }
  complete_finally(tc, bc_instr);
}

void HIRBuilder::emitSetupFinally(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  int handler_off = bc_instr.NextInstrOffset() + bc_instr.oparg();
  int stack_level = tc.frame.stack.size();
  tc.frame.block_stack.push(
      ExecutionBlock{SETUP_FINALLY, handler_off, stack_level});
}

void HIRBuilder::emitAsyncForHeaderYieldFrom(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* send_value = tc.frame.stack.pop();
  Register* awaitable = tc.frame.stack.pop();
  Register* out = temps_.Allocate();
  // Unlike emitYieldFrom() we do not use tc.emitChecked() here.
  tc.emit<YieldFrom>(out, send_value, awaitable, false);
  tc.frame.stack.push(out);

  // If an exception was raised then exit the loop
  BasicBlock* yf_cont_block = getBlockAtOff(bc_instr.NextInstrOffset());
  int handler_off = tc.frame.block_stack.top().handler_off;
  int handler_idx = handler_off / sizeof(_Py_CODEUNIT);
  BasicBlock* yf_exc_block = getBlockAtOff(handler_off);
  end_async_for_frame_state_.emplace(handler_idx, tc.frame);
  tc.emit<CondBranch>(out, yf_cont_block, yf_exc_block);
}

void HIRBuilder::emitEndAsyncFor(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* is_stop = temps_.Allocate();
  tc.emit<IsErrStopAsyncIteration>(is_stop);
  FrameState& yield_from_frame =
      end_async_for_frame_state_.at(bc_instr.index());
  tc.emit<CheckExc>(is_stop, is_stop, yield_from_frame);
  tc.emit<ClearError>();

  // Pop finally block and discard exhausted async iterator.
  const ExecutionBlock& b = tc.frame.block_stack.top();
  JIT_CHECK(
      static_cast<int>(tc.frame.stack.size()) == b.stack_level,
      "Bad stack depth in END_ASYNC_FOR: block stack expects %d, stack is %d",
      b.stack_level,
      tc.frame.stack.size());
  tc.frame.block_stack.pop();
  tc.frame.stack.pop();
}

void HIRBuilder::emitGetAIter(TranslationContext& tc) {
  Register* obj = tc.frame.stack.pop();
  Register* out = temps_.Allocate();
  tc.emitChecked<CallCFunc>(
      1, out, CallCFunc::Func::k_PyEval_GetAIter, std::vector<Register*>{obj});
  tc.frame.stack.push(out);
}

void HIRBuilder::emitGetANext(TranslationContext& tc) {
  Register* obj = tc.frame.stack.top();
  Register* out = temps_.Allocate();
  tc.emitChecked<CallCFunc>(
      1, out, CallCFunc::Func::k_PyEval_GetANext, std::vector<Register*>{obj});
  tc.frame.stack.push(out);
}

Register* HIRBuilder::emitSetupWithCommon(
    TranslationContext& tc,
    _Py_Identifier* enter_id,
    _Py_Identifier* exit_id,
    bool swap_lookup) {
  // Load the enter and exit attributes from the manager, push exit, and return
  // the result of calling enter().
  auto& stack = tc.frame.stack;
  Register* manager = stack.pop();
  Register* enter = temps_.Allocate();
  Register* exit = temps_.Allocate();
  if (swap_lookup) {
    tc.emit<LoadAttrSpecial>(exit, manager, exit_id, tc.frame);
    tc.emit<LoadAttrSpecial>(enter, manager, enter_id, tc.frame);
  } else {
    tc.emit<LoadAttrSpecial>(enter, manager, enter_id, tc.frame);
    tc.emit<LoadAttrSpecial>(exit, manager, exit_id, tc.frame);
  }
  stack.push(exit);

  Register* enter_result = temps_.Allocate();
  VectorCall* call =
      tc.emit<VectorCall>(1, enter_result, false /* is_awaited */);
  call->setFrameState(tc.frame);
  call->SetOperand(0, enter);
  return enter_result;
}

void HIRBuilder::emitBeforeAsyncWith(TranslationContext& tc) {
  _Py_IDENTIFIER(__aenter__);
  _Py_IDENTIFIER(__aexit__);
  tc.frame.stack.push(
      emitSetupWithCommon(tc, &PyId___aenter__, &PyId___aexit__, true));
}

void HIRBuilder::emitSetupAsyncWith(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  // The finally block should be above the result of __aenter__.
  Register* top = tc.frame.stack.pop();
  emitSetupFinally(tc, bc_instr);
  tc.frame.stack.push(top);
}

void HIRBuilder::emitSetupWith(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  _Py_IDENTIFIER(__enter__);
  _Py_IDENTIFIER(__exit__);
  Register* enter_result =
      emitSetupWithCommon(tc, &PyId___enter__, &PyId___exit__, false);
  emitSetupFinally(tc, bc_instr);
  tc.frame.stack.push(enter_result);
}

void HIRBuilder::emitWithCleanupStart(TranslationContext& tc) {
  // We currently deopt when an instruction is raised, so we don't have to
  // worry about the exception case. TOS should always be NULL.
  auto& stack = tc.frame.stack;
  Register* null = stack.pop();
  Register* exit = stack.pop();
  stack.push(null);

  Register* none = temps_.Allocate();
  tc.emit<LoadConst>(none, TNoneType);
  Register* exit_result = temps_.Allocate();
  VectorCall* call =
      tc.emit<VectorCall>(4, exit_result, false /* is_awaited */);
  call->setFrameState(tc.frame);
  call->SetOperand(0, exit);
  call->SetOperand(1, none);
  call->SetOperand(2, none);
  call->SetOperand(3, none);

  stack.push(none);
  stack.push(exit_result);
}

void HIRBuilder::emitWithCleanupFinish(TranslationContext& tc) {
  auto& stack = tc.frame.stack;
  stack.pop(); // unused result of __exit__
  stack.pop(); // None
}

bool HIRBuilder::emitInvokeMethod(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr,
    bool is_awaited) {
  PyObject* descr = PyTuple_GET_ITEM(code_->co_consts, bc_instr.oparg());
  PyObject* target = PyTuple_GET_ITEM(descr, 0);
  long nargs = PyLong_AsLong(PyTuple_GET_ITEM(descr, 1)) + 1;

  ThreadedCompileSerialize guard;

  Py_ssize_t slot = _PyClassLoader_ResolveMethod(target);
  JIT_CHECK(
      slot != -1,
      "function lookup failed %s",
      repr(target)); // TODO: do better than this?

  PyMethodDescrObject* method = _PyClassLoader_ResolveMethodDef(target);
  if (method != NULL && tryEmitDirectMethodCall(method->d_method, tc, nargs)) {
    Py_XDECREF(method);
    return false;
  }

  InvokeMethod* invoke =
      tc.emitVariadic<InvokeMethod>(temps_, nargs, slot, is_awaited);
  PyObject* container;
  auto func =
      Ref<PyObject>::steal(_PyClassLoader_ResolveFunction(target, &container));
  if (func != nullptr) {
    Type ret_type = TObject;
    get_static_func_ret_type(func, &ret_type);
    // Since we are not doing a direct invoke, we will get a boxed int back; if
    // the function is supposed to return a primitive, we need to unbox it
    // because later code in the function will expect the primitive.
    if (ret_type <= TInternal) {
      tc.emit<PrimitiveUnbox>(
          invoke->GetOutput(), invoke->GetOutput(), ret_type);
    }
    Py_DECREF(container);
  }
  return true;
}

void HIRBuilder::emitInvokeTypedMethod(
    TranslationContext& tc,
    PyMethodDef* method,
    Py_ssize_t nargs) {
  _PyTypedMethodDef* def = (_PyTypedMethodDef*)method->ml_meth;

  Instr* staticCall;
  Register* out = NULL;
  Type type = get_methoddef_ret_type(method);
  if (type != TBottom) {
    out = temps_.Allocate();
    staticCall = tc.emit<CallStatic>(nargs, out, def->tmd_meth, type);
  } else {
    staticCall = tc.emit<CallStaticRetVoid>(nargs, def->tmd_meth);
  }

  auto& stack = tc.frame.stack;
  for (auto i = nargs - 1; i >= 0; i--) {
    Register* operand = stack.pop();
    // TODO: Can we add some checks here that assert the type is correct?
    staticCall->SetOperand(i, operand);
  }

  if (_Py_SIG_TYPE_MASK(def->tmd_ret) == TYPED_ERROR) {
    tc.emit<CheckNeg>(out, out, tc.frame);
  } else if (!(type <= TInternal)) {
    tc.emit<CheckExc>(out, out, tc.frame);
  }
  if (out == NULL || def->tmd_ret == _Py_SIG_ERROR) {
    // TODO: We should update the compiler so that in the future void
    // returning functions either are only used in void contexts, or
    // explicitly emit a LOAD_CONST None when not used in a void context.
    // For now we just assume basic Python semantics which everything
    // produces None.
    Register* tmp = temps_.Allocate();
    tc.emit<LoadConst>(tmp, TNoneType);
    stack.push(tmp);
  } else {
    stack.push(out);
  }
}

void HIRBuilder::emitLoadField(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  PyObject* field = PyTuple_GET_ITEM(code_->co_consts, bc_instr.oparg());
  int field_type;
  Py_ssize_t offset = THREADED_COMPILE_SERIALIZED_CALL(
      _PyClassLoader_ResolveFieldOffset(field, &field_type));
  JIT_CHECK(offset != -1, "failed to resolve field %s", repr(field));

  Type type = prim_type_to_type(field_type);
  Register* receiver = tc.frame.stack.pop();
  Register* result = temps_.Allocate();
  tc.emit<LoadField>(result, receiver, offset, type);
  if (field_type == TYPED_OBJECT) {
    tc.emit<CheckField>(result, result, tc.frame, bc_instr.oparg());
  }
  tc.frame.stack.push(result);
}

void HIRBuilder::emitStoreField(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  PyObject* field = PyTuple_GET_ITEM(code_->co_consts, bc_instr.oparg());
  int field_type;
  Py_ssize_t offset = THREADED_COMPILE_SERIALIZED_CALL(
      _PyClassLoader_ResolveFieldOffset(field, &field_type));
  JIT_CHECK(offset != -1, "failed to resolve field %s", repr(field));

  Type type = prim_type_to_type(field_type);
  Register* receiver = tc.frame.stack.pop();
  Register* value = tc.frame.stack.pop();
  Register* previous = temps_.Allocate();
  if (type <= TInternal) {
    Register* converted = temps_.Allocate();
    tc.emit<LoadConst>(previous, TNullptr);
    tc.emit<IntConvert>(converted, value, type);
    value = converted;
  } else {
    tc.emit<LoadField>(previous, receiver, offset, type, false);
  }
  tc.emit<StoreField>(receiver, offset, value, type, previous);
}

void HIRBuilder::emitCast(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  PyObject* descr = PyTuple_GET_ITEM(code_->co_consts, bc_instr.oparg());
  int optional;
  Ref<PyTypeObject> type = THREADED_COMPILE_SERIALIZED_CALL(
      Ref<PyTypeObject>::steal(_PyClassLoader_ResolveType(descr, &optional)));
  JIT_CHECK(type != NULL, "failed to resolve type %s", repr(descr));

  Register* value = tc.frame.stack.pop();
  Register* result = temps_.Allocate();
  tc.emit<Cast>(result, value, type, optional, tc.frame);
  tc.frame.stack.push(result);
}

void HIRBuilder::emitImportFrom(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& stack = tc.frame.stack;
  Register* name = stack.top();
  Register* res = temps_.Allocate();
  tc.emit<ImportFrom>(res, name, bc_instr.oparg(), tc.frame);
  stack.push(res);
}

void HIRBuilder::emitImportName(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& stack = tc.frame.stack;
  Register* fromlist = stack.pop();
  Register* level = stack.pop();
  Register* res = temps_.Allocate();
  tc.emit<ImportName>(res, bc_instr.oparg(), fromlist, level, tc.frame);
  stack.push(res);
}

void HIRBuilder::emitRaiseVarargs(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& stack = tc.frame.stack;
  switch (bc_instr.oparg()) {
    case 2: {
      auto cause = stack.pop();
      auto exc = stack.pop();
      tc.emit<Raise>(2, tc.frame, exc, cause);
      break;
    }
    case 1:
      tc.emit<Raise>(1, tc.frame, stack.pop());
      break;
    case 0:
      tc.emit<Raise>(0, tc.frame);
      break;
    default:
      JIT_CHECK(false, "unsupported RAISE_VARARGS op: %d", bc_instr.oparg());
      break;
  }
}

void HIRBuilder::emitYieldFrom(TranslationContext& tc, Register* out) {
  auto& stack = tc.frame.stack;
  auto send_value = stack.pop();
  auto iter = stack.pop();
  tc.emitChecked<YieldFrom>(out, send_value, iter, false);
  stack.push(out);
}

void HIRBuilder::emitYieldValue(TranslationContext& tc) {
  auto& stack = tc.frame.stack;
  auto in = stack.pop();
  auto out = temps_.Allocate();
  if (code_->co_flags & CO_ASYNC_GENERATOR) {
    tc.emitChecked<CallCFunc>(
        1,
        out,
        CallCFunc::Func::k_PyAsyncGenValueWrapperNew,
        std::vector<Register*>{in});
    in = out;
    out = temps_.Allocate();
  }
  tc.emitChecked<YieldValue>(out, in);
  stack.push(out);
}

void HIRBuilder::emitGetAwaitable(
    CFG& cfg,
    TranslationContext& tc,
    int prev_op) {
  OperandStack& stack = tc.frame.stack;
  Register* iterable = stack.pop();
  Register* iter = temps_.Allocate();

  // Most work is done by existing _PyCoro_GetAwaitableIter() utility.
  tc.emit<CallCFunc>(
      1,
      iter,
      CallCFunc::Func::k_PyCoro_GetAwaitableIter,
      std::vector<Register*>{iterable});
  if (prev_op == BEFORE_ASYNC_WITH || prev_op == WITH_CLEANUP_START) {
    BasicBlock* error_block = cfg.AllocateBlock();
    BasicBlock* ok_block = cfg.AllocateBlock();
    tc.emit<CondBranch>(iter, ok_block, error_block);
    tc.block = error_block;
    Register* type = temps_.Allocate();
    tc.emit<LoadField>(type, iterable, offsetof(PyObject, ob_type), TType);
    tc.emit<RaiseAwaitableError>(type, prev_op, tc.frame);

    tc.block = ok_block;
  } else {
    tc.emit<CheckExc>(iter, iter, tc.frame);
  }

  // For coroutines only, runtime assert it isn't already awaiting by checking
  // if it has a sub-iterator using _PyGen_yf().
  TranslationContext block_assert_not_awaited_coro{
      cfg.AllocateBlock(), tc.frame};
  TranslationContext block_done{cfg.AllocateBlock(), tc.frame};
  tc.emit<CondBranchCheckType>(
      iter,
      Type::fromTypeExact(&PyCoro_Type),
      block_assert_not_awaited_coro.block,
      block_done.block);
  Register* yf = temps_.Allocate();
  block_assert_not_awaited_coro.emit<CallCFunc>(
      1, yf, CallCFunc::Func::k_PyGen_yf, std::vector<Register*>{iter});
  TranslationContext block_coro_already_awaited{cfg.AllocateBlock(), tc.frame};
  block_assert_not_awaited_coro.emit<CondBranch>(
      yf, block_coro_already_awaited.block, block_done.block);
  block_coro_already_awaited.emit<RaiseStatic>(
      0, PyExc_RuntimeError, "coroutine is being awaited already", tc.frame);

  stack.push(iter);

  tc.block = block_done.block;
}

void HIRBuilder::emitBuildString(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto num_operands = bc_instr.oparg();
  tc.emitVariadic<BuildString>(temps_, num_operands);
}

void HIRBuilder::emitFormatValue(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto oparg = bc_instr.oparg();

  int have_fmt_spec = (oparg & FVS_MASK) == FVS_HAVE_SPEC;
  Register* fmt_spec;
  if (have_fmt_spec) {
    fmt_spec = tc.frame.stack.pop();
  } else {
    fmt_spec = temps_.Allocate();
    tc.emit<LoadConst>(fmt_spec, TNullptr);
  }
  Register* value = tc.frame.stack.pop();
  Register* dst = temps_.Allocate();
  int which_conversion = oparg & FVC_MASK;

  tc.emit<FormatValue>(dst, fmt_spec, value, which_conversion, tc.frame);
  tc.frame.stack.push(dst);
}

void HIRBuilder::emitMapAdd(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto oparg = bc_instr.oparg();
  auto& stack = tc.frame.stack;
  auto value = stack.pop();
  auto key = stack.pop();

  auto map = stack.peek(oparg);

  auto result = temps_.Allocate();
  tc.emit<SetDictItem>(result, map, key, value, tc.frame);
}

void HIRBuilder::emitSetAdd(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto oparg = bc_instr.oparg();
  auto& stack = tc.frame.stack;

  auto* v = stack.pop();
  auto* set = stack.peek(oparg);

  auto result = temps_.Allocate();
  tc.emit<SetSetItem>(result, set, v, tc.frame);
}

void HIRBuilder::emitDispatchEagerCoroResult(
    CFG& cfg,
    TranslationContext& tc,
    Register* out,
    BasicBlock* await_block,
    BasicBlock* post_await_block) {
  Register* stack_top = tc.frame.stack.top();

  TranslationContext has_wh_block{cfg.AllocateBlock(), tc.frame};
  tc.emit<CondBranchCheckType>(
      stack_top, TWaitHandle, has_wh_block.block, await_block);

  Register* wait_handle = stack_top;
  Register* wh_coro_or_result = temps_.Allocate();
  Register* wh_waiter = temps_.Allocate();
  has_wh_block.emit<WaitHandleLoadCoroOrResult>(wh_coro_or_result, wait_handle);
  has_wh_block.emit<WaitHandleLoadWaiter>(wh_waiter, wait_handle);
  has_wh_block.emit<WaitHandleRelease>(wait_handle);

  TranslationContext coro_block{cfg.AllocateBlock(), tc.frame};
  TranslationContext res_block{cfg.AllocateBlock(), tc.frame};
  has_wh_block.emit<CondBranch>(wh_waiter, coro_block.block, res_block.block);

  coro_block.emitChecked<YieldFrom>(out, wh_waiter, wh_coro_or_result, true);
  coro_block.emit<Branch>(post_await_block);

  res_block.emit<Assign>(out, wh_coro_or_result);
  res_block.emit<Branch>(post_await_block);
}

void HIRBuilder::insertEvalBreakerCheck(
    CFG& cfg,
    BasicBlock* check_block,
    BasicBlock* succ,
    const FrameState& frame) {
  TranslationContext check(check_block, frame);
  TranslationContext body(cfg.AllocateBlock(), frame);
  // Check if the eval breaker has been set
  Register* eval_breaker = temps_.Allocate();
  check.emit<LoadEvalBreaker>(eval_breaker);
  check.emit<CondBranch>(eval_breaker, body.block, succ);
  // If set, run periodic tasks
  body.snapshot();
  body.emit<RunPeriodicTasks>(temps_.Allocate(), body.frame);
  body.emit<Branch>(succ);
}

void HIRBuilder::insertEvalBreakerCheckForLoop(
    CFG& cfg,
    BasicBlock* loop_header) {
  auto snap = loop_header->entrySnapshot();
  JIT_CHECK(snap != nullptr, "block %d has no entry snapshot", loop_header->id);
  auto fs = snap->frameState();
  JIT_CHECK(
      fs != nullptr,
      "entry snapshot for block %d has no FrameState",
      loop_header->id);
  auto check_block = cfg.AllocateBlock();
  loop_header->retargetPreds(check_block);
  insertEvalBreakerCheck(cfg, check_block, loop_header, *fs);
}

void HIRBuilder::insertEvalBreakerCheckForExcept(
    CFG& cfg,
    TranslationContext& tc) {
  TranslationContext succ(cfg.AllocateBlock(), tc.frame);
  succ.snapshot();
  insertEvalBreakerCheck(cfg, tc.block, succ.block, tc.frame);
  tc.block = succ.block;
}

ExecutionBlock HIRBuilder::popBlock(CFG& cfg, TranslationContext& tc) {
  if (tc.frame.block_stack.top().opcode == SETUP_FINALLY) {
    insertEvalBreakerCheckForExcept(cfg, tc);
  }
  return tc.frame.block_stack.pop();
}

} // namespace hir
} // namespace jit

// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/hir/builder.h"

#include "Python.h"
#include "boolobject.h"
#include "ceval.h"
#include "object.h"
#include "opcode.h"
#include "preload.h"
#include "pyreadonly.h"
#include "structmember.h"
#include "type.h"

#include "Jit/bitvector.h"
#include "Jit/bytecode.h"
#include "Jit/codegen/environ.h"
#include "Jit/containers.h"
#include "Jit/hir/hir.h"
#include "Jit/hir/optimization.h"
#include "Jit/hir/preload.h"
#include "Jit/hir/ssa.h"
#include "Jit/hir/type.h"
#include "Jit/pyjit.h"
#include "Jit/ref.h"
#include "Jit/threaded_compile.h"

#include <algorithm>
#include <deque>
#include <memory>
#include <set>
#include <unordered_set>
#include <utility>
#include <vector>

namespace jit::hir {

// Allocate a temp register that may be used for the stack. It should not be a
// register that will be treated specially in the FrameState (e.g. tracked as
// containing a local or cell.)
Register* TempAllocator::AllocateStack() {
  Register* reg = env_->AllocateRegister();
  cache_.emplace_back(reg);
  return reg;
}

// Get the i-th stack temporary or allocate one.
Register* TempAllocator::GetOrAllocateStack(std::size_t idx) {
  if (idx < cache_.size()) {
    Register* reg = cache_[idx];
    return reg;
  }
  return AllocateStack();
}

// Allocate a temp register that will not be used for a stack value.
Register* TempAllocator::AllocateNonStack() {
  return env_->AllocateRegister();
}

// Opcodes that we know how to translate into HIR
#ifdef CINDER_PORTING_DONE
// This contains the set of unsupported opcodes. Move opcodes from this set
// into the one in the `#else` block below to enable them in the JIT.
const std::unordered_set<int> kUnsupportedOpcodes = {
    // Readonly
    FUNC_CREDENTIAL,
    READONLY_OPERATION,
};
#else
const std::unordered_set<int> kSupportedOpcodes = {
    BEFORE_ASYNC_WITH,
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
    BUILD_CHECKED_LIST,
    BUILD_CHECKED_MAP,
    BUILD_CONST_KEY_MAP,
    BUILD_LIST,
    BUILD_MAP,
    BUILD_SET,
    BUILD_SLICE,
    BUILD_STRING,
    BUILD_TUPLE,
    CALL_FUNCTION,
    CALL_FUNCTION_EX,
    CALL_FUNCTION_KW,
    CALL_METHOD,
    CAST,
    CHECK_ARGS,
    COMPARE_OP,
    CONVERT_PRIMITIVE,
    CONTAINS_OP,
    COPY_DICT_WITHOUT_KEYS,
    DELETE_ATTR,
    DELETE_FAST,
    DELETE_SUBSCR,
    DICT_MERGE,
    DICT_UPDATE,
    DUP_TOP,
    DUP_TOP_TWO,
    END_ASYNC_FOR,
    EXTENDED_ARG,
    FAST_LEN,
    FORMAT_VALUE,
    FOR_ITER,
    GEN_START,
    GET_AITER,
    GET_ANEXT,
    GET_AWAITABLE,
    GET_ITER,
    GET_LEN,
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
    INVOKE_FUNCTION,
    INVOKE_METHOD,
    INVOKE_NATIVE,
    IS_OP,
    JUMP_ABSOLUTE,
    JUMP_FORWARD,
    JUMP_IF_FALSE_OR_POP,
    JUMP_IF_NONZERO_OR_POP,
    JUMP_IF_NOT_EXC_MATCH,
    JUMP_IF_TRUE_OR_POP,
    JUMP_IF_ZERO_OR_POP,
    LIST_APPEND,
    LIST_EXTEND,
    LIST_TO_TUPLE,
    LOAD_ASSERTION_ERROR,
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
    LOAD_TYPE,
    MAKE_FUNCTION,
    MAP_ADD,
    MATCH_CLASS,
    MATCH_KEYS,
    MATCH_MAPPING,
    MATCH_SEQUENCE,
    NOP,
    POP_BLOCK,
    POP_EXCEPT,
    POP_JUMP_IF_FALSE,
    POP_JUMP_IF_NONZERO,
    POP_JUMP_IF_TRUE,
    POP_JUMP_IF_ZERO,
    POP_TOP,
    PRIMITIVE_BINARY_OP,
    PRIMITIVE_BOX,
    PRIMITIVE_COMPARE_OP,
    PRIMITIVE_LOAD_CONST,
    PRIMITIVE_UNARY_OP,
    PRIMITIVE_UNBOX,
    RAISE_VARARGS,
    REFINE_TYPE,
    RERAISE,
    RETURN_PRIMITIVE,
    RETURN_VALUE,
    ROT_FOUR,
    ROT_N,
    ROT_THREE,
    ROT_TWO,
    SEQUENCE_GET,
    SEQUENCE_REPEAT,
    SEQUENCE_SET,
    SET_ADD,
    SET_UPDATE,
    SETUP_ASYNC_WITH,
    SETUP_FINALLY,
    SETUP_WITH,
    STORE_ATTR,
    STORE_DEREF,
    STORE_FAST,
    STORE_FIELD,
    STORE_LOCAL,
    STORE_SUBSCR,
    TP_ALLOC,
    UNARY_INVERT,
    UNARY_NEGATIVE,
    UNARY_NOT,
    UNARY_POSITIVE,
    UNPACK_EX,
    UNPACK_SEQUENCE,
    WITH_EXCEPT_START,
    YIELD_FROM,
    YIELD_VALUE,
};
#endif

#ifdef CINDER_PORTING_DONE
// Readonly features needed
const std::unordered_set<int> kSupportedReadonlyOperations = {
    READONLY_MAKE_FUNCTION,      READONLY_CHECK_FUNCTION,
    READONLY_CHECK_LOAD_ATTR,    READONLY_BINARY_SUBTRACT,
    READONLY_BINARY_MULTIPLY,    READONLY_BINARY_MATRIX_MULTIPLY,
    READONLY_BINARY_TRUE_DIVIDE, READONLY_BINARY_FLOOR_DIVIDE,
    READONLY_BINARY_MODULO,      READONLY_BINARY_POWER,
    READONLY_BINARY_ADD,         READONLY_BINARY_LSHIFT,
    READONLY_BINARY_RSHIFT,      READONLY_BINARY_OR,
    READONLY_BINARY_XOR,         READONLY_BINARY_AND,
    READONLY_UNARY_INVERT,       READONLY_UNARY_NEGATIVE,
    READONLY_UNARY_POSITIVE,     READONLY_UNARY_NOT,
    READONLY_GET_ITER,           READONLY_FOR_ITER,
    READONLY_COMPARE_OP,
};

#define NAMES(op, value) {value, #op},
const UnorderedMap<int, const char*> kReadonlyOperationNames = {
    READONLY_OPERATIONS(NAMES)};
#undef NAMES
#endif

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
    } else if (opcode == READONLY_OPERATION) {
#ifdef CINDER_PORTING_DONE
      int oparg = bci.oparg();
      PyObject* op_tuple = PyTuple_GET_ITEM(code->co_consts, oparg);

      PyObject* opobj = PyTuple_GET_ITEM(op_tuple, 0);
      assert(opobj != nullptr);
      int op = PyLong_AsLong(opobj);

      if (!kSupportedReadonlyOperations.count(op)) {
        JIT_DLOG(
            "Readonly operation '%s' unsupported.",
            kReadonlyOperationNames.at(op));
        return false;
      }
#else
      PORT_ASSERT(
          "Need to re-review kSupportedReadonlyOperations + "
          "kReadonlyOperationNames");
#endif
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
  TranslationContext(BasicBlock* b, const FrameState& fs)
      : block(b), frame(fs) {}

  template <typename T, typename... Args>
  T* emit(Args&&... args) {
    auto instr = block->appendWithOff<T>(
        frame.instr_offset(), std::forward<Args>(args)...);
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
    Register* out = temps.AllocateStack();
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
  auto out = temps_.AllocateNonStack();
  tc.emit<InitialYield>(out, tc.frame);
}

// Add LoadArg instructions for each function argument. This ensures that the
// corresponding variables are always assigned and allows for a uniform
// treatment of registers that correspond to arguments (vs locals) during
// definite assignment analysis.
void HIRBuilder::addLoadArgs(TranslationContext& tc, int num_args) {
  PyCodeObject* code = tc.frame.code;
  int starargs_idx = (code->co_flags & CO_VARARGS)
      ? code->co_argcount + code->co_kwonlyargcount
      : -1;
  for (int i = 0; i < num_args; i++) {
    // Arguments in CPython are the first N locals
    Register* dst = tc.frame.locals[i];
    JIT_CHECK(dst != nullptr, "No register for argument %d", i);
    if (i == starargs_idx) {
      tc.emit<LoadArg>(dst, i, TTupleExact);
    } else {
      Type type = preloader_.checkArgType(i);
      tc.emit<LoadArg>(dst, i, type);
    }
  }
}

// Add a MakeCell for each cellvar and load each freevar from closure.
void HIRBuilder::addInitializeCells(
    TranslationContext& tc,
    Register* cur_func) {
  Py_ssize_t ncellvars = PyTuple_GET_SIZE(code_->co_cellvars);
  Py_ssize_t nfreevars = PyTuple_GET_SIZE(code_->co_freevars);

  Register* null_reg = ncellvars > 0 ? temps_.AllocateNonStack() : nullptr;
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
  Register* func_closure = temps_.AllocateNonStack();
  tc.emit<LoadField>(
      func_closure,
      cur_func,
      "func_closure",
      offsetof(PyFunctionObject, func_closure),
      TTuple);
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
    case JUMP_ABSOLUTE:
    case JUMP_FORWARD:
    case POP_JUMP_IF_FALSE:
    case POP_JUMP_IF_TRUE:
    case POP_JUMP_IF_ZERO:
    case POP_JUMP_IF_NONZERO:
    case RETURN_PRIMITIVE:
    case RETURN_VALUE:
    case RAISE_VARARGS:
    // These instructions only modify frame state and are always safe to
    // replay. We don't snapshot these in order to limit the amount of
    // unnecessary metadata in the lowered IR.
    case CHECK_ARGS:
    case CONVERT_PRIMITIVE:
    case DUP_TOP:
    case DUP_TOP_TWO:
    case EXTENDED_ARG:
    case IS_OP:
    case LOAD_ASSERTION_ERROR:
    case LOAD_CLOSURE:
    case LOAD_CONST:
    case LOAD_FAST:
    case LOAD_LOCAL:
    case NOP:
    case POP_TOP:
    case PRIMITIVE_BOX:
    case PRIMITIVE_LOAD_CONST:
    case PRIMITIVE_UNARY_OP:
    case PRIMITIVE_UNBOX:
    case REFINE_TYPE:
    case ROT_FOUR:
    case ROT_THREE:
    case ROT_TWO:
    case ROT_N:
    case STORE_FAST:
    case STORE_LOCAL: {
      return false;
    }
    // In an async-for header block YIELD_FROM controls whether we end the loop
    case YIELD_FROM: {
      return !is_in_async_for_header_block;
    }
    case READONLY_OPERATION: {
      switch (bci.ReadonlyOpcode()) {
        case READONLY_FOR_ITER:
          return false;
        default:
          return true;
      };
    }
    case JUMP_IF_NOT_EXC_MATCH:
    case RERAISE:
    case WITH_EXCEPT_START: {
      JIT_CHECK(
          false,
          "should not be compiling except blocks (opcode %d)\n",
          bci.opcode());
      break;
    }
    // Take a snapshot after translating all other bytecode instructions. This
    // may generate unnecessary deoptimization metadata but will always be
    // correct.
    default: {
      return true;
    }
  }
}

// Compute basic block boundaries and allocate corresponding HIR blocks
HIRBuilder::BlockMap HIRBuilder::createBlocks(
    Function& irfunc,
    const BytecodeInstructionBlock& bc_block) {
  BlockMap block_map;

  // Mark the beginning of each basic block in the bytecode
  std::set<BCIndex> block_starts = {BCIndex{0}};
  auto maybe_add_next_instr = [&](const BytecodeInstruction& bc_instr) {
    BCIndex next_instr_idx = bc_instr.NextInstrIndex();
    if (next_instr_idx < bc_block.size()) {
      block_starts.insert(next_instr_idx);
    }
  };
  for (auto bc_instr : bc_block) {
    if (bc_instr.IsBranch()) {
      maybe_add_next_instr(bc_instr);
      auto target = bc_instr.GetJumpTargetAsIndex();
      block_starts.insert(target);
    } else {
      auto opcode = bc_instr.opcode();
      if (
          // We always split after YIELD_FROM to handle the case where it's the
          // top of an async-for loop and so generate a HIR conditional jump.
          bc_instr.IsTerminator() || (opcode == YIELD_FROM)) {
        maybe_add_next_instr(bc_instr);
      } else {
        JIT_CHECK(!bc_instr.IsTerminator(), "Terminator should split block");
      }
    }
  }

  // Allocate blocks
  auto it = block_starts.begin();
  while (it != block_starts.end()) {
    BCIndex start_idx = *it;
    ++it;
    BCIndex end_idx;
    if (it != block_starts.end()) {
      end_idx = *it;
    } else {
      end_idx = BCIndex{bc_block.size()};
    }
    auto block = irfunc.cfg.AllocateBlock();
    block_map.blocks[start_idx] = block;
    block_map.bc_blocks.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(block),
        std::forward_as_tuple(
            bc_block.bytecode(), start_idx, end_idx, bc_block.code()));
  }

  return block_map;
}

BasicBlock* HIRBuilder::getBlockAtOff(BCOffset off) {
  auto it = block_map_.blocks.find(off);
  JIT_DCHECK(it != block_map_.blocks.end(), "No block for offset %d", off);
  return it->second;
}

// Convenience wrapper, used only in tests
std::unique_ptr<Function> buildHIR(BorrowedRef<PyFunctionObject> func) {
  JIT_CHECK(
      !g_threaded_compile_context.compileRunning(),
      "multi-thread compile must preload first");
  auto preloader = Preloader::getPreloader(func);
  if (preloader) {
    return buildHIR(*preloader);
  } else {
    return nullptr;
  }
}

std::unique_ptr<Function> buildHIR(const Preloader& preloader) {
  return HIRBuilder{preloader}.buildHIR();
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
std::unique_ptr<Function> HIRBuilder::buildHIR() {
  if (!can_translate(code_)) {
    JIT_DLOG("Can't translate all opcodes in %s", preloader_.fullname());
    return nullptr;
  }

  std::unique_ptr<Function> irfunc = preloader_.makeFunction();
  buildHIRImpl(irfunc.get(), /*frame_state=*/nullptr);
  // Use RemoveTrampolineBlocks and RemoveUnreachableBlocks directly instead of
  // Run because the rest of CleanCFG requires SSA.
  CleanCFG::RemoveTrampolineBlocks(&irfunc->cfg);
  CleanCFG::RemoveUnreachableBlocks(&irfunc->cfg);
  return irfunc;
}

BasicBlock* HIRBuilder::buildHIRImpl(
    Function* irfunc,
    FrameState* frame_state) {
  temps_ = TempAllocator(&irfunc->env);

  BytecodeInstructionBlock bc_instrs{code_};
  block_map_ = createBlocks(*irfunc, bc_instrs);

  // Ensure that the entry block isn't a loop header
  BasicBlock* entry_block = getBlockAtOff(BCOffset{0});
  for (const auto& bci : bc_instrs) {
    if (bci.IsBranch() && bci.GetJumpTarget() == 0) {
      entry_block = irfunc->cfg.AllocateBlock();
      break;
    }
  }
  if (frame_state == nullptr) {
    // Function is not being inlined (irfunc matches code) so set the whole
    // CFG's entry block.
    irfunc->cfg.entry_block = entry_block;
  }

  // Insert LoadArg, LoadClosureCell, and MakeCell/MakeNullCell instructions
  // for the entry block
  TranslationContext entry_tc{
      entry_block,
      FrameState{
          code_,
          preloader_.globals(),
          preloader_.builtins(),
          /*parent=*/frame_state}};
  AllocateRegistersForLocals(&irfunc->env, entry_tc.frame);
  AllocateRegistersForCells(&irfunc->env, entry_tc.frame);

  addLoadArgs(entry_tc, preloader_.numArgs());
  Register* cur_func = nullptr;
  // TODO(emacs): Check if the code object or preloader uses runtime func and
  // drop the frame_state == nullptr check. Inlined functions should load a
  // const instead of using LoadCurrentFunc.
  if (frame_state == nullptr && irfunc->uses_runtime_func) {
    cur_func = temps_.AllocateNonStack();
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

  BasicBlock* first_block = getBlockAtOff(BCOffset{0});
  if (entry_block != first_block) {
    entry_block->appendWithOff<Branch>(BCOffset{0}, first_block);
  }

  entry_tc.block = first_block;
  translate(*irfunc, bc_instrs, entry_tc);

  return entry_block;
}

void HIRBuilder::emitProfiledTypes(
    TranslationContext& tc,
    const CodeProfileData& profile_data,
    const BytecodeInstruction& bc_instr) {
  if (bc_instr.opcode() == CALL_METHOD) {
    // TODO(T107300350): Ignore profiling data for CALL_METHOD because we lie
    // about its stack inputs.
    return;
  }

  const PolymorphicTypes types =
      getProfiledTypes(profile_data, bc_instr.offset());
  if (types.empty() || types[0].size() > tc.frame.stack.size()) {
    // The types are either absent or invalid (e.g., from a different version
    // of the code than what we're running now).
    return;
  }

  const std::vector<BorrowedRef<PyTypeObject>> first_profile = types[0];

  // TODO(T115140951): Add a more robust method of determining what type
  // information differs between interpreter runs and static JITted bytecode
  if (bc_instr.opcode() == STORE_FIELD) {
    auto& [offset, type, name] = preloader_.fieldInfo(constArg(bc_instr));
    if (type <= TPrimitive) {
      return;
    }
  }

  // Except for a few special cases, all instructions profile all of their
  // inputs, with deeper stack elements first.
  // TODO(T127457244): Centralize this information.
  ssize_t stack_idx = first_profile.size() - 1;
  if (bc_instr.opcode() == CALL_FUNCTION) {
    stack_idx = bc_instr.oparg();
  } else if (bc_instr.opcode() == CALL_METHOD) {
    stack_idx = bc_instr.oparg() + 1;
  } else if (bc_instr.opcode() == WITH_EXCEPT_START) {
    stack_idx = 6;
  }
  if (types.size() == 1) {
    for (auto type : first_profile) {
      if (type != nullptr) {
        Register* value = tc.frame.stack.top(stack_idx);
        GuardType* guard =
            tc.emit<GuardType>(value, Type::fromTypeExact(type), value);
        guard->setGuiltyReg(value);
      }
      stack_idx--;
    }
  } else {
    ProfiledTypes all_types;
    for (auto type_vec : types) {
      std::vector<Type> types;
      for (auto type : type_vec) {
        if (type != nullptr) {
          types.emplace_back(Type::fromTypeExact(type));
        }
      }
      all_types.emplace_back(types);
    }
    std::vector<Register*> args;
    while (stack_idx >= 0) {
      args.emplace_back(tc.frame.stack.top(stack_idx--));
    }
    tc.emit<HintType>(args.size(), all_types, args);
  }
}

InlineResult HIRBuilder::inlineHIR(
    Function* caller,
    FrameState* caller_frame_state) {
  if (!can_translate(code_)) {
    JIT_DLOG("Can't translate all opcodes in %s", preloader_.fullname());
    return {nullptr, nullptr};
  }
  BasicBlock* entry_block = buildHIRImpl(caller, caller_frame_state);
  // Make one block with a Return that merges the return branches from the
  // callee. After SSA, it will turn into a massive Phi. The caller can find
  // the Return and use it as the output of the call instruction.
  Register* return_val = caller->env.AllocateRegister();
  BasicBlock* exit_block = caller->cfg.AllocateBlock();
  if (preloader_.returnType() <= TPrimitive) {
    exit_block->append<Return>(return_val, preloader_.returnType());
  } else {
    exit_block->append<Return>(return_val);
  }
  for (auto block : caller->cfg.GetRPOTraversal(entry_block)) {
    auto instr = block->GetTerminator();
    if (instr->IsReturn()) {
      auto assign = Assign::create(return_val, instr->GetOperand(0));
      auto branch = Branch::create(exit_block);
      instr->ExpandInto({assign, branch});
      delete instr;
    }
  }

  // Map of FrameState to parent pointers. We must completely disconnect the
  // inlined function's CFG from its caller for SSAify to run properly: it will
  // find uses (in FrameState) before defs and insert LoadConst<Nullptr>.
  UnorderedMap<FrameState*, FrameState*> framestate_parent;
  for (BasicBlock* block : caller->cfg.GetRPOTraversal(entry_block)) {
    for (Instr& instr : *block) {
      JIT_CHECK(
          !instr.IsBeginInlinedFunction(),
          "there should be no BeginInlinedFunction in inlined functions");
      JIT_CHECK(
          !instr.IsEndInlinedFunction(),
          "there should be no EndInlinedFunction in inlined functions");
      FrameState* fs = nullptr;
      if (auto db = instr.asDeoptBase()) {
        fs = db->frameState();
      } else if (instr.opcode() == Opcode::kSnapshot) {
        auto snap = dynamic_cast<Snapshot*>(&instr);
        fs = snap->frameState();
      }
      if (fs == nullptr || fs->parent == nullptr) {
        continue;
      }
      bool inserted = framestate_parent.emplace(fs, fs->parent).second;
      JIT_CHECK(inserted, "there should not be duplicate FrameState pointers");
      fs->parent = nullptr;
    }
  }

  // The caller function has already been converted to SSA form and all HIR
  // passes require input to be in SSA form. SSAify the inlined function.
  SSAify{}.Run(entry_block, &caller->env);

  // Re-link the CFG.
  for (auto& [fs, parent] : framestate_parent) {
    fs->parent = parent;
  }

  return {entry_block, exit_block};
}

void HIRBuilder::translate(
    Function& irfunc,
    const jit::BytecodeInstructionBlock& bc_instrs,
    const TranslationContext& tc) {
  std::deque<TranslationContext> queue = {tc};
  std::unordered_set<BasicBlock*> processed;
  std::unordered_set<BasicBlock*> loop_headers;

  const CodeProfileData* profile_data = getProfileData(tc.frame.code);

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

      if (profile_data != nullptr) {
        emitProfiledTypes(tc, *profile_data, bc_instr);
      }

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
        case BUILD_CHECKED_LIST: {
          emitBuildCheckedList(tc, bc_instr);
          break;
        }
        case BUILD_CHECKED_MAP: {
          emitBuildCheckedMap(tc, bc_instr);
          break;
        }
        case BUILD_MAP: {
          emitBuildMap(tc, bc_instr);
          break;
        }
        case BUILD_SET: {
          emitBuildSet(tc, bc_instr);
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
        case INVOKE_METHOD:
        case INVOKE_NATIVE: {
          emitAnyCall(irfunc.cfg, tc, bc_it, bc_instrs);
          break;
        }
        case FUNC_CREDENTIAL:
          emitFunctionCredential(tc, bc_instr);
          break;
        case IS_OP: {
          emitIsOp(tc, bc_instr.oparg());
          break;
        }
        case CONTAINS_OP: {
          emitContainsOp(tc, bc_instr.oparg(), 0);
          break;
        }
        case COMPARE_OP: {
          emitCompareOp(tc, bc_instr.oparg(), 0);
          break;
        }
        case COPY_DICT_WITHOUT_KEYS: {
          emitCopyDictWithoutKeys(tc);
          break;
        }
        case GET_LEN: {
          emitGetLen(tc);
          break;
        }
        case DELETE_ATTR: {
          emitDeleteAttr(tc, bc_instr);
          break;
        }
        case LOAD_ATTR: {
          emitLoadAttr(tc, bc_instr);
          break;
        }
        case LOAD_METHOD: {
          emitLoadMethod(tc, irfunc.env, bc_instr);
          break;
        }
        case LOAD_METHOD_SUPER: {
          emitLoadMethodOrAttrSuper(tc, bc_instr, true);
          break;
        }
        case LOAD_ASSERTION_ERROR: {
          emitLoadAssertionError(tc, irfunc.env);
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
        case LOAD_CLASS: {
          emitLoadClass(tc, bc_instr);
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
        case LOAD_TYPE: {
          emitLoadType(tc, bc_instr);
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
        case PRIMITIVE_COMPARE_OP: {
          emitPrimitiveCompare(tc, bc_instr);
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
        case READONLY_OPERATION: {
          emitReadonlyOperation(irfunc.cfg, tc, bc_instr);
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
        case RETURN_PRIMITIVE: {
          Type type = prim_type_to_type(bc_instr.oparg());
          JIT_CHECK(
              type <= preloader_.returnType(),
              "bad return type %s, expected %s",
              type,
              preloader_.returnType());
          Register* reg = tc.frame.stack.pop();
          tc.emit<Return>(reg, type);
          break;
        }
        case RETURN_VALUE: {
          Register* reg = tc.frame.stack.pop();
          // TODO add preloader_.returnType() to Return instr here to validate
          // that all values flowing to return are of correct type; will
          // require consistency of static compiler and JIT types, see
          // T86480663
          JIT_CHECK(
              tc.frame.block_stack.isEmpty(),
              "Returning with non-empty block stack");
          tc.emit<Return>(reg);
          break;
        }
        case ROT_N: {
          int oparg = bc_instr.oparg();
          if (oparg <= 1) {
            break;
          }
          OperandStack& stack = tc.frame.stack;
          Register* top = stack.top();

          std::copy_backward(stack.end() - oparg, stack.end() - 1, stack.end());
          stack.topPut(oparg - 1, top);
          break;
        }
        case END_ASYNC_FOR: {
          emitEndAsyncFor(tc);
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
          emitGetIter(tc, 0);
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
        case LIST_EXTEND: {
          emitListExtend(tc, bc_instr);
          break;
        }
        case LIST_TO_TUPLE: {
          emitListToTuple(tc);
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
          emitForIter(tc, bc_instr, 0);
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
        case TP_ALLOC: {
          emitTpAlloc(tc, bc_instr);
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
            emitYieldFrom(tc, temps_.AllocateStack());
          }
          break;
        }
        case GET_AWAITABLE: {
          BCIndex idx = bc_instr.index();
          int prev_prev_op = idx > 1 ? bc_instrs.at(idx - 2).opcode() : 0;
          int prev_op = idx != 0 ? bc_instrs.at(idx - 1).opcode() : 0;
          emitGetAwaitable(irfunc.cfg, tc, prev_prev_op, prev_op);
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
        case SET_UPDATE: {
          emitSetUpdate(tc, bc_instr);
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
        case MATCH_CLASS: {
          emitMatchClass(irfunc.cfg, tc, bc_instr);
          break;
        }
        case MATCH_KEYS: {
          emitMatchKeys(irfunc.cfg, tc);
          break;
        }
        case MATCH_MAPPING: {
          emitMatchMappingSequence(irfunc.cfg, tc, Py_TPFLAGS_MAPPING);
          break;
        }
        case MATCH_SEQUENCE: {
          emitMatchMappingSequence(irfunc.cfg, tc, Py_TPFLAGS_SEQUENCE);
          break;
        }
        case GEN_START: {
          // In the interpreter this instruction behaves like POP_TOP because
          // it assumes a generator will always be sent a superflous None value
          // to start execution via the stack. We skip doing this for JIT
          // functions. This should be fine as long as we can't de-opt after the
          // function is started but before GEN_START. This check ensures this.
          JIT_DCHECK(
              bc_instr.index() == 0 ||
                  (bc_instr.index() == 1 &&
                   bc_instrs.begin()->opcode() == CHECK_ARGS),
              "GEN_START must be first instruction, or preceded only by "
              "CHECK_ARGS");
          break;
        }
        case DICT_UPDATE: {
          emitDictUpdate(tc);
          break;
        }
        case DICT_MERGE: {
          emitDictMerge(tc, bc_instr);
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
      case READONLY_OPERATION: {
        switch (last_bc_instr.ReadonlyOpcode()) {
          case READONLY_FOR_ITER: {
            auto condbr = static_cast<CondBranchIterNotDone*>(last_instr);
            auto new_frame = tc.frame;
            // Sentinel value signaling iteration is complete and the iterator
            // itself
            new_frame.stack.discard(2);
            queue.emplace_back(condbr->true_bb(), tc.frame);
            queue.emplace_back(condbr->false_bb(), new_frame);
            break;
          }
          default: {
            break;
          }
        }
        break;
      }
      default: {
        if (last_bc_instr.opcode() == YIELD_FROM &&
            is_in_async_for_header_block()) {
          JIT_CHECK(
              last_instr->IsCondBranchIterNotDone(),
              "Async-for header should end with CondBranchIterNotDone");
          auto condbr = static_cast<CondBranchIterNotDone*>(last_instr);
          FrameState new_frame = tc.frame;
          // Pop sentinel value signaling that iteration is complete
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
    auto tmp = temps.AllocateStack();
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
    auto reg = temps.GetOrAllocateStack(i);
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
        auto tmp = temps.AllocateStack();
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
  BCIndex idx = bc_instr.index();
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
    case INVOKE_NATIVE: {
      call_used_is_awaited = emitInvokeNative(tc, bc_instr);
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
    Register* out = temps_.AllocateStack();
    TranslationContext await_block{cfg.AllocateBlock(), tc.frame};
    TranslationContext post_await_block{cfg.AllocateBlock(), tc.frame};

    emitDispatchEagerCoroResult(
        cfg, tc, out, await_block.block, post_await_block.block);

    tc.block = await_block.block;

    ++bc_it;
    int prev_prev_op = idx > 0 ? bc_instrs.at(idx - 1).opcode() : 0;
    emitGetAwaitable(cfg, tc, prev_prev_op, bc_instr.opcode());

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
  Register* result = temps_.AllocateStack();
  BinaryOpKind op_kind = get_bin_op_kind(bc_instr);
  tc.emit<BinaryOp>(result, op_kind, 0, left, right, tc.frame);
  stack.push(result);
}

static inline BinaryOpKind get_readonly_bin_op_kind(int readonly_op) {
#ifdef CINDER_PORTING_DONE
  switch (readonly_op) {
    case READONLY_BINARY_ADD: {
      return BinaryOpKind::kAdd;
    }
    case READONLY_BINARY_AND: {
      return BinaryOpKind::kAnd;
    }
    case READONLY_BINARY_FLOOR_DIVIDE: {
      return BinaryOpKind::kFloorDivide;
    }
    case READONLY_BINARY_LSHIFT: {
      return BinaryOpKind::kLShift;
    }
    case READONLY_BINARY_MATRIX_MULTIPLY: {
      return BinaryOpKind::kMatrixMultiply;
    }
    case READONLY_BINARY_MODULO: {
      return BinaryOpKind::kModulo;
    }
    case READONLY_BINARY_MULTIPLY: {
      return BinaryOpKind::kMultiply;
    }
    case READONLY_BINARY_OR: {
      return BinaryOpKind::kOr;
    }
    case READONLY_BINARY_POWER: {
      return BinaryOpKind::kPower;
    }
    case READONLY_BINARY_RSHIFT: {
      return BinaryOpKind::kRShift;
    }
    case READONLY_BINARY_SUBTRACT: {
      return BinaryOpKind::kSubtract;
    }
    case READONLY_BINARY_TRUE_DIVIDE: {
      return BinaryOpKind::kTrueDivide;
    }
    case READONLY_BINARY_XOR: {
      return BinaryOpKind::kXor;
    }
    default: {
      JIT_CHECK(false, "unhandled readonly binary op %d", readonly_op);
      // NOTREACHED
      break;
    }
  }
#else
  PORT_ASSERT("Need to handle not yet existing read-only opcodes");
  (void)readonly_op;
#endif
}

void HIRBuilder::emitReadonlyBinaryOp(
    TranslationContext& tc,
    int readonly_op,
    uint8_t readonly_flags) {
  auto& stack = tc.frame.stack;
  Register* right = stack.pop();
  Register* left = stack.pop();
  Register* result = temps_.AllocateStack();
  BinaryOpKind op_kind = get_readonly_bin_op_kind(readonly_op);
  tc.emit<BinaryOp>(result, op_kind, readonly_flags, left, right, tc.frame);
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
  Register* result = temps_.AllocateStack();
  InPlaceOpKind op_kind = get_inplace_op_kind(bc_instr);
  tc.emit<InPlaceOp>(result, op_kind, left, right, tc.frame);
  stack.push(result);
}

static inline UnaryOpKind get_unary_op_kind(
    const jit::BytecodeInstruction& bc_instr) {
  switch (bc_instr.opcode()) {
    case UNARY_NOT:
      return UnaryOpKind::kNot;

    case UNARY_NEGATIVE:
      return UnaryOpKind::kNegate;

    case UNARY_POSITIVE:
      return UnaryOpKind::kPositive;

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
  Register* result = temps_.AllocateStack();
  UnaryOpKind op_kind = get_unary_op_kind(bc_instr);
  tc.emit<UnaryOp>(result, op_kind, 0, operand, tc.frame);
  tc.frame.stack.push(result);
}

static inline UnaryOpKind get_readonly_unary_op_kind(int readonly_op) {
#ifdef CINDER_PORTING_DONE
  switch (readonly_op) {
    case READONLY_UNARY_NOT:
      return UnaryOpKind::kNot;

    case READONLY_UNARY_NEGATIVE:
      return UnaryOpKind::kPositive;

    case READONLY_UNARY_POSITIVE:
      return UnaryOpKind::kNegate;

    case READONLY_UNARY_INVERT:
      return UnaryOpKind::kInvert;

    default:
      JIT_CHECK(false, "unhandled readonly unary op %d", readonly_op);
      // NOTREACHED
      break;
  }
#else
  PORT_ASSERT("Need to handle not yet existing read-only opcodes");
  (void)readonly_op;
#endif
}

void HIRBuilder::emitReadonlyUnaryOp(
    TranslationContext& tc,
    int readonly_op,
    uint8_t readonly_flags) {
  Register* operand = tc.frame.stack.pop();
  Register* result = temps_.AllocateStack();
  UnaryOpKind op_kind = get_readonly_unary_op_kind(readonly_op);
  tc.emit<UnaryOp>(result, op_kind, readonly_flags, operand, tc.frame);
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
  Register* dst = temps_.AllocateStack();
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
  tc.emitVariadic<CallMethod>(temps_, num_operands, is_awaited, tc.frame);
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
  auto dst = temps_.AllocateStack();
  tc.emit<ListAppend>(dst, list, item, tc.frame);
}

void HIRBuilder::emitLoadIterableArg(
    CFG& cfg,
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto iterable = tc.frame.stack.pop();
  Register* tuple;
  if (iterable->type() != TTupleExact) {
    TranslationContext tuple_path{cfg.AllocateBlock(), tc.frame};
    tuple_path.snapshot();
    TranslationContext non_tuple_path{cfg.AllocateBlock(), tc.frame};
    non_tuple_path.snapshot();
    tc.emit<CondBranchCheckType>(
        iterable, TTuple, tuple_path.block, non_tuple_path.block);
    tc.block = cfg.AllocateBlock();
    tc.snapshot();

    tuple = temps_.AllocateStack();

    tuple_path.emit<Assign>(tuple, iterable);
    tuple_path.emit<Branch>(tc.block);

    non_tuple_path.emit<GetTuple>(tuple, iterable, tc.frame);
    non_tuple_path.emit<Branch>(tc.block);
  } else {
    tuple = iterable;
  }

  auto tmp = temps_.AllocateStack();
  auto tup_idx = temps_.AllocateStack();
  auto element = temps_.AllocateStack();
  tc.emit<LoadConst>(tmp, Type::fromCInt(bc_instr.oparg(), TCInt64));
  tc.emit<PrimitiveBox>(tup_idx, tmp, TCInt64, tc.frame);
  tc.emit<BinaryOp>(
      element, BinaryOpKind::kSubscript, 0, tuple, tup_idx, tc.frame);
  tc.frame.stack.push(element);
  tc.frame.stack.push(tuple);
}

bool HIRBuilder::tryEmitDirectMethodCall(
    const InvokeTarget& target,
    TranslationContext& tc,
    long nargs) {
  if (target.is_statically_typed || nargs == target.builtin_expected_nargs) {
    Instr* staticCall;
    Register* out = NULL;
    if (target.builtin_returns_void) {
      staticCall = tc.emit<CallStaticRetVoid>(nargs, target.builtin_c_func);
    } else {
      out = temps_.AllocateStack();
      Type ret_type =
          target.builtin_returns_error_code ? TCInt32 : target.return_type;
      staticCall =
          tc.emit<CallStatic>(nargs, out, target.builtin_c_func, ret_type);
    }

    auto& stack = tc.frame.stack;
    for (auto i = nargs - 1; i >= 0; i--) {
      Register* operand = stack.pop();
      staticCall->SetOperand(i, operand);
    }

    if (target.builtin_returns_error_code) {
      tc.emit<CheckNeg>(out, out, tc.frame);
    } else if (out != NULL && !(target.return_type.couldBe(TPrimitive))) {
      tc.emit<CheckExc>(out, out, tc.frame);
    }
    if (target.builtin_returns_void || target.builtin_returns_error_code) {
      // We could update the compiler so that void returning functions either
      // are only used in void contexts, or explicitly emit a LOAD_CONST None
      // when not used in a void context. For now we just produce None here (and
      // in _PyClassLoader_ConvertRet).
      Register* tmp = temps_.AllocateStack();
      tc.emit<LoadConst>(tmp, TNoneType);
      stack.push(tmp);
    } else {
      stack.push(out);
    }
    return true;
  }

  return false;
}

std::vector<Register*> HIRBuilder::setupStaticArgs(
    TranslationContext& tc,
    const InvokeTarget& target,
    long nargs) {
  auto arg_regs = std::vector<Register*>(nargs, nullptr);

  for (auto i = nargs - 1; i >= 0; i--) {
    arg_regs[i] = tc.frame.stack.pop();
  }

  // If we have patched a function that accepts/returns primitives,
  // but we couldn't emit a direct x64 call, we have to box any primitive args
  if (!target.primitive_arg_types.empty()) {
    for (auto [argnum, type] : target.primitive_arg_types) {
      Register* reg = arg_regs.at(argnum);
      auto boxed_primitive_tmp = temps_.AllocateStack();
      boxPrimitive(tc, boxed_primitive_tmp, reg, type);
      arg_regs[argnum] = boxed_primitive_tmp;
    }
  }

  return arg_regs;
}

void HIRBuilder::fixStaticReturn(
    TranslationContext& tc,
    Register* ret_val,
    Type ret_type) {
  Type boxed_ret = ret_type;
  if (boxed_ret <= TPrimitive) {
    boxed_ret = boxed_ret.asBoxed();
  }
  if (boxed_ret < TObject) {
    // TODO(T108048062): This should be a type check rather than a RefineType.
    tc.emit<RefineType>(ret_val, boxed_ret, ret_val);
  }

  // Since we are not doing an x64 call, we will get a boxed value; if the
  // function is supposed to return a primitive, we need to unbox it because
  // later code in the function will expect the primitive.
  if (ret_type <= TPrimitive) {
    unboxPrimitive(tc, ret_val, ret_val, ret_type);
  }
}

bool HIRBuilder::emitInvokeFunction(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr,
    bool is_awaited) {
  BorrowedRef<> arg = constArg(bc_instr);
  BorrowedRef<> descr = PyTuple_GET_ITEM(arg.get(), 0);
  long nargs = PyLong_AsLong(PyTuple_GET_ITEM(arg.get(), 1));

  const InvokeTarget& target = preloader_.invokeFunctionTarget(descr);

  Register* funcreg = temps_.AllocateStack();
  if (target.container_is_immutable) {
    // try to emit a direct x64 call (InvokeStaticFunction/CallStatic) if we can
    if (!target.uses_runtime_func) {
      if (target.is_function && target.is_statically_typed) {
        if (_PyJIT_CompileFunction(target.func()) == PYJIT_RESULT_RETRY) {
          JIT_DLOG(
              "Warning: recursive compile of '%s' failed as it is already "
              "being compiled",
              funcFullname(target.func()));
        }

        // Direct invoke is safe whether we succeeded in JIT-compiling or not,
        // it'll just have an extra indirection if not JIT compiled.
        Register* out = temps_.AllocateStack();
        Type typ = target.return_type;
        auto call =
            tc.emit<InvokeStaticFunction>(nargs, out, target.func(), typ);
        for (auto i = nargs - 1; i >= 0; i--) {
          Register* operand = tc.frame.stack.pop();
          call->SetOperand(i, operand);
        }
        call->setFrameState(tc.frame);

        tc.frame.stack.push(out);

        return false;
      } else if (
          target.is_builtin && tryEmitDirectMethodCall(target, tc, nargs)) {
        return false;
      }
    }

    // we couldn't emit an x64 call, but we know what object we'll vectorcall,
    // so load it directly
    tc.emit<LoadConst>(funcreg, Type::fromObject(target.callable));
  } else {
    // The target is patchable so we have to load it indirectly
    tc.emit<LoadFunctionIndirect>(
        target.indirect_ptr, descr, funcreg, tc.frame);
  }

  std::vector<Register*> arg_regs = setupStaticArgs(tc, target, nargs);

  Register* out = temps_.AllocateStack();
  VectorCallBase* call;
  if (target.container_is_immutable) {
    call = tc.emit<VectorCallStatic>(nargs + 1, out, is_awaited);
  } else {
    call = tc.emit<VectorCall>(nargs + 1, out, is_awaited);
  }
  for (auto i = 0; i < nargs; i++) {
    call->SetOperand(i + 1, arg_regs.at(i));
  }
  call->SetOperand(0, funcreg);
  call->setFrameState(tc.frame);

  fixStaticReturn(tc, out, target.return_type);
  tc.frame.stack.push(out);

  return true;
}

bool HIRBuilder::emitInvokeNative(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  BorrowedRef<> arg = constArg(bc_instr);
  BorrowedRef<> native_target_descr = PyTuple_GET_ITEM(arg.get(), 0);
  const NativeTarget& target =
      preloader_.invokeNativeTarget(native_target_descr);

  BorrowedRef<> signature = PyTuple_GET_ITEM(arg.get(), 1);

  // The last entry in the signature is the return type, so subtract 1
  Py_ssize_t nargs = PyTuple_GET_SIZE(signature.get()) - 1;

  Register* out = temps_.AllocateStack();
  Type typ = target.return_type;
  auto call = tc.emit<CallStatic>(nargs, out, target.callable, typ);
  for (auto i = nargs - 1; i >= 0; i--) {
    Register* operand = tc.frame.stack.pop();
    call->SetOperand(i, operand);
  }

  tc.frame.stack.push(out);
  return false;
}

bool HIRBuilder::emitInvokeMethod(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr,
    bool is_awaited) {
  BorrowedRef<> arg = constArg(bc_instr);
  BorrowedRef<> descr = PyTuple_GET_ITEM(arg.get(), 0);
  long nargs = PyLong_AsLong(PyTuple_GET_ITEM(arg.get(), 1)) + 1;
  bool is_classmethod = PyTuple_GET_SIZE(arg.get()) == 3 &&
      (PyTuple_GET_ITEM(arg.get(), 2) == Py_True);

  const InvokeTarget& target = preloader_.invokeMethodTarget(descr);

  if (target.is_builtin && tryEmitDirectMethodCall(target, tc, nargs)) {
    return false;
  }

  std::vector<Register*> arg_regs = setupStaticArgs(tc, target, nargs);

  Register* out = temps_.AllocateStack();
  auto call = tc.emit<InvokeMethod>(
      nargs, out, target.slot, is_awaited, is_classmethod);
  for (auto i = 0; i < nargs; i++) {
    call->SetOperand(i, arg_regs.at(i));
  }
  call->setFrameState(tc.frame);

  fixStaticReturn(tc, out, target.return_type);
  tc.frame.stack.push(out);

  return true;
}

void HIRBuilder::emitIsOp(TranslationContext& tc, int oparg) {
  auto& stack = tc.frame.stack;
  Register* right = stack.pop();
  Register* left = stack.pop();
  Register* result = temps_.AllocateStack();
  CompareOp op = oparg == 0 ? CompareOp::kIs : CompareOp::kIsNot;
  tc.emit<Compare>(result, op, /*readonly_mask=*/0, left, right, tc.frame);
  stack.push(result);
}

void HIRBuilder::emitContainsOp(
    TranslationContext& tc,
    int oparg,
    uint8_t readonly_mask) {
  auto& stack = tc.frame.stack;
  Register* right = stack.pop();
  Register* left = stack.pop();
  Register* result = temps_.AllocateStack();
  CompareOp op = oparg == 0 ? CompareOp::kIn : CompareOp::kNotIn;
  tc.emit<Compare>(result, op, readonly_mask, left, right, tc.frame);
  stack.push(result);
}

void HIRBuilder::emitCompareOp(
    TranslationContext& tc,
    int compare_op,
    uint8_t readonly_mask) {
  JIT_CHECK(compare_op >= Py_LT, "invalid op %d", compare_op);
  JIT_CHECK(compare_op <= Py_GE, "invalid op %d", compare_op);
  auto& stack = tc.frame.stack;
  Register* right = stack.pop();
  Register* left = stack.pop();
  Register* result = temps_.AllocateStack();
  CompareOp op = static_cast<CompareOp>(compare_op);
  tc.emit<Compare>(result, op, readonly_mask, left, right, tc.frame);
  stack.push(result);
}

void HIRBuilder::emitCopyDictWithoutKeys(TranslationContext& tc) {
  auto& stack = tc.frame.stack;
  Register* keys = stack.top();
  Register* subject = stack.top(1);
  Register* rest = temps_.AllocateStack();
  tc.emit<CopyDictWithoutKeys>(rest, subject, keys, tc.frame);
  stack.topPut(0, rest);
}

void HIRBuilder::emitGetLen(TranslationContext& tc) {
  FrameState state = tc.frame;
  auto& stack = tc.frame.stack;
  Register* obj = stack.top();
  Register* result = temps_.AllocateStack();
  tc.emit<GetLength>(result, obj, state);
  stack.push(result);
}

void HIRBuilder::emitJumpIf(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* var = tc.frame.stack.top();

  BCOffset true_offset, false_offset;
  bool check_truthy = true;
  switch (bc_instr.opcode()) {
    case JUMP_IF_NONZERO_OR_POP:
      check_truthy = false;
    case JUMP_IF_TRUE_OR_POP: {
      true_offset = bc_instr.GetJumpTarget();
      false_offset = bc_instr.NextInstrOffset();
      break;
    }
    case JUMP_IF_ZERO_OR_POP:
      check_truthy = false;
    case JUMP_IF_FALSE_OR_POP: {
      false_offset = bc_instr.GetJumpTarget();
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
    Register* tval = temps_.AllocateNonStack();
    // Registers that hold the result of `IsTruthy` are guaranteed to never be
    // the home of a value left on the stack at the end of a basic block, so we
    // don't need to worry about potentially storing a PyObject in them.
    tc.emit<IsTruthy>(tval, var, tc.frame);
    tc.emit<CondBranch>(tval, true_block, false_block);
  } else {
    tc.emit<CondBranch>(var, true_block, false_block);
  }
}

void HIRBuilder::emitDeleteAttr(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* receiver = tc.frame.stack.pop();
  tc.emit<DeleteAttr>(receiver, bc_instr.oparg(), tc.frame);
}

void HIRBuilder::emitLoadAttr(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* receiver = tc.frame.stack.pop();
  Register* result = temps_.AllocateStack();
  tc.emit<LoadAttr>(result, receiver, bc_instr.oparg(), tc.frame);
  tc.frame.stack.push(result);
}

void HIRBuilder::emitLoadMethod(
    TranslationContext& tc,
    Environment& env,
    const jit::BytecodeInstruction& bc_instr) {
  Register* receiver = tc.frame.stack.pop();
  env.allocateLoadMethodCache();
  Register* result = temps_.AllocateStack();
  Register* method_instance = temps_.AllocateStack();
  tc.emit<LoadMethod>(result, receiver, bc_instr.oparg(), tc.frame);
  tc.emit<GetLoadMethodInstance>(
      1, method_instance, std::vector<Register*>{receiver});
  tc.frame.stack.push(result);
  tc.frame.stack.push(method_instance);
}

void HIRBuilder::emitLoadMethodOrAttrSuper(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr,
    bool load_method) {
  Register* receiver = tc.frame.stack.pop();
  Register* type = tc.frame.stack.pop();
  Register* global_super = tc.frame.stack.pop();
  Register* result = temps_.AllocateStack();
  PyObject* oparg = PyTuple_GET_ITEM(code_->co_consts, bc_instr.oparg());
  int name_idx = PyLong_AsLong(PyTuple_GET_ITEM(oparg, 0));
  bool no_args_in_super_call = PyTuple_GET_ITEM(oparg, 1) == Py_True;
  if (load_method) {
    Register* method_instance = temps_.AllocateStack();
    tc.emit<LoadMethodSuper>(
        result,
        global_super,
        type,
        receiver,
        name_idx,
        no_args_in_super_call,
        tc.frame);
    tc.emit<GetLoadMethodInstance>(
        3,
        method_instance,
        std::vector<Register*>{receiver, global_super, type});
    tc.frame.stack.push(result);
    tc.frame.stack.push(method_instance);
  } else {
    tc.emit<LoadAttrSuper>(
        result,
        global_super,
        type,
        receiver,
        name_idx,
        no_args_in_super_call,
        tc.frame);
    tc.frame.stack.push(result);
  }
}

void HIRBuilder::emitLoadDeref(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  int idx = bc_instr.oparg();
  Register* src = tc.frame.cells[idx];
  Register* dst = temps_.AllocateStack();
  int frame_idx = tc.frame.locals.size() + idx;
  tc.emit<LoadCellItem>(dst, src);
  tc.emit<CheckVar>(dst, dst, getVarname(code_, frame_idx), tc.frame);
  tc.frame.stack.push(dst);
}

void HIRBuilder::emitStoreDeref(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* old = temps_.AllocateStack();
  Register* dst = tc.frame.cells[bc_instr.oparg()];
  Register* src = tc.frame.stack.pop();
  tc.emit<StealCellItem>(old, dst);
  tc.emit<SetCellItem>(dst, src, old);
}

void HIRBuilder::emitLoadAssertionError(
    TranslationContext& tc,
    Environment& env) {
  Register* result = temps_.AllocateStack();
  tc.emit<LoadConst>(
      result, Type::fromObject(env.addReference(PyExc_AssertionError)));
  tc.frame.stack.push(result);
}

void HIRBuilder::emitLoadClass(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* tmp = temps_.AllocateStack();
  auto pytype = preloader_.pyType(constArg(bc_instr));
  auto pytype_as_pyobj = BorrowedRef(pytype);
  tc.emit<LoadConst>(tmp, Type::fromObject(pytype_as_pyobj));
  tc.frame.stack.push(tmp);
}

void HIRBuilder::emitLoadConst(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* tmp = temps_.AllocateStack();
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
  tc.emit<CheckVar>(var, var, getVarname(code_, var_idx), tc.frame);
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

void HIRBuilder::emitLoadType(
    TranslationContext& tc,
    const jit::BytecodeInstruction&) {
  Register* instance = tc.frame.stack.pop();
  auto type = temps_.AllocateStack();
  tc.emit<LoadField>(
      type, instance, "ob_type", offsetof(PyObject, ob_type), TType);
  tc.frame.stack.push(type);
}

void HIRBuilder::emitConvertPrimitive(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* val = tc.frame.stack.pop();
  Register* out = temps_.AllocateStack();
  Type to_type = prim_type_to_type(bc_instr.oparg() >> 4);
  tc.emit<IntConvert>(out, val, to_type);
  tc.frame.stack.push(out);
}

void HIRBuilder::emitPrimitiveLoadConst(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* tmp = temps_.AllocateStack();
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

void HIRBuilder::emitPrimitiveBox(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* tmp = temps_.AllocateStack();
  Register* src = tc.frame.stack.pop();
  Type typ = prim_type_to_type(bc_instr.oparg());
  boxPrimitive(tc, tmp, src, typ);
  tc.frame.stack.push(tmp);
}

void HIRBuilder::emitPrimitiveUnbox(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* tmp = temps_.AllocateStack();
  Register* src = tc.frame.stack.pop();
  Type typ = prim_type_to_type(bc_instr.oparg());
  unboxPrimitive(tc, tmp, src, typ);
  tc.frame.stack.push(tmp);
}

void HIRBuilder::boxPrimitive(
    TranslationContext& tc,
    Register* dst,
    Register* src,
    Type type) {
  if (type <= TCBool) {
    tc.emit<PrimitiveBoxBool>(dst, src);
  } else {
    tc.emit<PrimitiveBox>(dst, src, type, tc.frame);
  }
}

void HIRBuilder::unboxPrimitive(
    TranslationContext& tc,
    Register* dst,
    Register* src,
    Type type) {
  tc.emit<PrimitiveUnbox>(dst, src, type);
  if (!(type <= (TCBool | TCDouble))) {
    Register* did_unbox_work = temps_.AllocateStack();
    tc.emit<IsNegativeAndErrOccurred>(did_unbox_work, dst, tc.frame);
  }
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
    case PRIM_OP_POW_UN_INT: {
      return BinaryOpKind::kPowerUnsigned;
    }
    case PRIM_OP_POW_INT:
    case PRIM_OP_POW_DBL: {
      return BinaryOpKind::kPower;
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
    case PRIM_OP_POW_INT:
    case PRIM_OP_POW_UN_INT:
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
    case PRIM_OP_MUL_DBL:
    case PRIM_OP_POW_DBL: {
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
    case SEQ_LIST:
    case SEQ_LIST_INEXACT:
    case SEQ_CHECKED_LIST:
    case SEQ_TUPLE:
      return TObject;
    case SEQ_ARRAY_INT64:
      return TCInt64;
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
  Register* result = temps_.AllocateStack();

  BinaryOpKind op_kind = get_primitive_bin_op_kind(bc_instr);

  if (is_double_binop(bc_instr.oparg())) {
    tc.emit<DoubleBinaryOp>(result, op_kind, left, right);
  } else {
    tc.emit<IntBinaryOp>(result, op_kind, left, right);
  }

  stack.push(result);
}

void HIRBuilder::emitPrimitiveCompare(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& stack = tc.frame.stack;
  Register* right = stack.pop();
  Register* left = stack.pop();
  Register* result = temps_.AllocateStack();
  PrimitiveCompareOp op;
  switch (bc_instr.oparg()) {
    case PRIM_OP_EQ_INT:
    case PRIM_OP_EQ_DBL:
      op = PrimitiveCompareOp::kEqual;
      break;
    case PRIM_OP_NE_INT:
    case PRIM_OP_NE_DBL:
      op = PrimitiveCompareOp::kNotEqual;
      break;
    case PRIM_OP_LT_INT:
      op = PrimitiveCompareOp::kLessThan;
      break;
    case PRIM_OP_LE_INT:
      op = PrimitiveCompareOp::kLessThanEqual;
      break;
    case PRIM_OP_GT_INT:
      op = PrimitiveCompareOp::kGreaterThan;
      break;
    case PRIM_OP_GE_INT:
      op = PrimitiveCompareOp::kGreaterThanEqual;
      break;
    case PRIM_OP_LT_UN_INT:
    case PRIM_OP_LT_DBL:
      op = PrimitiveCompareOp::kLessThanUnsigned;
      break;
    case PRIM_OP_LE_UN_INT:
    case PRIM_OP_LE_DBL:
      op = PrimitiveCompareOp::kLessThanEqualUnsigned;
      break;
    case PRIM_OP_GT_UN_INT:
    case PRIM_OP_GT_DBL:
      op = PrimitiveCompareOp::kGreaterThanUnsigned;
      break;
    case PRIM_OP_GE_UN_INT:
    case PRIM_OP_GE_DBL:
      op = PrimitiveCompareOp::kGreaterThanEqualUnsigned;
      break;
    default:
      JIT_CHECK(false, "unsupported comparison");
      break;
  }
  tc.emit<PrimitiveCompare>(result, op, left, right);
  stack.push(result);
}

void HIRBuilder::emitPrimitiveUnaryOp(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* value = tc.frame.stack.pop();
  Register* result = temps_.AllocateStack();
  PrimitiveUnaryOpKind op;
  switch (bc_instr.oparg()) {
    case PRIM_OP_NEG_INT: {
      op = PrimitiveUnaryOpKind::kNegateInt;
      tc.emit<PrimitiveUnaryOp>(result, op, value);
      break;
    }
    case PRIM_OP_INV_INT: {
      op = PrimitiveUnaryOpKind::kInvertInt;
      tc.emit<PrimitiveUnaryOp>(result, op, value);
      break;
    }
    case PRIM_OP_NOT_INT: {
      op = PrimitiveUnaryOpKind::kNotInt;
      tc.emit<PrimitiveUnaryOp>(result, op, value);
      break;
    }
    case PRIM_OP_NEG_DBL: {
      // For doubles, there's no easy way to unary negate a value, so just
      // multiply it by -1
      auto tmp = temps_.AllocateStack();
      tc.emit<LoadConst>(tmp, Type::fromCDouble(-1.0));
      tc.emit<DoubleBinaryOp>(result, BinaryOpKind::kMultiply, tmp, value);
      break;
    }
    default: {
      JIT_CHECK(false, "unsupported unary op");
      break;
    }
  }
  tc.frame.stack.push(result);
}

void HIRBuilder::emitFastLen(
    CFG& cfg,
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto result = temps_.AllocateStack();
  Register* collection;
  auto oparg = bc_instr.oparg();
  int inexact = oparg & FAST_LEN_INEXACT;
  std::size_t offset = 0;
  auto type = TBottom;

  oparg &= ~FAST_LEN_INEXACT;
  const char* name = "";
  if (oparg == FAST_LEN_LIST) {
    type = TListExact;
    offset = offsetof(PyVarObject, ob_size);
    name = "ob_size";
  } else if (oparg == FAST_LEN_TUPLE) {
    type = TTupleExact;
    offset = offsetof(PyVarObject, ob_size);
    name = "ob_size";
  } else if (oparg == FAST_LEN_ARRAY) {
    type = TArray;
    offset = offsetof(PyVarObject, ob_size);
    name = "ob_size";
  } else if (oparg == FAST_LEN_DICT) {
    type = TDictExact;
    offset = offsetof(PyDictObject, ma_used);
    name = "ma_used";
  } else if (oparg == FAST_LEN_SET) {
    type = TSetExact;
    offset = offsetof(PySetObject, used);
    name = "used";
  } else if (oparg == FAST_LEN_STR) {
    type = TUnicodeExact;
    // Note: In debug mode, the interpreter has an assert that
    // ensures the string is "ready", check PyUnicode_GET_LENGTH
    offset = offsetof(PyASCIIObject, length);
    name = "length";
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
    // TODO(T105038867): Remove once we have RefineTypeInsertion
    tc.emit<RefineType>(collection, type, collection);
  } else {
    collection = tc.frame.stack.pop();
  }

  tc.emit<LoadField>(result, collection, name, offset, TCInt64);
  tc.frame.stack.push(result);
}

void HIRBuilder::emitReadonlyOperation(
    CFG& cfg,
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
#ifdef CINDER_PORTING_DONE
  int oparg = bc_instr.oparg();
  PyObject* op_tuple = PyTuple_GET_ITEM(code_->co_consts, oparg);
  JIT_CHECK(op_tuple != nullptr, "op_tuple is nullptr");

  PyObject* opobj = PyTuple_GET_ITEM(op_tuple, 0);
  JIT_CHECK(opobj != nullptr, "opobj is nullptr");

  int op = PyLong_AsLong(opobj);
  constexpr size_t kFunctionMaskOffset =
      offsetof(PyFunctionObject, readonly_mask);
  switch (op) {
    case READONLY_MAKE_FUNCTION: {
      Register* func = tc.frame.stack.top();

      Register* mask_obj = temps_.AllocateStack();
      tc.emit<LoadConst>(
          mask_obj, Type::fromObject(PyTuple_GET_ITEM(op_tuple, 1)));

      Register* mask = temps_.AllocateStack();
      tc.emit<PrimitiveUnbox>(mask, mask_obj, TCUInt64);
      Register* previous = temps_.AllocateStack();
      tc.emit<LoadConst>(previous, TNullptr);

      tc.emit<StoreField>(
          func, "readonly_mask", kFunctionMaskOffset, mask, TCUInt64, previous);

      break;
    }
    case READONLY_CHECK_FUNCTION: {
      constexpr size_t kArgTupleNArgsIndex = 1;
      constexpr size_t kArgTupleMaskIndex = 2;
      constexpr size_t kArgTupleMethodFlagIndex = 3;

      PyObject* nargs_obj = PyTuple_GET_ITEM(op_tuple, kArgTupleNArgsIndex);
      PyObject* call_mask_obj = PyTuple_GET_ITEM(op_tuple, kArgTupleMaskIndex);
      PyObject* method_flag_obj =
          PyTuple_GET_ITEM(op_tuple, kArgTupleMethodFlagIndex);

      JIT_CHECK(nargs_obj != nullptr, "nargs_obj is nullptr");
      JIT_CHECK(call_mask_obj != nullptr, "call mask is nullptr");
      JIT_CHECK(method_flag_obj != nullptr, "method flag is nullptr");

      uint64_t objs_above_func = PyLong_AsUnsignedLongLong(nargs_obj);
      uint64_t call_mask = PyLong_AsUnsignedLong(call_mask_obj);
      uint64_t method_flag = PyLong_AsUnsignedLongLong(method_flag_obj);

      Register* initial_func = tc.frame.stack.peek(objs_above_func + 1);
      JIT_CHECK(
          initial_func != nullptr,
          "func is null on stack[-%d]",
          objs_above_func + 1);
      Register* func = temps_.AllocateStack();
      Register* call_mask_reg = temps_.AllocateNonStack();

      BasicBlock* done_block = cfg.AllocateBlock();
      BasicBlock* func_block = cfg.AllocateBlock();
      BasicBlock* default_func_block = cfg.AllocateBlock();

      // check whether the mask for non-methods will change
      uint64_t arg_call_mask = CLEAR_NONARG_READONLY_MASK(call_mask);
      uint64_t nonarg_call_mask = GET_NONARG_READONLY_MASK(call_mask);
      uint64_t non_method_call_mask = nonarg_call_mask | (arg_call_mask >> 1);
      bool call_mask_change = non_method_call_mask != call_mask;

      // generates logic that loads the mask and dispatch to func_block
      auto load_func_and_check = [&](Register* f, uint64_t mask) {
        // TODO(Shiyu): if call_mask_reg ends up being the same in both
        // non-method and default cases, LIR generation fails with the Phi node
        // missing a def. Therefore the `if(call_mask_change)` checks are
        // necessary
        if (call_mask_change) {
          tc.emit<LoadConst>(call_mask_reg, Type::fromCUInt(mask, TCUInt64));
        }
        tc.emit<Assign>(func, f);
        tc.emit<CondBranchCheckType>(func, TFunc, func_block, done_block);
      };

      if (method_flag != 0) {
        JIT_CHECK(method_flag == 1, "wrong flag %d", method_flag);
        // LOAD_METHOD case. Need to confirm whether LOAD_METHOD
        // put a method on stack or not
        BasicBlock* no_method_block = cfg.AllocateBlock();
        // in the case of LOAD_METHOD not finding a method, initial_func is None
        tc.emit<CondBranchCheckType>(
            initial_func, TNoneType, no_method_block, default_func_block);

        tc.block = no_method_block;

        // if func is None, the real callable is at stack[-objs_above_func]
        Register* non_method_func = tc.frame.stack.peek(objs_above_func);
        JIT_CHECK(
            non_method_func != nullptr,
            "non method func is null on stack[-%d]",
            objs_above_func);
        load_func_and_check(non_method_func, non_method_call_mask);
      } else {
        tc.emit<Branch>(default_func_block);
      }

      tc.block = default_func_block;
      load_func_and_check(initial_func, call_mask);

      tc.block = func_block;

      tc.emit<RefineType>(func, TFunc, func);
      Register* func_mask_reg = temps_.AllocateStack();
      tc.emit<LoadField>(
          func_mask_reg, func, "readonly_mask", kFunctionMaskOffset, TCUInt64);

      // if method and non-method masks are the same, previous blocks will skip
      // loading the mask. Therefore load the mask here
      if (!call_mask_change) {
        tc.emit<LoadConst>(call_mask_reg, Type::fromCUInt(call_mask, TCUInt64));
      }
      Register* args[] = {func, func_mask_reg, call_mask_reg};
      constexpr int kNumArgs = sizeof(args) / sizeof(Register*);
      auto static_call = tc.emit<CallStaticRetVoid>(
          kNumArgs, reinterpret_cast<void*>(PyFunction_ReportReadonlyErr));
      for (int i = 0; i < kNumArgs; i++) {
        static_call->SetOperand(i, args[i]);
      }
      tc.emit<Branch>(done_block);

      tc.block = done_block;
      break;
    }
    case READONLY_CHECK_LOAD_ATTR: {
      PyObject* check_return = PyTuple_GET_ITEM(op_tuple, 1);
      PyObject* check_read = PyTuple_GET_ITEM(op_tuple, 2);

      assert(check_return && check_read);
      assert(check_return == Py_True || check_read == Py_True);
      Register* obj = tc.frame.stack.top();

      Register* check_return_reg = temps_.AllocateStack();
      tc.emit<LoadConst>(
          check_return_reg, Type::fromCInt(check_return == Py_True, TCInt32));
      Register* check_read_reg = temps_.AllocateStack();
      tc.emit<LoadConst>(
          check_read_reg, Type::fromCInt(check_read == Py_True, TCInt32));

      Register* args[] = {obj, check_return_reg, check_read_reg};
      constexpr size_t kNumArgs = sizeof(args) / sizeof(Register*);
      auto static_call = tc.emit<CallStaticRetVoid>(
          kNumArgs, reinterpret_cast<void*>(PyReadonly_Check_LoadAttr));

      for (size_t i = 0; i < kNumArgs; i++) {
        static_call->SetOperand(i, args[i]);
      }
      break;
    }
    case READONLY_BINARY_ADD:
    case READONLY_BINARY_SUBTRACT:
    case READONLY_BINARY_MULTIPLY:
    case READONLY_BINARY_MATRIX_MULTIPLY:
    case READONLY_BINARY_TRUE_DIVIDE:
    case READONLY_BINARY_FLOOR_DIVIDE:
    case READONLY_BINARY_MODULO:
    case READONLY_BINARY_POWER:
    case READONLY_BINARY_LSHIFT:
    case READONLY_BINARY_RSHIFT:
    case READONLY_BINARY_OR:
    case READONLY_BINARY_XOR:
    case READONLY_BINARY_AND: {
      PyObject* mask = PyTuple_GET_ITEM(op_tuple, 1);
      JIT_CHECK(mask != nullptr, "mask is nullptr");
      emitReadonlyBinaryOp(tc, op, PyLong_AsUnsignedLongLong(mask));
      break;
    }
    case READONLY_UNARY_INVERT:
    case READONLY_UNARY_NEGATIVE:
    case READONLY_UNARY_POSITIVE:
    case READONLY_UNARY_NOT: {
      PyObject* mask = PyTuple_GET_ITEM(op_tuple, 1);
      JIT_CHECK(mask != nullptr, "mask is nullptr");
      emitReadonlyUnaryOp(tc, op, PyLong_AsUnsignedLongLong(mask));
      break;
    }

    case READONLY_COMPARE_OP: {
      PyObject* mask = PyTuple_GET_ITEM(op_tuple, 1);
      JIT_CHECK(mask != nullptr, "mask is nullptr");
      PyObject* compareOp = PyTuple_GET_ITEM(op_tuple, 2);
      JIT_CHECK(compareOp != nullptr, "compare op is nullptr");
      emitCompareOp(
          tc,
          PyLong_AsUnsignedLongLong(compareOp),
          PyLong_AsUnsignedLongLong(mask));
      break;
    }
    case READONLY_GET_ITER: {
      PyObject* mask = PyTuple_GET_ITEM(op_tuple, 1);
      JIT_CHECK(mask != nullptr, "mask is nullptr");
      emitGetIter(tc, PyLong_AsUnsignedLongLong(mask));
      break;
    }
    case READONLY_FOR_ITER: {
      PyObject* mask = PyTuple_GET_ITEM(op_tuple, 1);
      JIT_CHECK(mask != nullptr, "mask is nullptr");
      emitForIter(tc, bc_instr, PyLong_AsUnsignedLongLong(mask));
      break;
    }
  }
#else
  PORT_ASSERT("Need to handle not yet existing read-only opcodes");
  (void)cfg;
  (void)tc;
  (void)bc_instr;
#endif
}

void HIRBuilder::emitRefineType(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Type type = preloader_.type(constArg(bc_instr));
  Register* dst = tc.frame.stack.top();
  tc.emit<RefineType>(dst, type, dst);
}

void HIRBuilder::emitSequenceGet(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& stack = tc.frame.stack;
  auto idx = stack.pop();
  auto sequence = stack.pop();
  auto oparg = bc_instr.oparg();
  if (oparg == SEQ_LIST_INEXACT) {
    auto type = temps_.AllocateStack();
    tc.emit<LoadField>(
        type, sequence, "ob_type", offsetof(PyObject, ob_type), TType);
    tc.emit<GuardIs>(type, (PyObject*)&PyList_Type, type);
    tc.emit<RefineType>(sequence, TListExact, sequence);
  }

  Register* adjusted_idx;
  int unchecked = oparg & SEQ_SUBSCR_UNCHECKED;
  if (!unchecked) {
    adjusted_idx = temps_.AllocateStack();
    tc.emit<CheckSequenceBounds>(adjusted_idx, sequence, idx, tc.frame);
  } else {
    adjusted_idx = idx;
    oparg &= ~SEQ_SUBSCR_UNCHECKED;
  }
  auto ob_item = temps_.AllocateStack();
  auto result = temps_.AllocateStack();
  if (oparg == SEQ_LIST || oparg == SEQ_LIST_INEXACT ||
      oparg == SEQ_CHECKED_LIST) {
    int offset = offsetof(PyListObject, ob_item);
    tc.emit<LoadField>(ob_item, sequence, "ob_item", offset, TCPtr);
  } else if (oparg == SEQ_ARRAY_INT64) {
    Register* offset_reg = temps_.AllocateStack();
    tc.emit<LoadConst>(
        offset_reg,
        Type::fromCInt(offsetof(PyStaticArrayObject, ob_item), TCInt64));
    tc.emit<LoadFieldAddress>(ob_item, sequence, offset_reg);
  } else {
    JIT_CHECK(false, "Unsupported oparg for SEQUENCE_GET: %d", oparg);
  }

  auto type = element_type_from_seq_type(oparg);
  tc.emit<LoadArrayItem>(
      result, ob_item, adjusted_idx, sequence, /*offset=*/0, type);
  stack.push(result);
}

void HIRBuilder::emitSequenceRepeat(
    CFG& cfg,
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& stack = tc.frame.stack;
  Register* num;
  Register* seq;
  auto result = temps_.AllocateStack();
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
      // TODO(T105038867): Remove once we have RefineTypeInsertion
      tc.emit<RefineType>(num, TLongExact, num);
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
      // TODO(T105038867): Remove once we have RefineTypeInsertion
      tc.emit<RefineType>(
          seq, (oparg == SEQ_LIST) ? TListExact : TTupleExact, seq);
    }
  } else {
    num = stack.pop();
    seq = stack.pop();
  }

  if (!primitive_num) {
    auto unboxed_num = temps_.AllocateStack();
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
  auto adjusted_idx = temps_.AllocateStack();
  auto oparg = bc_instr.oparg();
  if (oparg == SEQ_LIST_INEXACT) {
    auto type = temps_.AllocateStack();
    tc.emit<LoadField>(
        type, sequence, "ob_type", offsetof(PyObject, ob_type), TType);
    tc.emit<GuardIs>(type, (PyObject*)&PyList_Type, type);
    tc.emit<RefineType>(sequence, TListExact, sequence);
  }
  tc.emit<CheckSequenceBounds>(adjusted_idx, sequence, idx, tc.frame);
  auto ob_item = temps_.AllocateStack();
  if (oparg == SEQ_ARRAY_INT64) {
    Register* offset_reg = temps_.AllocateStack();
    tc.emit<LoadConst>(
        offset_reg,
        Type::fromCInt(offsetof(PyStaticArrayObject, ob_item), TCInt64));
    tc.emit<LoadFieldAddress>(ob_item, sequence, offset_reg);
  } else if (oparg == SEQ_LIST || oparg == SEQ_LIST_INEXACT) {
    int offset = offsetof(PyListObject, ob_item);
    tc.emit<LoadField>(ob_item, sequence, "ob_item", offset, TCPtr);
  } else {
    JIT_CHECK(false, "Unsupported oparg for SEQUENCE_SET: %d", oparg);
  }
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
  Register* result = temps_.AllocateStack();

  auto try_fast_path = [&] {
    BorrowedRef<> value = preloader_.global(name_idx);
    if (value == nullptr) {
      return false;
    }
    tc.emit<LoadGlobalCached>(
        result, code_, preloader_.builtins(), preloader_.globals(), name_idx);
    auto guard_is = tc.emit<GuardIs>(result, value, result);
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
  Register* func = temps_.AllocateStack();
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

void HIRBuilder::emitFunctionCredential(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  int oparg = bc_instr.oparg();
  JIT_CHECK(
      oparg < PyTuple_Size(code_->co_consts),
      "FUNC_CREDENTIAL index out of bounds");
  Register* fc_tuple = temps_.AllocateStack();
  tc.emit<LoadConst>(
      fc_tuple, Type::fromObject(PyTuple_GET_ITEM(code_->co_consts, oparg)));
  Register* fc = temps_.AllocateStack();
  tc.emitChecked<CallCFunc>(
      1, fc, CallCFunc::Func::kfunc_cred_new, std::vector<Register*>{fc_tuple});

  tc.frame.stack.push(fc);
}

void HIRBuilder::emitMakeListTuple(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  bool is_tuple = (bc_instr.opcode() == BUILD_TUPLE);
  auto num_elems = static_cast<size_t>(bc_instr.oparg());
  auto dst = temps_.AllocateStack();
  tc.emit<MakeListTuple>(is_tuple, dst, num_elems, tc.frame);
  auto init_lt = tc.emit<InitListTuple>(num_elems + 1, is_tuple);
  init_lt->SetOperand(0, dst);
  for (size_t i = num_elems; i > 0; i--) {
    auto opnd = tc.frame.stack.pop();
    init_lt->SetOperand(i, opnd);
  }
  auto new_dst = temps_.AllocateStack();
  tc.emit<Assign>(new_dst, dst);
  tc.frame.stack.push(new_dst);
}

void HIRBuilder::emitListExtend(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* iterable = tc.frame.stack.pop();
  Register* list = tc.frame.stack.peek(bc_instr.oparg());
  Register* none = temps_.AllocateStack();
  tc.emit<ListExtend>(none, list, iterable, tc.frame);
}

void HIRBuilder::emitListToTuple(TranslationContext& tc) {
  Register* list = tc.frame.stack.pop();
  Register* tuple = temps_.AllocateStack();
  tc.emit<MakeTupleFromList>(tuple, list, tc.frame);
  tc.frame.stack.push(tuple);
}

void HIRBuilder::emitBuildCheckedList(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  BorrowedRef<> arg = constArg(bc_instr);
  BorrowedRef<> descr = PyTuple_GET_ITEM(arg.get(), 0);
  Py_ssize_t list_size = PyLong_AsLong(PyTuple_GET_ITEM(arg.get(), 1));

  Type type = preloader_.type(descr);
  JIT_CHECK(
      Ci_CheckedList_TypeCheck(type.uniquePyType()),
      "expected CheckedList type");

  Register* list = temps_.AllocateStack();
  tc.emit<MakeCheckedList>(list, list_size, type, tc.frame);
  // Fill list
  auto init_checked_list = tc.emit<InitListTuple>(list_size + 1, false);
  init_checked_list->SetOperand(0, list);
  for (size_t i = list_size; i > 0; i--) {
    auto operand = tc.frame.stack.pop();
    init_checked_list->SetOperand(i, operand);
  }
  tc.frame.stack.push(list);
}

void HIRBuilder::emitBuildCheckedMap(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  BorrowedRef<> arg = constArg(bc_instr);
  BorrowedRef<> descr = PyTuple_GET_ITEM(arg.get(), 0);
  Py_ssize_t dict_size = PyLong_AsLong(PyTuple_GET_ITEM(arg.get(), 1));

  Type type = preloader_.type(descr);
  JIT_CHECK(
      Ci_CheckedDict_TypeCheck(type.uniquePyType()),
      "expected CheckedDict type");

  Register* dict = temps_.AllocateStack();
  tc.emit<MakeCheckedDict>(dict, dict_size, type, tc.frame);
  // Fill dict
  auto& stack = tc.frame.stack;
  for (auto i = stack.size() - dict_size * 2, end = stack.size(); i < end;
       i += 2) {
    auto key = stack.at(i);
    auto value = stack.at(i + 1);
    auto result = temps_.AllocateStack();
    tc.emit<SetDictItem>(result, dict, key, value, tc.frame);
  }
  stack.discard(dict_size * 2);
  stack.push(dict);
}

void HIRBuilder::emitBuildMap(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto dict_size = bc_instr.oparg();
  Register* dict = temps_.AllocateStack();
  tc.emit<MakeDict>(dict, dict_size, tc.frame);
  // Fill dict
  auto& stack = tc.frame.stack;
  for (auto i = stack.size() - dict_size * 2, end = stack.size(); i < end;
       i += 2) {
    auto key = stack.at(i);
    auto value = stack.at(i + 1);
    auto result = temps_.AllocateStack();
    tc.emit<SetDictItem>(result, dict, key, value, tc.frame);
  }
  stack.discard(dict_size * 2);
  stack.push(dict);
}

void HIRBuilder::emitBuildSet(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* set = temps_.AllocateStack();
  tc.emit<MakeSet>(set, tc.frame);

  int oparg = bc_instr.oparg();
  for (int i = oparg; i > 0; i--) {
    auto item = tc.frame.stack.peek(i);

    auto result = temps_.AllocateStack();
    tc.emit<SetSetItem>(result, set, item, tc.frame);
  }

  tc.frame.stack.discard(oparg);

  tc.frame.stack.push(set);
}

void HIRBuilder::emitBuildConstKeyMap(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto dict_size = bc_instr.oparg();
  Register* dict = temps_.AllocateStack();
  tc.emit<MakeDict>(dict, dict_size, tc.frame);
  // Fill dict
  auto& stack = tc.frame.stack;
  Register* keys = stack.pop();
  // ceval.c checks the type and size of the keys tuple before proceeding; we
  // intentionally skip that here.
  for (auto i = 0; i < dict_size; ++i) {
    Register* key = temps_.AllocateStack();
    tc.emit<LoadTupleItem>(key, keys, i);
    Register* value = stack.at(stack.size() - dict_size + i);
    Register* result = temps_.AllocateStack();
    tc.emit<SetDictItem>(result, dict, key, value, tc.frame);
  }
  stack.discard(dict_size);
  stack.push(dict);
}

void HIRBuilder::emitPopJumpIf(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* var = tc.frame.stack.pop();
  BCOffset true_offset, false_offset;
  switch (bc_instr.opcode()) {
    case POP_JUMP_IF_ZERO:
    case POP_JUMP_IF_FALSE: {
      true_offset = bc_instr.NextInstrOffset();
      false_offset = bc_instr.GetJumpTarget();
      break;
    }
    case POP_JUMP_IF_NONZERO:
    case POP_JUMP_IF_TRUE: {
      true_offset = bc_instr.GetJumpTarget();
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
    Register* tval = temps_.AllocateNonStack();
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
  Register* result = temps_.AllocateStack();
  tc.emit<StoreAttr>(result, receiver, value, bc_instr.oparg(), tc.frame);
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
        tmp = temps_.AllocateStack();
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
  Register* result = temps_.AllocateStack();
  tc.emit<StoreSubscr>(result, container, sub, value, tc.frame);
}

void HIRBuilder::emitGetIter(TranslationContext& tc, uint8_t readonly_mask) {
  Register* iterable = tc.frame.stack.pop();
  Register* result = temps_.AllocateStack();
  tc.emit<GetIter>(result, iterable, readonly_mask, tc.frame);
  tc.frame.stack.push(result);
}

void HIRBuilder::emitForIter(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr,
    uint8_t readonly_mask) {
  Register* iterator = tc.frame.stack.top();
  Register* next_val = temps_.AllocateStack();
  tc.emit<InvokeIterNext>(next_val, iterator, readonly_mask, tc.frame);
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
  Register* iter_out = temps_.AllocateStack();
  tc.emit<CondBranchCheckType>(iter_in, TGen, nop_block, slow_path);

  tc.block = slow_path;
  tc.emit<GetIter>(iter_out, iter_in, 0, tc.frame);
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

  Register* tuple = temps_.AllocateStack();
  tc.emit<UnpackExToTuple>(tuple, seq, arg_before, arg_after, tc.frame);

  int total_args = arg_before + arg_after + 1;
  for (int i = total_args - 1; i >= 0; i--) {
    Register* item = temps_.AllocateStack();
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
  BasicBlock* list_check_path = cfg.AllocateBlock();
  BasicBlock* list_fast_path = cfg.AllocateBlock();
  BasicBlock* tuple_fast_path = cfg.AllocateBlock();
  Register* list_mem = temps_.AllocateStack();
  stack.pop();
  // TODO: The manual type checks and branches should go away once we get
  // PGO support to be able to optimize to known types.
  tc.emit<CondBranchCheckType>(
      seq, TTupleExact, tuple_fast_path, list_check_path);

  tc.block = list_check_path;
  tc.emit<CondBranchCheckType>(
      seq, TListExact, list_fast_path, deopt_path.block);

  tc.block = tuple_fast_path;
  Register* offset_reg = temps_.AllocateStack();
  tc.emit<LoadConst>(
      offset_reg, Type::fromCInt(offsetof(PyTupleObject, ob_item), TCInt64));
  tc.emit<LoadFieldAddress>(list_mem, seq, offset_reg);
  tc.emit<Branch>(fast_path);

  tc.block = list_fast_path;
  tc.emit<LoadField>(
      list_mem, seq, "ob_item", offsetof(PyListObject, ob_item), TCPtr);
  tc.emit<Branch>(fast_path);

  tc.block = fast_path;

  Register* seq_size = temps_.AllocateStack();
  Register* target_size = temps_.AllocateStack();
  Register* is_equal = temps_.AllocateStack();
  tc.emit<LoadVarObjectSize>(seq_size, seq);
  tc.emit<LoadConst>(target_size, Type::fromCInt(bc_instr.oparg(), TCInt64));
  tc.emit<PrimitiveCompare>(
      is_equal, PrimitiveCompareOp::kEqual, seq_size, target_size);
  fast_path = cfg.AllocateBlock();
  tc.emit<CondBranch>(is_equal, fast_path, deopt_path.block);
  tc.block = fast_path;

  Register* idx_reg = temps_.AllocateStack();
  for (int idx = bc_instr.oparg() - 1; idx >= 0; --idx) {
    Register* item = temps_.AllocateStack();
    tc.emit<LoadConst>(idx_reg, Type::fromCInt(idx, TCInt64));
    tc.emit<LoadArrayItem>(item, list_mem, idx_reg, seq, 0, TObject);
    stack.push(item);
  }
}

void HIRBuilder::emitSetupFinally(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  BCOffset handler_off =
      bc_instr.NextInstrOffset() + BCIndex{bc_instr.oparg()}.asOffset();
  int stack_level = tc.frame.stack.size();
  tc.frame.block_stack.push(
      ExecutionBlock{SETUP_FINALLY, handler_off, stack_level});
}

void HIRBuilder::emitAsyncForHeaderYieldFrom(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  Register* send_value = tc.frame.stack.pop();
  Register* awaitable = tc.frame.stack.top();
  Register* out = temps_.AllocateStack();
  if (code_->co_flags & CO_COROUTINE) {
    tc.emit<SetCurrentAwaiter>(awaitable);
  }
  tc.emit<YieldFromHandleStopAsyncIteration>(
      out, send_value, awaitable, tc.frame);
  tc.frame.stack.pop();
  tc.frame.stack.push(out);

  BasicBlock* yf_cont_block = getBlockAtOff(bc_instr.NextInstrOffset());
  BCOffset handler_off{tc.frame.block_stack.top().handler_off};
  BasicBlock* yf_done_block = getBlockAtOff(handler_off);
  tc.emit<CondBranchIterNotDone>(out, yf_cont_block, yf_done_block);
}

void HIRBuilder::emitEndAsyncFor(TranslationContext& tc) {
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
  Register* out = temps_.AllocateStack();
  tc.emit<GetAIter>(out, obj, tc.frame);
  tc.frame.stack.push(out);
}

void HIRBuilder::emitGetANext(TranslationContext& tc) {
  Register* obj = tc.frame.stack.top();
  Register* out = temps_.AllocateStack();
  tc.emit<GetANext>(out, obj, tc.frame);
  tc.frame.stack.push(out);
}

Register* HIRBuilder::emitSetupWithCommon(
    TranslationContext& tc,
    _Py_Identifier* enter_id,
    _Py_Identifier* exit_id) {
  // Load the enter and exit attributes from the manager, push exit, and return
  // the result of calling enter().
  auto& stack = tc.frame.stack;
  Register* manager = stack.pop();
  Register* enter = temps_.AllocateStack();
  Register* exit = temps_.AllocateStack();
  tc.emit<LoadAttrSpecial>(enter, manager, enter_id, tc.frame);
  tc.emit<LoadAttrSpecial>(exit, manager, exit_id, tc.frame);
  stack.push(exit);

  Register* enter_result = temps_.AllocateStack();
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
      emitSetupWithCommon(tc, &PyId___aenter__, &PyId___aexit__));
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
      emitSetupWithCommon(tc, &PyId___enter__, &PyId___exit__);
  emitSetupFinally(tc, bc_instr);
  tc.frame.stack.push(enter_result);
}

void HIRBuilder::emitLoadField(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& [offset, type, name] = preloader_.fieldInfo(constArg(bc_instr));

  Register* receiver = tc.frame.stack.pop();
  Register* result = temps_.AllocateStack();
  const char* field_name = PyUnicode_AsUTF8(name);
  if (field_name == nullptr) {
    PyErr_Clear();
    field_name = "";
  }
  tc.emit<LoadField>(result, receiver, field_name, offset, type);
  if (type.couldBe(TNullptr)) {
    CheckField* cf = tc.emit<CheckField>(result, result, name, tc.frame);
    cf->setGuiltyReg(receiver);
  }
  tc.frame.stack.push(result);
}

void HIRBuilder::emitStoreField(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& [offset, type, name] = preloader_.fieldInfo(constArg(bc_instr));
  const char* field_name = PyUnicode_AsUTF8(name);
  if (field_name == nullptr) {
    PyErr_Clear();
    field_name = "";
  }

  Register* receiver = tc.frame.stack.pop();
  Register* value = tc.frame.stack.pop();
  Register* previous = temps_.AllocateStack();
  if (type <= TPrimitive) {
    Register* converted = temps_.AllocateStack();
    tc.emit<LoadConst>(previous, TNullptr);
    tc.emit<IntConvert>(converted, value, type);
    value = converted;
  } else {
    tc.emit<LoadField>(previous, receiver, field_name, offset, type, false);
  }
  tc.emit<StoreField>(receiver, field_name, offset, value, type, previous);
}

void HIRBuilder::emitCast(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& [pytype, opt, exact] = preloader_.pyTypeOpt(constArg(bc_instr));
  Register* value = tc.frame.stack.pop();
  Register* result = temps_.AllocateStack();
  tc.emit<Cast>(result, value, pytype, opt, exact, tc.frame);
  tc.frame.stack.push(result);
}

void HIRBuilder::emitTpAlloc(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto pytype = preloader_.pyType(constArg(bc_instr));

  Register* result = temps_.AllocateStack();
  tc.emit<TpAlloc>(result, pytype, tc.frame);
  tc.frame.stack.push(result);
}

void HIRBuilder::emitImportFrom(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& stack = tc.frame.stack;
  Register* name = stack.top();
  Register* res = temps_.AllocateStack();
  tc.emit<ImportFrom>(res, name, bc_instr.oparg(), tc.frame);
  stack.push(res);
}

void HIRBuilder::emitImportName(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& stack = tc.frame.stack;
  Register* fromlist = stack.pop();
  Register* level = stack.pop();
  Register* res = temps_.AllocateStack();
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
  auto iter = stack.top();
  if (code_->co_flags & CO_COROUTINE) {
    tc.emit<SetCurrentAwaiter>(iter);
  }
  tc.emit<YieldFrom>(out, send_value, iter, tc.frame);
  stack.pop();
  stack.push(out);
}

void HIRBuilder::emitYieldValue(TranslationContext& tc) {
  auto& stack = tc.frame.stack;
  auto in = stack.pop();
  auto out = temps_.AllocateStack();
  if (code_->co_flags & CO_ASYNC_GENERATOR) {
    tc.emitChecked<CallCFunc>(
        1,
        out,
        CallCFunc::Func::k_PyAsyncGenValueWrapperNew,
        std::vector<Register*>{in});
    in = out;
    out = temps_.AllocateStack();
  }
  tc.emit<YieldValue>(out, in, tc.frame);
  stack.push(out);
}

void HIRBuilder::emitGetAwaitable(
    CFG& cfg,
    TranslationContext& tc,
    int prev_prev_op,
    int prev_op) {
  OperandStack& stack = tc.frame.stack;
  Register* iterable = stack.pop();
  Register* iter = temps_.AllocateStack();

  // Most work is done by existing _PyCoro_GetAwaitableIter() utility.
  tc.emit<CallCFunc>(
      1,
      iter,
      CallCFunc::Func::k_PyCoro_GetAwaitableIter,
      std::vector<Register*>{iterable});
  if (prev_op == BEFORE_ASYNC_WITH || prev_op == WITH_EXCEPT_START ||
      (prev_op == CALL_FUNCTION && prev_prev_op == DUP_TOP)) {
    BasicBlock* error_block = cfg.AllocateBlock();
    BasicBlock* ok_block = cfg.AllocateBlock();
    tc.emit<CondBranch>(iter, ok_block, error_block);
    tc.block = error_block;
    Register* type = temps_.AllocateStack();
    tc.emit<LoadField>(
        type, iterable, "ob_type", offsetof(PyObject, ob_type), TType);
    tc.emit<RaiseAwaitableError>(type, prev_prev_op, prev_op, tc.frame);

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
  Register* yf = temps_.AllocateStack();
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
    fmt_spec = temps_.AllocateStack();
    tc.emit<LoadConst>(fmt_spec, TNullptr);
  }
  Register* value = tc.frame.stack.pop();
  Register* dst = temps_.AllocateStack();
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

  auto result = temps_.AllocateStack();
  tc.emit<SetDictItem>(result, map, key, value, tc.frame);
}

void HIRBuilder::emitSetAdd(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto oparg = bc_instr.oparg();
  auto& stack = tc.frame.stack;

  auto* v = stack.pop();
  auto* set = stack.peek(oparg);

  auto result = temps_.AllocateStack();
  tc.emit<SetSetItem>(result, set, v, tc.frame);
}

void HIRBuilder::emitSetUpdate(
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto oparg = bc_instr.oparg();
  auto& stack = tc.frame.stack;
  auto* iterable = stack.pop();
  auto* set = stack.peek(oparg);
  auto result = temps_.AllocateStack();
  tc.emit<SetUpdate>(result, set, iterable, tc.frame);
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
  Register* wh_coro_or_result = temps_.AllocateStack();
  Register* wh_waiter = temps_.AllocateStack();
  has_wh_block.emit<WaitHandleLoadCoroOrResult>(wh_coro_or_result, wait_handle);
  has_wh_block.emit<WaitHandleLoadWaiter>(wh_waiter, wait_handle);
  has_wh_block.emit<WaitHandleRelease>(wait_handle);

  TranslationContext coro_block{cfg.AllocateBlock(), tc.frame};
  TranslationContext res_block{cfg.AllocateBlock(), tc.frame};
  has_wh_block.emit<CondBranch>(wh_waiter, coro_block.block, res_block.block);

  if (code_->co_flags & CO_COROUTINE) {
    coro_block.emit<SetCurrentAwaiter>(wh_coro_or_result);
  }
  coro_block.emit<YieldAndYieldFrom>(
      out, wh_waiter, wh_coro_or_result, tc.frame);
  coro_block.emit<Branch>(post_await_block);

  res_block.emit<Assign>(out, wh_coro_or_result);
  res_block.emit<Branch>(post_await_block);
}

void HIRBuilder::emitMatchMappingSequence(
    CFG& cfg,
    TranslationContext& tc,
    uint64_t tf_flag) {
  Register* top = tc.frame.stack.top();
  auto type = temps_.AllocateStack();
  tc.emit<LoadField>(type, top, "ob_type", offsetof(PyObject, ob_type), TType);
  auto tp_flags = temps_.AllocateStack();
  tc.emit<LoadField>(
      tp_flags, type, "tp_flags", offsetof(PyTypeObject, tp_flags), TCUInt64);
  auto flag = temps_.AllocateStack();
  tc.emit<LoadConst>(flag, Type::fromCUInt(tf_flag, TCUInt64));

  auto and_result = temps_.AllocateStack();
  tc.emit<IntBinaryOp>(and_result, BinaryOpKind::kAnd, tp_flags, flag);

  auto true_block = cfg.AllocateBlock();
  auto false_block = cfg.AllocateBlock();
  tc.emit<CondBranch>(and_result, true_block, false_block);

  auto result = temps_.AllocateStack();
  tc.block = true_block;
  tc.emit<LoadConst>(result, Type::fromObject(Py_True));
  auto done = cfg.AllocateBlock();
  tc.emit<Branch>(done);

  tc.block = false_block;
  tc.emit<LoadConst>(result, Type::fromObject(Py_False));
  tc.emit<Branch>(done);

  tc.block = done;

  tc.frame.stack.push(result);
}

void HIRBuilder::emitMatchClass(
    CFG& cfg,
    TranslationContext& tc,
    const jit::BytecodeInstruction& bc_instr) {
  auto& stack = tc.frame.stack;
  Register* names = stack.pop();
  Register* type = stack.pop();
  Register* subject = stack.pop();
  auto oparg = bc_instr.oparg();

  auto nargs = temps_.AllocateStack();
  tc.emit<LoadConst>(nargs, Type::fromCUInt(oparg, TCUInt64));

  auto attrs_tuple = temps_.AllocateStack();
  tc.emit<MatchClass>(attrs_tuple, subject, type, nargs, names);
  tc.emit<RefineType>(attrs_tuple, TOptTupleExact, attrs_tuple);

  Register* top = temps_.AllocateStack();
  Register* second = temps_.AllocateStack();
  stack.push(second);
  stack.push(top);

  auto true_block = cfg.AllocateBlock();
  auto false_block = cfg.AllocateBlock();
  auto done = cfg.AllocateBlock();

  tc.emit<CondBranch>(attrs_tuple, true_block, false_block);
  tc.block = true_block;
  tc.emit<RefineType>(second, TTupleExact, attrs_tuple);
  tc.emit<LoadConst>(top, Type::fromObject(Py_True));
  tc.emit<Branch>(done);

  tc.block = false_block;
  tc.emit<CheckErrOccurred>(tc.frame);
  tc.emit<LoadConst>(top, Type::fromObject(Py_False));
  tc.emit<Assign>(second, subject);
  tc.emit<Branch>(done);

  tc.block = done;
}

void HIRBuilder::emitMatchKeys(CFG& cfg, TranslationContext& tc) {
  auto& stack = tc.frame.stack;
  Register* keys = stack.top();
  Register* subject = stack.top(1);

  auto values_or_none = temps_.AllocateStack();
  tc.emit<MatchKeys>(values_or_none, subject, keys, tc.frame);
  stack.push(values_or_none);

  auto none = temps_.AllocateStack();
  tc.emit<LoadConst>(none, Type::fromObject(Py_None));
  auto is_none = temps_.AllocateStack();
  tc.emit<PrimitiveCompare>(
      is_none, PrimitiveCompareOp::kEqual, values_or_none, none);

  auto true_block = cfg.AllocateBlock();
  auto false_block = cfg.AllocateBlock();
  auto done = cfg.AllocateBlock();

  tc.emit<CondBranch>(is_none, true_block, false_block);
  auto obj = temps_.AllocateStack();
  tc.block = true_block;
  tc.emit<RefineType>(values_or_none, TNoneType, values_or_none);
  tc.emit<LoadConst>(obj, Type::fromObject(Py_False));
  tc.emit<Branch>(done);

  tc.block = false_block;
  tc.emit<RefineType>(values_or_none, TTupleExact, values_or_none);
  tc.emit<LoadConst>(obj, Type::fromObject(Py_True));
  tc.emit<Branch>(done);

  stack.push(obj);
  tc.block = done;
}

void HIRBuilder::emitDictUpdate(TranslationContext& tc) {
  auto& stack = tc.frame.stack;
  Register* update = stack.pop();
  Register* dict = stack.top();
  Register* out = temps_.AllocateStack();
  tc.emit<DictUpdate>(out, dict, update, tc.frame);
}

void HIRBuilder::emitDictMerge(
    TranslationContext& tc,
    const BytecodeInstruction& bc_instr) {
  auto& stack = tc.frame.stack;
  Register* dict = stack.top(bc_instr.oparg());
  Register* func = stack.top(bc_instr.oparg() + 2);
  Register* update = stack.pop();
  Register* out = temps_.AllocateStack();
  tc.emit<DictMerge>(out, dict, update, func, tc.frame);
}

void HIRBuilder::insertEvalBreakerCheck(
    CFG& cfg,
    BasicBlock* check_block,
    BasicBlock* succ,
    const FrameState& frame) {
  TranslationContext check(check_block, frame);
  TranslationContext body(cfg.AllocateBlock(), frame);
  // Check if the eval breaker has been set
  Register* eval_breaker = temps_.AllocateStack();
  check.emit<LoadEvalBreaker>(eval_breaker);
  check.emit<CondBranch>(eval_breaker, body.block, succ);
  // If set, run periodic tasks
  body.snapshot();
  body.emit<RunPeriodicTasks>(temps_.AllocateStack(), body.frame);
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

BorrowedRef<> HIRBuilder::constArg(const BytecodeInstruction& bc_instr) {
  return PyTuple_GET_ITEM(code_->co_consts, bc_instr.oparg());
}

} // namespace jit::hir

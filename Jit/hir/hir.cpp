// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/hir/hir.h"

#include "Jit/hir/printer.h"
#include "Jit/log.h"
#include "Jit/pyjit.h"
#include "Jit/ref.h"
#include "Jit/threaded_compile.h"

#include <fmt/format.h>

#include <algorithm>
#include <unordered_map>

namespace jit::hir {

const std::vector<void*> CallCFunc::kFuncPtrMap{
#define FUNC_PTR(name, ...) (void*)name,
    CallCFunc_FUNCS(FUNC_PTR)
#undef FUNC_PTR
};

const std::vector<const char*> CallCFunc::kFuncNames{
#define FUNC_NAME(name, ...) #name,
    CallCFunc_FUNCS(FUNC_NAME)
#undef FUNC_NAME
};

void Phi::setArgs(const std::unordered_map<BasicBlock*, Register*>& args) {
  JIT_DCHECK(NumOperands() == args.size(), "arg mismatch");

  basic_blocks_.clear();
  basic_blocks_.reserve(args.size());

  for (auto& kv : args) {
    basic_blocks_.push_back(kv.first);
  }

  std::sort(
      basic_blocks_.begin(),
      basic_blocks_.end(),
      [](const BasicBlock* a, const BasicBlock* b) -> bool {
        return a->id < b->id;
      });

  std::size_t i = 0;
  for (auto& block : basic_blocks_) {
    operandAt(i) = map_get(args, block);
    i++;
  }
}

std::size_t Phi::blockIndex(const BasicBlock* block) const {
  auto it = std::lower_bound(
      basic_blocks_.begin(), basic_blocks_.end(), block, [](auto b1, auto b2) {
        return b1->id < b2->id;
      });
  JIT_DCHECK(it != basic_blocks_.end(), "Bad CFG");
  JIT_DCHECK(*it == block, "Bad CFG");
  return std::distance(basic_blocks_.begin(), it);
}

const char* const kOpcodeNames[] = {
#define NAME_OP(opname) #opname,
    FOREACH_OPCODE(NAME_OP)
#undef NAME_OP
};

Edge::~Edge() {
  set_from(nullptr);
  set_to(nullptr);
}

void Edge::set_from(BasicBlock* new_from) {
  if (from_) {
    from_->out_edges_.erase(this);
  }
  if (new_from) {
    new_from->out_edges_.insert(this);
  }
  from_ = new_from;
}

void Edge::set_to(BasicBlock* new_to) {
  if (to_) {
    to_->in_edges_.erase(this);
  }
  if (new_to) {
    new_to->in_edges_.insert(this);
  }
  to_ = new_to;
}

Instr::~Instr() {}

bool Instr::IsTerminator() const {
  switch (opcode()) {
    case Opcode::kBranch:
    case Opcode::kDeopt:
    case Opcode::kCondBranch:
    case Opcode::kCondBranchIterNotDone:
    case Opcode::kCondBranchCheckType:
    case Opcode::kRaise:
    case Opcode::kRaiseAwaitableError:
    case Opcode::kRaiseStatic:
    case Opcode::kReturn:
    case Opcode::kUnreachable:
      return true;
    default:
      return false;
  }
}

bool Instr::isReplayable() const {
  switch (opcode()) {
    case Opcode::kAssign:
    case Opcode::kBitCast:
    case Opcode::kBuildString:
    case Opcode::kCast:
    case Opcode::kCheckErrOccurred:
    case Opcode::kCheckExc:
    case Opcode::kCheckField:
    case Opcode::kCheckFreevar:
    case Opcode::kCheckNeg:
    case Opcode::kCheckSequenceBounds:
    case Opcode::kCheckVar:
    case Opcode::kDoubleBinaryOp:
    case Opcode::kFormatValue:
    case Opcode::kGetLoadMethodInstance:
    case Opcode::kGuard:
    case Opcode::kGuardIs:
    case Opcode::kGuardType:
    case Opcode::kHintType:
    case Opcode::kIntBinaryOp:
    case Opcode::kIntConvert:
    case Opcode::kIsNegativeAndErrOccurred:
    case Opcode::kLoadArg:
    case Opcode::kLoadArrayItem:
    case Opcode::kLoadCellItem:
    case Opcode::kLoadConst:
    case Opcode::kLoadCurrentFunc:
    case Opcode::kLoadEvalBreaker:
    case Opcode::kLoadField:
    case Opcode::kLoadFieldAddress:
    case Opcode::kLoadFunctionIndirect:
    case Opcode::kLoadGlobalCached:
    case Opcode::kLoadTupleItem:
    case Opcode::kLoadTypeAttrCacheItem:
    case Opcode::kLoadVarObjectSize:
    case Opcode::kLongCompare:
    case Opcode::kPrimitiveBox:
    case Opcode::kPrimitiveBoxBool:
    case Opcode::kPrimitiveCompare:
    case Opcode::kPrimitiveUnaryOp:
    case Opcode::kPrimitiveUnbox:
    case Opcode::kRaise:
    case Opcode::kRaiseStatic:
    case Opcode::kRefineType:
    case Opcode::kStealCellItem:
    case Opcode::kUnicodeCompare:
    case Opcode::kUnicodeConcat:
    case Opcode::kUseType:
    case Opcode::kWaitHandleLoadCoroOrResult:
    case Opcode::kWaitHandleLoadWaiter: {
      return true;
    }
    case Opcode::kCompare: {
      auto op = static_cast<const Compare*>(this)->op();
      return op == CompareOp::kIs || op == CompareOp::kIsNot;
    }
    case Opcode::kCompareBool: {
      auto op = static_cast<const CompareBool*>(this)->op();
      return op == CompareOp::kIs || op == CompareOp::kIsNot;
    }
    case Opcode::kBatchDecref:
    case Opcode::kBeginInlinedFunction:
    case Opcode::kBinaryOp:
    case Opcode::kBranch:
    case Opcode::kBuildSlice:
    case Opcode::kCallCFunc:
    case Opcode::kCallEx:
    case Opcode::kCallExKw:
    case Opcode::kCallMethod:
    case Opcode::kCallStatic:
    case Opcode::kCallStaticRetVoid:
    case Opcode::kCondBranch:
    case Opcode::kCondBranchIterNotDone:
    case Opcode::kCondBranchCheckType:
    case Opcode::kCopyDictWithoutKeys:
    case Opcode::kDecref:
    case Opcode::kDeleteAttr:
    case Opcode::kDeleteSubscr:
    case Opcode::kDeopt:
    case Opcode::kDeoptPatchpoint:
    case Opcode::kDictMerge:
    case Opcode::kDictSubscr:
    case Opcode::kDictUpdate:
    case Opcode::kEndInlinedFunction:
    case Opcode::kFillTypeAttrCache:
    case Opcode::kGetAIter:
    case Opcode::kGetANext:
    case Opcode::kGetIter:
    case Opcode::kGetLength:
    case Opcode::kGetTuple:
    case Opcode::kImportName:
    case Opcode::kImportFrom:
    case Opcode::kInPlaceOp:
    case Opcode::kIncref:
    case Opcode::kInitialYield:
    case Opcode::kInitFunction:
    case Opcode::kInvokeIterNext:
    case Opcode::kInvokeStaticFunction:
    case Opcode::kInvokeMethod:
    case Opcode::kIsInstance:
    case Opcode::kIsTruthy:
    case Opcode::kListAppend:
    case Opcode::kListExtend:
    case Opcode::kLoadAttr:
    case Opcode::kLoadAttrSpecial:
    case Opcode::kLoadAttrSuper:
    case Opcode::kLoadGlobal:
    case Opcode::kLoadMethod:
    case Opcode::kLoadMethodSuper:
    case Opcode::kLongBinaryOp:
    case Opcode::kMakeCell:
    case Opcode::kMakeCheckedDict:
    case Opcode::kMakeCheckedList:
    case Opcode::kMakeDict:
    case Opcode::kMakeFunction:
    case Opcode::kMakeList:
    case Opcode::kMakeSet:
    case Opcode::kMakeTuple:
    case Opcode::kMakeTupleFromList:
    case Opcode::kMatchClass:
    case Opcode::kMatchKeys:
    case Opcode::kMergeSetUnpack:
    case Opcode::kPhi:
    case Opcode::kRaiseAwaitableError:
    case Opcode::kRepeatList:
    case Opcode::kRepeatTuple:
    case Opcode::kReturn:
    case Opcode::kRunPeriodicTasks:
    case Opcode::kSetCellItem:
    case Opcode::kSetCurrentAwaiter:
    case Opcode::kSetDictItem:
    case Opcode::kSetSetItem:
    case Opcode::kSetUpdate:
    case Opcode::kSetFunctionAttr:
    case Opcode::kStoreField:
    case Opcode::kSnapshot:
    case Opcode::kStoreArrayItem:
    case Opcode::kStoreAttr:
    case Opcode::kStoreSubscr:
    case Opcode::kTpAlloc:
    case Opcode::kUnaryOp:
    case Opcode::kUnicodeRepeat:
    case Opcode::kUnpackExToTuple:
    case Opcode::kUnreachable:
    case Opcode::kVectorCall:
    case Opcode::kVectorCallStatic:
    case Opcode::kVectorCallKW:
    case Opcode::kWaitHandleRelease:
    case Opcode::kYieldAndYieldFrom:
    case Opcode::kYieldFrom:
    case Opcode::kYieldFromHandleStopAsyncIteration:
    case Opcode::kYieldValue:
    case Opcode::kXDecref:
    case Opcode::kXIncref: {
      return false;
    }
  }
  JIT_CHECK(false, "Bad opcode %d", static_cast<int>(opcode()));
}

void Instr::set_block(BasicBlock* block) {
  block_ = block;
  if (IsTerminator()) {
    for (std::size_t i = 0, n = numEdges(); i < n; ++i) {
      edge(i)->set_from(block);
    }
  }
}

void Instr::link(BasicBlock* block) {
  JIT_CHECK(block_ == nullptr, "Instr is already linked");
  set_block(block);
}

void Instr::unlink() {
  JIT_CHECK(block_ != nullptr, "Instr isn't linked");
  block_node_.Unlink();
  set_block(nullptr);
}

const FrameState* Instr::getDominatingFrameState() const {
  if (block_ == nullptr) {
    return nullptr;
  }
  auto rend = block()->crend();
  auto it = block()->const_reverse_iterator_to(*this);
  for (it++; it != rend; it++) {
    if (it->IsSnapshot()) {
      auto snapshot = static_cast<const Snapshot*>(&*it);
      return snapshot->frameState();
    }
    if (!it->isReplayable()) {
      return nullptr;
    }
  }
  return nullptr;
}

BorrowedRef<PyCodeObject> Instr::code() const {
  const FrameState* fs = getDominatingFrameState();
  return fs == nullptr ? block()->cfg->func->code : fs->code;
}

Instr* BasicBlock::Append(Instr* instr) {
  instrs_.PushBack(*instr);
  instr->link(this);
  return instr;
}

void BasicBlock::push_front(Instr* instr) {
  instrs_.PushFront(*instr);
  instr->link(this);
}

Instr* BasicBlock::pop_front() {
  Instr* result = &(instrs_.ExtractFront());
  result->set_block(nullptr);
  return result;
}

void BasicBlock::insert(Instr* instr, Instr::List::iterator it) {
  instrs_.insert(*instr, it);
  instr->link(this);
}

void BasicBlock::clear() {
  while (!instrs_.IsEmpty()) {
    Instr* instr = &(instrs_.ExtractFront());
    delete instr;
  }
}

BasicBlock::~BasicBlock() {
  JIT_DCHECK(
      in_edges_.empty(), "Attempt to destroy a block with in-edges, %d", id);
  clear();
  JIT_DCHECK(
      out_edges_.empty(), "out_edges not empty after deleting all instrs");
}

Instr* BasicBlock::GetTerminator() {
  if (instrs_.IsEmpty()) {
    return nullptr;
  }
  return &instrs_.Back();
}

Snapshot* BasicBlock::entrySnapshot() {
  for (auto& instr : instrs_) {
    if (instr.IsPhi()) {
      continue;
    }
    if (instr.IsSnapshot()) {
      return static_cast<Snapshot*>(&instr);
    }
    return nullptr;
  }
  return nullptr;
}

bool BasicBlock::IsTrampoline() {
  for (auto& instr : instrs_) {
    if (instr.IsBranch()) {
      auto succ = instr.successor(0);
      // Don't consider a block a trampoline if its successor has one or more
      // Phis, since this block may be necessary to pass a specific value to
      // the Phi. This is correct but conservative: it's often safe to
      // eliminate trampolines that jump to Phis, but that requires more
      // involved analysis in the caller.
      return succ != this && (succ->empty() || !succ->front().IsPhi());
    }
    if (instr.IsSnapshot()) {
      continue;
    }
    return false;
  }
  // empty block
  return false;
}

BasicBlock* BasicBlock::splitAfter(Instr& instr) {
  JIT_CHECK(cfg != nullptr, "cannot split unlinked block");
  auto tail = cfg->AllocateBlock();
  for (auto it = std::next(instrs_.iterator_to(instr)); it != instrs_.end();) {
    auto& instr = *it;
    ++it;
    instr.unlink();
    tail->Append(&instr);
  }

  for (auto edge : tail->out_edges()) {
    edge->to()->fixupPhis(this, tail);
  }
  return tail;
}

void BasicBlock::fixupPhis(BasicBlock* old_pred, BasicBlock* new_pred) {
  // TODO(bsimmers): This won't work correctly if this block has two incoming
  // edges from the same block, but we already can't handle that correctly with
  // our current Phi setup.

  forEachPhi([&](Phi& phi) {
    std::unordered_map<BasicBlock*, Register*> args;
    for (size_t i = 0, n = phi.NumOperands(); i < n; ++i) {
      auto block = phi.basic_blocks()[i];
      if (block == old_pred) {
        block = new_pred;
      }
      args[block] = phi.GetOperand(i);
    }
    phi.setArgs(args);
  });
}

void BasicBlock::addPhiPredecessor(BasicBlock* old_pred, BasicBlock* new_pred) {
  std::vector<Phi*> replacements;
  forEachPhi([&](Phi& phi) {
    for (auto block : phi.basic_blocks()) {
      if (block == old_pred) {
        replacements.push_back(&phi);
        break;
      }
    }
  });

  for (auto phi : replacements) {
    std::unordered_map<BasicBlock*, Register*> args;
    for (size_t i = 0, n = phi->NumOperands(); i < n; ++i) {
      auto block = phi->basic_blocks()[i];
      if (block == old_pred) {
        args[new_pred] = phi->GetOperand(i);
      }
      args[block] = phi->GetOperand(i);
    }

    phi->ReplaceWith(*Phi::create(phi->GetOutput(), args));
    delete phi;
  }
}

void BasicBlock::removePhiPredecessor(BasicBlock* old_pred) {
  for (auto it = instrs_.begin(); it != instrs_.end();) {
    auto& instr = *it;
    ++it;
    if (!instr.IsPhi()) {
      break;
    }

    Phi* phi = static_cast<Phi*>(&instr);
    std::unordered_map<BasicBlock*, Register*> args;
    for (size_t i = 0, n = phi->NumOperands(); i < n; ++i) {
      auto block = phi->basic_blocks()[i];
      if (block == old_pred) {
        continue;
      }
      args[block] = phi->GetOperand(i);
    }
    phi->ReplaceWith(*Phi::create(phi->GetOutput(), args));
    delete phi;
  }
}

BasicBlock* CFG::AllocateBlock() {
  auto block = AllocateUnlinkedBlock();
  block->cfg = this;
  blocks.PushBack(*block);
  return block;
}

BasicBlock* CFG::AllocateUnlinkedBlock() {
  int id = next_block_id_;
  auto block = new BasicBlock(id);
  next_block_id_++;
  return block;
}

void CFG::InsertBlock(BasicBlock* block) {
  block->cfg = this;
  blocks.PushBack(*block);
}

void CFG::RemoveBlock(BasicBlock* block) {
  JIT_DCHECK(block->cfg == this, "block doesn't belong to us");
  block->cfg_node.Unlink();
  block->cfg = nullptr;
}

void CFG::splitCriticalEdges() {
  std::vector<Edge*> critical_edges;

  // Separately enumerate and process the critical edges to avoid mutating the
  // CFG while iterating it.
  for (auto& block : blocks) {
    auto term = block.GetTerminator();
    JIT_DCHECK(term != nullptr, "Invalid block");
    auto num_edges = term->numEdges();
    if (num_edges < 2) {
      continue;
    }
    for (std::size_t i = 0; i < num_edges; ++i) {
      auto edge = term->edge(i);
      if (edge->to()->in_edges().size() > 1) {
        critical_edges.emplace_back(edge);
      }
    }
  }

  for (auto edge : critical_edges) {
    auto from = edge->from();
    auto to = edge->to();
    auto split_bb = AllocateBlock();
    auto term = edge->from()->GetTerminator();
    split_bb->appendWithOff<Branch>(term->bytecodeOffset(), to);
    edge->set_to(split_bb);
    to->fixupPhis(from, split_bb);
  }
}

static void postorder_traverse(
    BasicBlock* block,
    std::vector<BasicBlock*>* traversal,
    std::unordered_set<BasicBlock*>* visited) {
  JIT_CHECK(block != nullptr, "visiting null block!");
  visited->emplace(block);

  // Add successors to be visited
  Instr* instr = block->GetTerminator();
  switch (instr->opcode()) {
    case Opcode::kCondBranch:
    case Opcode::kCondBranchIterNotDone:
    case Opcode::kCondBranchCheckType: {
      auto cbr = static_cast<CondBranch*>(instr);
      if (!visited->count(cbr->false_bb())) {
        postorder_traverse(cbr->false_bb(), traversal, visited);
      }
      if (!visited->count(cbr->true_bb())) {
        postorder_traverse(cbr->true_bb(), traversal, visited);
      }
      break;
    }
    case Opcode::kBranch: {
      auto br = static_cast<Branch*>(instr);
      if (!visited->count(br->target())) {
        postorder_traverse(br->target(), traversal, visited);
      }
      break;
    }
    case Opcode::kDeopt:
    case Opcode::kRaise:
    case Opcode::kRaiseAwaitableError:
    case Opcode::kRaiseStatic:
    case Opcode::kUnreachable:
    case Opcode::kReturn: {
      // No successor blocks
      break;
    }
    default: {
      /* NOTREACHED */
      JIT_CHECK(
          0, "block %d has invalid terminator %s", block->id, instr->opname());
      break;
    }
  }

  traversal->emplace_back(block);
}

std::vector<BasicBlock*> CFG::GetRPOTraversal() const {
  return GetRPOTraversal(entry_block);
}

std::vector<BasicBlock*> CFG::GetRPOTraversal(BasicBlock* start) {
  auto traversal = GetPostOrderTraversal(start);
  std::reverse(traversal.begin(), traversal.end());
  return traversal;
}

std::vector<BasicBlock*> CFG::GetPostOrderTraversal() const {
  return GetPostOrderTraversal(entry_block);
}

std::vector<BasicBlock*> CFG::GetPostOrderTraversal(BasicBlock* start) {
  std::vector<BasicBlock*> traversal;
  if (start == nullptr) {
    return traversal;
  }
  std::unordered_set<BasicBlock*> visited;
  postorder_traverse(start, &traversal, &visited);
  return traversal;
}

const BasicBlock* CFG::getBlockById(int id) const {
  for (auto& block : blocks) {
    if (block.id == id) {
      return &block;
    }
  }
  return nullptr;
}

CFG::~CFG() {
  while (!blocks.IsEmpty()) {
    BasicBlock* block = &(blocks.ExtractFront());
    // This is the one situation where it's not a bug to delete a reachable
    // block, since we're deleting everything. Clear block's incoming edges so
    // its destructor doesn't complain.
    for (auto it = block->in_edges().begin(); it != block->in_edges().end();) {
      auto edge = *it;
      ++it;
      const_cast<Edge*>(edge)->set_to(nullptr);
    }
    delete block;
  }
}

constexpr std::array<std::string_view, kNumCompareOps> kCompareOpNames = {
#define OP_STR(NAME) #NAME,
    FOREACH_COMPARE_OP(OP_STR)
#undef OP_STR
};

std::string_view GetCompareOpName(CompareOp op) {
  return kCompareOpNames[static_cast<int>(op)];
}

CompareOp ParseCompareOpName(std::string_view name) {
  for (size_t i = 0; i < kCompareOpNames.size(); ++i) {
    if (name == kCompareOpNames[i]) {
      return static_cast<CompareOp>(i);
    }
  }
  JIT_CHECK(false, "Invalid CompareOp '%s'", name);
}

constexpr std::array<std::string_view, kNumPrimitiveCompareOps>
    kPrimitiveCompareOpNames = {
#define OP_STR(NAME) #NAME,
        FOREACH_PRIMITIVE_COMPARE_OP(OP_STR)
#undef OP_STR
};

std::string_view GetPrimitiveCompareOpName(PrimitiveCompareOp op) {
  return kPrimitiveCompareOpNames[static_cast<int>(op)];
}

PrimitiveCompareOp ParsePrimitiveCompareOpName(std::string_view name) {
  for (size_t i = 0; i < kPrimitiveCompareOpNames.size(); i++) {
    if (name == kPrimitiveCompareOpNames[i]) {
      return static_cast<PrimitiveCompareOp>(i);
    }
  }
  JIT_CHECK(false, "Invalid PrimitiveCompareOp '%s'", name);
}

constexpr std::array<std::string_view, kNumBinaryOpKinds> kBinaryOpNames = {
#define OP_STR(NAME) #NAME,
    FOREACH_BINARY_OP_KIND(OP_STR)
#undef OP_STR
};

std::string_view GetBinaryOpName(BinaryOpKind op) {
  return kBinaryOpNames[static_cast<int>(op)];
}

BinaryOpKind ParseBinaryOpName(std::string_view name) {
  for (size_t i = 0; i < kBinaryOpNames.size(); ++i) {
    if (name == kBinaryOpNames[i]) {
      return static_cast<BinaryOpKind>(i);
    }
  }
  JIT_CHECK(false, "Invalid BinaryOpKind '%s'", name);
}

constexpr std::array<std::string_view, kNumUnaryOpKinds> kUnaryOpNames = {
#define OP_STR(NAME) #NAME,
    FOREACH_UNARY_OP_KIND(OP_STR)
#undef OP_STR
};

std::string_view GetUnaryOpName(UnaryOpKind op) {
  return kUnaryOpNames[static_cast<int>(op)];
}

UnaryOpKind ParseUnaryOpName(std::string_view name) {
  for (size_t i = 0; i < kUnaryOpNames.size(); ++i) {
    if (name == kUnaryOpNames[i]) {
      return static_cast<UnaryOpKind>(i);
    }
  }
  JIT_CHECK(false, "Invalid UnaryOpKind '%s'", name);
}

constexpr std::array<std::string_view, kNumPrimitiveUnaryOpKinds>
    kPrimitiveUnaryOpNames = {
#define OP_STR(NAME) #NAME,
        FOREACH_PRIMITIVE_UNARY_OP_KIND(OP_STR)
#undef OP_STR
};

std::string_view GetPrimitiveUnaryOpName(PrimitiveUnaryOpKind op) {
  return kPrimitiveUnaryOpNames[static_cast<int>(op)];
}

PrimitiveUnaryOpKind ParsePrimitiveUnaryOpName(std::string_view name) {
  for (size_t i = 0; i < kPrimitiveUnaryOpNames.size(); ++i) {
    if (name == kPrimitiveUnaryOpNames[i]) {
      return static_cast<PrimitiveUnaryOpKind>(i);
    }
  }
  JIT_CHECK(false, "Invalid PrimitiveUnaryOpKind '%s'", name);
}

// NB: This needs to be in the order that the values appear in the InPlaceOpKind
// enum
constexpr std::array<std::string_view, kNumInPlaceOpKinds> kInPlaceOpNames = {
#define OP_STR(NAME) #NAME,
    FOREACH_INPLACE_OP_KIND(OP_STR)
#undef OP_STR
};

std::string_view GetInPlaceOpName(InPlaceOpKind op) {
  return kInPlaceOpNames[static_cast<int>(op)];
}

InPlaceOpKind ParseInPlaceOpName(std::string_view name) {
  for (size_t i = 0; i < kInPlaceOpNames.size(); ++i) {
    if (name == kInPlaceOpNames[i]) {
      return static_cast<InPlaceOpKind>(i);
    }
  }
  JIT_CHECK(false, "Invalid InPlaceOpKind '%s'", name);
}

// NB: This needs to be in the order that the values appear in the FunctionAttr
// enum
static const char* gFunctionFields[] = {
    "func_closure",
    "func_annotations",
    "func_kwdefaults",
    "func_defaults",
};

const char* functionFieldName(FunctionAttr field) {
  return gFunctionFields[static_cast<int>(field)];
}

Register* Environment::AllocateRegister() {
  auto id = next_register_id_++;
  while (registers_.count(id)) {
    id = next_register_id_++;
  }
  auto res = registers_.emplace(id, std::make_unique<Register>(id));
  return res.first->second.get();
}

Register* Environment::getRegister(int id) {
  auto it = registers_.find(id);
  if (it == registers_.end()) {
    return nullptr;
  }
  return it->second.get();
}

const Environment::RegisterMap& Environment::GetRegisters() const {
  return registers_;
}

Function::Function() {
  cfg.func = this;
}

Function::~Function() {
  // Serialize as we alter ref-counts on potentially global objects.
  ThreadedCompileSerialize guard;
  code.reset();
  globals.reset();
}

Register* Environment::addRegister(std::unique_ptr<Register> reg) {
  auto id = reg->id();
  auto res = registers_.emplace(id, std::move(reg));
  JIT_CHECK(res.second, "register %d already in map", id);
  return res.first->second.get();
}

BorrowedRef<> Environment::addReference(Ref<>&& obj) {
  return references_.emplace(std::move(obj)).first->get();
}

BorrowedRef<> Environment::addReference(BorrowedRef<> obj) {
  // Serialize as we modify the ref-count to obj which may be widely accessible.
  ThreadedCompileSerialize guard;
  return addReference(Ref<>::create(obj));
}

const Environment::ReferenceSet& Environment::references() const {
  return references_;
}

bool usesRuntimeFunc(BorrowedRef<PyCodeObject> code) {
  return PyTuple_GET_SIZE(code->co_freevars) > 0;
}

void Function::setCode(BorrowedRef<PyCodeObject> code) {
  this->code.reset(code);
  uses_runtime_func = usesRuntimeFunc(code);
  frameMode = _PyJIT_ShadowFrame() ? FrameMode::kShadow : FrameMode::kNormal;
}

void Function::Print() const {
  HIRPrinter printer;
  printer.Print(*this);
}

void BasicBlock::Print() const {
  HIRPrinter printer;
  printer.Print(*this);
}

std::size_t Function::CountInstrs(InstrPredicate pred) const {
  std::size_t result = 0;
  for (const auto& block : cfg.blocks) {
    for (const auto& instr : block) {
      if (pred(instr)) {
        result++;
      }
    }
  }
  return result;
}

int Function::numArgs() const {
  if (code == nullptr) {
    // code might be null if we parsed from textual ir
    return 0;
  }
  return code->co_argcount + code->co_kwonlyargcount +
      bool(code->co_flags & CO_VARARGS) + bool(code->co_flags & CO_VARKEYWORDS);
}

Py_ssize_t Function::numVars() const {
  if (code == nullptr) {
    // code might be null if we parsed from textual ir
    return 0;
  }
  Py_ssize_t num_cellvars = PyTuple_GET_SIZE(code->co_cellvars);
  Py_ssize_t num_freevars = PyTuple_GET_SIZE(code->co_freevars);
  return code->co_nlocals + num_cellvars + num_freevars;
}

bool Function::canDeopt() const {
  for (const BasicBlock& block : cfg.blocks) {
    for (const Instr& instr : block) {
      if (instr.asDeoptBase()) {
        return true;
      }
    }
  }
  return false;
}

const char* const kFailureTypeMsgs[] = {
#define FAILURE_TYPE_MSG(failure, msg) msg,
    FOREACH_FAILURE_TYPE(FAILURE_TYPE_MSG)
#undef NAME_FAILURE_TYPE
};

const char* const kFailureTypeNames[] = {
#define NAME_TYPE(failure, msg) #failure,
    FOREACH_FAILURE_TYPE(NAME_TYPE)
#undef NAME_TYPE
};

const char* getInlineFailureMessage(InlineFailureType failure_type) {
  return kFailureTypeMsgs[static_cast<size_t>(failure_type)];
}

const char* getInlineFailureName(InlineFailureType failure_type) {
  return kFailureTypeNames[static_cast<size_t>(failure_type)];
}

std::ostream& operator<<(std::ostream& os, OperandType op) {
  switch (op.kind) {
    case Constraint::kType:
      return os << op.type;
    case Constraint::kOptObjectOrCIntOrCBool:
      return os << "(OptObject, CInt, CBool)";
    case Constraint::kOptObjectOrCInt:
      return os << "(OptObject, CInt)";
    case Constraint::kTupleExactOrCPtr:
      return os << "(TupleExact, CPtr)";
    case Constraint::kListOrChkList:
      return os << "(List, chklist)";
    case Constraint::kDictOrChkDict:
      return os << "(Dict, chkdict)";
    case Constraint::kMatchAllAsCInt:
      return os << "CInt";
    case Constraint::kMatchAllAsPrimitive:
      return os << "Primitive";
  }
  JIT_CHECK(false, "unknown constraint");
  return os << "<unknown>";
}

const FrameState* get_frame_state(const Instr& instr) {
  if (instr.IsSnapshot()) {
    return static_cast<const Snapshot&>(instr).frameState();
  }
  if (instr.IsBeginInlinedFunction()) {
    return static_cast<const BeginInlinedFunction&>(instr).callerFrameState();
  }
  if (auto db = instr.asDeoptBase()) {
    return db->frameState();
  }
  return nullptr;
}

FrameState* get_frame_state(Instr& instr) {
  return const_cast<FrameState*>(
      get_frame_state(const_cast<const Instr&>(instr)));
}

} // namespace jit::hir

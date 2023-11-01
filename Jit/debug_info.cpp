// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/debug_info.h"

#include "Jit/hir/hir.h"
#include "Jit/ref.h"

#include <algorithm>
#include <deque>

namespace jit {

CodeObjLoc DebugInfo::getCodeObjLoc(const LocNode& node) const {
  return CodeObjLoc(code_objs_.at(node.code_obj_id), node.bc_off);
}

std::optional<UnitCallStack> DebugInfo::getUnitCallStack(uintptr_t addr) const {
  auto it = addr_locs_.find(addr);
  if (it == addr_locs_.end()) {
    return std::nullopt;
  }

  LocNode node = it->second;
  UnitCallStack stack{getCodeObjLoc(node)};
  while (node.hasCaller()) {
    node = inlined_calls_[node.caller_id];
    stack.emplace_back(getCodeObjLoc(node));
  }
  std::reverse(stack.begin(), stack.end());

  return stack;
}

namespace {

struct Activation {
  Activation(BorrowedRef<PyCodeObject> c, const jit::hir::FrameState* cfs)
      : code_obj(c), caller_frame_state(cfs) {}
  // Code object for the activation
  BorrowedRef<PyCodeObject> code_obj;

  // Frame state of the caller, if this call was inlined. nullptr otherwise.
  const jit::hir::FrameState* caller_frame_state;
};

// Index of instruction to the Activation to which it belongs
using ActivationMap = std::unordered_map<const jit::hir::Instr*, Activation>;

struct WorkItem {
  WorkItem(const jit::hir::BasicBlock* b, const Activation& c)
      : block(b), activation(c) {}
  const jit::hir::BasicBlock* block;
  Activation activation;
};

// Build an ActivationMap for func.
//
// From an activation map we can retrieve the call stack for each HIR
// instruction, along with bytecode offsets for each entry, by walking the the
// caller_frame_state chain from the Activation. This is needed to recover
// the call stack for HIR instructions that do not have a FrameState but
// for which we still need debug info (e.g. DecRef).
ActivationMap buildActivationMap(const jit::hir::Function& func) {
  JIT_CHECK(func.code, "func has no code object");
  ActivationMap amap;
  std::deque<WorkItem> workq{
      WorkItem{func.cfg.entry_block, Activation{func.code, nullptr}}};
  std::unordered_set<const jit::hir::BasicBlock*> processed;
  while (!workq.empty()) {
    WorkItem item = std::move(workq.front());
    workq.pop_front();

    if (processed.count(item.block)) {
      continue;
    }

    for (const auto& instr : *item.block) {
      switch (instr.opcode()) {
        case jit::hir::Opcode::kBeginInlinedFunction: {
          auto bif = static_cast<const jit::hir::BeginInlinedFunction*>(&instr);
          item.activation = Activation{bif->code(), bif->callerFrameState()};
          amap.emplace(&instr, item.activation);
          break;
        }
        case jit::hir::Opcode::kEndInlinedFunction: {
          const jit::hir::FrameState* caller =
              item.activation.caller_frame_state;
          item.activation = Activation{caller->code, caller->parent};
          amap.emplace(&instr, item.activation);
          break;
        }
        default: {
          amap.emplace(&instr, item.activation);
          break;
        }
      }
    }

    processed.insert(item.block);

    for (const auto& edge : item.block->out_edges()) {
      workq.emplace_back(edge->to(), item.activation);
    }
  }

  return amap;
}

} // namespace

void DebugInfo::resolvePending(
    const std::vector<PendingDebugLoc>& pending,
    const jit::hir::Function& func,
    const asmjit::CodeHolder& code) {
  ActivationMap amap = buildActivationMap(func);
  // Add an entry for each pending location by walking the stack of inlined
  // calls that end at its instruction.
  JIT_CHECK(code.hasBaseAddress(), "code not generated");
  uint64_t base = code.baseAddress();
  for (const PendingDebugLoc& item : pending) {
    auto it = amap.find(item.instr);
    JIT_CHECK(it != amap.end(), "instr doesn't belong to func");
    uintptr_t addr = base + code.labelOffsetFromBase(item.label);
    const auto& [code_obj, caller_frame_state] = it->second;
    addUnitCallStack(
        addr, code_obj, item.instr->bytecodeOffset(), caller_frame_state);
  }
}

void DebugInfo::addUnitCallStack(
    uintptr_t addr,
    BorrowedRef<PyCodeObject> code,
    BCOffset bc_off,
    const jit::hir::FrameState* caller_frame_state) {
  uint16_t caller_id = getCallerID(caller_frame_state);
  uint16_t code_obj_id = getCodeObjID(code);
  addr_locs_.emplace(addr, LocNode{code_obj_id, caller_id, bc_off.value()});
}

uint16_t DebugInfo::getCodeObjID(BorrowedRef<PyCodeObject> code_obj) {
  // Search for the code object
  for (uint16_t i = 0; i < code_objs_.size(); i++) {
    // Using pointer equality is fine here - code objects live as long as the
    // JIT and its debug info.
    if (code_objs_[i] == code_obj) {
      return i;
    }
  }
  // Not found, assign it an id
  JIT_CHECK(code_objs_.size() < kMaxCodeObjs, "too many inlined functions");
  code_objs_.emplace_back(code_obj);
  return code_objs_.size() - 1;
}

uint16_t DebugInfo::getCallerID(const jit::hir::FrameState* caller) {
  // No caller
  if (caller == nullptr) {
    return kNoCallerID;
  }
  // Search for an existing node
  LocNode node{
      getCodeObjID(caller->code),
      getCallerID(caller->parent),
      caller->instr_offset().value()};
  for (uint16_t i = 0; i < inlined_calls_.size(); i++) {
    if (inlined_calls_[i] == node) {
      return i;
    }
  }
  // No existing node found, add one
  JIT_CHECK(inlined_calls_.size() < kMaxInlined, "too many inlined functions");
  inlined_calls_.emplace_back(node);
  return inlined_calls_.size() - 1;
}

} // namespace jit

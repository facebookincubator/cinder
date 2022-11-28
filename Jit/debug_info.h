// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"
#include "frameobject.h"

#include "Jit/bytecode_offsets.h"
#include "Jit/ref.h"

#include <asmjit/asmjit.h>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace jit {

namespace hir {
struct FrameState;
class Function;
class Instr;
} // namespace hir

// A location in a code object
struct CodeObjLoc {
  CodeObjLoc(BorrowedRef<PyFrameObject> py_frame)
      : code(py_frame->f_code),
        instr_offset(py_frame->f_lasti * int{sizeof(_Py_CODEUNIT)}) {}
  CodeObjLoc(BorrowedRef<PyCodeObject> code_, int lasti)
      : code(code_), instr_offset(lasti) {}
  BorrowedRef<PyCodeObject> code;

  // Bytecode offset. A value less than 0 indicates the position is unknown.
  int instr_offset{-1};

  int lineNo() const {
    return PyCode_Addr2Line(code, instr_offset);
  }
};

// Indicates that we want to associate the location information for instr at
// the point in generated code specified by label.
struct PendingDebugLoc {
  PendingDebugLoc(asmjit::Label l, const jit::hir::Instr* i)
      : label(l), instr(i) {}
  asmjit::Label label;
  const jit::hir::Instr* instr;
};

// UnitCallStack gives the code location information for all the active frames
// at a program point in a unit, in ascending order of inline depth (the
// non-inlined frame appears as the first element in the vector).
using UnitCallStack = std::vector<CodeObjLoc>;

// DebugInfo provides information about the runtime state of a unit.
class DebugInfo {
 public:
  // Get the locations of all the active frames at the given address in the
  // generated code.
  //
  // Returns std::nullopt if no location information was found.
  std::optional<UnitCallStack> getUnitCallStack(uintptr_t addr) const;

  // Add location information for pending by resolving labels to their
  // addresses in generated code.
  void resolvePending(
      const std::vector<PendingDebugLoc>& pending,
      const jit::hir::Function& func,
      const asmjit::CodeHolder& code);

 private:
  // Location information consumes a very large amount of memory, typically as
  // much or more as the generated code itself. Naively storing the entire call
  // stack in full fidelity (code object, bytecode offset) for each address
  // that is indexed would be prohibitively expensive. Instead, we store the
  // information in a compressed form in a graph.
  //
  // Nodes in the graph store a location (code object, bytecode offset) and a
  // reference to the location of the inlined call to which they belong, if
  // any. This arrangement lets us store location information for inlined calls
  // once.
  //
  // Given a node, the location information for its call stack is specified
  // by the node and the chain of inlined calls reachable from it.
  struct LocNode {
    LocNode(uint16_t cobj_id, uint16_t clr_id, int bco)
        : code_obj_id(cobj_id), caller_id(clr_id), bc_off(bco) {}
    // Index into code_objs of the PyCodeObject for this entry.
    uint16_t code_obj_id;

    // Index into inlined_calls_ if this entry is inside an inlined
    // function. LocNode::kNoCallerID otherwise.
    uint16_t caller_id;

    // Current bytecode offset.
    int bc_off;

    bool hasCaller() const {
      return caller_id != kNoCallerID;
    }

    bool operator==(const LocNode& other) const {
      return (code_obj_id == other.code_obj_id) &&
          (caller_id == other.caller_id) && (bc_off == other.bc_off);
    }

    bool operator!=(const LocNode& other) const {
      return !(*this == other);
    }
  };

  static const uint16_t kNoCallerID = UINT16_MAX;
  static const uint16_t kMaxInlined = kNoCallerID - 1;
  static const uint16_t kMaxCodeObjs = UINT16_MAX;

  // Record the unit call stack at addr using the (code, bc_off,
  // caller_frame_state) triple.
  //
  // The (code, bc_off) tuple gives the location in the innermost frame.
  // caller_frame_state gives the locations of all caller frames, if (code,
  // bc_off) appears in an inlined function.
  void addUnitCallStack(
      uintptr_t addr,
      BorrowedRef<PyCodeObject> code,
      BCOffset bc_off,
      const jit::hir::FrameState* caller_frame_state);

  // Get or assign an id for codeobj
  uint16_t getCodeObjID(BorrowedRef<PyCodeObject> codeobj);

  // Get or assign an id for the code location in caller
  uint16_t getCallerID(const jit::hir::FrameState* caller);

  // Decompress a LocNode to a CodeObjLoc
  CodeObjLoc getCodeObjLoc(const LocNode& node) const;

  // All the code objects in the unit
  std::vector<BorrowedRef<PyCodeObject>> code_objs_;

  // The locations of all the inline sites in the unit.
  std::vector<LocNode> inlined_calls_;

  // Index into the graph, keyed by address in the generated code
  // TODO(mpage): Store this in a vector sorted by address.
  std::unordered_map<uintptr_t, LocNode> addr_locs_;
};

} // namespace jit

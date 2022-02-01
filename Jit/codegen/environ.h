// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Jit/codegen/annotations.h"
#include "Jit/codegen/x86_64.h"
#include "Jit/inline_cache.h"
#include "Jit/jit_rt.h"
#include "Jit/lir/lir.h"
#include "Jit/runtime.h"

#include <asmjit/asmjit.h>

namespace jit {
namespace codegen {

struct Environ {
  Environ() {
  };

  // Metadata for annotated disassembly.
  Annotations annotations;

  // Assembler builder.
  asmjit::x86::Builder* as{nullptr};

  // Modified registers. Set by VariableManager and read by generatePrologue()
  // and generateEpilogue().
  PhyRegisterSet changed_regs{0};

  // Size of the stack frame.
  int frame_size{-1};

  // Offset from the base of the frame to the last callee-saved register stored
  // on the stack.
  int last_callee_saved_reg_off{-1};

  // Space used to spill values by VariableManager.
  int spill_size{0};

  // Various Labels that span major sections of the function.
  asmjit::Label static_arg_typecheck_failed_label;
  asmjit::Label hard_exit_label;
  asmjit::Label exit_label;
  asmjit::Label exit_for_yield_label;
  asmjit::Label gen_resume_entry_label;

  // Deopt exits. One per guard.
  struct DeoptExit {
    DeoptExit(size_t idx, asmjit::Label lbl)
        : deopt_meta_index(idx), label(lbl) {}
    size_t deopt_meta_index;
    asmjit::Label label;
  };
  std::vector<DeoptExit> deopt_exits;

  struct PendingDeoptPatcher {
    PendingDeoptPatcher(DeoptPatcher* p, asmjit::Label pp, asmjit::Label de)
        : patcher(p), patchpoint(pp), deopt_exit(de) {}
    DeoptPatcher* patcher;

    // Location of the patchpoint
    asmjit::Label patchpoint;

    // Location to jump to when the patchpoint is overwritten
    asmjit::Label deopt_exit;
  };
  std::vector<PendingDeoptPatcher> pending_deopt_patchers;

  // Maps points in the compiled code to bytecode offsets in the code object
  struct PendingIPtoBCOff {
    PendingIPtoBCOff(asmjit::Label ip_, int bc_off_)
        : ip(ip_), bc_off(bc_off_) {}

    // Point in the compiled code
    asmjit::Label ip;

    int bc_off;
  };
  std::vector<PendingIPtoBCOff> pending_ip_to_bc_offs;

  // Record that label corresponds to the bytcode offset that was ultimately
  // lowered into the supplied lir instruction.
  void addIPToBCMapping(
      asmjit::Label label,
      const jit::lir::Instruction* instr);

  // Load/Call method instructions for which we can avoid allocating a bound
  // method.
  std::unordered_set<const jit::hir::Instr*> optimizable_load_call_methods_;

  // Location of incoming arguments
  std::vector<PhyLocation> arg_locations;

  struct IndirectInfo {
    IndirectInfo(void** indirect_ptr) : indirect(indirect_ptr) {}

    void** indirect;
    asmjit::Label trampoline{0};
  };
  std::unordered_map<PyFunctionObject*, IndirectInfo> function_indirections;

  std::unordered_map<PyFunctionObject*, std::unique_ptr<_PyTypedArgsInfo>>
      function_typed_args;

  // Global runtime data.
  jit::Runtime* rt{nullptr};

  // Runtime data for this function.
  jit::CodeRuntime* code_rt{nullptr};

  template <typename T>
  void addAnnotation(T&& item, asmjit::BaseNode* start_cursor) {
    annotations.add(std::forward<T>(item), as, start_cursor);
  }

  // Map of GenYieldPoints which need their resume_target_ setting after code-
  // gen is complete.
  std::unordered_map<GenYieldPoint*, asmjit::Label> unresolved_gen_entry_labels;

  // maps an output name to its defining instruction
  std::unordered_map<std::string, jit::lir::Instruction*> output_map;

  // maps the original name to the propagated name.
  // TODO(tiansi, bsimmers): this is a temporary hack. Need to do the real
  // copy propagation after LIR cleanup is done. Related to
  // jit::lir::LIRGenerator::AnalyzeCopies().
  std::unordered_map<std::string, std::string> copy_propagation_map;

  // the operand needs to be fixed after code generation
  std::unordered_map<std::string, std::vector<jit::lir::LinkedOperand*>>
      operand_to_fix;

  std::unordered_map<jit::lir::BasicBlock*, asmjit::Label> block_label_map;

  // to support checking whether a predefined variable is used.
  // it may not be needed after the old backend is removed.
  std::unordered_set<std::string> predefined_;

  hir::FrameMode frame_mode;
  int initial_yield_spill_size_{-1};

  int max_arg_buffer_size{0};
};

} // namespace codegen
} // namespace jit

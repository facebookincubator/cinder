// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "cinderx/Jit/codegen/annotations.h"
#include "cinderx/Jit/codegen/x86_64.h"
#include "cinderx/Jit/containers.h"
#include "cinderx/Jit/debug_info.h"
#include "cinderx/Jit/inline_cache.h"
#include "cinderx/Jit/jit_rt.h"
#include "cinderx/Jit/runtime.h"

#include "cinderx/ThirdParty/asmjit/src/asmjit/asmjit.h"

namespace jit::codegen {

struct Environ {
  Environ(){};

  // Metadata for annotated disassembly.
  Annotations annotations;

  // Assembler builder.
  asmjit::x86::Builder* as{nullptr};

  // Modified registers. Set by VariableManager and read by generatePrologue()
  // and generateEpilogue().
  PhyRegisterSet changed_regs{0};

  // The size of all data stored on the C stack: shadow frames, spilled values,
  // saved callee-saved registers, and space for stack arguments to called
  // functions.
  int stack_frame_size{-1};

  // A subset of stack_frame_size: only the shadow frames and spilled values.
  int shadow_frames_and_spill_size{0};

  // Offset from the base of the frame to the last callee-saved register stored
  // on the stack.
  int last_callee_saved_reg_off{-1};

  // Various Labels that span major sections of the function.
  asmjit::Label static_arg_typecheck_failed_label;
  asmjit::Label hard_exit_label;
  asmjit::Label exit_label;
  asmjit::Label exit_for_yield_label;
  asmjit::Label gen_resume_entry_label;

  // Deopt exits. One per guard.
  struct DeoptExit {
    DeoptExit(size_t idx, asmjit::Label lbl, const jit::lir::Instruction* ins)
        : deopt_meta_index(idx), label(lbl), instr(ins) {}
    size_t deopt_meta_index;
    asmjit::Label label;
    const jit::lir::Instruction* instr;
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

  std::vector<PendingDebugLoc> pending_debug_locs;

  // Location of incoming arguments
  std::vector<PhyLocation> arg_locations;

  struct IndirectInfo {
    IndirectInfo(void** indirect_ptr) : indirect(indirect_ptr) {}

    void** indirect;
    asmjit::Label trampoline{0};
  };
  UnorderedMap<PyFunctionObject*, IndirectInfo> function_indirections;

  UnorderedMap<PyFunctionObject*, std::unique_ptr<_PyTypedArgsInfo>>
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
  UnorderedMap<GenYieldPoint*, asmjit::Label> unresolved_gen_entry_labels;

  // Maps HIR values to the LIR instruction that defines them.
  UnorderedMap<const hir::Register*, jit::lir::Instruction*> output_map;

  // Instruction definitions that are pinned to physical registers.
  jit::lir::Instruction* asm_tstate{nullptr};
  jit::lir::Instruction* asm_extra_args{nullptr};
  jit::lir::Instruction* asm_func{nullptr};

  // Maps HIR values to the HIR values they were copied from. Used for LIR
  // generation purposes.
  //
  // TODO(tiansi, bsimmers): this is a temporary hack. Need to do the real copy
  // propagation after LIR cleanup is done. Related to
  // jit::lir::LIRGenerator::AnalyzeCopies().
  UnorderedMap<const hir::Register*, hir::Register*> copy_propagation_map;

  UnorderedMap<jit::lir::BasicBlock*, asmjit::Label> block_label_map;

  FrameMode frame_mode;
  int initial_yield_spill_size_{-1};

  int max_arg_buffer_size{0};

  bool has_inlined_functions{false};
};

} // namespace jit::codegen

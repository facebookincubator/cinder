# Deoptimization

Deoptimization is the process of transferring execution of a running
JIT-compiled function to the interpreter. Deoptimization simplifies some
optimizations and enables more efficient implementations of others (notably
type specialized code). This document contains a high-level overview of how
deoptimization is implemented in the JIT.

## Frontend

Interpreter state is represented in HIR by `FrameState` objects. A `FrameState`
contains the following information:

- The offset of the next bytecode instruction to execute.
- The contents of the operand stack.
- The contents of the block stack.

In the future it may be extended to include the locals or information to
reconstruct objects that were not created due to scalar replacement.

Interpreter state is recorded explicitly in HIR using `Snapshot` instructions.
A `Snapshot` contains a pointer to a `FrameState` that represents the state of
the interpreter if we were to deopt at that point in the execution of the HIR
program.

Deoptimization points are represented in HIR by `Guard` instructions. A `Guard`
takes a single boolean operand, a `FrameState`, and a list of live registers
and the kind of reference that they hold (if any). When a guard executes,
control is transferred to the interpreter at the point specified in the
attached `FrameState` if its operand is false. The deoptimization metadata
needed by a `Guard` (the `FrameState` and live registers) is populated
automatically immediately before HIR is handed off to the backend for code
generation. A `Guard` inherits the `FrameState` of the dominating `Snapshot` in
its basic block.

Care must be taken when placing `Guard` instructions. There can be no side-effecting
HIR instructions between a `Guard` and the `Snapshot` that dominates it.

## Backend

During code generation, the backend generates a table of deoptimization metadata for
each guard. That metadata includes:

1. The physical location of each live register along with the kind of reference
   it holds.
2. A locals array that contains an index into (1) for each local.
3. An operand stack array that contains an index into (1) for each operand stack entry.
4. The block stack.
5. The offset of the next instruction to execute.

The generated code for a guard consists of:

1. A test for its operand.
2. A `jz` that jumps to a stage one trampoline that begins the deoptimization
   process.

The generated code for deoptimization consists of three stages of trampolines:

1. There is one stage one trampoline per guard. It pushes the index of the appropriate
   deoptimization metadata and jumps to the stage two trampoline.
2. There is one stage two trampoline per function. It pushes the address of the portion
   of the JIT epilogue that handles restoring callee-saved registers and jumps to
   the stage three trampoline.
3. There is one stage three trampoline per runtime. It does the following:
   1. Spills all registers to the stack.
   2. Calls a runtime helper that reifies a `PyFrameObject`.
   3. Calls `_PyEval_EvalFrameEx` to continue execution in the interpreter.
   4. Jumps to the JIT epilogue.

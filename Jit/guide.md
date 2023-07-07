# Cinder JIT Dev Guide

This is intended as a high-level description of the Cinder JIT. No component
is explained in great detail, but it should offer an understanding of how the
parts fit together and where to go looking in the source code for details.

## Overview

The JIT compiles [Python bytecode][1] from a PyCodeObject down to x64
assembly, via a number of intermediate steps and representations that will be
discussed in more detail below:

1. Preload values referenced in the bytecode that will be needed in
compilation.

2. Lower the Python bytecode to JIT HIR ("high-level intermediate
representation.")

3. Run a series of transformations on the HIR: transform to static single
assignment form, perform optimization passes, type specializations, and dead
code elimination, and insert reference counting operations.

4. Lower the HIR to LIR (low-level intermediate representation), which is a
thin abstraction over assembly.

5. Perform register allocation and some optimization passes on LIR.

6. Generate x64 machine code from LIR, via the [asmjit][2] library.

[1]: https://docs.python.org/3/library/dis.html#python-bytecode-instructions
[2]: https://github.com/asmjit/asmjit

## Python bytecode

For a Python function like the following:

```
def f(a, b, c):
    if c:
        return a + b
    return 24

# The JIT lazily compiles functions the first time they are executed,
# so call it to make sure it's compiled. The print() isn't required.
print(f(1, 2, 3))
```

We can see the Python bytecode by running `./python -mdis f.py`:

```
# ./python -mdis f.py

  1           0 LOAD_CONST               0 (<code object f at 0x7f3a9ca4fc90, file "f.py", line 1>)
              2 LOAD_CONST               1 ('f')
              4 MAKE_FUNCTION            0
              6 STORE_NAME               0 (f)

  6           8 LOAD_NAME                1 (print)
             10 LOAD_NAME                0 (f)
             12 LOAD_CONST               2 (1)
             14 LOAD_CONST               3 (2)
             16 LOAD_CONST               4 (3)
             18 CALL_FUNCTION            3
             20 CALL_FUNCTION            1
             22 POP_TOP
             24 LOAD_CONST               5 (None)
             26 RETURN_VALUE

Disassembly of <code object f at 0x7f3a9ca4fc90, file "f.py", line 1>:
  2           0 LOAD_FAST                2 (c)
              2 POP_JUMP_IF_FALSE       12

  3           4 LOAD_FAST                0 (a)
              6 LOAD_FAST                1 (b)
              8 BINARY_ADD
             10 RETURN_VALUE

  4     >>   12 LOAD_CONST               1 (24)
             14 RETURN_VALUE
```

You can see two chunks of bytecode. The first is the module body that creates
`f()`, calls it with a few arguments, then calls `print()` with the result.
The second is the body of `f()` itself. The JIT generally does not compile
module bodies, so for the rest of this document we’ll focus exclusively on
the body of `f()`.

## Entry to JIT compilation

By default if the JIT is enabled, all functions are compiled. If a JIT list
is provided, only functions on that list will be compiled. There is also an
option to JIT compile all Static Python functions, even if not on the JIT
list. In addition, the JIT can automatically compile hot functions based on 
their observed call count at runtime. The threshold used to automatically
compile hot functions is configurable.

These JIT options are set via `-X` options or environment variables; this
configuration is initialized in the `initFlagProcessor()` function in
`Jit/pyjit.cpp`. Use the option `-X jit-help` for an explanation of the
various options.

When a function is first called, if it should be JIT compiled we attempt to
compile it (see `PyEntry_LazyInit` and related functions in
`Python/ceval.c`.)

When `cinderjit.disable()` is called (this disables future JIT compilation,
it does not disable execution of JITted functions), any functions on the JIT
list that have been imported but have not yet been called (thus not yet
compiled) are compiled in a batch. If multi-threaded compilation is enabled,
we first serially perform preloading for all functions in the batch (this
requires the GIL and cannot be multi-threaded) and then spawn worker threads
to do JIT compilation in parallel. See `disable_jit()` function in
`Jit/pyjit.cpp` and related functions.

## Preloading

JIT compilation may require some globally-accessible Python values referenced
by the bytecode. This includes global values in the module referenced by
`LOAD_GLOBAL` opcodes (so that we can inline-cache the specific global value,
since it likely will not change) as well as types and invoke-targets
referenced by Static Python opcodes.

These must all be preloaded because loading them can trigger Python code
execution (especially in the presence of lazy imports), and Python code
execution during compilation can invalidate assumptions made by the compiler,
and cause deadlocks in multi-threaded compilation.

Preloading values for a function is handled by the `Preloader` class in
`Jit/hir/preload.{h,cpp}`.

NOTE: No other part of JIT compilation outside the preloader should ever do
anything that could trigger a Python import or any Python code execution,
including looking up values from a Python module, calling a classloader API,
or calling any PyObject C APIs that can trigger code execution (e.g. getting
attributes, subscripting, etc.)

Mutating Python objects (most commonly by touching their reference counts)
during JIT compilation is permitted but should be avoided as much as possible
by pushing work into the preloader. Where it occurs during compilation, it
must be wrapped in a `ThreadedCompileSerialize` guard to take the compile
lock, otherwise threads may race resulting in invalid mutations.

## Lowering to HIR

HIR is our "high-level [intermediate representation][3]." It looks roughly
similar to Python bytecode, though at a slightly lower level of abstraction,
and surfacing some behaviors important to performance that are hidden in
Python bytecode (e.g. reference counting).

Most of the HIR code lives in `Jit/hir/`. Lowering from Python bytecode to
HIR is implemented by `jit::hir::HIRBuilder` in `Jit/hir/builder.{h,cpp}`.
The entry point is the `BuildHIR` function.

NOTE: Types should never be directly associated with input operands or
outputs of HIR instructions in lowering; these types will just be lost when
types are flowed through HIR after SSA transformation. If type metadata from
the Python bytecode needs to be preserved, it should be stored in a field on
the relevant HIR instruction, and that field can be taken into account by
`outputType()` in `Jit/hir/ssa.cpp` when types are flowed.

[3]: https://en.wikipedia.org/wiki/Intermediate_representation

### HIR transformations

After lowering, HIR is converted into [SSA form][4] (in `Jit/hir/ssa.{h,cpp}`.)

HIR is typed (see `Jit/hir/type.md`), and this step also flows types through
the SSA HIR.

Other optimization passes are implemented in `Jit/hir/optimization.{h,cpp}`,
and run by `jit::Compiler::runPasses` (see `Jit/compiler.cpp`).

The last pass, `RefcountInsertion`, automatically inserts reference counting
operations into the optimized SSA HIR as needed based on metadata about the
reference-handling and memory effects of each HIR opcode. Reference-count
insertion is implemented in `Jit/hir/refcount_insertion.cpp`; see
`Jit/hir/refcount_insertion.md` for more details. Unlike optimization passes,
the refcount-insertion pass is required for correctness and is not
idempotent.

To see the final, optimized HIR for our function `f`, you can run::

```
# ./python -X jit-list-file=<(echo "__main__:f") -X jit-dump-final-hir f.py
JIT: ../../cinder/Jit/jit_list.cpp:33 -- Jit-list file: /proc/self/fd/12
JIT: ../../cinder/Jit/compiler.cpp:85 -- Optimized HIR for __main__:f:
fun __main__:f {
  bb 0 {
    v6:Object = LoadArg<0; "a">
    v7:Object = LoadArg<1; "b">
    v8:Object = LoadArg<2; "c">
    v10:CInt32 = IsTruthy v8 {
      LiveValues<3> b:v6 b:v7 b:v8
      NextInstrOffset 4
      Locals<3> v6 v7 v8
    }
    CondBranch<1, 2> v10
  }

  bb 1 (preds 0) {
    v13:Object = BinaryOp<Add> v6 v7 {
      LiveValues<3> b:v6 b:v7 b:v8
      NextInstrOffset 10
      Locals<3> v6 v7 v8
    }
    Return v13
  }

  bb 2 (preds 0) {
    v14:NoneType = LoadConst<NoneType>
    Incref v14
    Return v14
  }
}
```

[4]: https://en.wikipedia.org/wiki/Static_single_assignment_form

## Lowering to LIR

The final HIR is then lowered to LIR, our lower-level intermediate
representation, still in SSA form. Most of the LIR code lives in `Jit/lir/`,
and lowering to it is implemented in `jit::lir::Generator` in
`Jit/lir/generator.cpp`. Currently HIR is turned into a textual
representation of LIR, which is then parsed into LIR instruction objects (in
`Jit/lir/block_builder.cpp`), though we aim to eliminate the textual step.

## Register allocation and LIR optimizations

Register allocation is implemented in `Jit/lir/regalloc.cpp`. Other
optimizations on LIR (some before and some after register allocation) are
also implemented in the `Jit/lir/` directory. These are mostly
coordinated in `jit::codegen::NativeGenerator::GetEntryPoint()`.

To see the final LIR for our function, you can run
`./python -X jit-list-file=<(echo "__main__:f") -X jit-dump-lir f.py`.

Because LIR is quite low-level, this output is very long; a short snippet of
it (implementing the binary-add in our function by calling out to a CPython
runtime helper) looks like this (LIR dumps include the source HIR
instructions as comments):

```
# v13:Object = BinaryOp<Add> v6 v7 {
#   LiveValues<3> b:v6 b:v7 b:v8
#   NextInstrOffset 10
#   Locals<3> v6 v7 v8
# }
      RDI:Object = Move R12:Object
      RSI:Object = Move R13:Object
      RAX:Object = Move 17124416(0x1054c40):Object
                   Call RAX:Object
                   Guard 0(0x0):64bit, 1(0x1):Object, RAX:Object, 0(0x0):Object, R12:Object, R14:Object, R13:Object
```

## Code generation

x64 code generation is implemented in `Jit/codegen/autogen.cpp` and
`Jit/codegen/gen_asm.cpp`.

To see the final generated code for our function, you can run
`./python -X jit-list-file=<(echo "__main__:f") -X jit-dump-asm f.py`.

Again, the full output is quite long; the binary-add snippet corresponding to
the above LIR looks like this (HIR instruction context is still preserved):

```
v13:Object = BinaryOp<Add> v6 v7 {
  LiveValues<3> b:v6 b:v7 b:v8
  NextInstrOffset 10
  Locals<3> v6 v7 v8
}
  0x7f30f7db326b:        mov    %r12,%rdi
  0x7f30f7db326e:        mov    %r13,%rsi
  0x7f30f7db3271:        mov    $0x1054c40,%rax
  0x7f30f7db3278:        callq  *%rax
  0x7f30f7db327a:        test   %rax,%rax
  0x7f30f7db327d:        je     0x7f30f7db32b9
```

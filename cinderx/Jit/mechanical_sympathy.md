# Mechanical sympathy

*Note: This document is extremely unfinished and describes some thoughts about
a potential experiment.*

## Problem

It is hard to have an intuitive feel for how managed code performs. Take a
commit like DXXX. In its summary, it says "a previous commit did not provide
the performance gains expected. After manually looking at the bytecode, we
determined that...". This manual inspection of bytecode should be unnecessary;
the runtime knows both that the code is hot and that the code is not optimal.
Adding custom `printf`s to measure slow paths should become wholly unnecessary.
We should be able to better find performance wins if our tools tell us where to
dig.

See [this explanation](https://wa.aws.amazon.com/wellarchitected/2020-07-02T19-33-23/wat.concept.mechanical-sympathy.en.html):

>Mechanical sympathy is when you use a tool or system with an understanding of
>how it operates best.

and [this tweet](https://twitter.com/tekknolagi/status/1470685939441623041).

## Examples of finished product

The runtime should be able to expose information about any managed code.

#### "This is what the bytecode/HIR/LIR/asm looks like for this Python code"

#### "This opcode deopts a lot"

#### "The code never takes this branch"

#### "This opcode is super hot"

#### "This function has been called X times"

#### "This function has not been called in X time"

#### "This cache is polymorphic"

#### "Despite having monomorphic type profiles on builtin types, this code is very generic"

#### "This function cannot be inlined and the function's cache is monomorphic in each of its callers"

#### "This function cannot be inlined [because it's too big/is a coroutine/...]"

#### "This function allocates a lot but the allocations do not escape"

#### "This function is doing something bad to XYZ hardware performance counter"

icache, iTLB, etc

### Static Python specific

#### "This static function call does not use INVOKE"

Alt: "this code should be made static"

#### "X% of calls in this module never miss/deopt; consider making this module static"

#### "This builtin restoration/re-assignment code gets generated a lot in this static python module"

## Open questions

* Should this be streaming or batch?
* Should this be online or offline?
* Should this be integrated with the application or not?
* What is the max. amount of overhead we can afford?
* Can we use BPF?
* What about memory profiling? HProf?
* What existing formats are there for code annotations?

## Known complications

* Current explicit (read: not interpreting caches or compiled code) profiling
  code is at the bytecode level and cannot reason about the IRs it later
  compiles to because the JIT is turned off on that process.
* The on-disk format only cares about a mapping of `bc_offset` to type
  distribution. We will have to add more kinds of profiles and analysis inside
  the JIT.
* We'd need to output "stable" bytecode - strip out all addresses etc and
  replace them with some kind of IDs (should be able to adopt the stable
  disassembler from the Python compiler `dis_stable.py`)
* We would need to be able to map regions of Python code to bytecode to HIR to
  LIR to asm and back both for side-by-side viewing and for hw/sw performance
  counter attribution (is this a ["source map"](https://sourcemaps.info/spec.html)
  ([python impl](https://github.com/mattrobenolt/python-sourcemap))?)

## Simplifications

* Make a static dump of source code + annotations in HTML or something instead
  of attempting to live serve in LSP.
* Only surface information that's already being collected for other reasons,
  such as inline cache key types, deopts, etc.

## Concrete features

* Ability to diff static/non-static bytecode
* Explore the output of all compilation passes - Bytecode -> HIR -> LIR -> ASM
  (ideally, each of these will also be annotated with CPU usage information)
* Overlay bytecode branches with frequency of use (and potentially display this
  in a code browser/VSCode)

## MVP

The existing type profiler profiles type information at the bytecode level. We
can surface that information to code viewers, either in a the web source
viewer, via a language server (for text editors), or via some other custom
annotation mechanism.

We already log deoptimization points. We can surface them in a more
user-visible way and try out some code annotation tooling.

## Notes

* [Chrome trace event format](https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/edit)
* [Tracing C++ API](https://github.com/google/marl/blob/main/include/marl/trace.h)
* [Tracy](https://github.com/wolfpld/tracy)
* [JITProf](https://github.com/Berkeley-Correctness-Group/JITProf)
* [Optimization Coaching](https://www.ccs.neu.edu/home/stamourv/papers/optimization-coaching.pdf)
* [VSCode Godbolt](https://saveriomiroddi.github.io/Rust-lulz-godbolt-assembly-exploring-without-crate-limitations-in-visual-studio-code/)
* [Tokio Console](https://news.ycombinator.com/item?id=29594389)

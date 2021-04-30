Welcome to Cinder!
==================

Cinder is Instagram's internal performance-oriented production version of
CPython 3.8. It contains a number of performance optimizations, including
bytecode inline caching, eager evaluation of coroutines, a method-at-a-time
JIT, and an experimental bytecode compiler that uses type annotations to emit
type-specialized bytecode that performs better in the JIT.

For more information on CPython, see ``README.cpython.rst``.

Is this supported?
------------------

Short answer: no.

We've made Cinder publicly available in order to facilitate conversation
about potentially upstreaming some of this work to CPython and to reduce
duplication of effort among people working on CPython performance.

Cinder is not polished or documented for anyone else's use. We don't have the
capacity to support Cinder as an independent open-source project, nor any
desire for it to become an alternative to CPython. Our goal in making this
code available is a unified faster CPython. So while we do run Cinder in
production, if you choose to do so you are on your own. We can't commit to
fixing external bug reports or reviewing pull requests. We make sure Cinder
is sufficiently stable and fast for our production workload, but we make no
assurances about its stability or correctness or performance for any other
workload or use.

That said, if you have experience in dynamic language runtimes and have ideas
to make Cinder faster; or if you work on CPython and want to use Cinder as
inspiration for improvements in CPython (or help upstream parts of Cinder to
CPython), please reach out; we'd love to chat!


How do I build it?
------------------

It should build just like CPython; ``configure`` and ``make -j``.

Cinder is only built or tested on Linux x64; anything else (including OS X)
probably won't work. We validate in Github CI that it builds and tests pass
on Fedora 32 (see ``oss-build-and-test.sh`` and
``.github/workflows/cinder-oss-build-and-test.yml``), so Fedora 32 is
probably your best bet.

There are some new test targets that might be interesting. ``make
testcinder`` is pretty much the same as ``make test`` except that it skips a
few tests that are problematic in our dev environment. ``make
testcinder_jit`` runs the test suite with the JIT fully enabled, so all
functions are JITted. ``make testruntime`` runs a suite of C++ gtest unit
tests for the JIT. And ``make test_strict_module`` runs a test suite for
strict modules (see below).


What's here?
------------

Shadowcode
~~~~~~~~~~

"Shadowcode" or "shadow bytecode" is our inline caching implementation. It
observes particular optimizable cases in the execution of generic Python
opcodes and (for hot functions) dynamically replaces those opcodes with
specialized versions. The core of shadowcode lives in
``Python/shadowcode.c``, though the implementations for the specialized
bytecodes are in ``Python/ceval.c`` with the rest of the eval loop.
Shadowcode-specific tests are in ``Lib/test/test_shadowcode.py``.

Eager coroutine evaluation
~~~~~~~~~~~~~~~~~~~~~~~~~~

If a call to an async function is immediately awaited, we immediately execute
the called function up to its first ``await``. If the called function reaches
a ``return`` without needing to await, we will be able to return that value
directly without ever even creating a coroutine object or deferring to the
event loop. This is a significant (~5%) CPU optimization in our async-heavy
workload.

This is mostly implemented in ``Python/ceval.c``, via a new vectorcall flag
``_Py_AWAITED_CALL_MARKER``, indicating the caller is immediately awaiting
this call. Look for uses of the ``IS_AWAITED()`` macro and this vectorcall
flag, as well as the ``_PyEval_EvalEagerCoro`` function.

The Cinder JIT
~~~~~~~~~~~~~~

The Cinder JIT is a method-at-a-time custom JIT implemented in C++. It is
enabled via the ``-X jit`` flag or the ``PYTHONJIT=1`` environment variable.
It supports almost all Python opcodes, and can achieve 1.5-4x speed
improvements on many Python performance benchmarks.

By default when enabled it will JIT-compile every function that is ever
called, which may well make your program slower, not faster, due to overhead
of JIT-compiling rarely-called functions. The option ``-X
jit-list-file=/path/to/jitlist.txt`` or
``PYTHONJITLISTFILE=/path/to/jitlist.txt`` can point it to a text file
containing fully qualified function names (in the form
``path.to.module:funcname`` or ``path.to.module:ClassName.method_name``),
one per line, which should be JIT-compiled. We use this option to compile
only a set of hot functions derived from production profiling data. (A more
typical approach for a JIT would be to dynamically compile functions as they
are observed to be called frequently. It hasn't yet been worth it for us to
implement this, since our production architecture is a pre-fork webserver,
and for memory sharing reasons we wish to do all of our JIT compiling up
front in the initial process before workers are forked, which means we can't
observe the workload in-process before deciding which functions to
JIT-compile.)

The JIT lives in the ``Jit/`` directory, and its C++ tests live in
``RuntimeTests/`` (run these with ``make testruntime``). There are also some
Python tests for it in ``Lib/test/test_cinderjit.py``; these aren't meant to
be exhaustive, since we run the entire CPython test suite under the JIT via
``make testcinder_jit``; they cover JIT edge cases not otherwise found in the
CPython test suite.

See ``Jit/pyjit.cpp`` for some other ``-X`` options and environment variables
that influence the behavior of the JIT. There is also a ``cinderjit`` module
defined in that file which exposes some JIT utilities to Python code (e.g.
forcing a specific function to compile, checking if a function is compiled,
disabling the JIT). Note that ``cinderjit.disable()`` only disables future
compilation; it immediately compiles all known functions and keeps existing
JIT-compiled functions.

The JIT first lowers Python bytecode to a high-level intermediate
representation (HIR); this is implemented in ``Jit/hir/``. HIR maps
reasonably closely to Python bytecode, though it is a register machine
instead of a stack machine, it is a bit lower level, it is typed, and some
details that are obscured by Python bytecode but important for performance
(notably reference counting) are exposed explicitly in HIR. HIR is
transformed into SSA form, some optimization passes are performed on it, and
then reference counting operations are automatically inserted into it
according to metadata about the refcount and memory effects of HIR opcodes.

HIR is then lowered to a low-level intermediate representation (LIR), which
is an abstraction over assembly, implemented in ``Jit/lir/``. In LIR we do
register allocation, some additional optimization passes, and then finally
LIR is lowered to assembly (in ``Jit/codegen/``) using the excellent
`asmjit`_ library.

The JIT is in its early stages. While it can already eliminate interpreter
loop overhead and offers significant performance improvements for many
functions, we've only begun to scratch the surface of possible optimizations.
Many common compiler optimizations are not yet implemented. Our
prioritization of optimizations is largely driven by the characteristics of
the Instagram production workload.

.. _asmjit: https://asmjit.com/

Strict Modules
~~~~~~~~~~~~~~

Strict modules is a few things rolled into one:

1. A static analyzer capable of validating that executing a module's
top-level code will not have side effects visible outside that module.

2. An immutable ``StrictModule`` type usable in place of Python's default
module type.

3. A Python module loader capable of recognizing modules opted in to strict
mode (via an ``import __strict__`` at the top of the module), analyzing them
to validate no import side effects, and populating them in ``sys.modules`` as
a ``StrictModule`` object.

The version of strict modules that we currently use in production is written
in Python and is not part of Cinder. The ``StrictModules/`` directory in
Cinder is an in-progress C++ rewrite of the import side effects analyzer.

Static Python
~~~~~~~~~~~~~

Static Python is an experimental bytecode compiler that makes use of type
annotations to emit type-specialized and type-checked Python bytecode. Used
along with the Cinder JIT, it can deliver performance similar to `MyPyC`_ or
`Cython`_ in many cases, while offering a pure-Python developer experience
(normal Python syntax, no extra compilation step). Static Python plus Cinder
JIT achieves 7x the performance of stock CPython on a typed version of the
Richards benchmark. At Instagram we have successfully used Static Python in
production to replace the majority of the Cython modules in our primary
webserver codebase, with no performance regression.

The Static Python compiler is built on top of the Python ``compiler`` module
that was removed from the standard library in Python 3 and has since been
maintained and updated externally; this compiler is incorporated into Cinder
in ``Lib/compiler``. The Static Python compiler is implemented in
``Lib/compiler/static.py``, and its tests are in
``Lib/test/test_compiler/test_static.py``.

Classes defined in Static Python modules are automatically given typed slots
(based on inspection of their typed class attributes and annotated
assignments in ``__init__``), and attribute loads and stores against
instances of these types use new ``STORE_FIELD`` and ``LOAD_FIELD`` opcodes,
which in the JIT become direct loads/stores from/to a fixed memory offset in
the object, with none of the indirection of a ``LOAD_ATTR`` or
``STORE_ATTR``. Classes also gain vtables of their methods, for use by the
``INVOKE_*`` opcodes mentioned below. The runtime support for these features
is located in ``Include/classloader.h`` and ``Python/classloader.c``.

A static Python function begins with a new ``CHECK_ARGS`` opcode which checks
that the supplied arguments' types match the type annotations, and raises
``TypeError`` if not. Calls from a static Python function to another static
Python function will skip this opcode (since the types are already validated
by the compiler). Static to static calls can also avoid much of the overhead
of a typical Python function call. We emit an ``INVOKE_FUNCTION`` or
``INVOKE_METHOD`` opcode which carries with it metadata about the called
function or method; this plus optionally immutable modules (via
``StrictModule``) and types (via ``cinder.freeze_type()``, which we currently
apply to all types in strict and static modules in our import loader, but in
future may become an inherent part of Static Python) and compile-time
knowledge of the callee signature allow us to (in the JIT) turn many Python
function calls into direct calls to a fixed memory address using the x64
calling convention, with little more overhead than a C function call.

Static Python is still gradually typed, and supports code that is only
partially annotated or uses unknown types by falling back to normal Python
dynamic behavior. In some cases (e.g. when a value of statically-unknown type
is returned from a function with a return annotation), a runtime ``CAST``
opcode is inserted which will raise ``TypeError`` if the runtime type does
not match the expected type.

Static Python also supports new types for machine integers, bools, doubles,
and vectors/arrays. In the JIT these are handled as unboxed values, and e.g.
primitive integer arithmetic avoids all Python overhead. Some operations on
builtin types (e.g. list or dictionary subscript or ``len()``) are also
optimized.

Cinder doesn't currently come bundled with a module loader that is able to
automatically detect static modules and load them as static with cross-module
compilation; we currently do this via our strict/static import loader which
is not part of Cinder. Currently the best way to experiment with static
python in Cinder is to use ``./python -m compiler --static some_module.py``,
which will compile the module as static Python and execute it. (Add the
``--dis`` flag to also disassemble it after compilation.) Since this does not
use a ``StrictModule`` nor freeze types by default, the resulting code won't
JIT as optimally as what we get in prod, particularly for function and method
calls.


.. _MyPyC: https://github.com/mypyc/mypyc
.. _Cython: https://cython.org/

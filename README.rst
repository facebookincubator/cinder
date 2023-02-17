.. image:: CinderDoc/images/Cinder-Logo-White.svg#gh-dark-mode-only
  :width: 400
  :alt: Cinder Logo

.. image:: CinderDoc/images/Cinder-Logo-Dark.svg#gh-light-mode-only
  :width: 400
  :alt: Cinder Logo

.. image:: https://img.shields.io/badge/Support-Ukraine-FFD500?style=flat&labelColor=005BBB
   :alt: Support Ukraine - Help Provide Humanitarian Aid to Ukraine.
   :target: https://opensource.facebook.com/support-ukraine

.. image:: https://github.com/facebookincubator/cinder/actions/workflows/cinder-oss-build-and-test.yml/badge.svg?branch=cinder%2F3.10
   :alt: Cinder build status on GitHub Actions
   :target: https://github.com/facebookincubator/cinder/actions/workflows/cinder-oss-build-and-test.yml

Welcome to Cinder!
==================

Cinder is Meta's internal performance-oriented production version of
CPython 3.10. It contains a number of performance optimizations, including
bytecode inline caching, eager evaluation of coroutines, a method-at-a-time
JIT, and an experimental bytecode compiler that uses type annotations to emit
type-specialized bytecode that performs better in the JIT.

Cinder is powering Instagram, where it started, and is increasingly
used across more and more Python applications in Meta.

For more information on CPython, see ``README.cpython.rst``.

Is this supported?
------------------

Short answer: no.

We've made Cinder publicly available in order to facilitate conversation
about potentially upstreaming some of this work to CPython and to reduce
duplication of effort among people working on CPython performance.

Cinder is not polished or documented for anyone else's use. We don't have the
desire for it to become an alternative to CPython. Our goal in making this
code available is a unified faster CPython. So while we do run Cinder in
production, if you choose to do so you are on your own. We can't commit to
fixing external bug reports or reviewing pull requests. We make sure Cinder
is sufficiently stable and fast for our production workloads, but we make no
assurances about its stability or correctness or performance for any external
workloads or use-cases.

That said, if you have experience in dynamic language runtimes and have ideas
to make Cinder faster; or if you work on CPython and want to use Cinder as
inspiration for improvements in CPython (or help upstream parts of Cinder to
CPython), please reach out; we'd love to chat!


How do I build it?
------------------

Cinder should build just like CPython; ``configure`` and ``make -j``. However
as most development and usage of Cinder occurs in the highly specific context of
Meta we do not exercise it much in other environments. As such, the most
reliable way to build and run Cinder is to re-use the Docker-based setup from
our GitHub CI workflow.

If you just want to get a working Cinder without building it yourself, our
`Runtime Docker Image`_ is going to be the easiest (no repo clone needed!):

#. Install and setup Docker.
#. Fetch and run our cinder-runtime image:
    ``docker run -it --rm ghcr.io/facebookincubator/cinder-runtime:cinder-3.10``

If you want to build it yourself:

#. Install and setup Docker.
#. Clone the Cinder repo:
    ``git clone https://github.com/facebookincubator/cinder``
#. Run a shell in the Docker environment used by the CI:
    ``docker run -v "$PWD/cinder:/vol" -w /vol -it --rm ghcr.io/facebookincubator/cinder/python-build-env:latest bash``

   The above command does the following:
        * Downloads (if not already cached) a pre-built Docker image used by the
          CI from
          https://ghcr.io/facebookincubator/cinder/python-build-env.
        * Makes the Cinder checkout above (`$PWD/cinder`) available to the
          Docker environment at the mount point `/vol`.
        * Interactively (`-it`) runs `bash` in the `/vol` directory.
        * Cleanup the local image after it's finished (`--rm`) to avoid disk bloat.
#. Build Cinder from the shell started the Docker environment:
    ``./configure && make``

Please be aware that Cinder is only built or tested on Linux x64; anything else
(including macOS) probably won't work. The Docker image above is Fedora
Linux-based and built from a Docker spec file in the Cinder repo:
``.github/workflows/python-build-env/Dockerfile``.

There are some new test targets that might be interesting. ``make
testcinder`` is pretty much the same as ``make test`` except that it skips a
few tests that are problematic in our dev environment. ``make
testcinder_jit`` runs the test suite with the JIT fully enabled, so all
functions are JIT'ed. ``make testruntime`` runs a suite of C++ gtest unit
tests for the JIT. And ``make test_strict_module`` runs a test suite for
strict modules (see below).

Note that these steps produce a Cinder Python binary without PGO/LTO optimizations enabled,
so don't expect to use these instructions to get any speedup on any Python workload.

.. _Runtime Docker Image: https://github.com/facebookincubator/cinder/pkgs/container/cinder-runtime


How do I explore it?
--------------------

`Cinder Explorer`_ is a live playground, where you can
see how Cinder compiles Python code from source to assembly -- you're welcome
to try it out! Feel free to file feature requests and bug reports. Keep in mind
that the Cinder Explorer, like the rest of this, "supported" on a best-effort
basis.

.. _Cinder Explorer: https://trycinder.com

What's here?
------------

Immortal Instances
~~~~~~~~~~~~~~~~~~

Instagram uses a multi-process webserver architecture; the parent process
starts, performs initialization work (e.g. loading code), and forks tens of
worker processes to handle client requests. Worker processes are restarted
periodically for a number of reasons (e.g. memory leaks, code deployments) and
have a relatively short lifetime. In this model, the OS must copy the entire
page containing an object that was allocated in the parent process when the
object's reference count is modified. In practice, the objects allocated
in the parent process outlive workers; all the work related to reference
counting them is unnecessary.

Instagram has a very large Python codebase and the overhead due to
copy-on-write from reference counting long-lived objects turned out to be
significant. We developed a solution called "immortal instances" to provide a
way to opt-out objects from reference counting. See `Include/object.h` for
details. This feature is controlled by defining `Py_IMMORTAL_INSTANCES` and is
enabled by default in Cinder. This was a large win for us in production (~5%),
but it makes straight-line code slower. Reference counting operations occur
frequently and must check whether or not an object participates in reference
counting when this feature is enabled.


Shadowcode
~~~~~~~~~~

"Shadowcode" or "shadow bytecode" is our implementation of a specializing
interpreter. It observes particular optimizable cases in the execution of
generic Python opcodes and (for hot functions) dynamically replaces those
opcodes with specialized versions. The core of shadowcode lives in
``Python/shadowcode.c``, though the implementations for the specialized
bytecodes are in ``Python/ceval.c`` with the rest of the eval loop.
Shadowcode-specific tests are in ``Lib/test/test_shadowcode.py``.

It is similar in spirit to the specializing adaptive interpreter (PEP-659)
that will be built into CPython 3.11.

Await-aware function calls
~~~~~~~~~~~~~~~~~~~~~~~~~~

The Instagram Server is an async-heavy workload, where each web request may
trigger hundreds of thousands of async tasks, many of which can be completed
without suspension (e.g. thanks to memoized values).

We extended the vectorcall protocol to pass a new flag,
``Ci_Py_AWAITED_CALL_MARKER``, indicating the caller is immediately awaiting
this call.

When used with async function calls that are immediately awaited, we can
immediately (eagerly) evaluate the called function, up to completion, or up
to its first suspension. If the function completes without suspending, we are
able to return the value immediately, with no extra heap allocations.

When used with async gather, we can immediately (eagerly) evaluate the set of
passed awaitables, potentially avoiding the cost of creation and scheduling of
multiple tasks for coroutines that could be completed synchronously, completed
futures, memoized values, etc.

These optimizations resulted in a significant (~5%) CPU efficiency improvement.

This is mostly implemented in ``Python/ceval.c``, via a new vectorcall flag
``Ci_Py_AWAITED_CALL_MARKER``, indicating the caller is immediately awaiting
this call. Look for uses of the ``IS_AWAITED()`` macro and this vectorcall
flag.

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

Static Python
~~~~~~~~~~~~~

Static Python is a bytecode compiler that makes use of type annotations to
emit type-specialized and type-checked Python bytecode. Used along with the
Cinder JIT, it can deliver performance similar to `MyPyC`_ or `Cython`_ in
many cases, while offering a pure-Python developer experience (normal Python
syntax, no extra compilation step). Static Python plus Cinder JIT achieves
18x the performance of stock CPython on a typed version of the Richards
benchmark. At Instagram we have successfully used Static Python in production
to replace all Cython modules in our primary webserver codebase, with no
performance regression.

The Static Python compiler is built on top of the Python ``compiler`` module
that was removed from the standard library in Python 3 and has since been
maintained and updated externally; this compiler is incorporated into Cinder
in ``Lib/compiler``. The Static Python compiler is implemented in
``Lib/compiler/static/``, and its tests are in
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

Cinder supports gradual adoption of static modules via a strict/static module
loader that can automatically detect static modules and load them as static
with cross-module compilation. The loader will look for ``import __static__``
and ``import __strict__`` annotations at the top of a file, and compile
modules appropriately. To enable the loader, you have one of three options:

1. Explicitly install the loader at the top level of your application
via ``from compiler.strict.loader import install; install()``.

2. Set ``PYTHONINSTALLSTRICTLOADER=1`` in your env.

3. Run ``./python -X install-strict-loader application.py``.

Alternatively, you can compile all code statically by using
``./python -m compiler --static some_module.py``,
which will compile the module as static Python and execute it.

See ``CinderDoc/static_python.rst`` for more detailed documentation.


.. _MyPyC: https://github.com/mypyc/mypyc
.. _Cython: https://cython.org/

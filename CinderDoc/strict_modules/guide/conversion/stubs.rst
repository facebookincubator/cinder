Strict Module Stubs
###################

Sometimes your modules depend on other modules that cannot be directly
strictified - it could depend on a Cython module, or a module from a
third-party library whose source code you can't modify.

In this situation, if you are certain that the dependency is strict, you
can provide a strict module stub file (`.pys`) describing the behavior of
the module. Put the strict module stub file in your strict module stubs directory
(this is configured via `-X strict-module-stubs-path=...=` or
`PYTHONSTRICTMODULESTUBSPATH` env var, or by subclassing `StrictSourceFileLoader`
and passing a `stub_path` argument to `super().__init__(...)`.)

There are two ways to stub a class or function in a strict module stub file.
You can provide a full Python implementation, which is useful in the case
of stubbing a Cython file, or you can just provide a function/class name,
with a `@implicit` decorator. In the latter case, the stub triggers the
strict module analyzer to look for the source code on `sys.path` and analyze
the source code.

If the module you depend on is already actually strict-compliant you can
simplify the stub file down to just contain the single line `__implicit__`,
which just says "go use the real module contents, they're fine".  See
`cinderx/PythonLib/cinderx/compiler/strict/stubs/_collections_abc.pys` for an
existing example.  Per-class/function stubs are only needed where the stdlib
module does non-strict things at module level, so we need to extract just the
bits we depend on and verify them for strictness.

If both a `.py` file and a `.pys` file exist, the strict module analyzer will
prioritize the `.pys` file. This means adding stubs to existing
modules in your codebase will shadow the actual implementation.
You should probably avoid doing this.

Example of Cython stub:

**myproject/worker.py**

.. code-block:: python

    from some_cython_mod import plus1

    two = plus1(1)


Here you can provide a stub for the Cython implementation of `plus1`

**strict_modules/stubs/some_cython_mod.pys**

.. code-block:: python

    # a full reimplementation of plus1
    def plus1(arg):
        return arg + 1

Suppose you would like to use the standard library functions `functools.wraps`,
but the strict module analysis does not know of the library. You can add an implicit
stub:

**strict_modules/stubs/functools.pys**

.. code-block:: python

    @implicit
    def wraps(): ...

You can mix explicit and implicit stubs. See `CinderX/cinderx/compiler/strict/stubs` for some examples.

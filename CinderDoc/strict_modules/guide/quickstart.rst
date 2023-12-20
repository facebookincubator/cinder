# Quickstart

## How do I use it?

Using Strict Modules requires a module loader able to detect strict modules
based on some marker (we use the presence of `import __strict__`).
Such a loader is included (at `compiler.strict.loader.StrictSourceFileLoader`)
and you can install it by calling `compiler.strict.loader.install()` in the
"main" module of your program (before anything else is imported.)
Note this means the main module itself cannot be strict. Alternatively, set the
`PYTHONINSTALLSTRICTLOADER` environment variable to a nonzero value, and
the loader will be installed for you (but then you can't customize the loader).

## How do I make my module strict?

To opt your module in, place the line ``import __strict__`` at the top of the
module. The ``__strict__`` marker line should come after the docstring if
present, after any ``from __future__ import`` statements, and before any
other imports. Comments can also precede the ``__strict__`` marker.

If your module is marked as strict but violates the strict-mode rules, you
will get detailed errors when you try to import the module.

> Note: The "launcher" module (the `__main__` in Python terms) cannot be marked
> strict, because by default, it must have one side-effect (of launching the
> application).

What are the risks?
-------------------

Most of the strict-mode restrictions have purely local effect; if you are
able to import your module after marking it strict, you're mostly good to go!
There are a couple runtime changes that can impact code outside the module:

1. Strict mode makes the module itself and any classes in it immutable after
the module is done executing. This is most likely to impact tests that
monkeypatch the module or its classes. Refer to the :doc:`conversion/testing`
section to learn how to enable patching of strict moduels for testing.

2. Instances of strict classes have `__slots__
<https://docs.python.org/3/reference/datamodel.html#slots>`_ automatically
created for them. This means they will raise ``AttributeError`` if you try to
add any attribute to them that isn't declared with a type annotation on the
class itself (e.g. ``attrname: int``) or assigned in the ``__init__`` method.
If you aren't confident that this isn't happening to your class somewhere in
the codebase, you can temporarily place the ``strict_modules.loose_slots``
decorator on the class for a safer transition. See
:doc:`conversion/loose_slots` for details.

What are the benefits?
----------------------

When you convert your module to strict, you immediately get these benefits:

1. It becomes impossible to accidentally introduce import side effects in
your module, which prevents problems that can eat up debugging time or even
break prod.

2. It becomes impossible to accidentally modify global state by mutating your
module or one of the classes in your module, also preventing bugs and test
flakiness.

In the future, we hope that you will get other benefits too, like faster
imports when the module is unchanged since last import and production
efficiency improvements as well.

What if I get a StrictModuleException?
--------------------------------------

See :doc:`errors/index` for advice on handling errors in your strict module.

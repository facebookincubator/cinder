Deterministic Execution
#######################

One of the most significant limitations, and the primary point of strict
modules, is to enforce that their contents are deterministic.  This is
achieved by having an allow-list approach of what can occur inside of a strict
module at import time. The allow-list is composed of standard Python syntax
and the set of :doc:`builtins` which are allowed.  Use of anything that
isn't analyzable will result in an error when importing a strict module.

Strict modules themselves are verified to conform to the allow list by
an analysis done with an interpreter.  The interpreter
will precisely simulate the execution of your code, analyzing loops,
flow control, exceptions and all other typical Python language elements
that are available.  This means that you have available to you the full
breadth of the language.

Strict modules will only validate code at the module
top-level - that includes elements such as top-level class declarations,
annotations on functions (unless `from __future__ import annotations` is
applied), etc.  It will also analyze the result of calling any functions from
the top-level (which includes decorators on top-level definitions).

The result is that while you are more limited in what you can
do within your module definitions, your actual functions aren't limited in
what they are allowed to do. Effectively a slightly more limited Python is
your meta-programming language for defining your Python programs.

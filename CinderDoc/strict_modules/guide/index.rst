Users Guide
###########

Getting Started
---------------

Writing your first strict module is super easy.  Check out the
:doc:`quickstart` to find out more.

What does it mean if my module is strict?
-----------------------------------------
The short version of the strict-module rules: your module may not do anything
dynamic at import time that might have external side effects; it should just
define a set of names (constants, functions, and classes).

While that might seem a little bit limiting, there is still a wealth of
built-ins that are supported.  You can still do a lot of reflection over
types, use normal constructs like lists and dictionaries, comprehensions,
and a large number of built-in functions within your module definition.

Making your modules strict means you receive benefits of strictness, including
but not limited to increased reliability and performance improvements.


Limitations
-----------
Strict modules undergo a number of different checks to ensure that they will
reliably and consistently produce the same module for the same input source
code. This ensures that top-level modules will always reliably succeed and
won't take dependencies on external environmental factors.

.. toctree::
    :maxdepth: 1
    :titlesonly:
    :glob:

    limitations/*


.. _conversion_tips:

Conversion Tips
---------------
This section includes tips on how to convert a non-strict module into a strict
module, common problems which might come up, and how you can effectively
:doc:`test <conversion/testing>` your strict modules even though they're
immutable.


.. toctree::
    :maxdepth: 1
    :titlesonly:
    :glob:

    conversion/*


Strict Module Errors / Exceptions
---------------------------------
When converting a module to strict, or when modifying an existing strict
module, you may see many different errors reported by the linter.  These will
have an exception name associated with them along with a detailed message
explaining what and where something went wrong in verifying a module as
strict.  This section has detailed description of what error means and how you
can possibly fix it.



.. toctree::
    :maxdepth: 1
    :titlesonly:
    :glob:

    errors/index
    errors/*

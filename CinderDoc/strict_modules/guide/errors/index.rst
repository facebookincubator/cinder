Intro to Errors
###############

First, read the error and the documentation page linked from the error
message to understand what it's telling you!

One common theme in all of these error messages will be the concept of an
"unknown value". An unknown value in a strict module is a value that comes
from a source that the strict module doesn't understand. That typically means
it's imported from a non-strict module or from an operation on an unknown
value. The error message will attempt to give you detailed information on the
source of the value, including the initial unknown name that was imported or
used and the chain of operations performed against that value.

Typically an unknown value will be displayed as one of ``<imported module
unknown>`` where unknown is the name of the non-strict imported module,
``<some_name imported from unknown>`` where the value comes from ``from
unknown import some_name``, or just a simple name like ``<unknown>`` if the
value comes from an undefined global or unknown built-in.

There are a few basic causes of errors:

* Your code is actually doing something side-effecty or unsafe in top-level
  code (including functions called from top-level code, e.g. decorators), and
  strict modules is alerting you to the problem. In this case you should adjust
  your code to not do this. :doc:`../conversion/singletons` has some advice on
  moving side-effecty code out of the import codepath by making it lazy.

* Your code is actually fine, but you are using at module level
  some class or function that you imported from a non-strict module, so our
  analysis doesn't know about it and is flagging that you are using an "unknown
  value" at import time. Early in adoption, this will likely be a common case.
  Options for dealing with it:

  a. You can try converting the module you are importing from to be strict
     itself. If the module is hard to convert, but the specific piece of it you
     need is not, you could :doc:`split the module
     <../conversion/splitting_modules>`.
  b. If the dependency is external to your codebase (i.e. third-party),
     you can add a :doc:`stub file <../conversion/stubs>` to tell
     strict modules about it.
  c. If (a) and (b) are hard and you need to unblock yourself, you can
     remove `import __strict__` from your module for now. This could require
     de-strictifying other modules as well, if other strict modules import from
     yours.

* Your code is actually fine, but strict modules is not able to analyze it
  correctly (e.g. a missing built-in, or some aspect of the language we aren't
  fully analyzing correctly yet). In this case you should just remove
  `import __strict__` to unblock yourself and report a bug so we
  can fix the problem.

For guidance on specific errors please refer to this list:


.. toctree::
    :maxdepth: 1
    :titlesonly:
    :glob:

    *

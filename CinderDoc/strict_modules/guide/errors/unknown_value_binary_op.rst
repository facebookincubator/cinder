UnknownValueBinaryOpException
#############################

  Module-level binary operation on non-strict value 'lvalue [op] rvalue' is prohibited.

This error indicates that you are attempting to perform a binary operation on
a value that can't be analyzed by strict modules (e.g. because it is imported
from a non-strict module). Because binary operations can be overridden in
Python to execute arbitrary code, doing this on an unknown value can cause
arbitrary side effects and is prohibited at top level of a strict module.

.. code-block:: python

    from nonstrict import SOME_CONST

    MY_CONST = SOME_CONST + 1

This code will result in an error message such as:

``UnknownValueBinaryOpException`` '<SOME_CONST imported from nonstrict> + 1'

The error tells you strict modules' understanding of the values on each side
of the binary operation, and what the operation itself is.

Typically the best solution to this situation is to make the module
containing ``SOME_CONST`` strict.

You can look at :ref:`conversion_tips` for more ways to fix this error.

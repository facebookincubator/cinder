UnknownValueAttributeException
##############################

  Module-level attribute access on non-strict value 'value.attr' is prohibited.

This error indicates that you are attempting to access an attribute on a
value that can't be analyzed by strict modules (e.g. because it is imported
from a non-strict module). Because attribute access in Python can execute
arbitrary code, doing this on an unknown value (where we can't analyze the
effects) could cause arbitrary side effects and is prohibited at module
level.

.. code-block:: python

    from nonstrict import something

    class MyClass(something.SomeClass):
        pass

This code will result in an error message such as:

``UnknownValueAttributeException`` '<something imported from nonstrict>.SomeClass'

The error tells you both what unknown value you are accessing an attribute
on, and the name of the attribute.

One possible solution to this is making the nonstrict module strict so that
``something`` can be used at the top-level. If the nonstrict module is not in
your codebase you could create a :doc:`stub file <../conversion/stubs>` for it.

In a case like the above example where the unknown value is an imported module,
you can also solve it like this:

.. code-block:: python

    from nonstrict.something import SomeClass

    class MyClass(SomeClass):
        pass

You can look at :ref:`conversion_tips` for more ways to fix this error.

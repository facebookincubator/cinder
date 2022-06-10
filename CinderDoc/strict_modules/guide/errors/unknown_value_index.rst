UnknownValueIndexException
##########################

  Module-level index into non-strict value 'value[index]' is prohibited.

This error indicates that you are attempting to index into a value that
strict modules can't analyze.

.. code-block:: python

    from nonstrict import GenericClass

    class MyClass(GenericClass[int]):
        pass

This code will result in an error message such as:

``UnknownValueIndexException`` '<GenericClass imported from nonstrict>[<type: int>]'

Here you can see the error tells you both what value we are accessing which
is not statically analyzable, but also what value we are using to
index into the unknown value (in this case the int type).

One possible solution to this is making the nonstrict module strict so that
GenericClass can be used at the top-level.  If the non-strict module is
something from the Python standard library you might want to report a bug!

You can look at :ref:`conversion_tips` for more ways to fix this error.

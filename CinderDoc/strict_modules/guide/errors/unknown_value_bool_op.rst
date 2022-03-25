UnknownValueBoolException
#########################

  Module-level conversion to bool on non-strict value 'value' is prohibited.

This error indicates that you are attempting to convert a value to a bool,
either explicitly by calling bool(value) on it or implicitly by using it
in a conditional location (e.g. if, while, if expression, or, and, etc...)


.. code-block:: python

    from nonstrict_module import x

    if x:
        pass

This code will result in an error message such as:

``UnknownValueBoolException`` 'Conversion to bool on unknown
value '{x imported from nonstrict_module}' is prohibited.'

Here you can see the error tells you the value which is being checked for
truthiness.

One possible solution to this is making the nonstrict module strict so that
x can be used at the top-level.  If the non-strict module is something from
the Python standard module you might want to report a bug!

You can look at :ref:`conversion_tips` for more ways to fix this error.

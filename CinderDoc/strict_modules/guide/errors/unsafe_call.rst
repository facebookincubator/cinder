UnsafeCallException
###################

  Call 'function()' may have side effects and is prohibited at module level.

This error indicates that you are attempting to call a function whose execution
will cause side effects or has elements which can not be successfully verified.


.. code-block:: python

    import __strict__

    def side_effects():
        print("I have side effects")
        return 42

    FORTY_TWO = side_effects()

This code will result in an error message such as:

``UnsafeCallException`` 'Call 'side_effects()' may have side effects and
is prohibited at module level.'

In addition to this error from the call site you'll see an error about the
underlying violation in the function.  Here the underlying error turned out
to be a :doc:`unknown_call`.  In this case strict modules has no
definition of print because its sole purpose is to cause side effects:

``UnknownValueCallException`` Call of unknown value 'print()' is prohibited.

You can consider removing the prohibited operation from the function,
or move the call to the side effecting function out of the top-level.
You can look at :ref:`conversion_tips` for more ways to fix this error.

If the underlying operation in the function is something you think strict
modules should support the analysis of you might want to report a bug!

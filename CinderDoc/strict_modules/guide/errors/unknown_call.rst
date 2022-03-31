UnknownValueCallException
#########################

  Module-level call of non-strict value 'function()' is prohibited.

This error indicates that you are attempting to call a value at module level
which the strict modules analysis does not understand.

Currently the most likely reason you would see this error is because you are
importing something from a non-strict module and then calling it at module
level. For example:

.. code-block:: python

    import __strict__
    from nonstrict_module import something

    x = something()


This code will result in an error message such as:

``UnknownValueCallException`` 'Call of unknown value 'something()' is prohibited.'

If `something()` has no side effects (it only returns a value), your options,
in order of preference, are to a) strictify the `nonstrict_module`, b) call
`something()` lazily on demand rather than at module level, or c)
de-strictify the module you are currently working in. If `something()` does
in fact have side effects, the only option you should consider is (b).

Another case where you might see this is with an unsupported builtin:

.. code-block:: python

    import __strict__

    print('hi')


In this case we're attempting to call a built-in function which strict
modules don't support. Printing is typically a side effect so we don't
currently support it at module level. There are a number of other built-ins
which aren't currently supported as well. For the full list of supported
builtins see :doc:`/strict_modules/guide/limitations/builtins`.

We can fix this case by removing the usage of the built-in at the top-level.

If the function is a built-in and something you think strict modules should
support at module level, you might want to report a bug!

.. code-block:: python

    import __strict__

    ALPHABETE = lsit('abcdefghijklmnopqrstuvwxyz')


``UnknownValueCallException`` 'Call of unknown value 'lsit()' is prohibited.'

In this case we've simply made a mistake and misspelled the normal built-in
list.  One nice thing is that strict modules will detect this and give you
an early warning.  We can fix it by just fixing the spelling.

You can look at :ref:`conversion_tips` for other possible solutions to issues
like this.

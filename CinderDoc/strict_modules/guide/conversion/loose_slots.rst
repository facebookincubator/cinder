The @loose_slots decorator
##########################

Instances of strict classes have `__slots__
<https://docs.python.org/3/reference/datamodel.html#slots>`_ automatically
created for them. This means they will raise ``AttributeError`` if you try to
add any attribute to them that isn't declared with a type annotation on the
class itself (e.g. ``attrname: int``) or assigned in the ``__init__`` method.

When initially converting a module to strict, if it is widely-used it can be
hard to verify that there isn't code somewhere tacking extra attributes onto
instances of classes defined in that module. In this case, you can temporarily
place the ``strict_modules.loose_slots`` decorator on the class for a safer
transition. Example:

.. code-block:: python

    import __strict__

    from cinderx.compiler.strict.runtime import loose_slots

    @loose_slots
    class MyClass:
        ...

This decorator will allow extra attributes to be added to the class, but will
fire a warning when it happens. You can access these warnings by setting a
warnings callback function:

.. code-block:: python

    from cinder import cinder_set_warnings_handler

    def log_cinder_warning(msg: str, *args: object) -> None:
        # ...

    cinder_set_warnings_handler(log_cinder_warning)

Typically you'd want to set a warnings handler that logs these warnings somewhere,
then you can deploy some new strict modules using `@loose_slots`,
and once the code has been in production for a bit and you see no warnings
fired, you can safely remove `@loose_slots`.

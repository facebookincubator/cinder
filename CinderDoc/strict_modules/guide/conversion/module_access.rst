Top-level Module Access
#######################

A common pattern is to import a module and access members from that module:

.. code-block:: python

    from useful import submodule

    class MyClass(submodule.BaseClass):
        pass

If “submodule” is not strict, then we don't know what it is and what side
effects could happen by dotting through it. So this pattern is disallowed
inside of a strict module when importing from a non-strict module. Instead
you can transform the code to:

.. code-block:: python

    from useful.submodule import BaseClass

    class MyClass(BaseClass):
        pass

This will cause any side effects that are possible to occur only when
the non-strict module is imported; the execution of the rest of the
strict module will be known to be side effect free.

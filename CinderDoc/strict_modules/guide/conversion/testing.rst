Testing
#######

You might be wondering how you're going to go and mock out functionality in
a strict module when we've already asserted that strict modules are immutable.
While we certainly don't want you to modify strict modules in production
they can be monkey patched in testing scenarios!

To enable patching strict modules during testing, you will need to customize
your strict loader (see :doc:`../quickstart`) by creating a subclass and
passing `enable_patching = True` before installing the loader.

.. code-block:: python

    from cinderx.compiler.strict.loader import StrictSourceFileLoader
    from typing import final

    @final
    class StrictSourceFileLoaderWithPatching(StrictSourceFileLoader):
        def __init__(self) -> None:
            # ...
            super().__init__(
                # ...
                enable_patching = True,
                # ...
            )

With patching enabled, you will be able to patch symbols in strict modules:

.. code-block:: python

    mystrictmodule.patch("name", new_value)

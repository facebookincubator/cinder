Modifying External State
########################

Strict modules enforces object :doc:`ownership </strict_modules/guide/limitations/ownership>`,
and will not allow module-level code to modify any object defined
in a different module.

One common example of this is to have a global registry of some sort of
objects:

**methods.py**

.. code-block:: python

    import __strict__

    ROUTES = list()

    def route(f):
        ROUTES.append(f)
        return f

**routes.py**

.. code-block:: python

    import __strict__

    from methods import route

    @route
    def create_user(*args):
        ...


Here we have one module which is maintaining a global registry, which is
populated as a side effect of importing another module.  If for some reason
one module doesn't get imported or if the order of imports changes then the
program's execution can change. When strict modules analyzes this code it will
report a :doc:`/strict_modules/guide/errors/modify_imported_value`.

A better pattern for this is to explicitly register the values in a central
location:

**methods.py**

.. code-block:: python

    import __strict__

    from routes import create_user

    ROUTES = [create_user, ...]

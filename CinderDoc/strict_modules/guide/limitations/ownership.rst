Ownership
#########

Strict modules are analyzed with a concept of ownership.  That is, every value
that is produced from strict modules is owned by one and only one strict
module, and only that strict module is capable of mutating it.  This
requirement ensures that modules are deterministic, and there isn't state is
the system which develops in an ad-hoc manner based upon how different modules
are imported.

For a value to be owned by a module it doesn't need to actually be created
directly within the module which owns it.  Rather the owning module is the
caller that ultimately causes the value to be created.  Consider for example
this code:

**a.py**

.. code-block:: python

    import __strict__

    def f():
        return {}

**b.py**

.. code-block:: python

    import __strict__

    from a import f


    x = f()
    x["name"] = "value"

This example is fine and will be permitted by strict modules; the owner of
the dictionary referred to by the variable `x` is module `b`.

There are other useful patterns of this sort of modification.  For example a
decorator can safely be applied in a module which will mutate the defining
function:

**methods.py**

.. code-block:: python

    import __strict__

    def POST(f):
        f.method = 'POST'
        return f

**routes.py**

.. code-block:: python

    import __strict__

    from methods import POST

    @POST
    def create_user(*args):
        ...


But it bans other patterns which you may be used to.  For example you cannot
use a decorator to create a registry of functions:

**methods.py**

.. code-block:: python

    import __strict__

    ROUTES = set()

    def route(f):
        ROUTES.add(f)
        return f

**routes.py**

.. code-block:: python

    import __strict__

    from methods import route

    @route
    def create_user(*args):
        ...


This will result in a ``StrictModuleModifyImportedValueException`` "<dict>
from module methods is modified by routes; this is prohibited."

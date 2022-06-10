Class / Instance Conflict
#########################


One of the changes that strict modules introduces is the promotion of instance
members to being class level declarations.  For more information on this pattern
see :doc:`../limitations/class_attrs`.

A typical case for this is when you'd like to have a default method implementation
but override it on a per instance basis:

.. code-block:: python

    class C:
        def f(self):
            return 42

    a = C()
    a.f = lambda: "I'm a special snowflake"


If you attempt this inside of a strict module you'll get an AttributeError that
says "'C' object attribute 'f' is read-only".  This is because the instance
doesn't have any place to store the method.  You might think that you can declare
the field explicitly as specified in the documentation:

.. code-block:: python

    class C:
        f: ...
        def f(self):
            return 42

But instead you'll get an error reported by strict modules stating that there's
a conflict with the variable.  To get around this issue you can promote the function
to always be treated as an instance member:

.. code-block:: python

    class C:
        def __init__(self):
            self.f = self.default_f

        def default_f(self):
            return 42

    a = C()
    a.f = lambda: "I'm a special snowflake" # Ok, you are a special snowflake

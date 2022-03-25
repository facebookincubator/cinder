Class Attributes
################

Strict modules transform the way attributes are handled in Python from being
diffused between types and instances to be entirely declared on the type.

For normal Python definitions when you define a class, the class will have a
dictionary, its sub-types will have dictionaries, and the instances will also
have dictionaries. When an attribute is looked up you typically have to look in
at least the class dictionary and the instance dictionary. In strict modules
we've removed the instance dictionary and replaced it with attributes that
are always defined at the class level. We've done this by leveraging a
standard Python feature - ``__slots__``.

There are a few different benefits from this transformation. The first is that
slots generally provide faster access to an attribute than instance
dictionaries - to access an instance field first the type's dictionary needs
to be checked, and then the instance dictionary can be checked. With this
transformation in place we only ever need to look at the type's dictionary
and we're done. We've also applied additional optimizations within Cinder to
further improve the performance of this layout.

Another benefit is that it uses less memory - with a fixed number of slots
Python knows exactly the size of the instance that it needs to allocate to
store all of its fields. With a dictionary it may quickly need to be resized
multiple times while allocating the object, and dictionaries have a load factor
where a percentage of slots are necessarily unused. The dictionary is also its
own object instead of storing the attributes directly on the instance.

And a final benefit is developer productivity. When you can arbitrarily attach
any attribute to an instance it's easy to make a mistake where you have a typo
on a member name and don't understand why it's not being updated. By not
allowing arbitrary fields to be assigned we turn this into an immediate and
obvious error.

Class Attributes in Detail
--------------------------

Now let's look look at some detailed examples. This first example shows what
you can do today in Python without a strict module to get the same benefits:

.. code-block:: python

    class C:
        __slots__ = ('myattr', )
        def __init__(self):
            self.myattr = None

    a = C()
    a.my_attr = 42  # AttributeError: 'C' object has no attribute 'my_attr'

In this case we've defined a class using Python's ``__slots__`` feature and
specified that the type has an attribute "myattr". Later we've assigned
to "my_attr" accidentally. Without the presence of ``__slots__`` the Python
runtime would have happily allowed this assignment and we might have spent
a lot of time debugging while myattr doesn't have the right value.

Strict modules handles this transformation to ``__slots__`` automatically and
will typically not require extra intervention on the behalf of the
programmer. If we put this code into a strict module all we have to do
is remove the ``__slots__`` entry and we get the exact same behavior:

.. code-block:: python

    import __strict__

    class C:
        def __init__(self):
            self.myattr = None

    a = C()
    a.my_attr = 42  # AttributeError: 'C' object has no attribute 'my_attr'

Strict modules will automatically populate the entries for ``__slots__`` based
upon the fields that are assigned in ``__init__``. If you have fields which you
don't want to eagerly populate you can also use Python's class level
annotations to indicate the presence of a field:

.. code-block:: python

    class C:
        myattr: int

    a = C()
    a.myattr = 42  # OK

We anticipate this shouldn't be much of an additional burden on developers
because these annotations are already used for providing typing information
to static analysis tools like Pyre.

But these changes do have some subtle impacts - for example this code is
now an error where it wasn't before:

.. code-block:: python

    class C:
        def __init__(self):
            self.f = 42

        def f(self):
            return 42

    # Strict Module error: Class member conflicts with instance member: f

The problem here is that there's now contention for storing two things in the
type - one is the method for "f", and the other is storing a descriptor (an
object which knows where to get or set the value in the instance) for the
instance attribute. It's not very often that users want to override a class
attribute with an instance one, but when it occurs you'll need to
resort to other techniques.  For information on how to handle this see
:doc:`../conversion/class_inst_conflict`.

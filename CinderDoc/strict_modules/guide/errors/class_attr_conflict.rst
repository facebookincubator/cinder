ClassAttributesConflictException
################################

  Class member conflicts with instance member: foo


Strict modules require that instance attributes are distinct from class level
attributes such as methods.

.. code-block:: python

    class C:
        def __init__(self, flag: bool):
            if flag:
                self.f = lambda: 42

        def f(self):
            return '42'


``ClassAttributesConflictException`` 'Class member conflicts with instance member: f'

In this example we are attempting to override a method defined on the class
with a unique per-instance method.  We cannot do this in a strict module
because the instance attribute is actually defined at the class level.


.. code-block:: python

    class C:
        value = None
        def __init__(self, flag: bool):
            if flag:
                self.value = 42



``ClassAttributesConflictException`` 'Class member conflicts with instance member: value'

In this example we're attempting to provide a fallback value that's declared
at the class level.

In both of these cases the solution is to define the value either
completely at the instance or class level.  For example we could change the
first example to always set `self.f` in the constructor, just sometimes setting
it to the default value.

For additional information see the section on
:doc:`/strict_modules/guide/limitations/class_attrs`.

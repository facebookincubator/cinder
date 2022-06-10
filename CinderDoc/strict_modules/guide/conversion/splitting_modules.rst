Splitting Modules
#################

Sometimes a module might contain functionality which is dependent upon certain
behavior which cannot be analyzed - either it truly has external side effects,
it is dependent upon another module which cannot yet be strictified and needs
to be used at the top-level, or it is dependent upon something which strict
modules have not yet been able to analyze.

In these cases one possible solution, although generally a last resort,
is to break the module into two modules. The first module will only contain
the code which cannot be safely strictified.  The second module will contain
all of the code that can be safely treated as strict.  A better way to do this
is to not have the unverifable code happen at startup, but if that's not
possible then splitting is an acceptable option.

Because strict modules can still import non-strict modules the strict module
can continue to expose the same interface as it previously did, and no other
code needs to be updated.  The only limitation to this is that it requires
that the module being strictified doesn't need to interact with the non-strict
elements at the top level.  For example classes could still create instances
of them, but the strict module couldn't call functions in the non-strict
module at the top level.


.. code-block:: python

    import csv
    from random import choice

    FAMOUS_PEOPLE = list(csv.reader(open('famous_people.txt').readlines()))

    class FamousPerson:
        def __init__(self, name, age, height):
                self.name = name
                self.age = int(age)
                self.height = float(height)

    def get_random_person():
        return FamousPerson(*choice(FAMOUS_PEOPLE))


We can split this into two modules, one which does the unverifable read of our
sample data from disk and another which returns the random piece of sample data:


.. code-block:: python

    import csv

    FAMOUS_PEOPLE = list(csv.reader(open('famous_people.txt').readlines()))


And we can have another module which exports our FamousPerson class along with
the API to return a random famous person:

.. code-block:: python

    from random import choice
    from famous_people_data import FAMOUS_PEOPLE

    class FamousPerson:
        def __init__(self, name, age, height):
                self.name = name
                self.age = int(age)
                self.height = float(height)

    def get_random_person():
        return FamousPerson(*choice(FAMOUS_PEOPLE))

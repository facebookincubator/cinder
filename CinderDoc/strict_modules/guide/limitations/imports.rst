Imports
#######

Import Mechanics
================

There is also some impact on how imports behave.  Because strict modules are
immutable, child packages cannot be set on them when the child package is
imported.  Given that this is also a common source of errors and can cause
issues with order of imports this is a good thing.

You might have some code which does a `from package import child` in one
spot, and elsewhere you might do `import package` and then try and access
`package.child`.  When `package` is a strict module this will fail because
`child` is not published.

Explicitly Imported Child Packages
----------------------------------

If you'd like to enable this programming model you can still explicitly
publish the child module on the parent package

**package/__init__.py**

.. code-block:: python

    import __strict__

    from package import child


**package/child.py**

.. code-block:: python

    import __strict__

    def foo(): pass

This pattern is also okay and the child package will be published on the
parent package.


Using Imports
=============

You also may need to be a little bit careful about how imported values are
used within a strict module.  In order to verify that a strict module has no
side effects you cannot interact with any values from non-strict modules at
the top-level of your module.  While it may typically be obvious when you
are interacting with non-strict values there's at least one case when its
less obvious.


**package/__init__.py**

.. code-block:: python


    # I'm not strict

**package/a.py**

.. code-block:: python

    import __strict__

    class C:
        pass

**strict_mod.py**

.. code-block:: python

    import __strict__

    from package import a

    class C(a.C):  # not safe
        pass

In this case using "a" at the top-level is not safe because the package
itself isn't strict.  Because the package isn't strict random code
could sneak in and replace "a" with a value which isn't the strict module.

This can easily be solved by marking the package as strict.

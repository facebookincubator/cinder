import copy
import gc
import inspect
import pickle
import sys
import unittest
import weakref

from test import support

try:
    import _testcapi
except ImportError:
    _testcapi = None


class CinderX_GeneratorTest(unittest.TestCase):
    def test_name(self):
        def func():
            yield 1

        # check generator names
        gen = func()
        self.assertEqual(gen.__name__, "func")
        self.assertEqual(gen.__qualname__, "CinderX_GeneratorTest.test_name.<locals>.func")

        # modify generator names
        gen.__name__ = "name"
        gen.__qualname__ = "qualname"
        self.assertEqual(gen.__name__, "name")
        self.assertEqual(gen.__qualname__, "qualname")

        # generator names must be a string and cannot be deleted
        self.assertRaises(TypeError, setattr, gen, "__name__", 123)
        self.assertRaises(TypeError, setattr, gen, "__qualname__", 123)
        self.assertRaises(TypeError, delattr, gen, "__name__")
        self.assertRaises(TypeError, delattr, gen, "__qualname__")

        # modify names of the function creating the generator
        func.__qualname__ = "func_qualname"
        func.__name__ = "func_name"
        gen = func()

        # CinderX: This is changed from the original test to be more flexible.
        # Cinder uses the name and qualname from the code object, which won't
        # change.
        self.assertIn(gen.__name__, ["func_name", "func"])
        self.assertIn(
            gen.__qualname__, ["func_qualname", "CinderX_GeneratorTest.test_name.<locals>.func"]
        )

        # unnamed generator
        gen = (x for x in range(10))
        self.assertEqual(gen.__name__, "<genexpr>")
        self.assertEqual(gen.__qualname__, "CinderX_GeneratorTest.test_name.<locals>.<genexpr>")

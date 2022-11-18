from itertools import permutations

import _static

from .common import StaticTestBase


class StaticArrayTests(StaticTestBase):
    def test_exists(self):
        self.assertTrue(
            hasattr(_static, "staticarray"),
            "staticarray must exist in the '_static' module",
        )

        self.assertEqual(repr(_static.staticarray), "<class 'staticarray'>")

    def test_initialization(self):
        my_array = _static.staticarray(5)
        # must be initialized to zeros
        self.assertEqual(repr(my_array), "staticarray[5]([0, 0, 0, 0, 0])")

    def test_set(self):
        my_array = _static.staticarray(3)

        my_array[1] = 4
        self.assertEqual(repr(my_array), "staticarray[3]([0, 4, 0])")

        my_array[-2] = 7
        self.assertEqual(repr(my_array), "staticarray[3]([0, 7, 0])")

        my_array[0] = 777
        self.assertEqual(repr(my_array), "staticarray[3]([777, 7, 0])")

        with self.assertRaises(IndexError):
            my_array[10] = 7

        with self.assertRaises(IndexError):
            my_array[-10] = 7

        with self.assertRaises(OverflowError):
            my_array[1] = 10**1000

        with self.assertRaisesRegex(
            TypeError, "'object' object cannot be interpreted as an integer"
        ):
            my_array[0] = object()

    def test_get(self):
        my_array = _static.staticarray(3)

        my_array[0] = 10
        my_array[1] = 9
        my_array[2] = 8

        self.assertEqual(my_array[0], 10)
        self.assertEqual(my_array[1], 9)
        self.assertEqual(my_array[-2], 9)
        self.assertEqual(my_array[-1], 8)

        with self.assertRaises(IndexError):
            my_array[10]

        with self.assertRaises(IndexError):
            my_array[-10]

    def test_len(self):
        my_array = _static.staticarray(3)
        self.assertEqual(len(my_array), 3)

        my_array = _static.staticarray(0)
        self.assertEqual(len(my_array), 0)

    def test_concat(self):
        a = _static.staticarray(3)
        a[0] = 0
        a[1] = 1
        a[2] = 2

        b = _static.staticarray(4)
        b[0] = 3
        b[1] = 4
        b[2] = 5
        b[3] = 6

        c = a + b
        self.assertEqual(list(c), list(range(7)))

    def test_repeat(self):
        a = _static.staticarray(3)
        a[0] = 0
        a[1] = 1
        a[2] = 2

        c = a * 3
        self.assertEqual(list(c), [0, 1, 2, 0, 1, 2, 0, 1, 2])

        b = _static.staticarray(1)
        b[0] = 888
        d = b * 4
        self.assertEqual(list(d), [888] * 4)

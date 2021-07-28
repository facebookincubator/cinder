from __static__ import chklist

from .common import StaticTestBase


class CheckedListTests(StaticTestBase):
    def test_checked_list(self):
        x = chklist[int]()
        x.append(1)
        self.assertEqual(repr(x), "[1]")
        self.assertEqual(chklist[int].__module__, "__static__")

    def test_checked_list_append_bad_type(self):
        x = chklist[int]()
        # No error
        x.append(42)
        self.assertEqual(x[0], 42)
        with self.assertRaisesRegex(TypeError, "int"):
            x.append("A")

    def test_checked_list_free_list(self):
        t1 = chklist[str]
        t2 = chklist[str]
        x = t1()
        x_id1 = id(x)
        del x
        x = t2()
        x_id2 = id(x)
        self.assertEqual(x_id1, x_id2)

    def test_checked_list_insert(self):
        x = chklist[int]()
        x.insert(0, 12)
        x.insert(0, 23)
        self.assertEqual(repr(x), "[23, 12]")
        with self.assertRaisesRegex(TypeError, "argument 2 expected int"):
            x.insert(1, "ASD")

    def test_checked_list_reversed(self):
        x = chklist[int]()
        x.append(12)
        x.append(23)
        y = x.__reversed__()
        # TODO(T96351329): This should be a generic chklist_reverseiterator[int].
        self.assertEqual(repr(type(y)), "<class 'list_reverseiterator'>")
        self.assertEqual(repr(list(y)), "[23, 12]")

    def test_checked_list_sizeof(self):
        x = chklist[int]()
        x.append(12)
        x.append(23)
        # Ensure that the list is a reasonable size, and that the `__sizeof__` call succeeds.
        self.assertGreater(x.__sizeof__(), 20)
        self.assertLess(x.__sizeof__(), 100)

    def test_checked_list_clear(self):
        x = chklist[int]()
        x.append(12)
        x.append(23)
        self.assertEqual(repr(x), "[12, 23]")
        x.clear()
        self.assertEqual(repr(x), "[]")
        self.assertEqual(str(type(x)), "<class '__static__.chklist[int]'>")

    def test_checked_list_copy(self):
        x = chklist[int]()
        x.append(12)
        x.append(23)
        self.assertEqual(repr(x), "[12, 23]")
        y = x.copy()
        y.append(34)
        self.assertEqual(repr(x), "[12, 23]")
        self.assertEqual(repr(y), "[12, 23, 34]")
        self.assertEqual(str(type(y)), "<class '__static__.chklist[int]'>")

        # Test that the copy is shallow.
        class C:
            x: int = 0

        clist = chklist[C]()
        clist.append(C())
        clist_copy = clist.copy()
        self.assertEqual(clist[0].x, 0)
        clist_copy[0].x = 1
        self.assertEqual(clist[0].x, 1)

    def test_checked_list_extend(self):
        x = chklist[int]()
        x.append(3)
        x.extend([1, 2])
        self.assertEqual(repr(x), "[3, 1, 2]")
        self.assertEqual(repr(type(x)), "<class '__static__.chklist[int]'>")

    def test_checked_list_extend_type_error(self):
        x = chklist[int]()
        x.append(3)
        with self.assertRaisesRegex(TypeError, r"bad value 'str' for chklist\[int\]"):
            x.extend([1, "hi"])
        # Ensure that we leave the chklist in its old state when extend fails.
        self.assertEqual(repr(x), "[3]")

    def test_checked_list_extend_with_non_list(self):
        x = chklist[int]()
        d = {1: 2, 2: 3}
        x.extend(d.keys())
        self.assertEqual(repr(x), "[1, 2]")
        d["a"] = 4
        with self.assertRaisesRegex(TypeError, r"bad value 'str' for chklist\[int\]"):
            x.extend(d.keys())
        self.assertEqual(repr(x), "[1, 2, 1, 2]")

    def test_checked_list_pop(self):
        x = chklist[int]()
        x.extend([1, 2, 3, 4])
        self.assertEqual(x.pop(), 4)
        self.assertEqual(x.pop(), 3)
        self.assertEqual(x.pop(), 2)
        self.assertEqual(x.pop(), 1)
        with self.assertRaises(IndexError):
            x.pop()

    def test_checked_list_remove(self):
        x = chklist[int]()
        x.extend([1, 2, 3, 1, 2, 3])
        x.remove(1)
        self.assertEqual(repr(x), "[2, 3, 1, 2, 3]")
        x.remove(1)
        self.assertEqual(repr(x), "[2, 3, 2, 3]")
        with self.assertRaises(ValueError):
            x.remove(1)
        self.assertEqual(repr(x), "[2, 3, 2, 3]")

    def test_checked_list_index(self):
        x = chklist[int]()
        x.extend([1, 2, 3, 1, 2, 3])
        self.assertEqual(x.index(1), 0)
        x.remove(1)
        self.assertEqual(x.index(1), 2)
        x.remove(1)
        with self.assertRaises(ValueError):
            x.index(1)

    def test_checked_list_count(self):
        x = chklist[int]()
        x.extend([1, 2, 3, 1, 2, 3])
        self.assertEqual(x.count(1), 2)
        x.remove(1)
        self.assertEqual(x.count(1), 1)

    def test_checked_list_reverse(self):
        x = chklist[int]()
        x.extend([1, 2, 3, 4])
        self.assertEqual(repr(x), "[1, 2, 3, 4]")
        self.assertEqual(x.reverse(), None)
        self.assertEqual(repr(type(x)), "<class '__static__.chklist[int]'>")
        self.assertEqual(repr(x), "[4, 3, 2, 1]")

    def test_checked_list_sort(self):
        x = chklist[int]()
        x.extend([3, 1, 2, 4])
        self.assertEqual(x.sort(), None)
        self.assertEqual(repr(type(x)), "<class '__static__.chklist[int]'>")
        self.assertEqual(repr(x), "[1, 2, 3, 4]")

    def test_checked_list_richcompare(self):
        x = chklist[int]()
        x.extend([1, 2, 3])
        self.assertEqual(x < x, False)
        self.assertEqual(x <= x, True)
        y = chklist[int]()
        y.extend([2, 2, 3])
        self.assertEqual(y <= x, False)
        self.assertEqual(x < y, True)

        # Compare chklists of different types.
        z = chklist[str]()
        z.extend(["a", "a"])
        with self.assertRaises(TypeError):
            x < z

        # Compare chklists to lists.
        self.assertEqual(x < [2, 2, 3], True)
        self.assertEqual([1, 2, 2] < x, True)

        # This is a bit weird, but consistent with our chkdict semantics.
        self.assertEqual(x == [1, 2, 3], True)

from __static__ import CheckedList
from _static import SEQ_CHECKED_LIST

from .common import StaticTestBase
from .tests import bad_ret_type, type_mismatch


class CheckedListTests(StaticTestBase):
    def test_checked_list(self):
        x = CheckedList[int]()
        x.append(1)
        self.assertEqual(repr(x), "[1]")
        self.assertEqual(CheckedList[int].__module__, "__static__")

    def test_checked_list_append_bad_type(self):
        x = CheckedList[int]()
        # No error
        x.append(42)
        self.assertEqual(x[0], 42)
        with self.assertRaisesRegex(TypeError, "int"):
            x.append("A")

    def test_checked_list_free_list(self):
        t1 = CheckedList[str]
        t2 = CheckedList[str]
        x = t1()
        x_id1 = id(x)
        del x
        x = t2()
        x_id2 = id(x)
        self.assertEqual(x_id1, x_id2)

    def test_checked_list_insert(self):
        x = CheckedList[int]()
        x.insert(0, 12)
        x.insert(0, 23)
        self.assertEqual(repr(x), "[23, 12]")
        with self.assertRaisesRegex(TypeError, "argument 2 expected int"):
            x.insert(1, "ASD")

    def test_checked_list_reversed(self):
        x = CheckedList[int]()
        x.append(12)
        x.append(23)
        y = x.__reversed__()
        # TODO(T96351329): This should be a generic CheckedList_reverseiterator[int].
        self.assertEqual(repr(type(y)), "<class 'list_reverseiterator'>")
        self.assertEqual(repr(list(y)), "[23, 12]")

    def test_checked_list_sizeof(self):
        x = CheckedList[int]()
        x.append(12)
        x.append(23)
        # Ensure that the list is a reasonable size, and that the `__sizeof__` call succeeds.
        self.assertGreater(x.__sizeof__(), 20)
        self.assertLess(x.__sizeof__(), 100)

    def test_checked_list_clear(self):
        x = CheckedList[int]()
        x.append(12)
        x.append(23)
        self.assertEqual(repr(x), "[12, 23]")
        x.clear()
        self.assertEqual(repr(x), "[]")
        self.assertEqual(str(type(x)), "<class '__static__.chklist[int]'>")

    def test_checked_list_copy(self):
        x = CheckedList[int]()
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

        clist = CheckedList[C]()
        clist.append(C())
        clist_copy = clist.copy()
        self.assertEqual(clist[0].x, 0)
        clist_copy[0].x = 1
        self.assertEqual(clist[0].x, 1)

    def test_checked_list_extend(self):
        x = CheckedList[int]()
        x.append(3)
        x.extend([1, 2])
        self.assertEqual(repr(x), "[3, 1, 2]")
        self.assertEqual(repr(type(x)), "<class '__static__.chklist[int]'>")

    def test_checked_list_extend_type_error(self):
        x = CheckedList[int]()
        x.append(3)
        with self.assertRaisesRegex(TypeError, r"bad value 'str' for chklist\[int\]"):
            x.extend([1, "hi"])
        # Ensure that we leave the CheckedList in its old state when extend fails.
        self.assertEqual(repr(x), "[3]")

    def test_checked_list_extend_with_non_list(self):
        x = CheckedList[int]()
        d = {1: 2, 2: 3}
        x.extend(d.keys())
        self.assertEqual(repr(x), "[1, 2]")
        d["a"] = 4
        with self.assertRaisesRegex(TypeError, r"bad value 'str' for chklist\[int\]"):
            x.extend(d.keys())
        self.assertEqual(repr(x), "[1, 2, 1, 2]")

    def test_checked_list_pop(self):
        x = CheckedList[int]()
        x.extend([1, 2, 3, 4])
        self.assertEqual(x.pop(), 4)
        self.assertEqual(x.pop(), 3)
        self.assertEqual(x.pop(), 2)
        self.assertEqual(x.pop(), 1)
        with self.assertRaises(IndexError):
            x.pop()

    def test_checked_list_remove(self):
        x = CheckedList[int]()
        x.extend([1, 2, 3, 1, 2, 3])
        x.remove(1)
        self.assertEqual(repr(x), "[2, 3, 1, 2, 3]")
        x.remove(1)
        self.assertEqual(repr(x), "[2, 3, 2, 3]")
        with self.assertRaises(ValueError):
            x.remove(1)
        self.assertEqual(repr(x), "[2, 3, 2, 3]")

    def test_checked_list_index(self):
        x = CheckedList[int]()
        x.extend([1, 2, 3, 1, 2, 3])
        self.assertEqual(x.index(1), 0)
        x.remove(1)
        self.assertEqual(x.index(1), 2)
        x.remove(1)
        with self.assertRaises(ValueError):
            x.index(1)

    def test_checked_list_count(self):
        x = CheckedList[int]()
        x.extend([1, 2, 3, 1, 2, 3])
        self.assertEqual(x.count(1), 2)
        x.remove(1)
        self.assertEqual(x.count(1), 1)

    def test_checked_list_reverse(self):
        x = CheckedList[int]()
        x.extend([1, 2, 3, 4])
        self.assertEqual(repr(x), "[1, 2, 3, 4]")
        self.assertEqual(x.reverse(), None)
        self.assertEqual(repr(type(x)), "<class '__static__.chklist[int]'>")
        self.assertEqual(repr(x), "[4, 3, 2, 1]")

    def test_checked_list_sort(self):
        x = CheckedList[int]()
        x.extend([3, 1, 2, 4])
        self.assertEqual(x.sort(), None)
        self.assertEqual(repr(type(x)), "<class '__static__.chklist[int]'>")
        self.assertEqual(repr(x), "[1, 2, 3, 4]")

    def test_checked_list_richcompare(self):
        x = CheckedList[int]()
        x.extend([1, 2, 3])
        self.assertEqual(x < x, False)
        self.assertEqual(x <= x, True)
        y = CheckedList[int]()
        y.extend([2, 2, 3])
        self.assertEqual(y <= x, False)
        self.assertEqual(x < y, True)

        # Compare CheckedLists of different types.
        z = CheckedList[str]()
        z.extend(["a", "a"])
        with self.assertRaises(TypeError):
            x < z

        # Compare CheckedLists to lists.
        self.assertEqual(x < [2, 2, 3], True)
        self.assertEqual([1, 2, 2] < x, True)

        # This is a bit weird, but consistent with our chkdict semantics.
        self.assertEqual(x == [1, 2, 3], True)

    def test_checked_list_assign_subscript(self):
        x = CheckedList[int]([1, 2, 3, 4])
        self.assertEqual(x[2], 3)
        x[2] = 2
        self.assertEqual(x[2], 2)
        with self.assertRaises(TypeError):
            x[2] = "A"
        self.assertEqual(x[2], 2)

    def test_checked_list_init(self):
        x = CheckedList[int]()
        self.assertEqual(repr(x), "[]")
        x = CheckedList[int]([1, 2, 3])
        self.assertEqual(repr(x), "[1, 2, 3]")
        self.assertEqual(repr(type(x)), "<class '__static__.chklist[int]'>")
        with self.assertRaises(TypeError):
            CheckedList[str]([1, 2, 3])

        x = CheckedList[int]({3: "a", 2: "b", 1: "c"})
        self.assertEqual(repr(x), "[3, 2, 1]")
        with self.assertRaisesRegex(
            TypeError, r"chklist\(\) takes no keyword arguments"
        ):
            CheckedList[int](iterable=[1, 2, 3])

        with self.assertRaisesRegex(
            TypeError, "chklist expected at most 1 argument, got 2"
        ):
            CheckedList[int]([], [])

    def test_checked_list_getitem_bad_return_type(self):
        codestr = """
            from __static__ import CheckedList
            def testfunc(x: CheckedList[int]) -> CheckedList[int]:
                return x[1]
        """
        self.type_error(codestr, bad_ret_type("int", "chklist[int]"))

    def test_checked_list_getitem_slice_bad_return_type(self):
        codestr = """
            from __static__ import CheckedList
            def testfunc(x: CheckedList[int]) -> int:
                return x[1:2]
        """
        self.type_error(codestr, bad_ret_type("chklist[int]", "int"))

    def test_checked_list_compile_getitem(self):
        codestr = """
            from __static__ import CheckedList
            def testfunc(x: CheckedList[int]) -> int:
                return x[1]
        """
        with self.in_module(codestr) as mod:
            f = mod["testfunc"]
            l = CheckedList[int]([1, 2, 3])
            self.assertEqual(f(l), 2)

        codestr = """
            from __static__ import CheckedList
            def testfunc(x: CheckedList[int]) -> CheckedList[int]:
                return x[1:2]
        """
        with self.in_module(codestr) as mod:
            f = mod["testfunc"]
            l = CheckedList[int]([1, 2, 3])
            self.assertEqual(f(l), [2])

    def test_checked_list_compile_setitem(self):
        codestr = """
            from __static__ import CheckedList
            def assign_to_index_1(x: CheckedList[int]) -> None:
                x[1] = 2
        """
        with self.in_module(codestr) as mod:
            f = mod["assign_to_index_1"]
            l = CheckedList[int]([1, 1, 1])
            self.assertEqual(f(l), None)
            self.assertEqual(repr(l), "[1, 2, 1]")

    def test_checked_list_compile_setitem_bad_type(self):
        codestr = """
            from __static__ import CheckedList
            def assign_to_index_1(x: CheckedList[int]) -> None:
                x[1] = "a"
        """
        self.type_error(codestr, type_mismatch("Exact[str]", "int"))

    def test_checked_list_compile_setitem_slice(self):
        codestr = """
            from __static__ import CheckedList
            def assign_to_slice(x: CheckedList[int]) -> None:
                x[1:3] = CheckedList[int]([2, 3])
         """
        with self.in_module(codestr) as mod:
            f = mod["assign_to_slice"]
            l = CheckedList[int]([1, 1, 1])
            self.assertEqual(f(l), None)
            self.assertEqual(repr(l), "[1, 2, 3]")

    def test_checked_list_compile_setitem_slice_list_bad_type(self):
        codestr = """
            from __static__ import CheckedList
            def assign_to_slice(x: CheckedList[int]) -> None:
                x[1:3] = [2, 3]
         """
        self.type_error(codestr, type_mismatch("Exact[list]", "chklist[int]"))

    def test_checked_list_compile_setitem_slice_list_bad_index_type(self):
        codestr = """
            from __static__ import CheckedList
            def assign_to_slice(x: CheckedList[int]) -> None:
                x["A"] = [2, 3]
         """
        self.type_error(codestr, type_mismatch("Exact[str]", "int"))

    def test_checked_list_compile_jumpif(self):
        codestr = """
            from __static__ import CheckedList
            def testfunc(x: CheckedList[int]) -> int:
                if x:
                    return 1
                return 2
        """
        with self.in_module(codestr) as mod:
            f = mod["testfunc"]
            l = CheckedList[int]([])
            self.assertEqual(f(l), 2)
            l.append(1)
            self.assertEqual(f(l), 1)

    def test_checked_list_compile_len(self):
        codestr = """
            from __static__ import CheckedList
            def testfunc(x: CheckedList[int]) -> int:
                return len(x)
        """
        with self.in_module(codestr) as mod:
            f = mod["testfunc"]
            l = CheckedList[int]([])
            self.assertEqual(f(l), 0)
            l.append(1)
            self.assertEqual(f(l), 1)

    def test_checked_list_getitem_with_c_ints(self):
        codestr = """
            from __static__ import CheckedList, int64, unbox
            from typing import List
            def testfunc(x: CheckedList[int]) -> int:
                i: int64 = 0
                j: int64 = 1
                return x[i] + x[j]
        """
        with self.in_module(codestr) as mod:
            f = mod["testfunc"]
            cl = CheckedList[int]([1, 42, 3, 4, 5, 6])
            self.assertInBytecode(f, "SEQUENCE_GET", SEQ_CHECKED_LIST)
            self.assertEqual(f(cl), 43)

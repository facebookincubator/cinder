from __static__ import Array, int32, int64, int8

import itertools
import re
from compiler.static.types import FAST_LEN_ARRAY, SEQ_ARRAY_INT64, TypedSyntaxError
from copy import deepcopy
from typing import Mapping
from unittest import skipIf

from _static import SEQ_SUBSCR_UNCHECKED, TYPED_INT64

from .common import StaticTestBase, type_mismatch


try:
    import cinderjit
except ImportError:
    cinderjit = None


@skipIf(cinderjit is not None, "not supported under JIT yet")
class ArrayTests(StaticTestBase):
    def test_array_import(self):
        codestr = """
            from __static__ import int64, Array

            def test() -> Array[int64]:
                x: Array[int64] = Array[int64](1)
                x[0] = 1
                return x
        """

        with self.in_module(codestr) as mod:
            self.assertEqual(list(mod.test()), [1])

    def test_array_create(self):
        codestr = """
            from __static__ import int64, Array

            def test() -> Array[int64]:
                x: Array[int64] = Array[int64](3)
                return x
        """

        with self.in_module(codestr) as mod:
            test = mod.test
            self.assertNotInBytecode(test, "TP_ALLOC")
            # Elements must be initialized to zeros
            self.assertEqual(list(test()), [0, 0, 0])

    def test_array_create_failure(self):
        codestr = """
            from __static__ import Array

            class C: pass

            def test() -> Array[C]:
                return Array[C](10)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Invalid Array element type: foo.C"
        ):
            self.compile(codestr, modname="foo")

    def test_array_call_unbound(self):
        codestr = """
            from __static__ import Array

            def f() -> Array:
                return Array(10)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"create instances of a generic Type\[Array\[T\]\]",
        ):
            self.compile(codestr, modname="foo")

    def test_array_slice_load_unsupported(self):
        codestr = """
            from __static__ import Array, int64

            def m():
                a = Array[int64](6)
                a[1:3]
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"Static arrays cannot be sliced",
        ):
            self.compile(codestr, modname="foo")

    def test_array_slice_store_unsupported(self):
        codestr = """
            from __static__ import Array, int64

            def m():
                a = Array[int64](6)
                a[1:3] = [1, 2]
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"Static arrays cannot be sliced",
        ):
            self.compile(codestr, modname="foo")

    def test_array_slice_load_unsupported_nonstatic(self):
        v = Array[int64](10)
        with self.assertRaisesRegex(
            TypeError, "sequence index must be integer, not 'slice'"
        ):
            v[1:4]

    def test_array_slice_store_unsupported_nonstatic(self):
        v = Array[int64](10)
        with self.assertRaisesRegex(
            TypeError, "sequence index must be integer, not 'slice'"
        ):
            v[1:4] = [1, 2, 3]

    def test_array_len(self):
        codestr = """
            from __static__ import int64, char, double, Array

            def y():
                return len(Array[int64](5))
        """
        y = self.find_code(self.compile(codestr, modname="foo"), name="y")
        self.assertInBytecode(y, "FAST_LEN", FAST_LEN_ARRAY)
        with self.in_module(codestr) as mod:
            y = mod.y
            self.assertEqual(y(), 5)

    def test_array_isinstance(self):
        x = Array[int64](0)
        self.assertTrue(isinstance(x, Array[int64]))
        self.assertTrue(issubclass(Array[int64], Array[int64]))

    def test_array_weird_type_construction(self):
        self.assertIs(
            Array[int64],
            Array[
                int64,
            ],
        )

    def test_array_not_subclassable(self):

        with self.assertRaisesRegex(
            TypeError, "type 'staticarray' is not an acceptable base type"
        ):

            class C(Array[int64]):
                pass

        with self.assertRaisesRegex(
            TypeError, "type 'staticarray' is not an acceptable base type"
        ):

            class C(Array):
                pass

    def test_array_enum(self):
        codestr = """
            from __static__ import Array, clen, int64, box

            def f(x: Array[int64]):
                i: int64 = 0
                j: int64 = 0
                while i < clen(x):
                    j = j + x[i]
                    i+=1
                return box(j)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            a = Array[int64](4)
            a[0] = 1
            a[1] = 2
            a[2] = 3
            a[3] = 4
            self.assertEqual(f(a), 10)
            with self.assertRaises(TypeError):
                f(None)

    def test_optional_array_enum(self):
        codestr = """
            from __static__ import Array, clen, int64, box
            from typing import Optional

            def f(x: Optional[Array[int64]]):
                if x is None:
                    return 42

                i: int64 = 0
                j: int64 = 0
                while i < clen(x):
                    j += x[i]
                    i+=1
                return box(j)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            a = Array[int64](4)
            a[0] = 1
            a[1] = 2
            a[2] = 3
            a[3] = 4
            self.assertEqual(f(a), 10)
            self.assertEqual(f(None), 42)

    def test_array_get_primitive_idx(self):
        codestr = """
            from __static__ import Array, int64, box

            def m() -> int:
                n: int = 121
                content = list(range(n))
                a = Array[int64](n)
                for i, c in enumerate(content):
                    a[i] = int64(c)
                return box(a[int64(111)])
        """
        c = self.compile(codestr, modname="foo.py")
        m = self.find_code(c, "m")
        self.assertInBytecode(m, "PRIMITIVE_LOAD_CONST", (111, TYPED_INT64))
        self.assertInBytecode(m, "SEQUENCE_GET", SEQ_ARRAY_INT64)
        with self.in_module(codestr) as mod:
            m = mod.m
            actual = m()
            self.assertEqual(actual, 111)

    def test_array_get_nonprimitive_idx(self):
        codestr = """
            from __static__ import Array, int64, box

            def m() -> int:
                n: int = 121
                content = list(range(n))
                a = Array[int64](n)
                for i, c in enumerate(content):
                    a[i] = int64(c)

                return box(a[111])
        """
        c = self.compile(codestr, modname="foo.py")
        m = self.find_code(c, "m")
        self.assertInBytecode(m, "LOAD_CONST", 111)
        self.assertNotInBytecode(m, "PRIMITIVE_LOAD_CONST")
        self.assertInBytecode(m, "PRIMITIVE_UNBOX")
        self.assertInBytecode(m, "SEQUENCE_GET", SEQ_ARRAY_INT64)
        with self.in_module(codestr) as mod:
            m = mod.m
            actual = m()
            self.assertEqual(actual, 111)

    def test_array_get_dynamic_idx(self):
        codestr = """
            from __static__ import Array, int64, box

            def x():
                return 33

            def m() -> int:
                n: int = 121
                content = list(range(n))
                a = Array[int64](n)
                for i, c in enumerate(content):
                    a[i] = int64(c)
                return box(a[x()])
        """
        with self.in_module(codestr) as mod:
            m = mod.m
            actual = m()
            self.assertEqual(actual, 33)

    def test_array_get_failure(self):
        codestr = """
            from __static__ import Array, int64, box

            def m() -> int:
                a = Array[int64](3)
                return box(a[20])
        """
        with self.in_module(codestr) as mod:
            m = mod.m
            with self.assertRaisesRegex(IndexError, "index out of range"):
                m()

    def test_array_get_negative_idx(self):
        codestr = """
            from __static__ import Array, int64, box

            def m() -> int:
                content = [1, 3, -5]
                a = Array[int64](3)
                for i, c in enumerate(content):
                    a[i] = int64(c)
                return box(a[-1])
        """
        with self.in_module(codestr) as mod:
            m = mod.m
            self.assertEqual(m(), -5)

    # Note: This testcase actually ensures that we don't crash when the JIT is enabled and
    # such an error occurs
    def test_array_call_typecheck(self):
        codestr = """
            from __static__ import Array, int64
            def h(x: Array[int64]) -> int64:
                return x[0]
        """
        error_msg = re.escape("h expected 'staticarray' for argument x, got 'list'")
        with self.in_module(codestr) as mod:
            with self.assertRaisesRegex(TypeError, error_msg):
                mod.h(["B"])

    # Note: This testcase actually ensures that we don't crash when the JIT is enabled and
    # such an error occurs
    def test_array_call_typecheck_double(self):
        codestr = """
            from __static__ import Array, int64, double, box
            def h(x: Array[int64]) -> double:
                return double(float(box(x[0])))
        """
        error_msg = re.escape("h expected 'staticarray' for argument x, got 'list'")
        with self.in_module(codestr) as mod:
            with self.assertRaisesRegex(TypeError, error_msg):
                mod.h(["B"])

    def test_array_set_signed(self):
        signs = ["-", ""]
        value = 77

        for sign in signs:
            codestr = f"""
                from __static__ import Array, int64

                def m() -> Array[int64]:
                    a = Array[int64](3)
                    a[0] = 1
                    a[1] = 3
                    a[2] = -5

                    a[1] = {sign}{value}
                    return a
            """
            with self.subTest(type=type, sign=sign):
                val = -value if sign else value
                c = self.compile(codestr, modname="foo.py")
                m = self.find_code(c, "m")
                self.assertInBytecode(m, "PRIMITIVE_LOAD_CONST", (val, TYPED_INT64))
                self.assertInBytecode(m, "LOAD_CONST", 1)
                self.assertInBytecode(m, "SEQUENCE_SET", SEQ_ARRAY_INT64)
                with self.in_module(codestr) as mod:
                    m = mod.m
                    if sign:
                        expected = -value
                    else:
                        expected = value
                    result = m()
                    self.assertEqual(
                        list(result),
                        [1, expected, -5],
                        f"Failing case: {type}, {sign}",
                    )

    def test_array_set_negative_idx(self):
        codestr = """
            from __static__ import Array, int64

            def m() -> Array[int64]:
                a = Array[int64](3)
                a[0] = 1
                a[1] = 3
                a[2] = -5

                a[-2] = 7
                return a
        """
        c = self.compile(codestr, modname="foo.py")
        m = self.find_code(c, "m")
        self.assertInBytecode(m, "PRIMITIVE_LOAD_CONST", (7, TYPED_INT64))
        self.assertInBytecode(m, "LOAD_CONST", -2)
        self.assertInBytecode(m, "SEQUENCE_SET", SEQ_ARRAY_INT64)
        with self.in_module(codestr) as mod:
            m = mod.m
            self.assertEqual(list(m()), [1, 7, -5])

    def test_array_set_failure(self) -> object:
        codestr = """
            from __static__ import Array, int64

            def m() -> Array[int64]:
                a = Array[int64](3)
                a[-100] = 7
                return a
        """
        c = self.compile(codestr, modname="foo.py")
        m = self.find_code(c, "m")
        self.assertInBytecode(m, "PRIMITIVE_LOAD_CONST", (7, TYPED_INT64))
        self.assertInBytecode(m, "SEQUENCE_SET", SEQ_ARRAY_INT64)
        with self.in_module(codestr) as mod:
            m = mod.m
            with self.assertRaisesRegex(IndexError, "index out of range"):
                m()

    def test_array_set_failure_invalid_subscript(self):
        codestr = """
            from __static__ import Array, int64

            def x():
                return object()

            def m() -> Array[int64]:
                a = Array[int64](6)
                a[x()] = 7
                return a
        """
        c = self.compile(codestr, modname="foo.py")
        m = self.find_code(c, "m")
        self.assertInBytecode(m, "PRIMITIVE_LOAD_CONST", (7, TYPED_INT64))
        with self.in_module(codestr) as mod:
            m = mod.m
            with self.assertRaisesRegex(
                TypeError, "sequence index must be integer, not 'object'"
            ):
                m()

    def test_array_set_success_dynamic_subscript(self):
        codestr = """
            from __static__ import Array, int64

            def x():
                return 1

            def m() -> Array[int64]:
                a = Array[int64](3)
                a[x()] = 37
                return a
        """
        c = self.compile(codestr, modname="foo.py")
        m = self.find_code(c, "m")
        self.assertInBytecode(m, "PRIMITIVE_LOAD_CONST", (37, TYPED_INT64))
        with self.in_module(codestr) as mod:
            m = mod.m
            r = m()
            self.assertEqual(list(r), [0, 37, 0])

    def test_array_set_success_dynamic_subscript_2(self):
        codestr = """
            from __static__ import Array, int64

            def x():
                return 1

            def m() -> Array[int64]:
                a = Array[int64](3)
                v: int64 = 37
                a[x()] = v
                return a
        """
        c = self.compile(codestr, modname="foo.py")
        m = self.find_code(c, "m")
        self.assertInBytecode(m, "PRIMITIVE_LOAD_CONST", (37, TYPED_INT64))
        with self.in_module(codestr) as mod:
            m = mod.m
            r = m()
            self.assertEqual(list(r), [0, 37, 0])

    def test_fast_forloop(self):
        codestr = """
            from __static__ import Array, int64

            def f() -> int64:
                a: Array[int64] = Array[int64](3)

                a[0] = 1
                a[1] = 2
                a[2] = 3

                sum: int64 = 0
                for el in a:
                    sum += el
                return sum
        """
        with self.in_module(codestr) as mod:
            self.assertInBytecode(
                mod.f, "SEQUENCE_GET", SEQ_ARRAY_INT64 | SEQ_SUBSCR_UNCHECKED
            )
            self.assertEqual(mod.f(), 6)

    def test_no_forloop_unpack_fails(self):
        codestr = """
            from __static__ import Array, int64

            def f() -> int64:
                a: Array[int64] = Array[int64](3)

                a[0] = 1
                a[1] = 2
                a[2] = 3

                sum: int64 = 0
                for el, b in a:
                    sum += el
                return sum
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"cannot unpack multiple values from Array\[int64] while iterating",
        ):
            self.compile(codestr, modname="foo.py")

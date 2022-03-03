from __static__ import (
    Array,
    int8,
    int32,
    int64,
)

import itertools
import re
from array import array
from compiler.static.types import (
    TypedSyntaxError,
    FAST_LEN_ARRAY,
    SEQ_ARRAY_INT8,
    SEQ_ARRAY_INT16,
    SEQ_ARRAY_INT32,
    SEQ_ARRAY_INT64,
    SEQ_ARRAY_UINT8,
    SEQ_ARRAY_UINT16,
    SEQ_ARRAY_UINT32,
    SEQ_ARRAY_UINT64,
    SEQ_SUBSCR_UNCHECKED,
    TYPED_INT8,
)
from copy import deepcopy
from typing import Mapping

from _static import (
    TYPED_INT8,
    TYPED_INT16,
    TYPED_INT32,
    TYPED_INT64,
    TYPED_UINT8,
    TYPED_UINT16,
    TYPED_UINT32,
    TYPED_UINT64,
)

from .common import StaticTestBase, type_mismatch


prim_name_to_type: Mapping[str, int] = {
    "int8": TYPED_INT8,
    "int16": TYPED_INT16,
    "int32": TYPED_INT32,
    "int64": TYPED_INT64,
    "uint8": TYPED_UINT8,
    "uint16": TYPED_UINT16,
    "uint32": TYPED_UINT32,
    "uint64": TYPED_UINT64,
}


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
            test = mod.test
            self.assertEqual(test(), array("L", [1]))

    def test_array_create(self):
        codestr = """
            from __static__ import int64, Array

            def test() -> Array[int64]:
                x: Array[int64] = Array[int64]([1, 3, 5])
                return x
        """

        with self.in_module(codestr) as mod:
            test = mod.test
            self.assertEqual(test(), array("l", [1, 3, 5]))

    def test_array_create_failure(self):
        # todo - in the future we're going to support this, but for now fail it.
        codestr = """
            from __static__ import int64, Array

            class C: pass

            def test() -> Array[C]:
                return Array[C]([1, 3, 5])
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Invalid Array element type: foo.C"
        ):
            self.compile(codestr, modname="foo")

    def test_array_call_unbound(self):
        codestr = """
            from __static__ import Array

            def f() -> Array:
                return Array([1, 2, 3])
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"create instances of a generic Type\[Array\[T\]\]",
        ):
            self.compile(codestr, modname="foo")

    def test_array_assign_wrong_type(self):
        codestr = """
            from __static__ import int64, char, Array

            def test() -> None:
                x: Array[int64] = Array[char]([48])
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            type_mismatch(
                "Array[char]",
                "Array[int64]",
            ),
        ):
            self.compile(codestr, modname="foo")

    def test_array_types(self):
        codestr = """
            from __static__ import (
                int8,
                int16,
                int32,
                int64,
                uint8,
                uint16,
                uint32,
                uint64,
                char,
                double,
                Array
            )
            from typing import Tuple

            def test() -> Tuple[Array[int64], Array[char], Array[double]]:
                x1: Array[int8] = Array[int8]([1, 3, -5])
                x2: Array[uint8] = Array[uint8]([1, 3, 5])
                x3: Array[int16] = Array[int16]([1, -3, 5])
                x4: Array[uint16] = Array[uint16]([1, 3, 5])
                x5: Array[int32] = Array[int32]([1, 3, 5])
                x6: Array[uint32] = Array[uint32]([1, 3, 5])
                x7: Array[int64] = Array[int64]([1, 3, 5])
                x8: Array[uint64] = Array[uint64]([1, 3, 5])
                x9: Array[char] = Array[char]([ord('a')])
                x10: Array[double] = Array[double]([1.1, 3.3, 5.5])
                # TODO(T92687901): Support Array[single] for array("f", ...).
                arrays = [
                    x1,
                    x2,
                    x3,
                    x4,
                    x5,
                    x6,
                    x7,
                    x8,
                    x9,
                    x10,
                ]
                first_elements = []
                for ar in arrays:
                    first_elements.append(ar[0])
                return (arrays, first_elements)
        """

        with self.in_module(codestr) as mod:
            test = mod.test
            arrays, first_elements = test()
            exp_arrays = [
                array(*args)
                for args in [
                    ("h", [1, 3, -5]),
                    ("H", [1, 3, 5]),
                    ("i", [1, -3, 5]),
                    ("I", [1, 3, 5]),
                    ("l", [1, 3, 5]),
                    ("L", [1, 3, 5]),
                    ("q", [1, 3, 5]),
                    ("Q", [1, 3, 5]),
                    ("b", [ord("a")]),
                    ("d", [1.1, 3.3, 5.5]),
                    ("f", [1.1, 3.3, 5.5]),
                ]
            ]
            exp_first_elements = [ar[0] for ar in exp_arrays]
            for result, expectation in zip(arrays, exp_arrays):
                self.assertEqual(result, expectation)
            for result, expectation in zip(first_elements, exp_first_elements):
                self.assertEqual(result, expectation)

    def test_array_subscripting_slice(self):
        codestr = """
            from __static__ import Array, int8

            def m() -> Array[int8]:
                a = Array[int8]([1, 3, -5, -1, 7, 22])
                return a[1:3]
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.m(), Array[int8]([3, -5]))

    def test_array_slice(self):
        v = Array[int64]([1, 2, 3, 4])
        self.assertEqual(v[1:3], Array[int64]([2, 3]))
        self.assertEqual(type(v[1:2]), Array[int64])

    def test_array_deepcopy(self):
        v = Array[int64]([1, 2, 3, 4])
        self.assertEqual(v, deepcopy(v))
        self.assertIsNot(v, deepcopy(v))
        self.assertEqual(type(v), type(deepcopy(v)))

    def test_array_len(self):
        codestr = """
            from __static__ import int64, char, double, Array
            from array import array

            def y():
                return len(Array[int64]([1, 3, 5]))
        """
        y = self.find_code(self.compile(codestr, modname="foo"), name="y")
        self.assertInBytecode(y, "FAST_LEN", FAST_LEN_ARRAY)
        with self.in_module(codestr) as mod:
            y = mod.y
            self.assertEqual(y(), 3)

    def test_array_isinstance(self):
        x = Array[int64](0)
        self.assertTrue(isinstance(x, Array[int64]))
        self.assertFalse(isinstance(x, Array[int32]))
        self.assertTrue(issubclass(Array[int64], Array[int64]))
        self.assertFalse(issubclass(Array[int64], Array[int32]))

    def test_array_weird_type_construction(self):
        self.assertIs(
            Array[int64],
            Array[
                int64,
            ],
        )

    def test_array_not_subclassable(self):

        with self.assertRaises(TypeError):

            class C(Array[int64]):
                pass

        with self.assertRaises(TypeError):

            class C(Array):
                pass

    def test_array_enum(self):
        codestr = """
            from __static__ import Array, clen, int64, box

            def f(x: Array[int64]):
                i: int64 = 0
                j: int64 = 0
                while i < clen(x):
                    j += x[i]
                    i+=1
                return box(j)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            a = Array[int64]([1, 2, 3, 4])
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
            a = Array[int64]([1, 2, 3, 4])
            self.assertEqual(f(a), 10)
            self.assertEqual(f(None), 42)

    def test_array_get_primitive_idx(self):
        codestr = """
            from __static__ import Array, int8, box

            def m() -> int:
                content = list(range(121))
                a = Array[int8](content)
                return box(a[int8(111)])
        """
        c = self.compile(codestr, modname="foo.py")
        m = self.find_code(c, "m")
        self.assertInBytecode(m, "PRIMITIVE_LOAD_CONST", (111, TYPED_INT8))
        self.assertInBytecode(m, "SEQUENCE_GET", SEQ_ARRAY_INT8)
        with self.in_module(codestr) as mod:
            m = mod.m
            actual = m()
            self.assertEqual(actual, 111)

    def test_array_get_nonprimitive_idx(self):
        codestr = """
            from __static__ import Array, int8, box

            def m() -> int:
                content = list(range(121))
                a = Array[int8](content)
                return box(a[111])
        """
        c = self.compile(codestr, modname="foo.py")
        m = self.find_code(c, "m")
        self.assertInBytecode(m, "LOAD_CONST", 111)
        self.assertNotInBytecode(m, "PRIMITIVE_LOAD_CONST")
        self.assertInBytecode(m, "PRIMITIVE_UNBOX")
        self.assertInBytecode(m, "SEQUENCE_GET", SEQ_ARRAY_INT8)
        with self.in_module(codestr) as mod:
            m = mod.m
            actual = m()
            self.assertEqual(actual, 111)

    def test_array_get_dynamic_idx(self):
        codestr = """
            from __static__ import Array, int8, box

            def x():
                return 33

            def m() -> int:
                content = list(range(121))
                a = Array[int8](content)
                return box(a[x()])
        """
        with self.in_module(codestr) as mod:
            m = mod.m
            actual = m()
            self.assertEqual(actual, 33)

    def test_array_get_failure(self):
        codestr = """
            from __static__ import Array, int8, box

            def m() -> int:
                a = Array[int8]([1, 3, -5])
                return box(a[20])
        """
        with self.in_module(codestr) as mod:
            m = mod.m
            with self.assertRaisesRegex(IndexError, "index out of range"):
                m()

    def test_array_get_negative_idx(self):
        codestr = """
            from __static__ import Array, int8, box

            def m() -> int:
                a = Array[int8]([1, 3, -5])
                return box(a[-1])
        """
        with self.in_module(codestr) as mod:
            m = mod.m
            self.assertEqual(m(), -5)

    def test_array_call_typecheck(self):
        codestr = """
            from __static__ import Array, int32
            def h(x: Array[int32]) -> int32:
                return x[0]
        """
        error_msg = re.escape("h expected 'Array[int32]' for argument x, got 'list'")
        with self.in_module(codestr) as mod:
            with self.assertRaisesRegex(TypeError, error_msg):
                mod.h(["B"])

    def test_array_call_typecheck_double(self):
        codestr = """
            from __static__ import Array, int32, double, box
            def h(x: Array[int32]) -> double:
                return double(float(box(x[0])))
        """
        error_msg = re.escape("h expected 'Array[int32]' for argument x, got 'list'")
        with self.in_module(codestr) as mod:
            with self.assertRaisesRegex(TypeError, error_msg):
                mod.h(["B"])

    def test_array_set_signed(self):
        int_types = [
            "int8",
            "int16",
            "int32",
            "int64",
        ]
        seq_types = {
            "int8": SEQ_ARRAY_INT8,
            "int16": SEQ_ARRAY_INT16,
            "int32": SEQ_ARRAY_INT32,
            "int64": SEQ_ARRAY_INT64,
        }
        signs = ["-", ""]
        value = 77

        for type, sign in itertools.product(int_types, signs):
            codestr = f"""
                from __static__ import Array, {type}

                def m() -> Array[{type}]:
                    a = Array[{type}]([1, 3, -5])
                    a[1] = {sign}{value}
                    return a
            """
            with self.subTest(type=type, sign=sign):
                val = -value if sign else value
                c = self.compile(codestr, modname="foo.py")
                m = self.find_code(c, "m")
                self.assertInBytecode(
                    m, "PRIMITIVE_LOAD_CONST", (val, prim_name_to_type[type])
                )
                self.assertInBytecode(m, "LOAD_CONST", 1)
                self.assertInBytecode(m, "SEQUENCE_SET", seq_types[type])
                with self.in_module(codestr) as mod:
                    m = mod.m
                    if sign:
                        expected = -value
                    else:
                        expected = value
                    result = m()
                    self.assertEqual(
                        result,
                        array("q", [1, expected, -5]),
                        f"Failing case: {type}, {sign}",
                    )

    def test_array_set_unsigned(self):
        uint_types = [
            "uint8",
            "uint16",
            "uint32",
            "uint64",
        ]
        value = 77
        seq_types = {
            "uint8": SEQ_ARRAY_UINT8,
            "uint16": SEQ_ARRAY_UINT16,
            "uint32": SEQ_ARRAY_UINT32,
            "uint64": SEQ_ARRAY_UINT64,
        }
        for type in uint_types:
            codestr = f"""
                from __static__ import Array, {type}

                def m() -> Array[{type}]:
                    a = Array[{type}]([1, 3, 5])
                    a[1] = {value}
                    return a
            """
            with self.subTest(type=type):
                c = self.compile(codestr, modname="foo.py")
                m = self.find_code(c, "m")
                self.assertInBytecode(
                    m, "PRIMITIVE_LOAD_CONST", (value, prim_name_to_type[type])
                )
                self.assertInBytecode(m, "LOAD_CONST", 1)
                self.assertInBytecode(m, "SEQUENCE_SET", seq_types[type])
                with self.in_module(codestr) as mod:
                    m = mod.m
                    expected = value
                    result = m()
                    self.assertEqual(
                        result, array("q", [1, expected, 5]), f"Failing case: {type}"
                    )

    def test_array_set_negative_idx(self):
        codestr = """
            from __static__ import Array, int8

            def m() -> Array[int8]:
                a = Array[int8]([1, 3, -5])
                a[-2] = 7
                return a
        """
        c = self.compile(codestr, modname="foo.py")
        m = self.find_code(c, "m")
        self.assertInBytecode(m, "PRIMITIVE_LOAD_CONST", (7, TYPED_INT8))
        self.assertInBytecode(m, "LOAD_CONST", -2)
        self.assertInBytecode(m, "SEQUENCE_SET", SEQ_ARRAY_INT8)
        with self.in_module(codestr) as mod:
            m = mod.m
            self.assertEqual(m(), array("h", [1, 7, -5]))

    def test_array_set_failure(self) -> object:
        codestr = """
            from __static__ import Array, int8

            def m() -> Array[int8]:
                a = Array[int8]([1, 3, -5])
                a[-100] = 7
                return a
        """
        c = self.compile(codestr, modname="foo.py")
        m = self.find_code(c, "m")
        self.assertInBytecode(m, "PRIMITIVE_LOAD_CONST", (7, TYPED_INT8))
        self.assertInBytecode(m, "SEQUENCE_SET", SEQ_ARRAY_INT8)
        with self.in_module(codestr) as mod:
            m = mod.m
            with self.assertRaisesRegex(IndexError, "index out of range"):
                m()

    def test_array_set_failure_invalid_subscript(self):
        codestr = """
            from __static__ import Array, int8

            def x():
                return object()

            def m() -> Array[int8]:
                a = Array[int8]([1, 3, -5])
                a[x()] = 7
                return a
        """
        c = self.compile(codestr, modname="foo.py")
        m = self.find_code(c, "m")
        self.assertInBytecode(m, "PRIMITIVE_LOAD_CONST", (7, TYPED_INT8))
        with self.in_module(codestr) as mod:
            m = mod.m
            with self.assertRaisesRegex(TypeError, "array indices must be integers"):
                m()

    def test_array_set_success_dynamic_subscript(self):
        codestr = """
            from __static__ import Array, int8

            def x():
                return 1

            def m() -> Array[int8]:
                a = Array[int8]([1, 3, -5])
                a[x()] = 37
                return a
        """
        c = self.compile(codestr, modname="foo.py")
        m = self.find_code(c, "m")
        self.assertInBytecode(m, "PRIMITIVE_LOAD_CONST", (37, TYPED_INT8))
        with self.in_module(codestr) as mod:
            m = mod.m
            r = m()
            self.assertEqual(r, array("b", [1, 37, -5]))

    def test_array_set_success_dynamic_subscript_2(self):
        codestr = """
            from __static__ import Array, int8

            def x():
                return 1

            def m() -> Array[int8]:
                a = Array[int8]([1, 3, -5])
                v: int8 = 37
                a[x()] = v
                return a
        """
        c = self.compile(codestr, modname="foo.py")
        m = self.find_code(c, "m")
        self.assertInBytecode(m, "PRIMITIVE_LOAD_CONST", (37, TYPED_INT8))
        with self.in_module(codestr) as mod:
            m = mod.m
            r = m()
            self.assertEqual(r, array("b", [1, 37, -5]))

    def test_fast_forloop(self):
        codestr = """
            from __static__ import Array, int8

            def f() -> int8:
                a: Array[int8] = Array[int8]([1, 2, 3])
                sum: int8 = 0
                for el in a:
                    sum += el
                return sum
        """
        with self.in_module(codestr) as mod:
            self.assertInBytecode(
                mod.f, "SEQUENCE_GET", SEQ_ARRAY_INT8 | SEQ_SUBSCR_UNCHECKED
            )
            self.assertEqual(mod.f(), 6)

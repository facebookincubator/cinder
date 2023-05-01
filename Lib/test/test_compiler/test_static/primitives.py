from __static__ import TYPED_INT64

import gc
import itertools
import re
import string
import sys
import warnings
import weakref

from array import array
from compiler.errors import TypedSyntaxError
from compiler.static import StaticCodeGenerator
from compiler.static.types import (
    FAST_LEN_INEXACT,
    FAST_LEN_LIST,
    PRIM_OP_ADD_INT,
    PRIM_OP_GT_INT,
    PRIM_OP_LT_INT,
    SEQ_LIST,
    SEQ_LIST_INEXACT,
    TypedSyntaxError,
    TypeEnvironment,
)
from types import FunctionType

from unittest import skip, skipIf

import _static

from _static import TYPED_INT16, TYPED_INT32, TYPED_INT64

from .common import bad_ret_type, PRIM_NAME_TO_TYPE, StaticTestBase, type_mismatch

try:
    import cinderjit
except ImportError:
    cinderjit = None


class PrimitivesTests(StaticTestBase):
    def test_primitive_context_ifexp(self) -> None:
        codestr = """
            from __static__ import int64

            def f(x: int | None = None) -> int64:
                return int64(x) if x is not None else 0
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 0)
            self.assertEqual(f(1), 1)

    def test_no_useless_convert(self) -> None:
        codestr = """
            from __static__ import int64

            def f(x: int64) -> int64:
                return x + 1
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "CONVERT_PRIMITIVE")
            self.assertEqual(f(2), 3)

    def test_not(self) -> None:
        for typ, vals in [
            ("cbool", {True: False, False: True}),
            ("int64", {5: False, 0: True, -27: False}),
            ("uint8", {1: False, 0: True}),
        ]:
            codestr = f"""
                from __static__ import cbool, int64, uint8

                def f(x: {typ}) -> cbool:
                    return not x
            """
            with self.in_module(codestr) as mod:
                self.assertInBytecode(mod.f, "PRIMITIVE_UNARY_OP")
                for k, v in vals.items():
                    with self.subTest(typ=typ, val=k, res=v):
                        self.assertEqual(mod.f(k), v)

    def test_uninit_int(self):
        codestr = """
            from __static__ import int64, box

            def f():
                x0: int64
                return box(x0)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", ((0.0, TYPED_INT64)))
            self.assertEqual(f(), 0.0)

    def test_primitive_types_final(self):
        type_env = TypeEnvironment()
        PRIMITIVE_TYPES = type_env.all_cint_types + [
            type_env.cbool,
            type_env.char,
            type_env.double,
        ]
        PRIMITIVE_NAMES = [klass.instance_name for klass in PRIMITIVE_TYPES]
        for name in PRIMITIVE_NAMES:
            codestr = f"""
                from __static__ import {name}

                class C({name}): pass
            """
            with self.subTest(klass=name):
                self.type_error(
                    codestr,
                    f"Primitive type {name} cannot be subclassed: ",
                    at="class C",
                )

    def test_typed_slots_primitives(self):
        slot_types = [
            # signed
            (
                ("__static__", "byte", "#"),
                0,
                1,
                [(1 << 7) - 1, -(1 << 7)],
                [1 << 8],
                ["abc"],
            ),
            (
                ("__static__", "int8", "#"),
                0,
                1,
                [(1 << 7) - 1, -(1 << 7)],
                [1 << 8],
                ["abc"],
            ),
            (
                ("__static__", "int16", "#"),
                0,
                2,
                [(1 << 15) - 1, -(1 << 15)],
                [1 << 15, -(1 << 15) - 1],
                ["abc"],
            ),
            (
                ("__static__", "int32", "#"),
                0,
                4,
                [(1 << 31) - 1, -(1 << 31)],
                [1 << 31, -(1 << 31) - 1],
                ["abc"],
            ),
            (
                ("__static__", "int64", "#"),
                0,
                8,
                [(1 << 63) - 1, -(1 << 63)],
                [],
                [1 << 63],
            ),
            # unsigned
            (
                ("__static__", "uint8", "#"),
                0,
                1,
                [(1 << 8) - 1, 0],
                [1 << 8, -1],
                ["abc"],
            ),
            (
                ("__static__", "uint16", "#"),
                0,
                2,
                [(1 << 16) - 1, 0],
                [1 << 16, -1],
                ["abc"],
            ),
            (
                ("__static__", "uint32", "#"),
                0,
                4,
                [(1 << 32) - 1, 0],
                [1 << 32, -1],
                ["abc"],
            ),
            (("__static__", "uint64", "#"), 0, 8, [(1 << 64) - 1, 0], [], [1 << 64]),
            # pointer
            (
                ("__static__", "ssize_t", "#"),
                0,
                self.ptr_size,
                [1, sys.maxsize, -sys.maxsize - 1],
                [],
                [sys.maxsize + 1, -sys.maxsize - 2],
            ),
            # floating point
            (("__static__", "single", "#"), 0.0, 4, [1.0], [], ["abc"]),
            (("__static__", "double", "#"), 0.0, 8, [1.0], [], ["abc"]),
            # misc
            (("__static__", "char", "#"), "\x00", 1, ["a"], [], ["abc"]),
            (("__static__", "cbool", "#"), False, 1, [True], [], ["abc", 1]),
        ]

        target_size = (
            self.base_size + self.ptr_size
        )  # all of our data types round up to a minimum pointer sized object
        for type_spec, default, size, test_vals, warn_vals, err_vals in slot_types:
            with self.subTest(
                type_spec=type_spec,
                default=default,
                size=size,
                test_vals=test_vals,
                warn_vals=warn_vals,
                err_vals=err_vals,
            ):

                # Since object sizes are aligned to 8 bytes, figure out how
                # many slots of each type we need to get to 8 bytes.
                self.assertEqual(8 % size, 0)
                num_slots = 8 // size

                C = self.build_static_type(
                    tuple(f"a{i}" for i in range(num_slots)),
                    {f"a{i}": type_spec for i in range(num_slots)},
                )
                a = C()
                self.assertEqual(sys.getsizeof(a), target_size, type_spec)
                self.assertEqual(a.a0, default)
                self.assertEqual(type(a.a0), type(default))
                for val in test_vals:
                    a.a0 = val
                    self.assertEqual(a.a0, val)

                with warnings.catch_warnings():
                    warnings.simplefilter("error", category=RuntimeWarning)
                    for val in warn_vals:
                        with self.assertRaises(RuntimeWarning):
                            a.a0 = val

                for val in err_vals:
                    with self.assertRaises((TypeError, OverflowError)):
                        a.a0 = val

    def test_int_bad_assign(self):
        with self.assertRaisesRegex(TypedSyntaxError, type_mismatch("str", "int64")):
            self.compile(
                """
            from __static__ import ssize_t
            def f():
                x: ssize_t = 'abc'
            """,
                StaticCodeGenerator,
            )

    def test_sign_extend(self):
        codestr = f"""
            from __static__ import int16, int64, box
            def testfunc():
                x: int16 = -40
                y: int64 = x
                return box(y)
            """
        f = self.run_code(codestr)["testfunc"]
        self.assertEqual(f(), -40)

    def test_field_size(self):
        for type in [
            "int8",
            "int16",
            "int32",
            "int64",
            "uint8",
            "uint16",
            "uint32",
            "uint64",
        ]:
            codestr = f"""
                from __static__ import {type}, box
                class C{type}:
                    def __init__(self):
                        self.a: {type} = 1
                        self.b: {type} = 1

                def testfunc(c: C{type}):
                    c.a = 2
                    c.b = 3
                    return box(c.a + c.b)
                """
            with self.subTest(type=type):
                with self.in_module(codestr) as mod:
                    C = getattr(mod, "C" + type)
                    f = mod.testfunc
                    self.assertEqual(f(C()), 5)

    def test_field_sign_ext(self):
        """tests that we do the correct sign extension when loading from a field"""
        for type, val in [
            ("int32", 65537),
            ("int16", 256),
            ("int8", 0x7F),
            ("uint32", 65537),
        ]:
            codestr = f"""
                from __static__ import {type}, box
                class C{type}:
                    def __init__(self):
                        self.value: {type} = {val}

                def testfunc(c: C{type}):
                    return box(c.value)
                """
            with self.subTest(type=type, val=val):
                with self.in_module(codestr) as mod:
                    C = getattr(mod, "C" + type)
                    f = mod.testfunc
                    self.assertEqual(f(C()), val)

    def test_field_unsign_ext(self):
        """tests that we do the correct sign extension when loading from a field"""
        for type, val, test in [("uint32", 65537, -1)]:
            codestr = f"""
                from __static__ import {type}, int64, box
                class C{type}:
                    def __init__(self):
                        self.value: {type} = {val}

                def testfunc(c: C{type}):
                    z: int64 = {test}
                    if c.value < z:
                        return True
                    return False
                """
            with self.subTest(type=type, val=val, test=test):
                with self.in_module(codestr) as mod:
                    C = getattr(mod, "C" + type)
                    f = mod.testfunc
                    self.assertEqual(f(C()), False)

    def test_field_sign_compare(self):
        for type, val, test in [("int32", -1, -1)]:
            codestr = f"""
                from __static__ import {type}, box
                class C{type}:
                    def __init__(self):
                        self.value: {type} = {val}

                def testfunc(c: C{type}):
                    if c.value == {test}:
                        return True
                    return False
                """
            with self.subTest(type=type, val=val, test=test):
                with self.in_module(codestr) as mod:
                    C = getattr(mod, "C" + type)
                    f = mod.testfunc
                    self.assertTrue(f(C()))

    def test_field_verifies_type(self):
        codestr = """
        from __static__ import int64

        class C:
            def __init__(self):
                self.x: int64 = 1

        def f():
            return [C().x]

        """
        self.type_error(codestr, "type mismatch: int64 cannot be assigned to dynamic")

    def test_clen_verifies_type(self):
        codestr = """
        from __static__ import int64, clen

        def f():
            return [clen([1])]

        """
        self.type_error(codestr, "type mismatch: int64 cannot be assigned to dynamic")

    def test_or_verifies_type(self):
        codestr = """
        from __static__ import cbool

        def f():
            x: cbool = False
            y: cbool = True
            return [x or y]

        """
        self.type_error(codestr, "type mismatch: cbool cannot be assigned to dynamic")

    def test_ifexp_verifies_type(self):
        codestr = """
        from __static__ import int64, clen, cbool

        def f(c):
            x: int64 = 1
            y: int64 = 2
            return [x if c else y]

        """
        self.type_error(codestr, "type mismatch: int64 cannot be assigned to dynamic")

    def test_mixed_cmpop_sign(self):

        """mixed signed/unsigned ops should be promoted to signed"""
        codestr = """
            from __static__ import int8, uint8, box
            def testfunc(tst=False):
                x: uint8 = 42
                y: int8 = 2
                if tst:
                    x += 1
                    y += 1

                if x < y:
                    return True
                return False
        """
        code = self.compile(codestr)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_COMPARE_OP", PRIM_OP_LT_INT)
        with self.in_module(codestr) as mod:
            f = mod.testfunc
            self.assertEqual(f(), False)

        codestr = """
            from __static__ import int8, uint8, box
            def testfunc(tst=False):
                x: int8 = 42
                y: uint8 = 2
                if tst:
                    x += 1
                    y += 1

                if x < y:
                    return True
                return False
        """
        code = self.compile(codestr)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_COMPARE_OP", PRIM_OP_LT_INT)
        with self.in_module(codestr) as mod:
            f = mod.testfunc
            self.assertEqual(f(), False)

    def test_store_signed_to_unsigned(self):
        codestr = """
            from __static__ import int8, uint8, uint64, box
            def testfunc(tst=False):
                x: uint8 = 42
                y: int8 = 2
                x = y
        """
        with self.assertRaisesRegex(TypedSyntaxError, type_mismatch("int8", "uint8")):
            self.compile(codestr)

    def test_store_unsigned_to_signed(self):
        """promote int/uint to int, can't add to uint64"""

        codestr = """
            from __static__ import int8, uint8, uint64, box
            def testfunc(tst=False):
                x: uint8 = 42
                y: int8 = 2
                y = x
        """
        with self.assertRaisesRegex(TypedSyntaxError, type_mismatch("uint8", "int8")):
            self.compile(codestr)

    def test_mixed_assign_larger(self):
        """promote int/uint to int16"""

        codestr = """
            from __static__ import int8, uint8, int16, box
            def testfunc(tst=False):
                x: uint8 = 42
                y: int8 = 2
                z: int16 = x + y

                return box(z)
        """
        code = self.compile(codestr)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        with self.in_module(codestr) as mod:
            f = mod.testfunc
            self.assertEqual(f(), 44)

    def test_mixed_assign_larger_2(self):
        """promote int/uint to int16"""

        codestr = """
            from __static__ import int8, uint8, int16, box
            def testfunc(tst=False):
                x: uint8 = 42
                y: int8 = 2
                z: int16
                z = x + y

                return box(z)
        """
        code = self.compile(codestr)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        with self.in_module(codestr) as mod:
            f = mod.testfunc
            self.assertEqual(f(), 44)

    @skipIf(True, "this isn't implemented yet")
    def test_unwind(self):
        codestr = f"""
            from __static__ import int32
            def raises():
                raise IndexError()

            def testfunc():
                x: int32 = 1
                raises()
                print(x)
            """

        with self.in_module(codestr) as mod:
            f = mod.testfunc
            with self.assertRaises(IndexError):
                f()

    def test_int_constant_range(self):
        for type, val, low, high in [
            ("int8", 128, -128, 127),
            ("int8", -129, -128, 127),
            ("int16", 32768, -32768, 32767),
            ("int16", -32769, -32768, 32767),
            ("int32", 2147483648, -2147483648, 2147483647),
            ("int32", -2147483649, -2147483648, 2147483647),
            ("int64", 9223372036854775808, -9223372036854775808, 9223372036854775807),
            ("int64", -9223372036854775809, -9223372036854775808, 9223372036854775807),
            ("uint8", 257, 0, 255),
            ("uint8", -1, 0, 255),
            ("uint16", 65537, 0, 65535),
            ("uint16", -1, 0, 65535),
            ("uint32", 4294967297, 0, 4294967295),
            ("uint32", -1, 0, 4294967295),
            ("uint64", 18446744073709551617, 0, 18446744073709551615),
            ("uint64", -1, 0, 18446744073709551615),
        ]:
            codestr = f"""
                from __static__ import {type}
                def testfunc(tst):
                    x: {type} = {val}
            """
            with self.subTest(type=type, val=val, low=low, high=high):
                with self.assertRaisesRegex(
                    TypedSyntaxError,
                    f"type mismatch: Literal\\[{val}\\] cannot be assigned to {type}",
                ):
                    self.compile(codestr)

    def test_int_assign_float(self):
        codestr = """
            from __static__ import int8
            def testfunc(tst):
                x: int8 = 1.0
        """
        with self.assertRaisesRegex(TypedSyntaxError, type_mismatch("float", "int")):
            self.compile(codestr)

    def test_int_assign_str_constant(self):
        codestr = """
            from __static__ import int8
            def testfunc(tst):
                x: int8 = 'abc' + 'def'
        """
        with self.assertRaisesRegex(TypedSyntaxError, type_mismatch("str", "int8")):
            self.compile(codestr)

    def test_int_large_int_constant(self):
        codestr = """
            from __static__ import int64
            def testfunc(tst):
                x: int64 = 0x7FFFFFFF + 1
        """
        code = self.compile(codestr)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", (0x80000000, TYPED_INT64))

    def test_int_int_constant(self):
        codestr = """
            from __static__ import int64
            def testfunc(tst):
                x: int64 = 0x7FFFFFFE + 1
        """
        code = self.compile(codestr)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", (0x7FFFFFFF, TYPED_INT64))

    def test_int_add_mixed_64(self):
        codestr = """
            from __static__ import uint64, int64, box
            def testfunc(tst):
                x: uint64 = 0
                y: int64 = 1
                if tst:
                    x = x + 1
                    y = y + 2

                return box(x + y)
        """
        with self.assertRaisesRegex(TypedSyntaxError, "cannot add uint64 and int64"):
            self.compile(codestr)

    def test_int_overflow_add(self):
        tests = [
            ("int8", 100, 100, -56),
            ("int16", 200, 200, 400),
            ("int32", 200, 200, 400),
            ("int64", 200, 200, 400),
            ("int16", 20000, 20000, -25536),
            ("int32", 40000, 40000, 80000),
            ("int64", 40000, 40000, 80000),
            ("int32", 2000000000, 2000000000, -294967296),
            ("int64", 2000000000, 2000000000, 4000000000),
            ("int8", 127, 127, -2),
            ("int16", 32767, 32767, -2),
            ("int32", 2147483647, 2147483647, -2),
            ("int64", 9223372036854775807, 9223372036854775807, -2),
            ("uint8", 200, 200, 144),
            ("uint16", 200, 200, 400),
            ("uint32", 200, 200, 400),
            ("uint64", 200, 200, 400),
            ("uint16", 40000, 40000, 14464),
            ("uint32", 40000, 40000, 80000),
            ("uint64", 40000, 40000, 80000),
            ("uint32", 2000000000, 2000000000, 4000000000),
            ("uint64", 2000000000, 2000000000, 4000000000),
            ("uint8", 1 << 7, 1 << 7, 0),
            ("uint16", 1 << 15, 1 << 15, 0),
            ("uint32", 1 << 31, 1 << 31, 0),
            ("uint64", 1 << 63, 1 << 63, 0),
            ("uint8", 1 << 6, 1 << 6, 128),
            ("uint16", 1 << 14, 1 << 14, 32768),
            ("uint32", 1 << 30, 1 << 30, 2147483648),
            ("uint64", 1 << 62, 1 << 62, 9223372036854775808),
        ]

        for type, x, y, res in tests:
            codestr = f"""
            from __static__ import {type}, box
            def f():
                x: {type} = {x}
                y: {type} = {y}
                z: {type} = x + y
                return box(z)
            """
            with self.subTest(type=type, x=x, y=y, res=res):
                f = self.run_code(codestr)["f"]
                self.assertEqual(f(), res, f"{type} {x} {y} {res}")

    def test_int_unary(self):
        tests = [
            ("int8", "-", 1, -1),
            ("uint8", "-", 1, (1 << 8) - 1),
            ("int16", "-", 1, -1),
            ("int16", "-", 256, -256),
            ("uint16", "-", 1, (1 << 16) - 1),
            ("int32", "-", 1, -1),
            ("int32", "-", 65536, -65536),
            ("uint32", "-", 1, (1 << 32) - 1),
            ("int64", "-", 1, -1),
            ("int64", "-", 1 << 32, -(1 << 32)),
            ("uint64", "-", 1, (1 << 64) - 1),
            ("int8", "~", 1, -2),
            ("uint8", "~", 1, (1 << 8) - 2),
            ("int16", "~", 1, -2),
            ("uint16", "~", 1, (1 << 16) - 2),
            ("int32", "~", 1, -2),
            ("uint32", "~", 1, (1 << 32) - 2),
            ("int64", "~", 1, -2),
            ("uint64", "~", 1, (1 << 64) - 2),
        ]
        for type, op, x, res in tests:
            codestr = f"""
            from __static__ import {type}, box
            def testfunc(tst):
                x: {type} = {x}
                if tst:
                    x = x + 1
                x = {op}x
                return box(x)
            """
            with self.subTest(type=type, op=op, x=x, res=res):
                f = self.run_code(codestr)["testfunc"]
                self.assertEqual(f(False), res, f"{type} {op} {x} {res}")

    def test_int_compare(self):
        tests = [
            ("int8", 1, 2, "==", False),
            ("int8", 1, 2, "!=", True),
            ("int8", 1, 2, "<", True),
            ("int8", 1, 2, "<=", True),
            ("int8", 2, 1, "<", False),
            ("int8", 2, 1, "<=", False),
            ("int8", -1, 2, "==", False),
            ("int8", -1, 2, "!=", True),
            ("int8", -1, 2, "<", True),
            ("int8", -1, 2, "<=", True),
            ("int8", 2, -1, "<", False),
            ("int8", 2, -1, "<=", False),
            ("uint8", 1, 2, "==", False),
            ("uint8", 1, 2, "!=", True),
            ("uint8", 1, 2, "<", True),
            ("uint8", 1, 2, "<=", True),
            ("uint8", 2, 1, "<", False),
            ("uint8", 2, 1, "<=", False),
            ("uint8", 255, 2, "==", False),
            ("uint8", 255, 2, "!=", True),
            ("uint8", 255, 2, "<", False),
            ("uint8", 255, 2, "<=", False),
            ("uint8", 2, 255, "<", True),
            ("uint8", 2, 255, "<=", True),
            ("int16", 1, 2, "==", False),
            ("int16", 1, 2, "!=", True),
            ("int16", 1, 2, "<", True),
            ("int16", 1, 2, "<=", True),
            ("int16", 2, 1, "<", False),
            ("int16", 2, 1, "<=", False),
            ("int16", -1, 2, "==", False),
            ("int16", -1, 2, "!=", True),
            ("int16", -1, 2, "<", True),
            ("int16", -1, 2, "<=", True),
            ("int16", 2, -1, "<", False),
            ("int16", 2, -1, "<=", False),
            ("uint16", 1, 2, "==", False),
            ("uint16", 1, 2, "!=", True),
            ("uint16", 1, 2, "<", True),
            ("uint16", 1, 2, "<=", True),
            ("uint16", 2, 1, "<", False),
            ("uint16", 2, 1, "<=", False),
            ("uint16", 65535, 2, "==", False),
            ("uint16", 65535, 2, "!=", True),
            ("uint16", 65535, 2, "<", False),
            ("uint16", 65535, 2, "<=", False),
            ("uint16", 2, 65535, "<", True),
            ("uint16", 2, 65535, "<=", True),
            ("int32", 1, 2, "==", False),
            ("int32", 1, 2, "!=", True),
            ("int32", 1, 2, "<", True),
            ("int32", 1, 2, "<=", True),
            ("int32", 2, 1, "<", False),
            ("int32", 2, 1, "<=", False),
            ("int32", -1, 2, "==", False),
            ("int32", -1, 2, "!=", True),
            ("int32", -1, 2, "<", True),
            ("int32", -1, 2, "<=", True),
            ("int32", 2, -1, "<", False),
            ("int32", 2, -1, "<=", False),
            ("uint32", 1, 2, "==", False),
            ("uint32", 1, 2, "!=", True),
            ("uint32", 1, 2, "<", True),
            ("uint32", 1, 2, "<=", True),
            ("uint32", 2, 1, "<", False),
            ("uint32", 2, 1, "<=", False),
            ("uint32", 4294967295, 2, "!=", True),
            ("uint32", 4294967295, 2, "<", False),
            ("uint32", 4294967295, 2, "<=", False),
            ("uint32", 2, 4294967295, "<", True),
            ("uint32", 2, 4294967295, "<=", True),
            ("int64", 1, 2, "==", False),
            ("int64", 1, 2, "!=", True),
            ("int64", 1, 2, "<", True),
            ("int64", 1, 2, "<=", True),
            ("int64", 2, 1, "<", False),
            ("int64", 2, 1, "<=", False),
            ("int64", -1, 2, "==", False),
            ("int64", -1, 2, "!=", True),
            ("int64", -1, 2, "<", True),
            ("int64", -1, 2, "<=", True),
            ("int64", 2, -1, "<", False),
            ("int64", 2, -1, "<=", False),
            ("uint64", 1, 2, "==", False),
            ("uint64", 1, 2, "!=", True),
            ("uint64", 1, 2, "<", True),
            ("uint64", 1, 2, "<=", True),
            ("uint64", 2, 1, "<", False),
            ("uint64", 2, 1, "<=", False),
            ("int64", 2, -1, ">", True),
            ("uint64", 2, 18446744073709551615, ">", False),
            ("int64", 2, -1, "<", False),
            ("uint64", 2, 18446744073709551615, "<", True),
            ("int64", 2, -1, ">=", True),
            ("uint64", 2, 18446744073709551615, ">=", False),
            ("int64", 2, -1, "<=", False),
            ("uint64", 2, 18446744073709551615, "<=", True),
        ]
        for type, x, y, op, res in tests:
            codestr = f"""
            from __static__ import {type}, box
            def testfunc(tst):
                x: {type} = {x}
                y: {type} = {y}
                if tst:
                    x = x + 1
                    y = y + 2

                if x {op} y:
                    return True
                return False
            """
            with self.subTest(type=type, x=x, y=y, op=op, res=res):
                f = self.run_code(codestr)["testfunc"]
                self.assertEqual(f(False), res, f"{type} {x} {op} {y} {res}")

    def test_int_compare_unboxed(self):
        codestr = f"""
        from __static__ import ssize_t, unbox
        def testfunc(x, y):
            x1: ssize_t = unbox(x)
            y1: ssize_t = unbox(y)

            if x1 > y1:
                return True
            return False
        """
        f = self.run_code(codestr)["testfunc"]
        self.assertInBytecode(f, "POP_JUMP_IF_ZERO")
        self.assertEqual(f(1, 2), False)

    def test_int_compare_mixed(self):
        codestr = """
        from __static__ import box, ssize_t
        x = 1

        def testfunc():
            i: ssize_t = 0
            j = 0
            while box(i < 100) and x:
                i = i + 1
                j = j + 1
            return j
        """
        f = self.run_code(codestr)["testfunc"]
        self.assertEqual(f(), 100)
        self.assert_jitted(f)

    def test_int_unbox_from_call(self):
        codestr = f"""
        from __static__ import int64
        def foo() -> int:
            return 1234

        def testfunc() -> int64:
            return int64(foo())
        """
        with self.in_module(codestr) as mod:
            f = mod.testfunc
            self.assertEqual(f(), 1234)

    def test_int_compare_or(self):
        codestr = """
        from __static__ import box, ssize_t

        def testfunc():
            i: ssize_t = 0
            j = i > 2 or i < -2
            return box(j)
        """

        with self.in_module(codestr) as mod:
            f = mod.testfunc
            self.assertInBytecode(f, "JUMP_IF_NONZERO_OR_POP")
            self.assertIs(f(), False)

    def test_int_compare_and(self):
        codestr = """
        from __static__ import box, ssize_t

        def testfunc():
            i: ssize_t = 0
            j = i > 2 and i > 3
            return box(j)
        """

        with self.in_module(codestr) as mod:
            f = mod.testfunc
            self.assertInBytecode(f, "JUMP_IF_ZERO_OR_POP")
            self.assertIs(f(), False)

    def test_disallow_prim_nonprim_union(self):
        codestr = """
            from __static__ import int32

            def f(y: int):
                x: int32 = 2
                z = x or y
                return z
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"invalid union type Union\[int32, int\]; unions cannot include primitive types",
        ):
            self.compile(codestr)

    def test_int_compare_mixed_sign(self):
        tests = [
            ("uint16", 10000, "int16", -1, "<", False),
            ("uint16", 10000, "int16", -1, "<=", False),
            ("int16", -1, "uint16", 10000, ">", False),
            ("int16", -1, "uint16", 10000, ">=", False),
            ("uint32", 10000, "int16", -1, "<", False),
        ]
        for type1, x, type2, y, op, res in tests:
            codestr = f"""
            from __static__ import {type1}, {type2}, box
            def testfunc(tst):
                x: {type1} = {x}
                y: {type2} = {y}
                if tst:
                    x = x + 1
                    y = y + 2

                if x {op} y:
                    return True
                return False
            """
            with self.subTest(type1=type1, x=x, type2=type2, y=y, op=op, res=res):
                f = self.run_code(codestr)["testfunc"]
                self.assertEqual(f(False), res, f"{type} {x} {op} {y} {res}")

    def test_int_compare64_mixed_sign(self):
        codestr = """
            from __static__ import uint64, int64
            def testfunc(tst):
                x: uint64 = 0
                y: int64 = 1
                if tst:
                    x = x + 1
                    y = y + 2

                if x < y:
                    return True
                return False
        """
        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr)

    def test_compile_method(self):
        code = self.compile(
            """
            from __static__ import ssize_t
            def f():
                x: ssize_t = 42
            """
        )

        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", (42, TYPED_INT64))

    def test_mixed_compare(self):
        codestr = """
        from __static__ import ssize_t, box, unbox
        def f(a):
            x: ssize_t = 0
            while x != a:
                pass
        """
        with self.assertRaisesRegex(TypedSyntaxError, "can't compare int64 to dynamic"):
            self.compile(codestr)

    def test_unbox(self):
        for size, val in [
            ("int8", 126),
            ("int8", -128),
            ("int16", 32766),
            ("int16", -32768),
            ("int32", 2147483646),
            ("int32", -2147483648),
            ("int64", 9223372036854775806),
            ("int64", -9223372036854775808),
            ("uint8", 254),
            ("uint16", 65534),
            ("uint32", 4294967294),
            ("uint64", 18446744073709551614),
        ]:
            codestr = f"""
            from __static__ import {size}, box, unbox
            def f(x):
                y: {size} = unbox(x)
                y = y + 1
                return box(y)
            """

            code = self.compile(codestr)
            f = self.find_code(code)
            f = self.run_code(codestr)["f"]
            self.assertEqual(f(val), val + 1)

    def test_int_loop_inplace(self):
        codestr = """
        from __static__ import ssize_t, box
        def f():
            i: ssize_t = 0
            while i < 100:
                i += 1
            return box(i)
        """

        code = self.compile(codestr)
        f = self.find_code(code)
        f = self.run_code(codestr)["f"]
        self.assertEqual(f(), 100)

    def test_int_loop(self):
        codestr = """
        from __static__ import ssize_t, box
        def testfunc():
            i: ssize_t = 0
            while i < 100:
                i = i + 1
            return box(i)
        """

        code = self.compile(codestr)
        f = self.find_code(code)

        f = self.run_code(codestr)["testfunc"]
        self.assertEqual(f(), 100)

        self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", (0, TYPED_INT64))
        self.assertInBytecode(f, "LOAD_LOCAL", (0, ("__static__", "int64", "#")))
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        self.assertInBytecode(f, "PRIMITIVE_COMPARE_OP", PRIM_OP_LT_INT)
        self.assertInBytecode(f, "POP_JUMP_IF_ZERO")

    def test_int_assert(self):
        codestr = """
        from __static__ import ssize_t, box
        def testfunc():
            i: ssize_t = 0
            assert i == 0, "hello there"
        """

        code = self.compile(codestr)
        f = self.find_code(code)
        self.assertInBytecode(f, "POP_JUMP_IF_NONZERO")

        f = self.run_code(codestr)["testfunc"]
        self.assertEqual(f(), None)

    def test_int_assert_raises(self):
        codestr = """
        from __static__ import ssize_t, box
        def testfunc():
            i: ssize_t = 0
            assert i != 0, "hello there"
        """

        code = self.compile(codestr)
        f = self.find_code(code)
        self.assertInBytecode(f, "POP_JUMP_IF_NONZERO")

        with self.assertRaises(AssertionError):
            f = self.run_code(codestr)["testfunc"]
            self.assertEqual(f(), None)

    def test_int_loop_reversed(self):
        codestr = """
        from __static__ import ssize_t, box
        def testfunc():
            i: ssize_t = 0
            while 100 > i:
                i = i + 1
            return box(i)
        """

        code = self.compile(codestr)
        f = self.find_code(code)
        f = self.run_code(codestr)["testfunc"]
        self.assertEqual(f(), 100)

        self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", (0, TYPED_INT64))
        self.assertInBytecode(f, "LOAD_LOCAL", (0, ("__static__", "int64", "#")))
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        self.assertInBytecode(f, "PRIMITIVE_COMPARE_OP", PRIM_OP_GT_INT)
        self.assertInBytecode(f, "POP_JUMP_IF_ZERO")

    def test_int_loop_chained(self):
        codestr = """
        from __static__ import ssize_t, box
        def testfunc():
            i: ssize_t = 0
            while -1 < i < 100:
                i = i + 1
            return box(i)
        """

        code = self.compile(codestr)
        f = self.find_code(code)
        f = self.run_code(codestr)["testfunc"]
        self.assertEqual(f(), 100)

        self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", (0, TYPED_INT64))
        self.assertInBytecode(f, "LOAD_LOCAL", (0, ("__static__", "int64", "#")))
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        self.assertInBytecode(f, "PRIMITIVE_COMPARE_OP", PRIM_OP_LT_INT)
        self.assertInBytecode(f, "POP_JUMP_IF_ZERO")

    def test_compat_int_math(self):
        codestr = """
        from __static__ import ssize_t, box
        def f():
            x: ssize_t = 42
            z: ssize_t = 1 + x
            return box(z)
        """

        code = self.compile(codestr)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", (42, TYPED_INT64))
        self.assertInBytecode(f, "LOAD_LOCAL", (0, ("__static__", "int64", "#")))
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        f = self.run_code(codestr)["f"]
        self.assertEqual(f(), 43)

    def test_unbox_long(self):
        codestr = """
        from __static__ import unbox, int64
        def f():
            x:int64 = unbox(1)
        """

        self.compile(codestr)

    def test_unbox_str(self):
        codestr = """
        from __static__ import unbox, int64
        def f():
            x:int64 = unbox('abc')
        """

        with self.in_module(codestr) as mod:
            f = mod.f
            with self.assertRaisesRegex(TypeError, "expected 'int', got 'str'"):
                f()

    def test_unbox_typed(self):
        codestr = """
        from __static__ import int64, box
        def f(i: object):
            x = int64(i)
            return box(x)
        """

        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(42), 42)
            self.assertInBytecode(f, "PRIMITIVE_UNBOX")
            with self.assertRaisesRegex(TypeError, "expected 'int', got 'str'"):
                self.assertEqual(f("abc"), 42)

    def test_unbox_typed_bool(self):
        codestr = """
        from __static__ import int64, box
        def f(i: object):
            x = int64(i)
            return box(x)
        """

        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(42), 42)
            self.assertInBytecode(f, "PRIMITIVE_UNBOX")
            self.assertEqual(f(True), 1)
            self.assertEqual(f(False), 0)

    def test_unbox_cbool(self):
        codestr = """
        from __static__ import cbool, box
        def f(i: object):
            x = cbool(i)
            return box(x)
        """

        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "PRIMITIVE_UNBOX")
            self.assertInBytecode(f, "CAST", ("builtins", "bool", "!"))
            self.assertEqual(f(True), True)
            self.assertEqual(f(False), False)
            with self.assertRaises(TypeError):
                self.assertEqual(f(42), True)

    def test_unbox_cbool_typed(self):
        codestr = """
        from __static__ import cbool, box
        def f(i: bool):
            x = cbool(i)
            return box(x)
        """

        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(True), True)
            self.assertEqual(f(False), False)
            self.assertInBytecode(f, "PRIMITIVE_UNBOX")
            self.assertNotInBytecode(f, "CAST", ("builtins", "bool", "!"))

    def test_unbox_cbool_typed_unsupported(self):
        codestr = """
        from __static__ import cbool, box
        def f(i: int):
            x = cbool(i)
            return box(x)
        """

        self.type_error(codestr, "type mismatch: int cannot be assigned to cbool")

    def test_box_cbool_to_bool(self):
        codestr = """
            from typing import final
            from __static__ import cbool

            def foo() -> bool:
                b: cbool = True
                return bool(b)
        """
        with self.in_module(codestr) as mod:
            self.assertInBytecode(mod.foo, "PRIMITIVE_BOX")
            self.assertTrue(mod.foo())

    def test_unbox_incompat_type(self):
        codestr = """
        from __static__ import int64, box
        def f(i: str):
            x:int64 = int64(i)
            return box(x)
        """

        with self.assertRaisesRegex(TypedSyntaxError, type_mismatch("str", "int64")):
            self.compile(codestr)

    def test_uninit_value(self):
        codestr = """
        from __static__ import box, int64
        def f():
            x:int64
            return box(x)
            x = 0
        """
        f = self.run_code(codestr)["f"]
        self.assertEqual(f(), 0)

    def test_uninit_value_2(self):
        codestr = """
        from __static__ import box, int64
        def testfunc(x):
            if x:
                y:int64 = 42
            return box(y)
        """
        self.type_error(codestr, "Name `y` is not defined.")

    def test_bad_box(self):
        codestr = """
        from __static__ import box
        box('abc')
        """

        with self.assertRaisesRegex(TypedSyntaxError, "can't box non-primitive: str"):
            self.compile(codestr)

    def test_bad_unbox(self):
        codestr = """
        from __static__ import unbox, int64
        def f():
            x:int64 = 42
            unbox(x)
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, "Call argument cannot be a primitive"
        ):
            self.compile(codestr)

    def test_bad_box_2(self):
        codestr = """
        from __static__ import box
        box('abc', 'foo')
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, "box only accepts a single argument"
        ):
            self.compile(codestr)

    def test_bad_unbox_2(self):
        codestr = """
        from __static__ import unbox, int64
        def f():
            x:int64 = 42
            unbox(x, y)
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, "unbox only accepts a single argument"
        ):
            self.compile(codestr)

    def test_int_reassign(self):
        codestr = """
        from __static__ import ssize_t, box
        def f():
            x: ssize_t = 42
            z: ssize_t = 1 + x
            x = 100
            x = x + x
            return box(z)
        """

        code = self.compile(codestr)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", (42, TYPED_INT64))
        self.assertInBytecode(f, "LOAD_LOCAL", (0, ("__static__", "int64", "#")))
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        f = self.run_code(codestr)["f"]
        self.assertEqual(f(), 43)

    def test_cmpop(self):
        codestr = """
            from __static__ import int32
            def f():
                i: int32 = 0
                j: int = 0

                if i == 0:
                    return 0
                if j == 0:
                    return 1
        """
        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "f")
        self.assertInBytecode(x, "PRIMITIVE_COMPARE_OP", 0)
        self.assertInBytecode(x, "COMPARE_OP", "==")

    def test_error_starred_primitive(self):
        code = """
            from __static__ import int64

            def g(*args):
                pass

            def f(a):
                x: int64 = 0
                return f(*x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "starred expression cannot be primitive"
        ):
            self.compile(code)

    def test_error_primitive_after_starred(self):
        code = """
            from __static__ import int64

            def g(*args):
                pass

            def f(a):
                x: int64 = 0
                y = []
                return g(*y, x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Call argument cannot be a primitive"
        ):
            self.compile(code)

    def test_error_primitive_builtin_method_desc(self):
        code = """
            from __static__ import int64

            def f(a):
                x: int64 = 0
                return tuple.index((1,2,3), x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Call argument cannot be a primitive"
        ):
            self.compile(code)

    def test_error_primitive_len(self):
        code = """
            from __static__ import int64

            def f(a):
                x: int64 = 0
                return len(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Call argument cannot be a primitive"
        ):
            self.compile(code)

    def test_error_primitive_sorted(self):
        code = """
            from __static__ import int64

            def f(a):
                x: int64 = 0
                return sorted(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Call argument cannot be a primitive"
        ):
            self.compile(code)

    def test_error_primitive_isinstance(self):
        code = """
            from __static__ import int64

            def f(a):
                x: int64 = 0
                return isinstance(x, int)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Call argument cannot be a primitive"
        ):
            self.compile(code)

    def test_error_primitive_issubclass(self):
        code = """
            from __static__ import int64

            def f(a):
                x: int64 = 0
                return issubclass(x, int)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Call argument cannot be a primitive"
        ):
            self.compile(code)

    def test_error_primitive_cast(self):
        code = """
            from __static__ import int64, cast

            def f(a):
                x: int64 = 0
                return cast(x, int)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Call argument cannot be a primitive"
        ):
            self.compile(code)

    def test_primitive_defaults(self):
        code = """
            from __static__ import int64, box

            def f(a: int64 = 42) -> int64:
                return a

            def g():
                return box(f())
        """
        with self.in_module(code) as mod:
            g = mod.g
            f = mod.f
            self.assertEqual(g(), 42)
            self.assertEqual(f(), 42)
            self.assertEqual(f(0), 0)

    def test_primitive_defaults_nested_func(self):
        code = """
            from __static__ import int64, box

            def g():
                def f(a: int64 = 42) -> int64:
                    return a
                return f
        """
        with self.in_module(code) as mod:
            g = mod.g
            self.assertEqual(g()(), 42)

    def test_error_primitive_sorted_kw(self):
        code = """
            from __static__ import int64

            def f(a):
                x: int64 = 0
                return sorted([], key = x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Call argument cannot be a primitive"
        ):
            self.compile(code)

    def test_error_nested_ann(self):
        code = """
            from __static__ import int64

            def f():
                x: int64 = 0
                def g(foo: x):
                    pass
                return g
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "argument annotation cannot be a primitive"
        ):
            self.compile(code)

    def test_error_nested_starargs_ann(self):
        code = """
            from __static__ import int64

            def f():
                x: int64 = 0
                def g(*args: x):
                    pass
                return g
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "argument annotation cannot be a primitive"
        ):
            self.compile(code)

    def test_error_nested_kwargs_ann(self):
        code = """
            from __static__ import int64

            def f():
                x: int64 = 0
                def g(**kwargs: x):
                    pass
                return g
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "argument annotation cannot be a primitive"
        ):
            self.compile(code)

    def test_error_nested_kwonly_ann(self):
        code = """
            from __static__ import int64

            def f():
                x: int64 = 0
                def g(*, foo: x = 42):
                    pass
                return g
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "argument annotation cannot be a primitive"
        ):
            self.compile(code)

    def test_error_nested_annass_prim_annotation(self):
        code = """
            from __static__ import int64

            def f():
                x: int64 = 0
                y: x = 2
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "annotation can not be a primitive value"
        ):
            self.compile(code)

    def test_assert_primitive(self):
        code = """
            from __static__ import int64

            def f():
                x: int64 = 1
                assert x
        """
        with self.in_module(code) as mod:
            f = mod.f
            self.assertInBytecode(f, "POP_JUMP_IF_NONZERO")

    def test_assert_primitive_msg(self):
        code = """
            from __static__ import int64

            def f():
                x: int64 = 1
                assert False, x
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "assert message cannot be a primitive"
        ):
            self.compile(code)

    def test_lambda_ret_primitive(self):
        code = """
            from __static__ import int64
            from typing import Final

            X: Final[int] = 42
            def f():
                return lambda: int64(X)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "lambda cannot return primitive value"
        ):
            self.compile(code)

    def test_list_slice_primitive(self):
        code = """
            from __static__ import int64

            def f():
                x = [2,3,4]
                y: int64 = 1
                return x[y:2]
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, "slice indices cannot be primitives"
        ):
            self.compile(code)

        code = """
            from __static__ import int64

            def f():
                x = [2,3,4]
                y: int64 = 1
                return x[0:y]
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, "slice indices cannot be primitives"
        ):
            self.compile(code)

        code = """
            from __static__ import int64

            def f():
                x = [2,3,4]
                y: int64 = 1
                return x[0:2:y]
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, "slice indices cannot be primitives"
        ):
            self.compile(code)

    def test_dict_primitive(self):
        code = """
            from __static__ import int64

            def f():
                x: int64 = 1
                return {x: 42}
        """
        with self.assertRaisesRegex(TypedSyntaxError, "dict keys cannot be primitives"):
            self.compile(code)

        code = """
            from __static__ import int64

            def f():
                x: int64 = 1
                return {42: x}
        """
        with self.assertRaisesRegex(TypedSyntaxError, "dict keys cannot be primitives"):
            self.compile(code)

        code = """
            from __static__ import int64

            def f():
                x: int64 = 1
                return {**x}
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "dict splat cannot be a primitive"
        ):
            self.compile(code)

    def test_set_primitive(self):
        code = """
            from __static__ import int64

            def f():
                x: int64 = 1
                return {x}
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "set members cannot be primitives"
        ):
            self.compile(code)

    def test_generator_primitive_condition(self):
        code = """
            from __static__ import cbool
            from typing import Final
            COND: Final[bool] = False

            def f(abc):
                return [x for x in abc if cbool(COND)]
        """
        with self.in_module(code) as mod:
            f = mod.f
            self.assertInBytecode(f.__code__, "POP_JUMP_IF_ZERO")
            self.assertEqual(f([1, 2, 3]), [])

    def test_generator_primitive_iter(self):
        code = """
            from __static__ import cbool
            from typing import Final
            COND: Final[bool] = False

            def f(abc):
                return [x for x in cbool(COND)]
        """
        with self.assertRaisesRegex(TypedSyntaxError, "cannot iterate over cbool"):
            self.compile(code)

    def test_generator_primitive_element(self):
        code = """
            from __static__ import cbool
            from typing import Final
            COND: Final[bool] = True

            def f(abc):
                return [cbool(COND) for x in abc]
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "generator element cannot be a primitive"
        ):
            self.compile(code)

    def test_format_primitive(self):
        code = """
            from __static__ import int64

            def f():
                x: int64 = 0
                return f"{x}"
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "cannot use primitive in formatted value"
        ):
            self.compile(code)

    def test_subscr_primitive(self):
        code = """
            from __static__ import int64

            def f():
                x: int64 = 0
                return [*x]
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "cannot use primitive in starred expression"
        ):
            self.compile(code)

    def test_dict_comp_primitive_element(self):
        code = """
            from __static__ import cbool
            from typing import Final
            COND: Final[bool] = True

            def f(abc):
                return {k:cbool(COND) for k in abc}
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "dictionary comprehension value cannot be a primitive"
        ):
            self.compile(code)

        code = """
            from __static__ import cbool
            from typing import Final
            COND: Final[bool] = True

            def f(abc):
                return {cbool(COND):v for v in abc}
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "dictionary comprehension key cannot be a primitive"
        ):
            self.compile(code)

    def test_await_primitive(self):
        code = """
            from __static__ import cbool
            from typing import Final
            COND: Final[bool] = True

            async def f(abc):
                await cbool(COND)
        """
        with self.assertRaisesRegex(TypedSyntaxError, "cannot await a primitive value"):
            self.compile(code)

    def test_yield_primitive(self):
        code = """
            from __static__ import cbool
            from typing import Final
            COND: Final[bool] = True

            def f(abc):
                yield cbool(COND)
        """
        with self.assertRaisesRegex(TypedSyntaxError, "cannot yield a primitive value"):
            self.compile(code)

    def test_yield_from_primitive(self):
        code = """
            from __static__ import cbool
            from typing import Final
            COND: Final[bool] = True

            def f(abc):
                yield from cbool(COND)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "cannot yield from a primitive value"
        ):
            self.compile(code)

    def test_error_nested_return_ann(self):
        code = """
            from __static__ import int64

            def f():
                x: int64 = 0
                def g() -> x:
                    pass
                return g
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "return annotation cannot be a primitive"
        ):
            self.compile(code)

    def test_error_nested_prim_decorator(self):
        code = """
            from __static__ import int64

            def f():
                x: int64 = 0
                @x
                def g():
                    pass
                return g
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "decorator cannot be a primitive"
        ):
            self.compile(code)

    def test_error_nested_class_prim_decorator(self):
        code = """
            from __static__ import int64, unbox
            from typing import Final

            X: Final[int] = 42

            @int64(X)
            class C: pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "decorator cannot be a primitive"
        ):
            self.compile(code)

    def test_error_nested_class_prim_kwarg(self):
        code = """
            from __static__ import int64, unbox
            from typing import Final

            X: Final[int] = 42

            class C(metaclass=int64(X)): pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "class kwarg cannot be a primitive"
        ):
            self.compile(code)

    def test_error_nested_class_prim_base(self):
        code = """
            from __static__ import int64, unbox
            from typing import Final

            X: Final[int] = 42

            class C(int64(X)): pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "class base cannot be a primitive"
        ):
            self.compile(code)

    def test_unbox_kw_args(self):
        code = """
            from __static__ import int64, unbox

            def f(a):
                x: int64 = unbox(42, x=2)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "unbox\\(\\) takes no keyword arguments"
        ):
            self.compile(code)

    def test_len_kw_args(self):
        code = """
            from __static__ import int64

            def f(a):
                len([], x=2)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "len\\(\\) takes no keyword arguments"
        ):
            self.compile(code)

    def test_primitive_invoke(self) -> None:
        codestr = """
            from __static__ import int8
            def f():
                x: int8 = 42
                print(x.__str__())
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "cannot load attribute from int8"
        ):
            self.compile(codestr)

    def test_primitive_call(self) -> None:
        codestr = """
            from __static__ import int8
            def f():
                x: int8 = 42
                print(x())
        """
        with self.assertRaisesRegex(TypedSyntaxError, "cannot call int8"):
            self.compile(codestr)

    def test_primitive_subscr(self) -> None:
        codestr = """
            from __static__ import int8
            def f():
                x: int8 = 42
                print(x[42])
        """
        with self.assertRaisesRegex(TypedSyntaxError, "cannot index int8"):
            self.compile(codestr)

    def test_primitive_iter(self) -> None:
        codestr = """
            from __static__ import int8
            def f():
                x: int8 = 42
                for a in x:
                    pass
        """
        with self.assertRaisesRegex(TypedSyntaxError, "cannot iterate over int8"):
            self.compile(codestr)

    def test_error_return_int(self):
        with self.assertRaisesRegex(TypedSyntaxError, bad_ret_type("int64", "dynamic")):
            self.compile(
                """
                from __static__ import ssize_t
                def f():
                    y: ssize_t = 1
                    return y
                """
            )

    def test_index_by_int(self):
        codestr = """
            from __static__ import int32
            def f(x):
                i: int32 = 0
                return x[i]
        """
        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr)

    def test_list_get_primitive_int(self):
        codestr = """
            from __static__ import int8
            def f():
                l = [1, 2, 3]
                x: int8 = 1
                return l[x]
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "SEQUENCE_GET", SEQ_LIST)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), 2)

    def test_list_get_primitive_int_unknown(self):
        codestr = """
            from __static__ import int8
            def f(x: int8):
                l = [1, 2, 3]
                return l[x]
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "SEQUENCE_GET", SEQ_LIST)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(1), 2)

    def test_list_set_primitive_int(self):
        codestr = """
            from __static__ import int8
            def f():
                l = [1, 2, 3]
                x: int8 = 1
                l[x] = 5
                return l
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "SEQUENCE_SET")
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), [1, 5, 3])

    def test_list_set_primitive_int_2(self):
        codestr = """
            from __static__ import int64
            def f(l1):
                l2 = [None] * len(l1)
                i: int64 = 0
                for item in l1:
                    l2[i] = item + 1
                    i += 1
                return l2
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "SEQUENCE_SET")
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f([1, 2000]), [2, 2001])

    def test_list_del_primitive_int(self):
        codestr = """
            from __static__ import int8
            def f():
                l = [1, 2, 3]
                x: int8 = 1
                del l[x]
                return l
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "LIST_DEL")
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), [1, 3])

    def test_list_append(self):
        codestr = """
            from __static__ import int8
            def f():
                l = [1, 2, 3]
                l.append(4)
                return l
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "LIST_APPEND", 1)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), [1, 2, 3, 4])

    def test_assign_prim_to_class(self):
        codestr = """
            from __static__ import int64
            class C: pass

            def f():
                x: C = C()
                y: int64 = 42
                x = y
        """
        with self.assertRaisesRegex(TypedSyntaxError, type_mismatch("int64", "foo.C")):
            self.compile(codestr, modname="foo")

    def test_int_swap(self):
        codestr = """
            from __static__ import int64, box

            def test():
                x: int64 = 42
                y: int64 = 100
                x, y = y, x
                return box(x), box(y)
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, type_mismatch("int64", "dynamic")
        ):
            self.compile(codestr, modname="foo")

    def test_widening_assign(self):
        codestr = """
            from __static__ import int8, int16, box

            def testfunc():
                x: int16
                y: int8
                x = y = 42
                return box(x), box(y)
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            self.assertEqual(test(), (42, 42))

    def test_widening_assign_reassign(self):
        codestr = """
            from __static__ import int8, int16, box

            def testfunc():
                x: int16
                y: int8
                x = y = 42
                x = 257
                return box(x), box(y)
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            self.assertEqual(test(), (257, 42))

    def test_widening_assign_reassign_error(self):
        codestr = """
            from __static__ import int8, int16, box

            def testfunc():
                x: int16
                y: int8
                x = y = 42
                y = 128
                return box(x), box(y)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "type mismatch: Literal\\[128\\] cannot be assigned to int8",
        ):
            self.compile(codestr, modname="foo")

    def test_narrowing_assign_literal(self):
        codestr = """
            from __static__ import int8, int16, box

            def testfunc():
                x: int8
                y: int16
                x = y = 42
                return box(x), box(y)
        """
        self.compile(codestr, modname="foo")

    def test_narrowing_assign_out_of_range(self):
        codestr = """
            from __static__ import int8, int16, box

            def testfunc():
                x: int8
                y: int16
                x = y = 300
                return box(x), box(y)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "type mismatch: Literal\\[300\\] cannot be assigned to int8",
        ):
            self.compile(codestr, modname="foo")

    def test_module_primitive(self):
        codestr = """
            from __static__ import int8
            x: int8
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "cannot use primitives in global or closure scope"
        ):
            self.compile(codestr, modname="foo")

    def test_implicit_module_primitive(self):
        codestr = """
            from __static__ import int8
            x = y = int8(0)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "cannot use primitives in global or closure scope"
        ):
            self.compile(codestr, modname="foo")

    def test_chained_primitive_to_non_primitive(self):
        codestr = """
            from __static__ import int8
            def f():
                x: object
                y: int8 = 42
                x = y = 42
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Literal\\[42\\] cannot be assigned to object"
        ):
            self.compile(codestr, modname="foo")

    def test_closure_primitive(self):
        codestr = """
            from __static__ import int8
            def f():
                x: int8 = 0
                def g():
                    return x
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "cannot use primitives in global or closure scope"
        ):
            self.compile(codestr, modname="foo")

    def test_nonlocal_primitive(self):
        codestr = """
            from __static__ import int8
            def f():
                x: int8 = 0
                def g():
                    nonlocal x
                    x = 1
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "cannot use primitives in global or closure scope"
        ):
            self.compile(codestr, modname="foo")

    def test_dynamic_chained_assign_param(self):
        codestr = """
            from __static__ import int16, box
            def testfunc(y):
                x: int16
                x = y = 42
                return box(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, type_mismatch("Literal[42]", "int16")
        ):
            self.compile(codestr, modname="foo")

    def test_dynamic_chained_assign_param_2(self):
        codestr = """
            from __static__ import int16
            def testfunc(y):
                x: int16
                y = x = 42
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, type_mismatch("Literal[42]", "dynamic")
        ):
            self.compile(codestr, modname="foo")

    def test_dynamic_chained_assign_1(self):
        codestr = """
            from __static__ import int16, box
            def testfunc():
                x: int16
                x = y = 42
                return box(x)
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            self.assertEqual(test(), 42)

    def test_dynamic_chained_assign_2(self):
        codestr = """
            from __static__ import int16, box
            def testfunc():
                x: int16
                y = x = 42
                return box(y)
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            self.assertEqual(test(), 42)

    def test_tuple_assign_list(self):
        codestr = """
            from __static__ import int16, box
            def testfunc(a: int, b: int):
                x: int
                y: str
                x, y = [a, b]
        """
        with self.assertRaisesRegex(TypedSyntaxError, "int cannot be assigned to str"):
            self.compile(codestr, modname="foo")

    def test_tuple_assign_tuple(self):
        codestr = """
            from __static__ import int16, box
            def testfunc(a: int, b: int):
                x: int
                y: str
                x, y = a, b
        """
        with self.assertRaisesRegex(TypedSyntaxError, "int cannot be assigned to str"):
            self.compile(codestr, modname="foo")

    def test_tuple_assign_constant(self):
        codestr = """
            from __static__ import int16, box
            def testfunc():
                x: int
                y: str
                x, y = 1, 1
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"type mismatch: int cannot be assigned to str",
        ):
            self.compile(codestr, modname="foo")

    def test_chained_assign_type_inference(self):
        codestr = """
            from __static__ import int64, char

            def test2():
                y = x = 4
                reveal_type(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"'x' has declared type 'dynamic' and local type 'Literal\[4\]'",
        ):
            self.compile(codestr, modname="foo")

    def test_chained_assign_type_inference_2(self):
        codestr = """
            from __static__ import int64, char

            def test2():
                y = x = 4
                reveal_type(y)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"'y' has declared type 'dynamic' and local type 'Literal\[4\]'",
        ):
            self.compile(codestr, modname="foo")

    def test_user_enumerate_list(self):
        codestr = """
            from __static__ import int64, box, clen

            def f(x: list):
                i: int64 = 0
                res = []
                while i < clen(x):
                    elem = x[i]
                    res.append((box(i), elem))
                    i += 1
                return res
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "SEQUENCE_GET", SEQ_LIST_INEXACT)
            res = f([1, 2, 3])
            self.assertEqual(res, [(0, 1), (1, 2), (2, 3)])

    def test_user_enumerate_list_nooverride(self):
        class mylist(list):
            pass

        codestr = """
            from __static__ import int64, box, clen

            def f(x: list):
                i: int64 = 0
                res = []
                while i < clen(x):
                    elem = x[i]
                    res.append((box(i), elem))
                    i += 1
                return res
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "SEQUENCE_GET", SEQ_LIST_INEXACT)
            res = f(mylist([1, 2, 3]))
            self.assertEqual(res, [(0, 1), (1, 2), (2, 3)])

    def test_user_enumerate_list_subclass(self):
        class mylist(list):
            def __getitem__(self, idx):
                return list.__getitem__(self, idx) + 1

        codestr = """
            from __static__ import int64, box, clen

            def f(x: list):
                i: int64 = 0
                res = []
                while i < clen(x):
                    elem = x[i]
                    res.append((box(i), elem))
                    i += 1
                return res
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "SEQUENCE_GET", SEQ_LIST_INEXACT)
            res = f(mylist([1, 2, 3]))
            self.assertEqual(res, [(0, 2), (1, 3), (2, 4)])

    def test_list_assign_subclass(self):
        class mylist(list):
            def __setitem__(self, idx, value):
                return list.__setitem__(self, idx, value + 1)

        codestr = """
            from __static__ import int64, box, clen

            def f(x: list):
                i: int64 = 0
                x[i] = 42
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "SEQUENCE_SET", SEQ_LIST_INEXACT)
            l = mylist([0])
            f(l)
            self.assertEqual(l[0], 43)

    def test_inexact_list_negative(self):
        codestr = """
            from __static__ import int64, box, clen

            def f(x: list):
                i: int64 = 1
                return x[-i]
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "SEQUENCE_GET", SEQ_LIST_INEXACT)
            res = f([1, 2, 3])
            self.assertEqual(res, 3)

    def test_inexact_list_negative_int8(self):
        codestr = """
            from __static__ import int8

            def f(x: list):
                i: int8 = 1
                return x[-i]
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f([1, 2, 3]), 3)

    def test_inexact_list_negative_int16(self):
        codestr = """
            from __static__ import int16

            def f(x: list):
                i: int16 = 1
                return x[-i]
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f([1, 2, 3]), 3)

    def test_inexact_list_negative_int32(self):
        codestr = """
            from __static__ import int32

            def f(x: list):
                i: int32 = 1
                return x[-i]
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f([1, 2, 3]), 3)

    def test_inexact_list_negative_int64(self):
        codestr = """
            from __static__ import int64

            def f(x: list):
                i: int64 = 1
                return x[-i]
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f([1, 2, 3]), 3)

    def test_inexact_list_large_unsigned(self):
        codestr = """
            from __static__ import uint64
            def f(x: list):
                i: uint64 = 0xffffffffffffffff
                return x[i]
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "type mismatch: uint64 cannot be assigned to dynamic"
        ):
            self.compile(codestr)

    def test_primitive_args_funcdef(self):
        codestr = """
            from __static__ import int8, box

            def n(val: int8):
                return box(val)

            def x():
                y: int8 = 42
                return n(y)
        """
        with self.in_strict_module(codestr) as mod:
            x = mod.x
            self.assertEqual(x(), 42)
            self.assertEqual(mod.n(-128), -128)
            self.assertEqual(mod.n(127), 127)
            with self.assertRaises(OverflowError):
                print(mod.n(-129))
            with self.assertRaises(OverflowError):
                print(mod.n(128))

    def test_primitive_args_funcdef_unjitable(self):
        codestr = """
            from __static__ import int8, box

            X = 42
            def n(val: int8):
                global X; X = 42; del X
                return box(val)

            def x():
                y: int8 = 42
                return n(y)
        """
        with self.in_strict_module(codestr) as mod:
            n = mod.n
            x = mod.x
            self.assertEqual(x(), 42)
            self.assertEqual(mod.n(-128), -128)
            self.assertEqual(mod.n(127), 127)
            with self.assertRaises(OverflowError):
                print(mod.n(-129))
            with self.assertRaises(OverflowError):
                print(mod.n(128))
            self.assert_not_jitted(n)

    def test_primitive_args_funcdef_too_many_args(self):
        codestr = """
            from __static__ import int8, box

            def n(x: int8):
                return box(x)
        """
        with self.in_strict_module(codestr) as mod:
            with self.assertRaises(TypeError):
                print(mod.n(-128, x=2))
            with self.assertRaises(TypeError):
                print(mod.n(-128, 2))

    def test_primitive_args_funcdef_missing_starargs(self):
        codestr = """
            from __static__ import int8, box

            def x(val: int8, *foo):
                return box(val), foo
            def y(val: int8, **foo):
                return box(val), foo
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.x(-128), (-128, ()))
            self.assertEqual(mod.y(-128), (-128, {}))

    def test_primitive_args_many_args(self):
        codestr = """
            from __static__ import int8, int16, int32, int64, uint8, uint16, uint32, uint64, box

            def x(i8: int8, i16: int16, i32: int32, i64: int64, u8: uint8, u16: uint16, u32: uint32, u64: uint64):
                return box(i8), box(i16), box(i32), box(i64), box(u8), box(u16), box(u32), box(u64)

            def y():
                return x(1,2,3,4,5,6,7,8)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(mod.y, "INVOKE_FUNCTION", ((mod.__name__, "x"), 8))
            self.assertEqual(mod.y(), (1, 2, 3, 4, 5, 6, 7, 8))
            self.assertEqual(mod.x(1, 2, 3, 4, 5, 6, 7, 8), (1, 2, 3, 4, 5, 6, 7, 8))

    def test_primitive_args_sizes(self):
        cases = [
            ("cbool", True, False),
            ("cbool", False, False),
            ("int8", (1 << 7), True),
            ("int8", (-1 << 7) - 1, True),
            ("int8", -1 << 7, False),
            ("int8", (1 << 7) - 1, False),
            ("int16", (1 << 15), True),
            ("int16", (-1 << 15) - 1, True),
            ("int16", -1 << 15, False),
            ("int16", (1 << 15) - 1, False),
            ("int32", (1 << 31), True),
            ("int32", (-1 << 31) - 1, True),
            ("int32", -1 << 31, False),
            ("int32", (1 << 31) - 1, False),
            ("int64", (1 << 63), True),
            ("int64", (-1 << 63) - 1, True),
            ("int64", -1 << 63, False),
            ("int64", (1 << 63) - 1, False),
            ("uint8", (1 << 8), True),
            ("uint8", -1, True),
            ("uint8", (1 << 8) - 1, False),
            ("uint8", 0, False),
            ("uint16", (1 << 16), True),
            ("uint16", -1, True),
            ("uint16", (1 << 16) - 1, False),
            ("uint16", 0, False),
            ("uint32", (1 << 32), True),
            ("uint32", -1, True),
            ("uint32", (1 << 32) - 1, False),
            ("uint32", 0, False),
            ("uint64", (1 << 64), True),
            ("uint64", -1, True),
            ("uint64", (1 << 64) - 1, False),
            ("uint64", 0, False),
        ]
        for type, val, overflows in cases:
            codestr = f"""
                from __static__ import {type}, box

                def x(val: {type}):
                    return box(val)
            """
            with self.subTest(type=type, val=val, overflows=overflows):
                with self.in_strict_module(codestr) as mod:
                    if overflows:
                        with self.assertRaises(OverflowError):
                            mod.x(val)
                    else:
                        self.assertEqual(mod.x(val), val)

    def test_primitive_args_funcdef_missing_kw_call(self):
        codestr = """
            from __static__ import int8, box

            def testfunc(x: int8, foo):
                return box(x), foo
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.testfunc(-128, foo=42), (-128, 42))

    def test_primitive_args_funccall(self):
        codestr = """
            from __static__ import int8

            def f(foo):
                pass

            def n() -> int:
                x: int8 = 3
                return f(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"int8 received for positional arg 'foo', expected dynamic",
        ):
            self.compile(codestr, modname="foo.py")

    def test_primitive_args_funccall_int(self):
        codestr = """
            from __static__ import int8

            def f(foo: int):
                pass

            def n() -> int:
                x: int8 = 3
                return f(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"int8 received for positional arg 'foo', expected int",
        ):
            self.compile(codestr, modname="foo.py")

    def test_primitive_args_typecall(self):
        codestr = """
            from __static__ import int8

            def n() -> int:
                x: int8 = 3
                return int(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Call argument cannot be a primitive"
        ):
            self.compile(codestr, modname="foo.py")

    def test_primitive_args_typecall_kwarg(self):
        codestr = """
            from __static__ import int8

            def n() -> int:
                x: int8 = 3
                return dict(a=x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Call argument cannot be a primitive"
        ):
            self.compile(codestr, modname="foo.py")

    def test_primitive_args_nonstrict(self):
        codestr = """
            from __static__ import int8, int16, box

            def f(x: int8, y: int16) -> int16:
                return x + y

            def g() -> int:
                return box(f(1, 300))
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.g(), 301)

    def test_primitive_args_and_return(self):
        cases = [
            ("cbool", 1),
            ("cbool", 0),
            ("int8", -1 << 7),
            ("int8", (1 << 7) - 1),
            ("int16", -1 << 15),
            ("int16", (1 << 15) - 1),
            ("int32", -1 << 31),
            ("int32", (1 << 31) - 1),
            ("int64", -1 << 63),
            ("int64", (1 << 63) - 1),
            ("uint8", (1 << 8) - 1),
            ("uint8", 0),
            ("uint16", (1 << 16) - 1),
            ("uint16", 0),
            ("uint32", (1 << 32) - 1),
            ("uint32", 0),
            ("uint64", (1 << 64) - 1),
            ("uint64", 0),
        ]
        for typ, val in cases:
            if typ == "cbool":
                op = "or"
                expected = True
                other = "cbool(True)"
                boxed = "bool"
            else:
                op = "+" if val <= 0 else "-"
                expected = val + (1 if op == "+" else -1)
                other = "1"
                boxed = "int"
            with self.subTest(typ=typ, val=val, op=op, expected=expected):
                codestr = f"""
                    from __static__ import {typ}, box

                    def f(x: {typ}, y: {typ}) -> {typ}:
                        return x {op} y

                    def g() -> {boxed}:
                        return box(f({val}, {other}))
                """
                with self.in_strict_module(codestr) as mod:
                    self.assertEqual(mod.g(), expected)

    def test_primitive_return(self):
        cases = [
            ("cbool", True),
            ("cbool", False),
            ("int8", -1 << 7),
            ("int8", (1 << 7) - 1),
            ("int16", -1 << 15),
            ("int16", (1 << 15) - 1),
            ("int32", -1 << 31),
            ("int32", (1 << 31) - 1),
            ("int64", -1 << 63),
            ("int64", (1 << 63) - 1),
            ("uint8", (1 << 8) - 1),
            ("uint8", 0),
            ("uint16", (1 << 16) - 1),
            ("uint16", 0),
            ("uint32", (1 << 32) - 1),
            ("uint32", 0),
            ("uint64", (1 << 64) - 1),
            ("uint64", 0),
        ]
        tf = [True, False]
        for (type, val), box, strict, error, unjitable in itertools.product(
            cases, tf, tf, tf, tf
        ):
            if type == "cbool":
                op = "or"
                other = "False"
                boxed = "bool"
            else:
                op = "*"
                other = "1"
                boxed = "int"
            unjitable_code = "global X; X = 42; del X" if unjitable else ""
            codestr = f"""
                from __static__ import {type}, box

                X = 42
                def f(error: bool) -> {type}:
                    {unjitable_code}
                    if error:
                        raise RuntimeError("boom")
                    return {val}
            """
            if box:
                codestr += f"""

                def g() -> {boxed}:
                    return box(f({error}) {op} {type}({other}))
                """
            else:
                codestr += f"""

                def g() -> {type}:
                    return f({error}) {op} {type}({other})
                """
            ctx = self.in_strict_module if strict else self.in_module
            oparg = PRIM_NAME_TO_TYPE[type]
            with self.subTest(
                type=type,
                val=val,
                strict=strict,
                box=box,
                error=error,
                unjitable=unjitable,
            ):
                with ctx(codestr) as mod:
                    f = mod.f
                    g = mod.g
                    self.assertInBytecode(f, "RETURN_PRIMITIVE", oparg)
                    if box:
                        self.assertNotInBytecode(g, "RETURN_PRIMITIVE")
                    else:
                        self.assertInBytecode(g, "RETURN_PRIMITIVE", oparg)
                    if error:
                        with self.assertRaisesRegex(RuntimeError, "boom"):
                            g()
                    else:
                        self.assertEqual(g(), val)
                    self.assert_jitted(g)
                    if unjitable:
                        self.assert_not_jitted(f)
                    else:
                        self.assert_jitted(f)

    def test_primitive_return_recursive(self):
        codestr = """
            from __static__ import int32

            def fib(n: int32) -> int32:
                if n <= 1:
                    return n
                return fib(n-1) + fib(n-2)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(
                mod.fib,
                "INVOKE_FUNCTION",
                ((mod.__name__, "fib"), 1),
            )
            self.assertEqual(mod.fib(2), 1)
            self.assert_jitted(mod.fib)

    def test_primitive_return_unannotated(self):
        codestr = """
            from __static__ import int32

            def f():
                x: int32 = 1
                return x
        """
        with self.assertRaisesRegex(TypedSyntaxError, bad_ret_type("int32", "dynamic")):
            self.compile(codestr)

    def test_primitive_return_bad_call(self):
        codestr = """
        from __static__ import int64

        def fn(x: int, y: int) -> int64:
            i = int64(x)
            j = int64(y)
            return i + j
        """
        with self.in_module(codestr) as mod:
            fn = mod.fn
            with self.assertRaisesRegex(
                TypeError,
                re.escape("fn() missing 2 required positional arguments: 'x' and 'y'"),
            ):
                fn()  # bad call

    def test_int_unbox_with_conversion(self):
        codestr = """
            from __static__ import int64

            def f(x) -> int64:
                return int64(int(x))
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(42.0), 42)

    def test_int_compare_to_cbool(self):
        codestr = """
            from __static__ import int64, cbool
            def foo(i: int64) -> cbool:
                return i  == 0
        """
        with self.in_module(codestr) as mod:
            foo = mod.foo
            self.assertEqual(foo(0), True)
            self.assertEqual(foo(1), False)

    def test_int_compare_to_cbool_reversed(self):
        codestr = """
            from __static__ import int64, cbool
            def foo(i: int64) -> cbool:
                return 0 == i
        """
        with self.in_module(codestr) as mod:
            foo = mod.foo
            self.assertEqual(foo(0), True)
            self.assertEqual(foo(1), False)

    def test_cbool_compare_to_cbool(self):
        for a, b in [
            ("True", "True"),
            ("True", "False"),
            ("False", "False"),
            ("False", "True"),
        ]:
            codestr = f"""
            from __static__ import cbool

            def f() -> int:
                a: cbool = {a}
                b: cbool = {b}
                if a < b:
                    return 1
                else:
                    return 2
            """
            with self.subTest(a=a, b=b):
                with self.in_module(codestr) as mod:
                    f = mod.f
                    if a == "True":
                        self.assertEqual(f(), 2)
                    elif a == "False" and b == "False":
                        self.assertEqual(f(), 2)
                    else:
                        self.assertEqual(f(), 1)

    def test_assign_bool_to_primitive_int(self):
        codestr = f"""
        from __static__ import int8

        def f() -> int:
            a: int8 = True
        """
        with self.assertRaisesRegex(TypedSyntaxError, type_mismatch("bool", "int8")):
            self.compile(codestr)

    def test_inline_primitive(self):

        codestr = """
            from __static__ import int64, cbool, inline

            @inline
            def x(i: int64) -> cbool:
                return i == 1

            def foo(i: int64) -> cbool:
                return i >0 and x(i)
        """
        with self.in_module(codestr, optimize=2) as mod:
            foo = mod.foo
            self.assertEqual(foo(0), False)
            self.assertEqual(foo(1), True)
            self.assertEqual(foo(2), False)
            self.assertNotInBytecode(foo, "STORE_FAST")
            self.assertInBytecode(foo, "STORE_LOCAL")

    def test_inline_primitive_multiple(self):
        codestr = """
            from __static__ import cbool, inline, int64, int32

            @inline
            def f(x: int64) -> cbool:
                return x == 1

            @inline
            def g(x: int32) -> cbool:
                return x == 2

            def h(a: int64, b: int32) -> cbool:
                return f(a) and g(b)
        """
        with self.in_module(codestr, optimize=2) as mod:
            h = mod.h
            self.assertNotInBytecode(h, "INVOKE_FUNCTION")
            self.assertNotInBytecode(h, "CALL_FUNCTION")
            self.assertEqual(h(1, 2), True)

    def test_primitive_compare_immediate_no_branch_on_result(self):
        for rev in [True, False]:
            compare = "0 == xp" if rev else "xp == 0"
            codestr = f"""
                from __static__ import box, int64, int32

                def f(x: int) -> bool:
                    xp = int64(x)
                    y = {compare}
                    return box(y)
            """
            with self.subTest(rev=rev):
                with self.in_module(codestr) as mod:
                    f = mod.f
                    self.assertEqual(f(3), 0)
                    self.assertEqual(f(0), 1)
                    self.assertIs(f(0), True)

    def test_chained_compare_primitive_mixed(self):
        for jumpif in [False, True]:
            with self.subTest(jumpif=jumpif):
                if jumpif:
                    pre = ""
                    test = "a < x < b"
                else:
                    pre = "y = a < x < b"
                    test = "y"
                codestr = f"""
                    from __static__ import int16, int32, int64

                    def f(x: int16):
                        a: int32 = 1
                        b: int64 = 5
                        if x:
                            a += 1
                            b += 1
                        {pre}
                        if {test}:
                            return 1
                        return 0
                """
                with self.in_module(codestr) as mod:
                    f = mod.f
                    self.assertInBytecode(
                        f, "CONVERT_PRIMITIVE", TYPED_INT16 | (TYPED_INT32 << 4)
                    )
                    self.assertInBytecode(
                        f, "CONVERT_PRIMITIVE", TYPED_INT16 | (TYPED_INT64 << 4)
                    )
                    self.assertEqual(f(2), 0)
                    self.assertEqual(f(3), 1)
                    self.assertEqual(f(5), 1)
                    self.assertEqual(f(6), 0)

    def test_clen(self):
        codestr = """
            from __static__ import box, clen, int64
            from typing import List

            def f(l: List[int]):
                x: int64 = clen(l)
                return box(x)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "FAST_LEN", FAST_LEN_LIST | FAST_LEN_INEXACT)
            self.assertEqual(f([1, 2, 3]), 3)

            class MyList(list):
                def __len__(self):
                    return 99

            self.assertEqual(f(MyList([1, 2])), 99)

    def test_clen_bad_arg(self):
        codestr = """
            from __static__ import clen

            def f(l):
                clen(l)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "bad argument type 'dynamic' for clen()"
        ):
            self.compile(codestr)

    def test_load_int_const_sizes(self):
        cases = [
            ("int8", (1 << 7), True),
            ("int8", (-1 << 7) - 1, True),
            ("int8", -1 << 7, False),
            ("int8", (1 << 7) - 1, False),
            ("int16", (1 << 15), True),
            ("int16", (-1 << 15) - 1, True),
            ("int16", -1 << 15, False),
            ("int16", (1 << 15) - 1, False),
            ("int32", (1 << 31), True),
            ("int32", (-1 << 31) - 1, True),
            ("int32", -1 << 31, False),
            ("int32", (1 << 31) - 1, False),
            ("int64", (1 << 63), True),
            ("int64", (-1 << 63) - 1, True),
            ("int64", -1 << 63, False),
            ("int64", (1 << 63) - 1, False),
            ("uint8", (1 << 8), True),
            ("uint8", -1, True),
            ("uint8", (1 << 8) - 1, False),
            ("uint8", 0, False),
            ("uint16", (1 << 16), True),
            ("uint16", -1, True),
            ("uint16", (1 << 16) - 1, False),
            ("uint16", 0, False),
            ("uint32", (1 << 32), True),
            ("uint32", -1, True),
            ("uint32", (1 << 32) - 1, False),
            ("uint32", 0, False),
            ("uint64", (1 << 64), True),
            ("uint64", -1, True),
            ("uint64", (1 << 64) - 1, False),
            ("uint64", 0, False),
        ]
        for type, val, overflows in cases:
            codestr = f"""
                from __static__ import {type}, box

                def f() -> int:
                    x: {type} = {val}
                    return box(x)
            """
            with self.subTest(type=type, val=val, overflows=overflows):
                if overflows:
                    with self.assertRaisesRegex(
                        TypedSyntaxError,
                        f"type mismatch: Literal\\[{val}\\] cannot be assigned to {type}",
                    ):
                        self.compile(codestr)
                else:
                    with self.in_strict_module(codestr) as mod:
                        self.assertEqual(mod.f(), val)

    def test_load_int_const_signed(self):
        int_types = [
            "int8",
            "int16",
            "int32",
            "int64",
        ]
        signs = ["-", ""]
        values = [12]

        for type, sign, value in itertools.product(int_types, signs, values):
            expected = value if sign == "" else -value

            codestr = f"""
                from __static__ import {type}, box

                def y() -> int:
                    x: {type} = {sign}{value}
                    return box(x)
            """
            with self.subTest(type=type, sign=sign, value=value):
                y = self.find_code(self.compile(codestr, modname="foo"), name="y")
                self.assertInBytecode(y, "PRIMITIVE_LOAD_CONST")
                with self.in_module(codestr) as mod:
                    y = mod.y
                    self.assertEqual(y(), expected)

    def test_load_int_const_unsigned(self):
        int_types = [
            "uint8",
            "uint16",
            "uint32",
            "uint64",
        ]
        values = [12]

        for type, value in itertools.product(int_types, values):
            expected = value
            codestr = f"""
                from __static__ import {type}, box

                def y() -> int:
                    return box({type}({value}))
            """
            with self.subTest(type=type, value=value):
                y = self.find_code(self.compile(codestr, modname="foo"), name="y")
                self.assertInBytecode(y, "PRIMITIVE_LOAD_CONST")
                with self.in_module(codestr) as mod:
                    y = mod.y
                    self.assertEqual(y(), expected)

    def test_primitive_out_of_range(self):
        codestr = f"""
            from __static__ import int8, box

            def f() -> int:
                x = int8(255)
                return box(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "type mismatch: Literal\\[255\\] cannot be assigned to int8",
        ):
            self.compile(codestr)

    def test_primitive_conversions(self):
        cases = [
            ("int8", "int8", 5, 5),
            ("int8", "int16", 5, 5),
            ("int8", "int32", 5, 5),
            ("int8", "int64", 5, 5),
            ("int8", "uint8", -1, 255),
            ("int8", "uint8", 12, 12),
            ("int8", "uint16", -1, 65535),
            ("int8", "uint16", 12, 12),
            ("int8", "uint32", -1, 4294967295),
            ("int8", "uint32", 12, 12),
            ("int8", "uint64", -1, 18446744073709551615),
            ("int8", "uint64", 12, 12),
            ("int16", "int8", 5, 5),
            ("int16", "int8", -1, -1),
            ("int16", "int8", 32767, -1),
            ("int16", "int16", 5, 5),
            ("int16", "int32", -5, -5),
            ("int16", "int64", -6, -6),
            ("int16", "uint8", 32767, 255),
            ("int16", "uint8", -1, 255),
            ("int16", "uint16", 32767, 32767),
            ("int16", "uint16", -1, 65535),
            ("int16", "uint32", 1000, 1000),
            ("int16", "uint32", -1, 4294967295),
            ("int16", "uint64", 1414, 1414),
            ("int16", "uint64", -1, 18446744073709551615),
            ("int32", "int8", 5, 5),
            ("int32", "int8", -1, -1),
            ("int32", "int8", 2147483647, -1),
            ("int32", "int16", 5, 5),
            ("int32", "int16", -1, -1),
            ("int32", "int16", 2147483647, -1),
            ("int32", "int32", 5, 5),
            ("int32", "int64", 5, 5),
            ("int32", "uint8", 5, 5),
            ("int32", "uint8", 65535, 255),
            ("int32", "uint8", -1, 255),
            ("int32", "uint16", 5, 5),
            ("int32", "uint16", 2147483647, 65535),
            ("int32", "uint16", -1, 65535),
            ("int32", "uint32", 5, 5),
            ("int32", "uint32", -1, 4294967295),
            ("int32", "uint64", 5, 5),
            ("int32", "uint64", -1, 18446744073709551615),
            ("int64", "int8", 5, 5),
            ("int64", "int8", -1, -1),
            ("int64", "int8", 65535, -1),
            ("int64", "int16", 5, 5),
            ("int64", "int16", -1, -1),
            ("int64", "int16", 4294967295, -1),
            ("int64", "int32", 5, 5),
            ("int64", "int32", -1, -1),
            ("int64", "int32", 9223372036854775807, -1),
            ("int64", "int64", 5, 5),
            ("int64", "uint8", 5, 5),
            ("int64", "uint8", 65535, 255),
            ("int64", "uint8", -1, 255),
            ("int64", "uint16", 5, 5),
            ("int64", "uint16", 4294967295, 65535),
            ("int64", "uint16", -1, 65535),
            ("int64", "uint32", 5, 5),
            ("int64", "uint32", 9223372036854775807, 4294967295),
            ("int64", "uint32", -1, 4294967295),
            ("int64", "uint64", 5, 5),
            ("int64", "uint64", -1, 18446744073709551615),
            ("uint8", "int8", 5, 5),
            ("uint8", "int8", 255, -1),
            ("uint8", "int16", 255, 255),
            ("uint8", "int32", 255, 255),
            ("uint8", "int64", 255, 255),
            ("uint8", "uint8", 5, 5),
            ("uint8", "uint16", 255, 255),
            ("uint8", "uint32", 255, 255),
            ("uint8", "uint64", 255, 255),
            ("uint16", "int8", 5, 5),
            ("uint16", "int8", 65535, -1),
            ("uint16", "int16", 5, 5),
            ("uint16", "int16", 65535, -1),
            ("uint16", "int32", 65535, 65535),
            ("uint16", "int64", 65535, 65535),
            ("uint16", "uint8", 65535, 255),
            ("uint16", "uint16", 65535, 65535),
            ("uint16", "uint32", 65535, 65535),
            ("uint16", "uint64", 65535, 65535),
            ("uint32", "int8", 4, 4),
            ("uint32", "int8", 4294967295, -1),
            ("uint32", "int16", 5, 5),
            ("uint32", "int16", 4294967295, -1),
            ("uint32", "int32", 65535, 65535),
            ("uint32", "int32", 4294967295, -1),
            ("uint32", "int64", 4294967295, 4294967295),
            ("uint32", "uint8", 4, 4),
            ("uint32", "uint8", 65535, 255),
            ("uint32", "uint16", 4294967295, 65535),
            ("uint32", "uint32", 5, 5),
            ("uint32", "uint64", 4294967295, 4294967295),
            ("uint64", "int8", 4, 4),
            ("uint64", "int8", 18446744073709551615, -1),
            ("uint64", "int16", 4, 4),
            ("uint64", "int16", 18446744073709551615, -1),
            ("uint64", "int32", 4, 4),
            ("uint64", "int32", 18446744073709551615, -1),
            ("uint64", "int64", 4, 4),
            ("uint64", "int64", 18446744073709551615, -1),
            ("uint64", "uint8", 5, 5),
            ("uint64", "uint8", 65535, 255),
            ("uint64", "uint16", 4294967295, 65535),
            ("uint64", "uint32", 18446744073709551615, 4294967295),
            ("uint64", "uint64", 5, 5),
        ]

        for src, dest, val, expected in cases:
            codestr = f"""
                from __static__ import {src}, {dest}, box

                def y() -> int:
                    x = {dest}({src}({val}))
                    return box(x)
            """
            with self.subTest(src=src, dest=dest, val=val, expected=expected):
                with self.in_module(codestr) as mod:
                    y = mod.y
                    actual = y()
                    self.assertEqual(
                        actual,
                        expected,
                        f"failing case: {[src, dest, val, actual, expected]}",
                    )

    def test_no_cast_after_box(self):
        codestr = """
            from __static__ import int64, box

            def f(x: int) -> int:
                y = int64(x) + 1
                return box(y)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "CAST")
            self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", (1, TYPED_INT64))
            self.assertEqual(f(3), 4)

    def test_rand(self):
        codestr = """
        from __static__ import rand, RAND_MAX, box, int64

        def test():
            x: int64 = rand()
            return box(x)
        """
        with self.in_module(codestr) as mod:
            test = mod.test
            self.assertEqual(type(test()), int)

    def test_rand_max_inlined(self):
        codestr = """
            from __static__ import rand, RAND_MAX, box, int64

            def f() -> int:
                x: int64 = rand() // int64(RAND_MAX)
                return box(x)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "LOAD_CONST")
            self.assertInBytecode(f, "PRIMITIVE_UNBOX")
            self.assertIsInstance(f(), int)

    def test_rand_max_inlined2(self):
        codestr = """
            from __static__ import rand, RAND_MAX, box, int8, int64

            def f() -> int:
                x: int64 = rand() // int8(RAND_MAX)
                return box(x)
        """
        self.type_error(
            codestr,
            re.escape("type mismatch: Literal[2147483647] cannot be assigned to int8"),
        )

    def test_cbool(self):
        for b in ("True", "False"):
            codestr = f"""
            from __static__ import cbool

            def f() -> int:
                x: cbool = {b}
                if x:
                    return 1
                else:
                    return 2
            """
            with self.subTest(b=b):
                with self.in_module(codestr) as mod:
                    f = mod.f
                    self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST")
                    self.assertInBytecode(
                        f, "STORE_LOCAL", (0, ("__static__", "cbool", "#"))
                    )
                    self.assertInBytecode(f, "POP_JUMP_IF_ZERO")
                    self.assertEqual(f(), 1 if b == "True" else 2)

    def test_cbool_field(self):
        codestr = """
            from __static__ import cbool

            class C:
                def __init__(self, x: cbool) -> None:
                    self.x: cbool = x

            def f(c: C):
                if c.x:
                    return True
                return False
        """
        with self.in_module(codestr) as mod:
            f, C = mod.f, mod.C
            self.assertInBytecode(f, "LOAD_FIELD", (mod.__name__, "C", "x"))
            self.assertInBytecode(f, "POP_JUMP_IF_ZERO")
            self.assertIs(C(True).x, True)
            self.assertIs(C(False).x, False)
            self.assertIs(f(C(True)), True)
            self.assertIs(f(C(False)), False)

    def test_cbool_cast(self):
        codestr = """
        from __static__ import cbool

        def f(y: bool) -> int:
            x: cbool = y
            if x:
                return 1
            else:
                return 2
        """
        with self.assertRaisesRegex(TypedSyntaxError, type_mismatch("bool", "cbool")):
            self.compile(codestr, modname="foo")

    def test_primitive_compare_returns_cbool(self):
        codestr = """
            from __static__ import cbool, int64

            def f(x: int64, y: int64) -> cbool:
                return x == y
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertIs(f(1, 1), True)
            self.assertIs(f(1, 2), False)

    def test_no_cbool_math(self):
        codestr = """
            from __static__ import cbool

            def f(x: cbool, y: cbool) -> cbool:
                return x + y
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "cbool is not a valid operand type for add"
        ):
            self.compile(codestr)

    def test_primitive_stack_spill(self):
        # Create enough locals that some must get spilled to stack, to test
        # shuffling stack-spilled values across basic block transitions, and
        # field reads/writes with stack-spilled values. These can create
        # mem->mem moves that otherwise wouldn't exist, and trigger issues
        # like push/pop not supporting 8 or 32 bits on x64.
        varnames = string.ascii_lowercase[:20]
        sizes = ["uint8", "int16", "int32", "int64"]
        for size in sizes:
            indent = " " * 20
            attrs = f"\n{indent}".join(f"{var}: {size}" for var in varnames)
            inits = f"\n{indent}".join(
                f"{var}: {size} = {val}" for val, var in enumerate(varnames)
            )
            assigns = f"\n{indent}".join(f"val.{var} = {var}" for var in varnames)
            reads = f"\n{indent}".join(f"{var} = val.{var}" for var in varnames)
            indent = " " * 24
            incrs = f"\n{indent}".join(f"{var} += 1" for var in varnames)
            codestr = f"""
                from __static__ import {size}

                class C:
                    {attrs}

                def f(val: C, flag: {size}) -> {size}:
                    {inits}
                    if flag:
                        {incrs}
                    {assigns}
                    {reads}
                    return {' + '.join(varnames)}
            """
            with self.subTest(size=size):
                with self.in_module(codestr) as mod:
                    f, C = mod.f, mod.C
                    c = C()
                    self.assertEqual(f(c, 0), sum(range(len(varnames))))
                    for val, var in enumerate(varnames):
                        self.assertEqual(getattr(c, var), val)

                    c = C()
                    self.assertEqual(f(c, 1), sum(range(len(varnames) + 1)))
                    for val, var in enumerate(varnames):
                        self.assertEqual(getattr(c, var), val + 1)

    def test_primitive_with_weakref(self):
        codestr = """
            from __static__ import cbool
            from typing import Any

            class C:
                __weakref__: Any
                def __init__(self):
                    self.foo: cbool = False
                    self.bar: cbool = False

            a = C()
        """
        with self.in_module(codestr) as mod:
            # target_size includes 2 words for GC header, 1 for weak_ref, and 1 for the
            # two bools which are 1 byte each.
            target_size = self.base_size + self.ptr_size * 4

            self.assertEqual(sys.getsizeof(mod.a), target_size)

            was_called = False

            def called(x):
                nonlocal was_called

                was_called = True

            ref = weakref.ref(mod.a, called)
            self.assertEqual(ref(), mod.a)
            del mod.a
            self.assertTrue(was_called)

    def test_primitive_with_dict(self):
        codestr = """
            from __static__ import cbool
            from typing import Any

            class C:
                __dict__: Any
                def __init__(self):
                    self.foo: cbool = False
                    self.bar: cbool = False

            a = C()
        """
        with self.in_module(codestr) as mod:
            # target_size includes 2 words for GC header, 1 for __dict__, and 1 for the bools
            target_size = self.base_size + self.ptr_size * 4

            self.assertEqual(sys.getsizeof(mod.a), target_size)

    def test_unbox_overflow(self):
        cases = [
            (
                "uint8",
                [
                    (-1, OverflowError),
                    (0, 0),
                    (255, 255),
                    (256, OverflowError),
                ],
            ),
            (
                "int8",
                [
                    (-129, OverflowError),
                    (-128, -128),
                    (127, 127),
                    (128, OverflowError),
                ],
            ),
            (
                "uint16",
                [
                    (-1, OverflowError),
                    (0, 0),
                    ((1 << 16) - 1, (1 << 16) - 1),
                    ((1 << 16), OverflowError),
                ],
            ),
            (
                "int16",
                [
                    (-(1 << 15) - 1, OverflowError),
                    (-(1 << 15), -(1 << 15)),
                    ((1 << 15) - 1, (1 << 15) - 1),
                    ((1 << 15), OverflowError),
                ],
            ),
            (
                "uint32",
                [
                    (-1, OverflowError),
                    (0, 0),
                    ((1 << 32) - 1, (1 << 32) - 1),
                    ((1 << 32), OverflowError),
                ],
            ),
            (
                "int32",
                [
                    (-(1 << 31) - 1, OverflowError),
                    (-(1 << 31), -(1 << 31)),
                    ((1 << 31) - 1, (1 << 31) - 1),
                    ((1 << 31), OverflowError),
                ],
            ),
            (
                "uint64",
                [
                    (-1, OverflowError),
                    (0, 0),
                    ((1 << 64) - 1, (1 << 64) - 1),
                    ((1 << 64), OverflowError),
                ],
            ),
            (
                "int64",
                [
                    (-(1 << 63) - 1, OverflowError),
                    (-(1 << 63), -(1 << 63)),
                    ((1 << 63) - 1, (1 << 63) - 1),
                    ((1 << 63), OverflowError),
                ],
            ),
        ]
        for typ, values in cases:
            codestr = f"""
                from __static__ import {typ}

                def f(x: int) -> {typ}:
                    return {typ}(x)
            """
            with self.in_module(codestr) as mod:
                f = mod.f
                for val, result in values:
                    with self.subTest(typ=typ, val=val, result=result):
                        if result is OverflowError:
                            with self.assertRaises(result):
                                f(val)
                        else:
                            self.assertEqual(f(val), val)

    def test_emits_convert_primitive_while_boxing(self):
        codestr = """
        import __static__
        from __static__ import rand, RAND_MAX, box, int64
        def f():
            x: int64 = rand()
            return box(x)
        """
        with self.in_module(codestr) as mod:
            self.assertInBytecode(mod.f, "CONVERT_PRIMITIVE")

    def test_overflow_while_unboxing(self):
        codestr = """
        from __static__ import int8, int64, box, unbox

        def f(x: int64) -> int8:
            y: int8 = unbox(box(x))
            return y
        """
        with self.in_module(codestr) as mod:
            with self.assertRaises(OverflowError):
                mod.f(128)

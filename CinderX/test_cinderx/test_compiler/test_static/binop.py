from compiler.errors import TypedSyntaxError
from compiler.static.types import (
    PRIM_OP_ADD_INT,
    PRIM_OP_DIV_INT,
    TYPED_INT16,
    TYPED_INT8,
)
from unittest import skip

from .common import StaticTestBase

try:
    import cinderjit
except ImportError:
    cinderjit = None


class BinopTests(StaticTestBase):
    def test_pow_of_int64s_returns_double(self):
        codestr = """
        from __static__ import int64
        def foo():
            x: int64 = 0
            y: int64 = 1
            z: int64 = x ** y
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "double cannot be assigned to int64"
        ):
            self.compile(codestr, modname="foo")

    def test_int_binop(self):
        tests = [
            ("int8", 1, 2, "/", 0),
            ("int8", 4, 2, "/", 2),
            ("int8", 4, -2, "/", -2),
            ("uint8", 0xFF, 0x7F, "/", 2),
            ("int16", 4, -2, "/", -2),
            ("uint16", 0xFF, 0x7F, "/", 2),
            ("uint32", 0xFFFF, 0x7FFF, "/", 2),
            ("int32", 4, -2, "/", -2),
            ("uint32", 0xFF, 0x7F, "/", 2),
            ("uint32", 0xFFFFFFFF, 0x7FFFFFFF, "/", 2),
            ("int64", 4, -2, "/", -2),
            ("uint64", 0xFF, 0x7F, "/", 2),
            ("uint64", 0xFFFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFFFF, "/", 2),
            ("int8", 1, -2, "-", 3),
            ("int8", 1, 2, "-", -1),
            ("int16", 1, -2, "-", 3),
            ("int16", 1, 2, "-", -1),
            ("int32", 1, -2, "-", 3),
            ("int32", 1, 2, "-", -1),
            ("int64", 1, -2, "-", 3),
            ("int64", 1, 2, "-", -1),
            ("int8", 1, -2, "*", -2),
            ("int8", 1, 2, "*", 2),
            ("int16", 1, -2, "*", -2),
            ("int16", 1, 2, "*", 2),
            ("int32", 1, -2, "*", -2),
            ("int32", 1, 2, "*", 2),
            ("int64", 1, -2, "*", -2),
            ("int64", 1, 2, "*", 2),
            ("int8", 1, -2, "&", 0),
            ("int8", 1, 3, "&", 1),
            ("int16", 1, 3, "&", 1),
            ("int16", 1, 3, "&", 1),
            ("int32", 1, 3, "&", 1),
            ("int32", 1, 3, "&", 1),
            ("int64", 1, 3, "&", 1),
            ("int64", 1, 3, "&", 1),
            ("int8", 1, 2, "|", 3),
            ("uint8", 1, 2, "|", 3),
            ("int16", 1, 2, "|", 3),
            ("uint16", 1, 2, "|", 3),
            ("int32", 1, 2, "|", 3),
            ("uint32", 1, 2, "|", 3),
            ("int64", 1, 2, "|", 3),
            ("uint64", 1, 2, "|", 3),
            ("int8", 1, 3, "^", 2),
            ("uint8", 1, 3, "^", 2),
            ("int16", 1, 3, "^", 2),
            ("uint16", 1, 3, "^", 2),
            ("int32", 1, 3, "^", 2),
            ("uint32", 1, 3, "^", 2),
            ("int64", 1, 3, "^", 2),
            ("uint64", 1, 3, "^", 2),
            ("int8", 1, 3, "%", 1),
            ("uint8", 1, 3, "%", 1),
            ("int16", 1, 3, "%", 1),
            ("uint16", 1, 3, "%", 1),
            ("int32", 1, 3, "%", 1),
            ("uint32", 1, 3, "%", 1),
            ("int64", 1, 3, "%", 1),
            ("uint64", 1, 3, "%", 1),
            ("int8", 1, -3, "%", 1),
            ("uint8", 1, 0xFF, "%", 1),
            ("int16", 1, -3, "%", 1),
            ("uint16", 1, 0xFFFF, "%", 1),
            ("int32", 1, -3, "%", 1),
            ("uint32", 1, 0xFFFFFFFF, "%", 1),
            ("int64", 1, -3, "%", 1),
            ("uint64", 1, 0xFFFFFFFFFFFFFFFF, "%", 1),
            ("int8", 1, 2, "<<", 4),
            ("uint8", 1, 2, "<<", 4),
            ("int16", 1, 2, "<<", 4),
            ("uint16", 1, 2, "<<", 4),
            ("int32", 1, 2, "<<", 4),
            ("uint32", 1, 2, "<<", 4),
            ("int64", 1, 2, "<<", 4),
            ("uint64", 1, 2, "<<", 4),
            ("int8", 4, 1, ">>", 2),
            ("int8", -1, 1, ">>", -1),
            ("uint8", 0xFF, 1, ">>", 127),
            ("int16", 4, 1, ">>", 2),
            ("int16", -1, 1, ">>", -1),
            ("uint16", 0xFFFF, 1, ">>", 32767),
            ("int32", 4, 1, ">>", 2),
            ("int32", -1, 1, ">>", -1),
            ("uint32", 0xFFFFFFFF, 1, ">>", 2147483647),
            ("int64", 4, 1, ">>", 2),
            ("int64", -1, 1, ">>", -1),
            ("uint64", 0xFFFFFFFFFFFFFFFF, 1, ">>", 9223372036854775807),
            ("int64", 2, 2, "**", 4.0, "double"),
            ("int16", -1, 1, "**", -1, "double"),
            ("int32", -1, 1, "**", -1, "double"),
            ("int64", -1, 1, "**", -1, "double"),
            ("int64", -2, -3, "**", -0.125, "double"),
            ("uint8", 0xFF, 2, "**", float(0xFF * 0xFF), "double"),
            ("uint16", 0xFFFF, 2, "**", float(0xFFFF * 0xFFFF), "double"),
            ("uint32", 0xFFFFFFFF, 2, "**", float(0xFFFFFFFF * 0xFFFFFFFF), "double"),
            (
                "uint64",
                0xFFFFFFFFFFFFFFFF,
                1,
                "**",
                float(0xFFFFFFFFFFFFFFFF),
                "double",
            ),
        ]
        for type, x, y, op, res, *output_type_option in tests:
            if len(output_type_option) == 0:
                output_type = type
            else:
                output_type = output_type_option[0]
            codestr = f"""
            from __static__ import {type}, box
            from __static__ import {output_type}
            def testfunc(tst):
                x: {type} = {x}
                y: {type} = {y}
                if tst:
                    x = x + 1
                    y = y + 2

                z: {output_type} = x {op} y
                return box(z), box(x {op} y)
            """
            with self.subTest(type=type, x=x, y=y, op=op, res=res):
                with self.in_module(codestr) as mod:
                    f = mod.testfunc
                    self.assertEqual(
                        f(False), (res, res), f"{type} {x} {op} {y} {res} {output_type}"
                    )

    def test_primitive_arithmetic(self):
        cases = [
            ("int8", 127, "*", 1, 127),
            ("int8", -64, "*", 2, -128),
            ("int8", 0, "*", 4, 0),
            ("uint8", 51, "*", 5, 255),
            ("uint8", 5, "*", 0, 0),
            ("int16", 3123, "*", -10, -31230),
            ("int16", -32767, "*", -1, 32767),
            ("int16", -32768, "*", 1, -32768),
            ("int16", 3, "*", 0, 0),
            ("uint16", 65535, "*", 1, 65535),
            ("uint16", 0, "*", 4, 0),
            ("int32", (1 << 31) - 1, "*", 1, (1 << 31) - 1),
            ("int32", -(1 << 30), "*", 2, -(1 << 31)),
            ("int32", 0, "*", 1, 0),
            ("uint32", (1 << 32) - 1, "*", 1, (1 << 32) - 1),
            ("uint32", 0, "*", 4, 0),
            ("int64", (1 << 63) - 1, "*", 1, (1 << 63) - 1),
            ("int64", -(1 << 62), "*", 2, -(1 << 63)),
            ("int64", 0, "*", 1, 0),
            ("uint64", (1 << 64) - 1, "*", 1, (1 << 64) - 1),
            ("uint64", 0, "*", 4, 0),
            ("int8", 127, "//", 4, 31),
            ("int8", -128, "//", 4, -32),
            ("int8", 0, "//", 4, 0),
            ("uint8", 255, "//", 5, 51),
            ("uint8", 0, "//", 5, 0),
            ("int16", 32767, "//", -1000, -32),
            ("int16", -32768, "//", -1000, 32),
            ("int16", 0, "//", 4, 0),
            ("uint16", 65535, "//", 5, 13107),
            ("uint16", 0, "//", 4, 0),
            ("int32", (1 << 31) - 1, "//", (1 << 31) - 1, 1),
            ("int32", -(1 << 31), "//", 1, -(1 << 31)),
            ("int32", 0, "//", 1, 0),
            ("uint32", (1 << 32) - 1, "//", 500, 8589934),
            ("uint32", 0, "//", 4, 0),
            ("int64", (1 << 63) - 1, "//", 2, (1 << 62) - 1),
            ("int64", -(1 << 63), "//", 2, -(1 << 62)),
            ("int64", 0, "//", 1, 0),
            ("uint64", (1 << 64) - 1, "//", (1 << 64) - 1, 1),
            ("uint64", 0, "//", 4, 0),
            ("int8", 127, "%", 4, 3),
            ("int8", -128, "%", 4, 0),
            ("int8", 0, "%", 4, 0),
            ("uint8", 255, "%", 6, 3),
            ("uint8", 0, "%", 5, 0),
            ("int16", 32767, "%", -1000, 767),
            ("int16", -32768, "%", -1000, -768),
            ("int16", 0, "%", 4, 0),
            ("uint16", 65535, "%", 7, 1),
            ("uint16", 0, "%", 4, 0),
            ("int32", (1 << 31) - 1, "%", (1 << 31) - 1, 0),
            ("int32", -(1 << 31), "%", 1, 0),
            ("int32", 0, "%", 1, 0),
            ("uint32", (1 << 32) - 1, "%", 500, 295),
            ("uint32", 0, "%", 4, 0),
            ("int64", (1 << 63) - 1, "%", 2, 1),
            ("int64", -(1 << 63), "%", 2, 0),
            ("int64", 0, "%", 1, 0),
            ("uint64", (1 << 64) - 1, "%", (1 << 64) - 1, 0),
            ("uint64", 0, "%", 4, 0),
        ]
        for typ, a, op, b, res in cases:
            for const in ["noconst", "constfirst", "constsecond"]:
                if const == "noconst":
                    codestr = f"""
                        from __static__ import {typ}

                        def f(a: {typ}, b: {typ}) -> {typ}:
                            return a {op} b
                    """
                elif const == "constfirst":
                    codestr = f"""
                        from __static__ import {typ}

                        def f(b: {typ}) -> {typ}:
                            return {a} {op} b
                    """
                elif const == "constsecond":
                    codestr = f"""
                        from __static__ import {typ}

                        def f(a: {typ}) -> {typ}:
                            return a {op} {b}
                    """

                with self.subTest(typ=typ, a=a, op=op, b=b, res=res, const=const):
                    with self.in_module(codestr) as mod:
                        f = mod.f
                        act = None
                        if const == "noconst":
                            act = f(a, b)
                        elif const == "constfirst":
                            act = f(b)
                        elif const == "constsecond":
                            act = f(a)
                        self.assertEqual(act, res)

    def test_int_binop_type_context(self):
        codestr = f"""
            from __static__ import box, int8, int16

            def f(x: int8, y: int8) -> int:
                z: int16 = x * y
                return box(z)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(
                f, "CONVERT_PRIMITIVE", TYPED_INT8 | (TYPED_INT16 << 4)
            )
            self.assertEqual(f(120, 120), 14400)

    def test_mixed_binop(self):
        with self.assertRaisesRegex(
            TypedSyntaxError, "cannot add int64 and Literal\\[1\\]"
        ):
            self.bind_module(
                """
                from __static__ import ssize_t

                def f():
                    x: ssize_t = 1
                    y = 1
                    x + y
            """
            )

        with self.assertRaisesRegex(
            TypedSyntaxError, "cannot add Literal\\[1\\] and int64"
        ):
            self.bind_module(
                """
                from __static__ import ssize_t

                def f():
                    x: ssize_t = 1
                    y = 1
                    y + x
            """
            )

    def test_mixed_binop_okay(self):
        codestr = """
            from __static__ import ssize_t, box

            def f():
                x: ssize_t = 1
                y = x + 1
                return box(y)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 2)

    def test_mixed_binop_okay_1(self):
        codestr = """
            from __static__ import ssize_t, box

            def f():
                x: ssize_t = 1
                y = 1 + x
                return box(y)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 2)

    def test_inferred_primitive_type(self):
        codestr = """
        from __static__ import ssize_t, box

        def f():
            x: ssize_t = 1
            y = x
            return box(y)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 1)

    def test_mixed_binop_sign(self):
        """mixed signed/unsigned ops should be promoted to signed"""
        codestr = """
            from __static__ import int8, uint8, box
            def testfunc():
                x: uint8 = 42
                y: int8 = 2
                return box(x / y)
        """
        code = self.compile(codestr)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_DIV_INT)
        with self.in_module(codestr) as mod:
            f = mod.testfunc
            self.assertEqual(f(), 21)

        codestr = """
            from __static__ import int8, uint8, box
            def testfunc():
                x: int8 = 42
                y: uint8 = 2
                return box(x / y)
        """
        code = self.compile(codestr)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_DIV_INT)
        with self.in_module(codestr) as mod:
            f = mod.testfunc
            self.assertEqual(f(), 21)

        codestr = """
            from __static__ import uint32, box
            def testfunc():
                x: uint32 = 2
                a = box(x / -2)
                return box(x ** -2)
        """
        with self.in_module(codestr) as mod:
            f = mod.testfunc
            self.assertEqual(f(), 0.25)

        codestr = """
            from __static__ import int32, box
            def testfunc():
                x: int32 = 2
                return box(x ** -2)
        """
        with self.in_module(codestr) as mod:
            f = mod.testfunc
            self.assertEqual(f(), 0.25)

        codestr = """
            from __static__ import uint32, box
            def testfunc():
                x: uint32 = 2
                return box(x ** -2)
        """
        with self.in_module(codestr) as mod:
            f = mod.testfunc
            self.assertEqual(f(), 0.25)

        codestr = """
            from __static__ import int8, uint8, box
            def testfunc():
                x: int8 = 4
                y: uint8 = 2
                return box(x ** y)
        """
        with self.assertRaisesRegex(TypedSyntaxError, "cannot pow int8 and uint8"):
            self.compile(codestr)

        codestr = """
            from __static__ import int8, uint8, box
            def testfunc():
                x: uint8 = 2
                y: int8 = -3
                return box(x ** y)
        """
        with self.assertRaisesRegex(TypedSyntaxError, "cannot pow uint8 and int8"):
            self.compile(codestr)

        codestr = """
            from __static__ import uint8, box, double
            def testfunc():
                x: uint8 = 2
                y: double = -3.0
                return box(x ** y)
        """
        with self.assertRaisesRegex(TypedSyntaxError, "cannot pow uint8 and double"):
            self.compile(codestr)

    def test_double_binop(self):
        tests = [
            (1.732, 2.0, "+", 3.732),
            (1.732, 2.0, "-", -0.268),
            (1.732, 2.0, "/", 0.866),
            (1.732, 2.0, "*", 3.464),
            (1.732, 2, "+", 3.732),
            (2.5, 2, "**", 6.25),
            (2.5, 2.5, "**", 9.882117688026186),
        ]

        if cinderjit is not None:
            # test for division by zero
            tests.append((1.732, 0.0, "/", float("inf")))

        for x, y, op, res in tests:
            codestr = f"""
            from __static__ import double, box
            def testfunc(tst):
                x: double = {x}
                y: double = {y}

                z: double = x {op} y
                return box(z)
            """
            with self.subTest(type=type, x=x, y=y, op=op, res=res):
                with self.in_module(codestr) as mod:
                    f = mod.testfunc
                    self.assertEqual(f(False), res, f"{type} {x} {op} {y} {res}")

    def test_double_sub_with_reg_pressure(self):
        """
        Test the behavior of double subtraction under register pressure:
        we had one bug where a rewrite rule inserted an invalid instruction,
        and another where the register allocator didn't keep all inputs to the
        Fsub instruction alive long enough.
        """

        codestr = f"""
        from __static__ import box, double

        def testfunc(f0: double, f1: double) -> double:
            f2 = f0 + f1
            f3 = f1 + f2
            f4 = f2 + f3
            f5 = f3 + f4
            f6 = f4 + f5
            f7 = f5 + f6
            f8 = f6 + f7
            f9 = f7 + f8
            f10 = f8 + f9
            f11 = f9 + f10
            f12 = f10 + f11
            f13 = f11 + f12
            f14 = f12 + f13
            f15 = f13 + f14
            f16 = f1 - f0
            return (
                f1
                + f2
                + f3
                + f4
                + f5
                + f6
                + f7
                + f8
                + f9
                + f10
                + f11
                + f12
                + f13
                + f14
                + f15
                + f16
            )
        """

        with self.in_module(codestr) as mod:
            f = mod.testfunc
            self.assertEqual(f(1.0, 2.0), 4179.0)

    def test_double_binop_with_literal(self):
        codestr = f"""
            from __static__ import double, unbox

            def f():
                y: double = 1.2
                y + 1.0
        """
        f = self.run_code(codestr)["f"]
        f()

    def test_subclass_binop(self):
        codestr = """
            class C: pass
            class D(C): pass

            def f(x: C, y: D):
                return x + y
        """
        code = self.compile(codestr, modname="foo")
        f = self.find_code(code, "f")
        self.assertInBytecode(f, "BINARY_ADD")

    def test_mixed_add_reversed(self):
        codestr = """
            from __static__ import int8, uint8, int64, box, int16
            def testfunc(tst=False):
                x: int8 = 42
                y: int16 = 2
                if tst:
                    x += 1
                    y += 1

                return box(y + x)
        """
        code = self.compile(codestr)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        with self.in_module(codestr) as mod:
            f = mod.testfunc
            self.assertEqual(f(), 44)

    def test_mixed_tri_add(self):
        codestr = """
            from __static__ import int8, uint8, int64, box
            def testfunc(tst=False):
                x: uint8 = 42
                y: int8 = 2
                z: int64 = 3
                if tst:
                    x += 1
                    y += 1

                return box(x + y + z)
        """
        code = self.compile(codestr)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        with self.in_module(codestr) as mod:
            f = mod.testfunc
            self.assertEqual(f(), 47)

    def test_mixed_tri_add_unsigned(self):
        """promote int/uint to int, can't add to uint64"""

        codestr = """
            from __static__ import int8, uint8, uint64, box
            def testfunc(tst=False):
                x: uint8 = 42
                y: int8 = 2
                z: uint64 = 3

                return box(x + y + z)
        """

        with self.assertRaisesRegex(TypedSyntaxError, "cannot add int16 and uint64"):
            self.compile(codestr)

    def test_literal_int_binop_inferred_type(self):
        """primitive literal doesn't wrongly carry through arithmetic"""
        for rev in [False, True]:
            with self.subTest(rev=rev):
                op = "1 + x" if rev else "x + 1"
                codestr = f"""
                    from __static__ import int64

                    def f(x: int64):
                        reveal_type({op})
                """
                self.type_error(codestr, "'int64'", f"reveal_type({op})")

    def test_error_type_ctx_left_operand_mismatch(self):
        codestr = f"""
            from __static__ import int64

            def f(k: int64):
                l = [1, 2, 3]
                # slices cannot be primitives, so this is invalid
                l[:k + 1] = [0]
                return l
        """
        self.type_error(codestr, "int64 cannot be assigned to dynamic", f"k + 1")

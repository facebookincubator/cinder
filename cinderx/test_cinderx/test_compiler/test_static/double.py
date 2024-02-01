from __static__ import TYPED_DOUBLE

import re
from unittest import skip, skipIf

from cinderx.compiler.errors import TypedSyntaxError

from .common import StaticTestBase

try:
    import cinderjit
except ImportError:
    cinderjit = None


class DoubleTests(StaticTestBase):
    def test_primitive_double_return_bad_call(self):
        codestr = """
        from __static__ import double

        def fn(x: float, y: float) -> double:
            i = double(x)
            j = double(y)
            return i + j
        """
        with self.in_module(codestr) as mod:
            fn = mod.fn
            with self.assertRaisesRegex(
                TypeError,
                re.escape("fn() missing 2 required positional arguments: 'x' and 'y'"),
            ):
                fn()  # bad call

    def test_double_return(self):
        codestr = """
        from __static__ import double

        def fn() -> double:
            return double(3.14159)
        """
        with self.in_module(codestr) as mod:
            fn = mod.fn
            r = fn()
            self.assertEqual(r, 3.14159)

    def test_double_return_static_passthrough(self):
        codestr = """
            from __static__ import double

            def g() -> double:
                return 42.0

            def f() -> double:
                return g()
        """
        with self.in_strict_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 42.0)

    def test_double_return_raises(self):
        codestr = """
            from __static__ import double

            def g() -> double:
                raise ValueError('no way')

            def f() -> double:
                return g()
        """
        with self.in_strict_module(codestr) as mod:
            f = mod.f
            with self.assertRaises(ValueError):
                f()

    def test_double_return_static(self):
        codestr = """
        from __static__ import double, box

        def fn() -> double:
            return double(3.14159)

        def lol():
            return box(fn()) + 1.0
        """
        with self.in_module(codestr) as mod:
            lol = mod.lol
            r = lol()
            self.assertEqual(r, 4.14159)

    def test_double_return_2(self):
        codestr = """
        from __static__ import double

        def fn(x: float, y: float) -> double:
            i = double(x)
            j = double(y)
            return i + j
        """
        with self.in_module(codestr) as mod:
            fn = mod.fn
            r = fn(1.2, 2.3)
            self.assertEqual(r, 3.5)

    def test_double_return_with_default_args(self):
        codestr = """
        from __static__ import double

        def fn(x: float, y: float = 3.2) -> double:
            i = double(x)
            j = double(y)
            return i + j
        """
        with self.in_module(codestr) as mod:
            fn = mod.fn
            r = fn(1.2)
            self.assertEqual(r, 4.4)

    def test_double_unbox(self):
        codestr = f"""
        from __static__ import double, box, unbox
        def fn(x, y):
            a: double = unbox(x)
            b: double = unbox(y)
            return box(a + b)
        """

        f = self.run_code(codestr)["fn"]
        x = 3.14
        y = 1.732
        self.assertEqual(f(x, y), x + y)

    def test_double_unbox_using_double(self):
        codestr = f"""
            from __static__ import double, box

            def f():
                x = 1.2
                y = double(x)
                return box(y + 1.0)
        """
        f = self.run_code(codestr)["f"]
        self.assertEqual(f(), 2.2)

    def test_uninit_try(self):
        codestr = """
            from __static__ import double, box

            def f():
                x0: double
                try:
                    x0 = 42
                except:
                    pass
                return box(x0)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", ((0.0, TYPED_DOUBLE)))
            self.assertEqual(f(), 42.0)

    def test_uninit_except(self):
        codestr = """
            from __static__ import double, box

            def f():
                x0: double
                try:
                    pass
                except:
                    x0 = 42
                return box(x0)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", ((0.0, TYPED_DOUBLE)))
            self.assertEqual(f(), 0.0)

    def test_uninit_except_else(self):
        codestr = """
            from __static__ import double, box

            def f():
                x0: double
                try:
                    pass
                except:
                    pass
                else:
                    x0 = 42
                return box(x0)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", ((0.0, TYPED_DOUBLE)))
            self.assertEqual(f(), 42.0)

    def test_uninit_finally(self):
        codestr = """
            from __static__ import double, box

            def f():
                x0: double
                try:
                    pass
                finally:
                    x0 = 42
                return box(x0)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "PRIMITIVE_LOAD_CONST", ((0.0, TYPED_DOUBLE)))
            self.assertEqual(f(), 42.0)

    def test_uninit_with(self):
        codestr = """
            from __static__ import double, box
            from contextlib import nullcontext

            def f():
                x0: double
                with nullcontext():
                    x0 = 42.0
                return box(x0)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", ((0.0, TYPED_DOUBLE)))
            self.assertEqual(f(), 42.0)

    def test_uninit_for(self):
        codestr = """
            from __static__ import double, box

            def f(x):
                x0: double
                for i in x:
                    x0 = 42.0
                return box(x0)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", ((0.0, TYPED_DOUBLE)))
            self.assertEqual(f([1]), 42.0)
            self.assertEqual(f([]), 0.0)

    def test_uninit_for_else(self):
        codestr = """
            from __static__ import double, box

            def f(x):
                x0: double
                for i in x:
                    if i:
                        break
                else:
                    x0 = 42.0
                return box(x0)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", ((0.0, TYPED_DOUBLE)))
            self.assertEqual(f([]), 42.0)
            self.assertEqual(f([1]), 0.0)

    def test_uninit_if(self):
        codestr = """
            from __static__ import double, box

            def f(x):
                x0: double
                if x:
                    x0 = 42
                return box(x0)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", ((0.0, TYPED_DOUBLE)))
            self.assertEqual(f(True), 42.0)
            self.assertEqual(f(False), 0.0)

    def test_uninit_if_else(self):
        codestr = """
            from __static__ import double, box

            def f(x):
                x0: double
                if x:
                    pass
                else:
                    x0 = 42
                return box(x0)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", ((0.0, TYPED_DOUBLE)))
            self.assertEqual(f(False), 42.0)
            self.assertEqual(f(True), 0.0)

    def test_uninit_if_else_both_assign(self):
        codestr = """
            from __static__ import double, box

            def f(x):
                x0: double
                if x:
                    x0 = 42
                else:
                    x0 = 100
                return box(x0)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "PRIMITIVE_LOAD_CONST", ((0.0, TYPED_DOUBLE)))
            self.assertEqual(f(True), 42.0)
            self.assertEqual(f(False), 100.0)

    def test_uninit_while(self):
        codestr = """
            from __static__ import double, box

            def f(x):
                x0: double
                while x:
                    x0 = 42
                return box(x0)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", ((0.0, TYPED_DOUBLE)))
            self.assertEqual(f(False), 0.0)

            class C:
                def __init__(self):
                    self.count = 2

                def __bool__(self):
                    if self.count:
                        self.count -= 1
                    return self.count != 0

            self.assertEqual(f(C()), 42.0)

    def test_uninit_while_else(self):

        codestr = """
            from __static__ import double, box

            def f(x):
                x0: double
                while x:
                    if x:
                        break
                else:
                    x0 = 42
                return box(x0)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", ((0.0, TYPED_DOUBLE)))
            self.assertEqual(f(False), 42.0)

            class C:
                def __init__(self):

                    self.count = 3

                def __bool__(self):

                    if self.count:
                        self.count -= 1

                    return self.count != 0

            self.assertEqual(f(C()), 0.0)

    def test_double_unary(self):
        tests = [
            ("-", 1.0, -1.0),
            ("+", 1.0, 1.0),
            ("-", -1.0, 1.0),
        ]
        for op, x, res in tests:
            codestr = f"""
            from __static__ import double, box
            def testfunc(tst):
                x: double = {x}
                if tst:
                    x = x + 1
                x = {op}x
                return box(x)
            """
            with self.subTest(type=type, op=op, x=x, res=res):
                f = self.run_code(codestr)["testfunc"]
                self.assertEqual(f(False), res, f"{type} {op} {x} {res}")

    def test_double_unary_unsupported(self):
        codestr = f"""
        from __static__ import double, box
        def testfunc(tst):
            x: double = 1.0
            if tst:
                x = x + 1
            x = ~x
            return box(x)
        """
        with self.assertRaisesRegex(TypedSyntaxError, "Cannot invert/not a double"):
            self.compile(codestr)

    def test_uninit_augassign(self):
        codestr = """
            from __static__ import double

            def f(b: double = 0.0) -> double:
                x0: double
                for i in range(2):
                    x0 += b

                return x0
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", ((0.0, TYPED_DOUBLE)))
            self.assertEqual(f(2.0), 4.0)

    def test_float_field(self):
        codestr = """
            from __static__ import double, box

            class C:
                def __init__(self):
                    self.x: double = 42.1

                def get_x(self):
                    return box(self.x)

                def set_x(self, val):
                    self.x = double(val)
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            val = C()
            self.assertEqual(val.x, 42.1)
            self.assertEqual(type(val.x), float)

            self.assertEqual(val.get_x(), 42.1)
            self.assertEqual(type(val.get_x()), float)

            val.set_x(100.0)
            self.assertEqual(val.x, 100.0)
            self.assertEqual(type(val.x), float)

    def test_double_function(self):
        codestr = """
            from __static__ import double, box

            def f(x: double):
                return box(x)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(42.1), 42.1)

    def test_primitive_float_arg_not_clobbered(self):
        codestr = """
            from __static__ import double

            def g(x: double) -> double:
                return x

            def f() -> double:
                return g(42.0)
        """
        with self.in_strict_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 42.0)

    def test_double_function_with_obj(self):
        codestr = """
            from __static__ import double, box

            def f(obj, x: double):
                return box(x) / obj
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(0.1, 42.1), 421.0)

    def test_double_function_with_obj_reversed(self):
        codestr = """
            from __static__ import double, box

            def f(x: double, obj):
                return box(x) / obj
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(42.1, 0.1), 421.0)

    def test_all_arg_regs_used_plus_one(self):
        codestr = """
            from __static__ import double

            def g(x0: double, x1: double, x2: double, x3: double, x4: double,
                  x5: double, x6: double, x7: double,
                  y0: int) -> double:
                return x0 + x1 + x2 + x3 + x4 + x5 + x6 + x7 + double(y0 + y0)

            def f() -> double:
                return g(0, 1, 2, 3, 4, 5, 6, 7, 8)
        """
        with self.in_strict_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 44.0)

    def test_primitive_float_arg_not_clobbered_all(self):
        codestr = """
            from __static__ import double

            def g(x0: double, x1: double, x2: double, x3: double, x4: double, x5: double, x6: double, x7: double) -> double:
                return x0 + x1 + x2 + x3 + x4 + x5 + x6 + x7

            def f() -> double:
                return g(0, 1, 2, 3, 4, 5, 6, 7)
        """
        with self.in_strict_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 28.0)

    def test_primitive_float_arg_not_clobbered_all_offset(self):
        codestr = """
            from __static__ import double

            def g(unused, x0: double, x1: double, x2: double, x3: double, x4: double, x5: double, x6: double, x7: double) -> double:
                return x0 + x1 + x2 + x3 + x4 + x5 + x6 + x7

            def f() -> double:
                return g(None, 0, 1, 2, 3, 4, 5, 6, 7)
        """
        with self.in_strict_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 28.0)

    def test_primitive_float_arg_not_clobbered_all_plus_one(self):
        codestr = """
            from __static__ import double

            def g(x0: double, x1: double, x2: double, x3: double, x4: double, x5: double, x6: double, x7: double, x8: double) -> double:
                return x0 + x1 + x2 + x3 + x4 + x5 + x6 + x7 + x8

            def f() -> double:
                return g(0, 1, 2, 3, 4, 5, 6, 7, 8)
        """
        with self.in_strict_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 36.0)

    def test_double_augassign(self) -> None:
        codestr = """
            from __static__ import box, double

            def f() -> float:
                x: double = 3
                x /= 2
                return box(x)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "LOAD_FAST")
            self.assertNotInBytecode(f, "STORE_FAST")
            self.assertEqual(f(), 1.5)

    def test_double_compare(self):
        tests = [
            (1.0, 2.0, "==", False),
            (1.0, 2.0, "!=", True),
            (1.0, 2.0, "<", True),
            (1.0, 2.0, "<=", True),
            (2.0, 1.0, "<", False),
            (2.0, 1.0, "<=", False),
        ]
        for x, y, op, res in tests:
            codestr = f"""
            from __static__ import double, box
            def testfunc(tst):
                x: double = {x}
                y: double = {y}
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

    def test_double_compare_with_literal(self):
        codestr = f"""
        from __static__ import double
        def testfunc(x: float) -> bool:
            y = double(x)
            if y > 3.14:
                return True
            return False
        """
        with self.in_module(codestr) as mod:
            f = mod.testfunc
            self.assertTrue(f(4.1))
            self.assertFalse(f(1.1))

    def test_double_compare_with_integer_literal(self):
        codestr = f"""
        from __static__ import double
        def testfunc(x: float) -> bool:
            y = double(x)
            if y > 3:
                return True
            return False
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, re.escape("can't compare double to Literal[3]")
        ):
            self.compile(codestr)

    def test_double_mixed_compare(self):
        codestr = """
        from __static__ import double, box, unbox
        def f(a):
            x: double = 0
            while x != a:
                pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "can't compare double to dynamic"
        ):
            self.compile(codestr)

    def test_double_mixed_compare_reverse(self):
        codestr = """
        from __static__ import double, box, unbox
        def f(a):
            x: double = 0
            while a > x:
                pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "can't compare double to dynamic"
        ):
            self.compile(codestr)

    def test_double_load_const(self):
        codestr = """
        from __static__ import double

        def t():
            pi: double = 3.14159
        """
        with self.in_module(codestr) as mod:
            t = mod.t
            self.assertInBytecode(t, "PRIMITIVE_LOAD_CONST", (3.14159, TYPED_DOUBLE))
            t()
            self.assert_jitted(t)

    def test_double_box(self):
        codestr = """
        from __static__ import double, box

        def t() -> float:
            pi: double = 3.14159
            return box(pi)
        """
        with self.in_module(codestr) as mod:
            t = mod.t
            self.assertInBytecode(t, "PRIMITIVE_LOAD_CONST", (3.14159, TYPED_DOUBLE))
            self.assertNotInBytecode(t, "CAST")
            self.assertEqual(t(), 3.14159)
            self.assert_jitted(t)

    def test_unbox_double_from_dynamic(self):
        codestr = """
            from __static__ import double

            def f(x) -> double:
                return double(x)
        """
        with self.in_module(codestr) as mod:
            self.assertInBytecode(mod.f, "CAST")
            self.assertEqual(mod.f(2.13), 2.13)
            f1 = mod.f(1)
            self.assertIs(type(f1), float)
            self.assertEqual(f1, 1.0)
            with self.assertRaises(TypeError):
                mod.f("foo")

    def test_unbox_double_from_int(self):
        codestr = """
            from __static__ import double

            def f(x: int) -> double:
                return double(x)
        """
        with self.in_module(codestr) as mod:
            self.assertInBytecode(mod.f, "CAST")
            f1 = mod.f(1)
            self.assertIs(type(f1), float)
            self.assertEqual(f1, 1.0)
            with self.assertRaises(TypeError):
                mod.f("foo")

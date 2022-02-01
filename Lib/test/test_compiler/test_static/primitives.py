from unittest import skipUnderCinderJIT

from __static__ import TYPED_INT64, TYPED_DOUBLE

from .common import StaticTestBase


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

    def test_double_return(self):
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
            # self.assertEqual(f(2.0), 4.0)

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

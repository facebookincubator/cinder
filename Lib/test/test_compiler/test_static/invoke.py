import asyncio

from .common import StaticTestBase


class InvokeTests(StaticTestBase):
    def test_invoke_simple(self):
        codestr = """
            class C:
                def f(self):
                    return 1

            def x(c: C):
                return c.f()
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertEqual(mod.x(c), 1)
            self.assertEqual(mod.x(c), 1)

    def test_invoke_simple_load_field(self):
        codestr = """
            class C:
                def __init__(self):
                    self.x = 42

                def f(self):
                    return self.x

            def x(c: C):
                return c.f()
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertEqual(mod.x(c), 42)
            self.assertEqual(mod.x(c), 42)

    def test_invoke_super(self):
        """tests invoke against a function which has a free var which
        gets introduced due to the super() call"""
        codestr = """
            class B:
                def f(self):
                    return 42

            class C(B):
                def f(self):
                    return super().f()

            def x(b: B):
                return b.f()
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "B", "f"), 0))

        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertEqual(mod.C.f.__code__.co_freevars, ("__class__",))
            self.assertEqual(mod.x(c), 42)
            self.assertEqual(mod.x(c), 42)

    def test_invoke_primitive(self):
        codestr = """
            from __static__ import int64, box

            class C:
                def f(self, a: int64):
                    return box(a)

            def x(c: C, a: int64):
                return c.f(a)
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 1))

        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertEqual(mod.x(c, 42), 42)
            self.assertEqual(mod.x(c, 43), 43)

    def test_invoke_primitive_property(self):
        codestr = """
            from __static__ import int64, box

            class C:
                @property
                def f(self) -> int64:
                    return 42

            def x(c: C):
                return box(c.f)
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", ((("foo", "C", ("f", "fget")), 0)))

        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertEqual(mod.x(c), 42)
            self.assertEqual(mod.x(c), 42)

    def test_invoke_primitive_ret(self):
        codestr = """
            from __static__ import int64, box

            class C:
                def f(self) -> int64:
                    return 42

            def x(c: C):
                return box(c.f())
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertEqual(mod.x(c), 42)
            self.assertEqual(mod.x(c), 42)

    def test_invoke_primitive_ret_non_jittable(self):
        codestr = """
            from __static__ import cbool, box

            X = None
            class C:
                def f(self) -> cbool:
                    global X; X = None; del X
                    return False

            def x(c: C):
                return box(c.f())
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertEqual(mod.x(c), False)
            self.assertEqual(mod.x(c), False)

    def test_invoke_primitive_ret_raises(self):
        codestr = """
            from __static__ import int64, box

            class C:
                def f(self) -> int64:
                    raise ValueError()

            def x(c: C):
                return box(c.f())
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertRaises(ValueError, mod.x, c)
            self.assertRaises(ValueError, mod.x, c)

    def test_invoke_primitive_ret_non_jittable_raises(self):
        codestr = """
            from __static__ import cbool, box

            X = None
            class C:
                def f(self) -> cbool:
                    global X; X = None; del X
                    raise ValueError()

            def x(c: C):
                return box(c.f())
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertRaises(ValueError, mod.x, c)
            self.assertRaises(ValueError, mod.x, c)

    def test_invoke_primitive_ret_override(self):
        codestr = """
            from __static__ import int64, box

            class C:
                def f(self) -> int64:
                    return 42

            def x(c: C):
                return box(c.f())
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:

            class D(mod.C):
                def f(self):
                    return 23

            d = D()
            self.assertEqual(mod.x(d), 23)
            self.assertEqual(mod.x(d), 23)

    def test_invoke_primitive_ret_override_nonfunc(self):
        codestr = """
            from __static__ import int64, box

            class C:
                def f(self) -> int64:
                    return 42

            def x(c: C):
                return box(c.f())
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:

            class Callable:
                def __call__(self, *args):
                    return 23

            class D(mod.C):
                f = Callable()

            d = D()
            self.assertEqual(mod.x(d), 23)
            self.assertEqual(mod.x(d), 23)

    def test_invoke_primitive_ret_override_nonfunc_instance(self):
        codestr = """
            from __static__ import int64, box

            class C:
                def f(self) -> int64:
                    return 42

            def x(c: C):
                return box(c.f())
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:

            class Callable:
                def __call__(self, *args):
                    return 23

            class D(mod.C):
                def __init__(self):
                    def f(*args):
                        if len(args):
                            raise Exception("no-way")
                        return 100

                    self.f = f

                f = Callable()

            d = D()
            self.assertEqual(mod.x(d), 100)
            self.assertEqual(mod.x(d), 100)

    def test_invoke_primitive_ret_classmethod(self):
        codestr = """
            from __static__ import int64, box

            class C:
                @classmethod
                def f(cls) -> int64:
                    return 42

            def x(c: C):
                return box(c.f())
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertEqual(mod.x(c), 42)
            self.assertEqual(mod.x(c), 42)

    def test_invoke_async_many_args_error(self):
        codestr = """
            from __static__ import int64, box

            class C:
                async def f(self, a, b, c, d, e, f, g, h) -> object:
                    raise ValueError()

                async def g(self):
                    return await self.f(1, 2, 3, 4, 5, 6, 7, 8)


            def x(c: C):
                return await c.g()
        """

        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertRaises(ValueError, asyncio.run, mod.x(c))
            self.assertRaises(ValueError, asyncio.run, mod.x(c))

    def test_invoke_primitive_ret_override_classmethod(self):
        codestr = """
            from __static__ import int64, box

            class C:
                @classmethod
                def f(cls) -> int64:
                    return 42

            def x(c: C):
                return box(c.f())
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:

            class D(mod.C):
                @classmethod
                def f(cls):
                    return 23

            c = D()

            self.assertEqual(mod.x(c), 23)
            self.assertEqual(mod.x(c), 23)

    def test_invoke_primitive_ret_staticmethod(self):
        codestr = """
            from __static__ import int64, box

            class C:
                @staticmethod
                def f() -> int64:
                    return 42

            def x(c: C):
                return box(c.f())
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertEqual(mod.x(c), 42)
            self.assertEqual(mod.x(c), 42)

    def test_invoke_primitive_ret_override_staticmethod(self):
        codestr = """
            from __static__ import int64, box

            class C:
                @staticmethod
                def f() -> int64:
                    return 42

            def x(c: C):
                return box(c.f())
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:

            class D(mod.C):
                @staticmethod
                def f():
                    return 23

            c = D()

            self.assertEqual(mod.x(c), 23)
            self.assertEqual(mod.x(c), 23)

    def test_invoke_primitive_ret_override_staticmethod_instance(self):
        return

        codestr = """
            from __static__ import int64, box

            class C:
                @staticmethod
                def f() -> int64:
                    return 42

            def x(c: C):
                return box(c.f())
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:

            class D(mod.C):
                def __init__(self):
                    self.f = lambda: 23

            c = D()
            self.assertEqual(mod.x(c), 23)
            self.assertEqual(mod.x(c), 23)

    def test_invoke_staticmethod_args(self):
        codestr = """
            class C:
                @staticmethod
                def f(a, b):
                    return a + b

            def x(c: C):
                return c.f(21, 21)
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 2))

        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertEqual(mod.x(c), 42)
            self.assertEqual(mod.x(c), 42)

    def test_invoke_classmethod_to_staticmethod_args(self):
        codestr = """
            class C:
                @staticmethod
                def f(a, b):
                    return a + b

                @classmethod
                def g(cls, a, b):
                    return cls.f(a, b)

            def x(c: C):
                return c.g(21, 21)
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "g"), 2))

        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertEqual(mod.x(c), 42)
            self.assertEqual(mod.x(c), 42)

    def test_invoke_primitive_ret_override_typeddescriptor(self):
        codestr = """
            from __static__ import int64, box

            class C:
                x: int = 42

            def x(c: C):
                return c.x
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", ("x", "fget")), 0))

        with self.in_module(codestr) as mod:

            class D(mod.C):
                @property
                def x(self):
                    return 23

            c = D()
            self.assertEqual(mod.x(c), 23)

            self.assertEqual(mod.x(c), 23)

    def test_invoke_primitive_ret_double_raise(self):
        codestr = """
            from __static__ import double, box

            class C:
                def f(self) -> double:
                    raise ValueError()

            def x(c: C):
                return box(c.f())
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertRaises(ValueError, mod.x, c)
            self.assertRaises(ValueError, mod.x, c)

    def test_invoke_primitive_ret_double(self):
        codestr = """
            from __static__ import double, box

            class C:
                def f(self) -> double:
                    return 42.0

            def x(c: C):
                return box(c.f())
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertEqual(mod.x(c), 42.0)
            self.assertEqual(mod.x(c), 42.0)

    def test_invoke_primitive_ret_non_jittable_double(self):
        codestr = """
            from __static__ import double, box

            X = None
            class C:
                def f(self) -> double:
                    global X; X = None; del X
                    return 42.0

            def x(c: C):
                return box(c.f())
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertEqual(mod.x(c), 42.0)
            self.assertEqual(mod.x(c), 42.0)

    def test_invoke_method_takes_primitive(self):
        codestr = """
            from __static__ import int64

            class C:
                def __init__(self, x: int64) -> None:
                    self.x: int64 = x

                def incr(self, by: int64) -> None:
                    self.x += by

            def f() -> int64:
                c = C(2)
                by: int64 = 2
                c.incr(by)
                return c.x
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), 4)

    def test_cannot_use_keyword_for_posonly_arg(self):
        codestr = """
            def f(x: int, /):
                pass

            def g():
                f(x=1)
        """
        self.type_error(
            codestr,
            r"Missing value for positional-only arg 0",
            at="f(x=1)",
        )

    def test_invoke_super_final(self):
        """tests invoke against a function which has a free var which
        gets introduced due to the super() call"""
        codestr = """
            from typing import final
            import sys

            class B:
                def f(self):
                    return 42

            @final
            class C(B):
                def f(self):
                    return super().f()

            def x(c: C):
                return c.f()
        """

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_FUNCTION", (("foo", "C", "f"), 1))

        with self.in_strict_module(codestr) as mod:
            c = mod.C()
            self.assertEqual(mod.C.f.__code__.co_freevars, ("__class__",))
            self.assertEqual(mod.x(c), 42)
            self.assertEqual(mod.x(c), 42)

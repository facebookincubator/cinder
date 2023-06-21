import asyncio
from unittest import skip, skipIf
from unittest.mock import patch

from .common import StaticTestBase

try:
    import cinderjit
except ImportError:
    cinderjit = None


class ClassMethodTests(StaticTestBase):
    def test_classmethod_from_non_final_class_calls_invoke_function(self):
        codestr = """
            class C:
                 @classmethod
                 def foo(cls):
                     return cls
            def f():
                return C.foo()
        """
        with self.in_module(codestr, name="mymod") as mod:
            f = mod.f
            C = mod.C
            self.assertInBytecode(f, "INVOKE_FUNCTION", (("mymod", "C", "foo"), 1))
            self.assertEqual(f(), C)

    def test_classmethod_from_final_class_calls_invoke_function(self):
        codestr = """
            from typing import final
            @final
            class C:
                 @classmethod
                 def foo(cls):
                     return cls
            def f():
                return C.foo()
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            C = mod.C
            self.assertInBytecode(f, "INVOKE_FUNCTION")
            self.assertEqual(f(), C)

    def test_classmethod_from_instance_calls_invoke_method(self):
        codestr = """
            class C:
                 @classmethod
                 def foo(cls):
                     return cls
            def f(c: C):
                return c.foo()
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            C = mod.C
            c = C()
            self.assertInBytecode(f, "INVOKE_METHOD")
            self.assertEqual(f(c), C)

    def test_classmethod_override_from_instance_calls_override(self):
        codestr = """
            class C:
                 @classmethod
                 def foo(cls, x: int) -> int:
                     return x
            class D(C):
                 @classmethod
                 def foo(cls, x: int) -> int:
                     return x + 2

            def f(c: C):
                return c.foo(0)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            D = mod.D
            d = D()
            self.assertInBytecode(f, "INVOKE_METHOD")
            self.assertEqual(f(d), 2)

    def test_classmethod_override_from_non_static_instance_calls_override(self):
        codestr = """
            class C:
                 @classmethod
                 def foo(cls, x: int) -> int:
                     return x

            def f(c: C) -> int:
                return c.foo(42)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            C = mod.C

            class D(C):
                @classmethod
                def foo(cls, x: int) -> int:
                    return x + 30

            d = D()
            self.assertInBytecode(f, "INVOKE_METHOD")
            self.assertEqual(f(d), 72)

    def test_classmethod_non_class_method_override(self):
        codestr = """
            class C:
                 @classmethod
                 def foo(cls, x: int) -> int:
                     return x
            class D(C):
                 def foo(cls, x: int) -> int:
                     return x + 2

            def f(c: C):
                return c.foo(0)
        """
        self.type_error(codestr, "class cannot hide inherited member")

    def test_classmethod_dynamic_call(self):
        codestr = """
            class C:
                def __init__(self, x: int) -> None:
                    self.x = x

                @classmethod
                def foo(cls, *, x: int) -> int:
                    return x

            d = C.foo(x=1)
        """
        with self.in_module(codestr) as mod:
            d = mod.d
            self.assertEqual(d, 1)

    def test_final_classmethod_calls_another(self):
        codestr = """
            from typing import final
            @final
            class C:
                @classmethod
                def foo(cls) -> int:
                    return 3

                @classmethod
                def bar(cls, i: int) -> int:
                    return cls.foo() + i
        """
        with self.in_module(codestr, name="mymod") as mod:
            C = mod.C
            self.assertInBytecode(C.bar, "INVOKE_FUNCTION", (("mymod", "C", "foo"), 1))
            self.assertEqual(C.bar(6), 9)

    def test_classmethod_calls_another(self):
        codestr = """
            class C:
                @classmethod
                def foo(cls) -> int:
                    return 3

                @classmethod
                def bar(cls, i: int) -> int:
                    return cls.foo() + i
        """
        with self.in_module(codestr, name="mymod") as mod:
            C = mod.C
            self.assertNotInBytecode(C.bar, "INVOKE_FUNCTION")
            self.assertInBytecode(C.bar, "INVOKE_METHOD")
            self.assertEqual(C.bar(6), 9)

    def test_classmethod_calls_another_from_static_subclass(self):
        codestr = """
            class C:
                @classmethod
                def foo(cls) -> int:
                    return 3

                @classmethod
                def bar(cls, i: int) -> int:
                    return cls.foo() + i
            class D(C):
                @classmethod
                def foo(cls) -> int:
                    return 42
        """
        with self.in_module(codestr, name="mymod") as mod:
            D = mod.D
            self.assertInBytecode(D.bar, "INVOKE_METHOD")
            self.assertEqual(D.bar(6), 48)

    def test_classmethod_calls_another_from_nonstatic_subclass(self):
        codestr = """
            class C:
                @classmethod
                def foo(cls) -> int:
                    return 3

                @classmethod
                def bar(cls, i: int) -> int:
                    return cls.foo() + i
        """
        with self.in_module(codestr, name="mymod") as mod:
            C = mod.C

            class D(C):
                @classmethod
                def foo(cls) -> int:
                    return 42

            self.assertInBytecode(D.bar, "INVOKE_METHOD")
            self.assertEqual(D.bar(6), 48)

    def test_classmethod_dynamic_subclass(self):
        codestr = """
            class C:
                @classmethod
                async def foo(cls) -> int:
                    return 3

                async def bar(self) -> int:
                    return await self.foo()

                def return_foo_typ(self):
                    return self.foo()
        """
        with self.in_module(codestr, name="mymod") as mod:
            C = mod.C

            class D(C):
                pass

            d = D()
            asyncio.run(d.bar())

    def test_patch(self):
        codestr = """
            class C:
                @classmethod
                def caller(cls):
                    if cls.is_testing():
                        return True
                    return False

                @classmethod
                def is_testing(cls):
                    return True

            class Child(C):
                pass
        """

        with self.in_module(codestr) as mod:
            with patch(f"{mod.__name__}.C.is_testing", return_value=False) as p:
                c = mod.Child()

                self.assertEqual(c.caller(), False)
                self.assertEqual(p.call_args[0], (mod.Child,))

    def test_classmethod_on_type(self):
        codestr = """
            class C(type):
                @classmethod
                def x(cls):
                    return cls

            def f(c: C):
                return c.x()

            def f1(c: type[C]):
                return c.x()
        """

        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(mod.C("foo", (object,), {})), mod.C)
            self.assertEqual(mod.f1(mod.C), mod.C)

    def test_classmethod_dynamic_subclass_override_async(self):
        codestr = """
            class C:
                @classmethod
                async def foo(cls) -> int:
                    return 3

                async def bar(self) -> int:
                    return await self.foo()

                def return_foo_typ(self):
                    return self.foo()
        """
        with self.in_module(codestr, name="mymod") as mod:
            C = mod.C

            class D(C):
                async def foo(self) -> int:
                    return 42

            d = D()
            asyncio.run(d.bar())

    def test_classmethod_dynamic_subclass_override_nondesc_async(self):
        codestr = """
            class C:
                @classmethod
                async def foo(cls) -> int:
                    return 3

                async def bar(self) -> int:
                    return await self.foo()

                def return_foo_typ(self):
                    return self.foo()
        """
        with self.in_module(codestr, name="mymod") as mod:
            C = mod.C

            class Callable:
                async def __call__(self):
                    return 42

            class D(C):
                foo = Callable()

            d = D()
            asyncio.run(d.bar())

    def test_classmethod_dynamic_subclass_override(self):
        codestr = """
            class C:
                @classmethod
                def foo(cls) -> int:
                    return 3

                def bar(self) -> int:
                    return self.foo()

                def return_foo_typ(self):
                    return self.foo()
        """
        with self.in_module(codestr, name="mymod") as mod:
            C = mod.C

            class D(C):
                def foo(self) -> int:
                    return 42

            d = D()
            self.assertEqual(d.bar(), 42)

    def test_classmethod_other_dec(self):
        codestr = """
            from typing import final

            def mydec(f):
                return f
            @final
            class C:
                @classmethod
                @mydec
                def foo(cls) -> int:
                    return 3

                def f(self):
                    return self.foo()
        """
        with self.in_module(codestr, name="mymod") as mod:
            C = mod.C
            self.assertEqual(C().f(), 3)

    def test_invoke_non_static_subtype_async_classmethod(self):
        codestr = """
            class C:
                x = 3

                @classmethod
                async def f(cls) -> int:
                    return cls.x

                async def g(self) -> int:
                    return await self.f()
        """
        with self.in_module(codestr) as mod:

            class D(mod.C):
                pass

            d = D()
            self.assertEqual(asyncio.run(d.g()), 3)

    def test_classmethod_invoke_method_cached(self):
        cases = [True, False]
        for should_make_hot in cases:
            with self.subTest(should_make_hot=should_make_hot):
                codestr = """
                    class C:
                        @classmethod
                        def foo(cls) -> int:
                            return 3

                    def f(c: C):
                        return c.foo()
                """
                with self.in_module(codestr, name="mymod") as mod:
                    C = mod.C
                    f = mod.f

                    c = C()
                    if should_make_hot:
                        for i in range(50):
                            f(c)
                    self.assertInBytecode(f, "INVOKE_METHOD")
                    self.assertEqual(f(c), 3)

    def test_classmethod_async_invoke_method_cached(self):
        cases = [True, False]
        for should_make_hot in cases:

            with self.subTest(should_make_hot=should_make_hot):
                codestr = """
                class C:
                    async def instance_method(self) -> int:
                        return (await self.foo())

                    @classmethod
                    async def foo(cls) -> int:
                        return 3

                async def f(c: C):
                    return await c.instance_method()
                """
                with self.in_module(codestr, name="mymod") as mod:
                    C = mod.C
                    f = mod.f

                    async def make_hot():
                        c = C()
                        for i in range(50):
                            await f(c)

                    if should_make_hot:
                        asyncio.run(make_hot())
                    self.assertInBytecode(C.instance_method, "INVOKE_METHOD")
                    self.assertEqual(asyncio.run(f(C())), 3)

    def test_invoke_starargs(self):
        codestr = """

            class C:
                @classmethod
                def foo(self, x: int) -> int:
                    return 3

                def f(self, *args):
                    return self.foo(*args)
        """
        with self.in_module(codestr, name="mymod") as mod:
            C = mod.C
            self.assertEqual(C().f(42), 3)

    def test_invoke_starargs_starkwargs(self):
        codestr = """

            class C:
                @classmethod
                def foo(self, x: int) -> int:
                    return 3

                def f(self, *args, **kwargs):
                    return self.foo(*args, **kwargs)
        """
        with self.in_module(codestr, name="mymod") as mod:
            C = mod.C
            self.assertNotInBytecode(C.f, "INVOKE_METHOD")
            self.assertEqual(C().f(42), 3)

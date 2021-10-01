import asyncio
from .common import StaticTestBase


class ClassMethodTests(StaticTestBase):
    def test_classmethod_from_class_calls_invoke_function(self):
        codestr = """
            class C:
                 @classmethod
                 def foo(cls):
                     return cls
            def f():
                return C.foo()
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            C = mod["C"]
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
            f = mod["f"]
            C = mod["C"]
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
            f = mod["f"]
            D = mod["D"]
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
                return c.foo(0)
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            C = mod["C"]

            class D(C):
                @classmethod
                def foo(cls, x: int) -> int:
                    return x + 30

            d = D()
            self.assertInBytecode(f, "INVOKE_METHOD")
            self.assertEqual(f(d), 30)

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
            d = mod["d"]
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
            C = mod["C"]
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
            C = mod["C"]
            self.assertNotInBytecode(C.bar, "INVOKE_FUNCTION")
            self.assertNotInBytecode(C.bar, "INVOKE_METHOD")
            self.assertEqual(C.bar(6), 9)

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
            C = mod["C"]

            class D(C):
                pass

            d = D()
            asyncio.run(d.bar())

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
            C = mod["C"]
            self.assertEqual(C().f(), 3)

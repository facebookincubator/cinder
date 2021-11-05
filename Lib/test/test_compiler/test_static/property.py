from .common import StaticTestBase
from .tests import bad_ret_type


class PropertyTests(StaticTestBase):
    def test_property_getter(self):
        codestr = """
            from typing import final
            class C:
                @final
                @property
                def foo(self) -> int:
                    return 42
                def bar(self) -> int:
                    return 0

            def bar(c: C) -> int:
                return c.foo
        """
        with self.in_module(codestr) as mod:
            f = mod.bar
            C = mod.C
            self.assertEqual(f(C()), 42)
            self.assertInBytecode(f, "INVOKE_FUNCTION")

    def test_property_getter_final_class(self):
        codestr = """
            from typing import final
            @final
            class C:
                @property
                def foo(self) -> int:
                    return 42
                def bar(self) -> int:
                    return 0

            def bar(c: C) -> int:
                return c.foo
        """
        with self.in_module(codestr) as mod:
            f = mod.bar
            C = mod.C
            self.assertEqual(f(C()), 42)
            self.assertInBytecode(f, "INVOKE_FUNCTION")

    def test_property_getter_non_final(self):
        codestr = """
            class C:
                @property
                def foo(self) -> int:
                    return 42

            def bar(c: C) -> int:
                return c.foo
        """
        with self.in_module(codestr) as mod:
            f = mod.bar
            C = mod.C
            self.assertNotInBytecode(f, "INVOKE_FUNCTION")
            self.assertInBytecode(f, "INVOKE_METHOD")
            self.assertEqual(f(C()), 42)

    def test_property_getter_inheritance(self):
        codestr = """
            class C:
                @property
                def foo(self) -> int:
                    return 42

            class D(C):
                @property
                def foo(self) -> int:
                    return 43

            def bar(c: C) -> int:
                return c.foo
        """
        with self.in_module(codestr) as mod:
            f = mod.bar
            D = mod.D
            self.assertInBytecode(f, "INVOKE_METHOD")
            self.assertEqual(f(D()), 43)

    def test_property_getter_inheritance_no_override(self):
        codestr = """
            class C:
                @property
                def foo(self) -> int:
                    return 42

            class D(C):
                pass

            def bar(c: C) -> int:
                return c.foo
        """
        with self.in_module(codestr) as mod:
            f = mod.bar
            D = mod.D
            self.assertInBytecode(f, "INVOKE_METHOD")
            self.assertEqual(f(D()), 42)

    def test_property_getter_non_static_inheritance(self):
        codestr = """
            class C:
                @property
                def foo(self) -> int:
                    return 42

            def bar(c: C) -> int:
                return c.foo
        """
        with self.in_module(codestr) as mod:
            f = mod.bar
            C = mod.C

            class D(C):
                @property
                def foo(self) -> int:
                    return 43

            self.assertInBytecode(f, "INVOKE_METHOD")
            self.assertEqual(f(D()), 43)

    def test_property_getter_non_static_inheritance_with_non_property(self):
        codestr = """
            class C:
                @property
                def foo(self) -> int:
                    return 42

            def bar(c: C) -> int:
                return c.foo
        """
        with self.in_module(codestr) as mod:
            f = mod.bar
            C = mod.C

            class D(C):
                def foo(self) -> int:
                    return 43

            self.assertInBytecode(f, "INVOKE_METHOD")
            with self.assertRaises(TypeError):
                f(D())

    def test_property_getter_non_static_inheritance_with_get_descriptor(self):
        codestr = """
            class C:
                @property
                def foo(self) -> int:
                    return 42

            def bar(c: C) -> int:
                return c.foo
        """
        with self.in_module(codestr) as mod:
            f = mod.bar
            C = mod.C

            class MyDesc:
                def __get__(self, inst, ctx):
                    return 43

            class D(C):
                foo = MyDesc()

            self.assertInBytecode(f, "INVOKE_METHOD")
            with self.assertRaises(TypeError):
                f(D())

    def test_property_getter_type_error(self):
        codestr = """
            from typing import final
            class C:
                @final
                @property
                def foo(self) -> int:
                    return 42

            def bar(c: C) -> str:
                return c.foo
        """
        self.type_error(codestr, bad_ret_type("int", "str"))

    def test_property_class_type_error(self):
        codestr = """
            @property
            class C:
                def foo(self) -> int:
                    return 42

        """
        self.type_error(codestr, "Cannot decorate a class with @property")

    def test_property_setter(self):
        codestr = """
            from typing import final
            class C:
                def __init__(self, x: int) -> None:
                    self.x = x

                @final
                @property
                def foo(self) -> int:
                    return -self.x

                @foo.setter
                def foo(self, x: int) -> None:
                    self.x = x

            def bar(c: C) -> None:
                c.foo = 3
        """
        with self.in_module(codestr) as mod:
            f = mod.bar
            C = mod.C
            self.assertInBytecode(f, "INVOKE_FUNCTION")
            c = C(2)
            self.assertEqual(f(c), None)
            self.assertEqual(c.foo, -3)

    def test_property_setter_inheritance(self):
        codestr = """
            from typing import final
            class C:
                def __init__(self, x: int) -> None:
                    self.x = x

                @property
                def foo(self) -> int:
                    return self.x

                @foo.setter
                def foo(self, x: int) -> None:
                    self.x = x

            class D(C):
                @property
                def foo(self) -> int:
                    return self.x

                @foo.setter
                def foo(self, x: int) -> None:
                    self.x = x + 1


            def bar(c: C) -> None:
                c.foo = 3
        """
        with self.in_module(codestr) as mod:
            f = mod.bar
            D = mod.D
            self.assertInBytecode(f, "INVOKE_METHOD")
            d = D(2)
            self.assertEqual(f(d), None)
            self.assertEqual(d.foo, 4)

    def test_property_setter_non_static_inheritance(self):
        codestr = """
            from typing import final
            class C:
                def __init__(self, x: int) -> None:
                    self.x = x

                @property
                def foo(self) -> int:
                    return self.x

                @foo.setter
                def foo(self, x: int) -> None:
                    self.x = x

            def bar(c: C) -> None:
                c.foo = 3
        """
        with self.in_module(codestr) as mod:
            f = mod.bar
            C = mod.C
            self.assertInBytecode(f, "INVOKE_METHOD")

            class D(C):
                @property
                def foo(self) -> int:
                    return self.x

                @foo.setter
                def foo(self, x: int) -> None:
                    self.x = x + 10

            d = D(2)
            self.assertEqual(f(d), None)
            self.assertEqual(d.x, 13)

    def test_property_no_setter(self):
        codestr = """
            class C:
                @property
                def prop(self) -> int:
                    return 1

                def set(self, val: int) -> None:
                    self.prop = val
        """
        with self.in_module(codestr) as mod:
            c = mod.C()
            with self.assertRaisesRegex(AttributeError, "can't set attribute"):
                c.prop = 2
            with self.assertRaisesRegex(AttributeError, "can't set attribute"):
                c.set(2)

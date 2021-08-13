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

            def bar(c: C) -> int:
                return c.foo
        """
        with self.in_module(codestr) as mod:
            f = mod["bar"]
            C = mod["C"]
            self.assertInBytecode(f, "INVOKE_FUNCTION")
            self.assertEqual(f(C()), 42)

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
            f = mod["bar"]
            C = mod["C"]
            self.assertNotInBytecode(f, "INVOKE_FUNCTION")
            self.assertNotInBytecode(f, "INVOKE_METHOD")
            self.assertEqual(f(C()), 42)

    def test_property_setter(self):
        codestr = """
            from typing import final
            class C:
                def __init__(self, x: int) -> None:
                    self.x = x

                @final
                @property
                def foo(self) -> int:
                    return 42 + self.x

                @foo.setter
                def foo(self, x: int) -> None:
                    self.x = x

            def bar(c: C) -> int:
                c.foo = 3
                return c.foo
        """
        with self.in_module(codestr) as mod:
            f = mod["bar"]
            C = mod["C"]
            self.assertInBytecode(f, "INVOKE_FUNCTION")
            self.assertNotInBytecode(f, "INVOKE_METHOD")
            self.assertEqual(f(C(2)), 45)

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
            f = mod["bar"]
            D = mod["D"]
            self.assertNotInBytecode(f, "INVOKE_FUNCTION")
            self.assertNotInBytecode(f, "INVOKE_METHOD")
            self.assertEqual(f(D()), 43)

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

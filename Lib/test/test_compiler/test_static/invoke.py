from .common import StaticTestBase


class InvokeTests(StaticTestBase):
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

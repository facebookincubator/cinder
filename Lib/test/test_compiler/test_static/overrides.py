from .common import StaticTestBase


class OverridesTests(StaticTestBase):
    def test_literal_false_return_override(self) -> None:
        codestr = """
            from typing import Literal

            class A:
                def f(self) -> Literal[False]:
                    return False

            class B(A):
                def f(self) -> bool:
                    return False
        """
        self.type_error(
            codestr,
            (
                r"<module>.B.f overrides <module>.A.f inconsistently. "
                r"Returned type `bool` is not a subtype of the overridden return `Literal\[False\]`"
            ),
            at="def f(self) -> bool",
        )

    def test_name_irrelevant_for_posonlyarg(self) -> None:
        codestr = """
            class A:
                def f(self, x: int, /) -> int:
                    return x + 1

            class B(A):
                def f(self, y: int, /) -> int:
                    return y + 2

            def f(a: A) -> int:
                return a.f(1)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(mod.A()), 2)
            self.assertEqual(mod.f(mod.B()), 3)

    def test_cannot_override_normal_arg_with_posonly(self) -> None:
        codestr = """
            class A:
                def f(self, x: int) -> None:
                    pass

            class B(A):
                def f(self, x: int, /) -> None:
                    pass
        """
        self.type_error(
            codestr,
            r"<module>.B.f overrides <module>.A.f inconsistently. `x` is positional-only in override, not in base",
            at="def f(self, x: int, /)",
        )

    def test_can_override_posonly_arg_with_normal(self) -> None:
        codestr = """
            class A:
                def f(self, x: int, /) -> int:
                    return x + 1

            class B(A):
                def f(self, y: int) -> int:
                    return y + 2

            def f(a: A) -> int:
                return a.f(1)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(mod.A()), 2)
            self.assertEqual(mod.f(mod.B()), 3)

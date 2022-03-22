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

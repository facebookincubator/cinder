from .common import StaticTestBase


class LintTests(StaticTestBase):
    def test_lint_primitive_ifexp(self) -> None:
        codestr = """
        from __static__ import int64

        def f(x: int, y: bool) -> int64:
            return int64(x) if y else 0
        """

        errors = self.lint(codestr)
        errors.check(
            errors.match(
                "if expression has incompatible types: int64 and Literal[0]",
                at="int64(x)",
            ),
        )

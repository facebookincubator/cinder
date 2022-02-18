from .common import StaticTestBase


class PerfLintTests(StaticTestBase):
    def test_two_starargs(self) -> None:
        codestr = """
        def f(x: int, y: int, z: int) -> int:
            return x + y + z

        a = [1, 2]
        b = [3]
        f(*a, *b)
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings(
            errors.match(
                "Multiple *args prevents more efficient static call",
                at="f(*a, *b)",
            ),
        )

    def test_positional_after_starargs(self) -> None:
        codestr = """
        def f(x: int, y: int, z: int) -> int:
            return x + y + z

        a = [1, 2]
        f(*a, 3)
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings(
            errors.match(
                "Positional arg after *args prevents more efficient static call",
                at="f(*a, 3)",
            ),
        )

    def test_multiple_kwargs(self) -> None:
        codestr = """
        def f(x: int, y: int, z: int) -> int:
            return x + y + z

        a = {{"x": 1, "y": 2}}
        b = {{"z": 3}}
        f(**a, **b)
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings(
            errors.match(
                "Multiple **kwargs prevents more efficient static call",
                at="f(**a, **b)",
            ),
        )

    def test_starargs_and_default(self) -> None:
        codestr = """
        def f(x: int, y: int, z: int = 0) -> int:
            return x + y + z

        a = [3]
        f(1, 2, *a)
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings(
            errors.match(
                "Passing *args to function with default values prevents more efficient static call",
                at="f(1, 2, *a)",
            ),
        )

    def test_kwonly(self) -> None:
        codestr = """
        def f(*, x: int = 0) -> int:
            return x

        f(1)
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings(
            errors.match(
                "Keyword-only args in called function prevents more efficient static call",
                at="f(1)",
            ),
        )

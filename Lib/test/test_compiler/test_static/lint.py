from .common import StaticTestBase


class LintTests(StaticTestBase):
    def test_lint(self) -> None:
        code = """
            def f():
                x: int = 'abc'
                b: str = 42
        """
        errors = self.lint(code)
        errors.check(
            errors.match("Exact[str] cannot be assigned to int", at="'abc'"),
            errors.match("Literal[42] cannot be assigned to str", at="42"),
        )

    def test_calls(self) -> None:
        code = """
            def f(x: int):
                pass

            f('abc')
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Exact[str] received for positional arg 'x', expected int",
                at="'abc'",
            )
        )

    def test_names(self) -> None:
        code = """
            def f(a: int, b: str):
                x: int = b
                y: str = a
        """
        errors = self.lint(code)
        errors.check(
            errors.match("str cannot be assigned to int", at="b"),
            errors.match("int cannot be assigned to str", at="a"),
        )

    def test_prim_math(self) -> None:
        code = """
            from __static__ import int64
            def f(x: str):
                y: int64 = 42
                if y + x:
                    print('hi')

            f('abc')
        """
        errors = self.lint(code)
        errors.check(errors.match("cannot add int64 and str", at="y + x"))

    def test_double(self) -> None:
        code = """
            from __static__ import double
            def f(x: str):
                double(x)

            f('abc')
        """
        errors = self.lint(code)
        errors.check(errors.match("double cannot be created from str", at="double(x)"))

    def test_ifexp(self) -> None:
        codestr = """
        from __static__ import int64

        def f(x: bool) -> int64:
            return "foo" if x else 0
        """

        errors = self.lint(codestr)
        errors.check(
            errors.match(
                "invalid union type Union[Exact[str], Literal[0]]",
                at='"foo"',
            ),
        )

    def test_two_starargs(self) -> None:
        codestr = """
        def f(x: int, y: int, z: int) -> int:
            return x + y + z

        a = [1, 2]
        b = [3]
        f(*a, *b)
        """

        errors = self.lint(codestr)
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

        errors = self.lint(codestr)
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

        errors = self.lint(codestr)
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

        errors = self.lint(codestr)
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

        errors = self.lint(codestr)
        errors.check_warnings(
            errors.match(
                "Keyword-only args in called function prevents more efficient static call",
                at="f(1)",
            ),
        )

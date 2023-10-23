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
            errors.match("str cannot be assigned to int", at="'abc'"),
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
                "str received for positional arg 'x', expected int",
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
                "invalid union type Union[str, Literal[0]]",
                at='"foo"',
            ),
        )

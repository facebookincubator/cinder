from .common import ReadonlyTestBase


class StaticCompatTest(ReadonlyTestBase):
    def test_static_lint(self) -> None:
        code = """
            def f():
                x: Readonly[int] = 'abc'
                b: str = readonly(42)
        """
        errors = self.static_lint(code)
        errors.check(
            errors.match("Exact[str] cannot be assigned to int", at="'abc'"),
            errors.match("Literal[42] cannot be assigned to str", at="readonly(42)"),
        )

    def test_calls(self) -> None:
        code = """
            def f(x: Readonly[int]):
                pass

            f('abc')
        """
        errors = self.static_lint(code)
        errors.check(
            errors.match(
                "Exact[str] received for positional arg 'x', expected int",
                at="'abc'",
            )
        )

    def test_calls_0(self) -> None:
        code = """
            def f(x: int):
                pass

            f(readonly('abc'))
        """
        errors = self.static_lint(code)
        errors.check(
            errors.match(
                "Exact[str] received for positional arg 'x', expected int",
                at="readonly('abc')",
            )
        )

    def test_calls_1(self) -> None:
        code = """
            @readonly_func
            def f(x: int):
                pass

            f(readonly('abc'))
        """
        errors = self.static_lint(code)
        errors.check(
            errors.match(
                "Exact[str] received for positional arg 'x', expected int",
                at="readonly('abc')",
            )
        )

    def test_names(self) -> None:
        code = """
            def f(a: Readonly[int], b: str):
                x: int = b
                y: Readonly[str] = a
        """
        errors = self.static_lint(code)
        errors.check(
            errors.match("str cannot be assigned to int", at="b"),
            errors.match("int cannot be assigned to str", at="a"),
        )

    def test_prim_math(self) -> None:
        code = """
            from __static__ import int64
            def f(x: Readonly[str]):
                y: Readonly[int64] = 42
                if y + x:
                    print('hi')

            f('abc')
        """
        errors = self.static_lint(code)
        errors.check(errors.match("cannot add int64 and str", at="y + x"))

    def test_double(self) -> None:
        code = """
            from __static__ import double
            def f(x: Readonly[str]):
                double(x)

            f('abc')
        """
        errors = self.static_lint(code)
        errors.check(errors.match("double cannot be created from str", at="double(x)"))

    def test_ifexp(self) -> None:
        codestr = """
        from __static__ import int64

        def f(x: bool) -> Readonly[int64]:
            return "foo" if x else 0
        """

        errors = self.static_lint(codestr)
        errors.check(
            errors.match(
                "invalid union type Union[Exact[str], Literal[0]]",
                at='"foo"',
            ),
        )

    def test_return(self) -> None:
        codestr = """
        from __static__ import int64

        def f() -> Readonly[int64]:
            return 0

        def g():
            z: int64 = f()
            y: str = f()
        """

        errors = self.static_lint(codestr)
        errors.check(
            errors.match(
                "int64 cannot be assigned to str",
                at="f()",
            ),
        )

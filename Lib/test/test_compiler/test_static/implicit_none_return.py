from .common import StaticTestBase


class ImplicitNoneReturnTests(StaticTestBase):
    def test_implicit_none_return_good(self) -> None:
        codestr = """
            def f() -> int | None:
                pass
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), None)

    def test_implicit_none_return_error(self) -> None:
        codestr = """
            def f() -> int:
                pass
        """
        self.type_error(
            codestr,
            r"Function has declared return type 'int' but can implicitly return None",
            "def f() -> int",
        )

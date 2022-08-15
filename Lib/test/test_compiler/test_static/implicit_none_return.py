from unittest import skipIf

from .common import StaticTestBase

try:
    import cinderjit
except ImportError:
    cinderjit = None


@skipIf(cinderjit is not None, "TODO(T128836962): We don't have JIT support yet.")
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

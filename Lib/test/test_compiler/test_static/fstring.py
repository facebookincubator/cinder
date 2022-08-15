from unittest import skipIf

from .common import StaticTestBase

try:
    import cinderjit
except ImportError:
    cinderjit = None


@skipIf(cinderjit is not None, "TODO(T128836962): We don't have JIT support yet.")
class FStringTests(StaticTestBase):
    def test_format_spec(self):
        codestr = """
            def f(x: float) -> str:
                return f"{x:.2f}"
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(2.134), "2.13")

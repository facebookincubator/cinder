from .common import StaticTestBase


class PrimitivesTests(StaticTestBase):
    def test_primitive_context_ifexp(self) -> None:
        codestr = """
            from __static__ import int64

            def f(x: int | None = None) -> int64:
                return int64(x) if x is not None else 0
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 0)
            self.assertEqual(f(1), 1)

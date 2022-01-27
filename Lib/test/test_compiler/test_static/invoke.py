from .common import StaticTestBase


class InvokeTests(StaticTestBase):
    def test_invoke_method_takes_primitive(self):
        codestr = """
            from __static__ import int64

            class C:
                def __init__(self, x: int64) -> None:
                    self.x: int64 = x

                def incr(self, by: int64) -> None:
                    self.x += by

            def f() -> int64:
                c = C(2)
                by: int64 = 2
                c.incr(by)
                return c.x
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), 4)

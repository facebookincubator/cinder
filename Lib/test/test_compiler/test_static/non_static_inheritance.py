from .common import StaticTestBase


class NonStaticInheritanceTests(StaticTestBase):
    def test_static_return_is_resolved_with_multiple_levels_of_inheritance(self):
        codestr = """
            class C:
                def foobar(self, x: int) -> int:
                    return x
                def f(self) -> int:
                    return self.foobar(1)
        """
        with self.in_strict_module(codestr, name="mymod", enable_patching=True) as mod:
            C = mod.C

            class D(C):
                def foobar(self, x: int) -> int:
                    return x + 1

            class E(D):
                def foobar(self, x: int) -> int:
                    return x + 2

            self.assertEqual(D().f(), 2)
            self.assertEqual(E().f(), 3)

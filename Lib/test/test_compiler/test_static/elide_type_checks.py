from .common import StaticTestBase


class ElideTypeChecksTests(StaticTestBase):
    def test_check_args_precedes_gen_start(self) -> None:
        codestr = """
            async def f():
                pass
        """
        with self.in_module(codestr) as mod:
            self.assertInBytecode(mod.f, "CHECK_ARGS", (), index=0)
            self.assertInBytecode(mod.f, "GEN_START", 1, index=1)

    def test_elide_check_with_one_optional(self) -> None:
        codestr = """
            from typing import Optional
            def foo() -> int:
                def bar(g: Optional[str] = None) -> int:
                    return int(g or "42")
                return bar()
        """
        with self.in_module(codestr) as mod:
            f = mod.foo
            self.assertEqual(f(), 42)

    def test_type_error_raised_when_eliding_defaults(self) -> None:
        codestr = """
            from typing import Optional
            def foo(f: int, g: Optional[str] = None) -> int:
                return int(g or "42")
        """
        with self.in_module(codestr) as mod:
            f = mod.foo
            with self.assertRaises(TypeError):
                f("1")

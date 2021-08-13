from .common import StaticTestBase


class ElideTypeChecksTests(StaticTestBase):
    def test_elide_check_with_one_optional(self) -> None:
        codestr = """
            from typing import Optional
            def foo() -> int:
                def bar(g: Optional[str] = None) -> int:
                    return int(g or "42")
                return bar()
        """
        with self.in_module(codestr) as mod:
            f = mod["foo"]
            self.assertEqual(f(), 42)

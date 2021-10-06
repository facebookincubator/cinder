import re

from .common import StaticTestBase
from .tests import type_mismatch


class UnionCompilationTests(StaticTestBase):
    def test_static_incompatible_union(self) -> None:
        codestr = """
        from typing import Optional
        def expects_bool(b: bool) -> None:
            return
        def foo(x: Optional[bool]) -> None:
            expects_bool(x)
        """
        self.type_error(
            codestr,
            re.escape(
                "type mismatch: Optional[Exact[bool]] received for positional arg 'b', expected Exact[bool]"
            ),
        )

    def test_static_compatible_union(self) -> None:
        codestr = """
        from typing import Optional
        def expects_object(b: object) -> None:
            return
        def foo(x: Optional[bool]) -> None:
            expects_object(x)
        """
        with self.in_module(codestr) as mod:
            foo = mod.foo
            self.assertEqual(foo(True), None)
            self.assertEqual(foo(None), None)

    def test_static_assigning_union_of_subclasses_to_base(self) -> None:
        codestr = """
        class C:
            pass
        class D(C):
            pass
        class E(C):
            pass
        def expects_object(c: C) -> None:
            return
        def foo(d_or_e: D | E) -> None:
            expects_object(d_or_e)
        """
        with self.in_module(codestr) as mod:
            foo = mod.foo


if __name__ == "__main__":
    unittest.main()

from compiler.errors import TypedSyntaxError
from unittest import skip

from .common import StaticTestBase


class ReadonlyTests(StaticTestBase):
    def test_readonly_assign_0(self):
        codestr = """
        from typing import List
        def foo():
            x: List[int] = readonly([])
        """
        self.type_error(
            codestr,
            "type mismatch: Readonly\\[list\\] cannot be assigned to list",
            "readonly([])",
        )

    def test_readonly_assign_1(self):
        codestr = """
        from typing import List
        def foo():
            x: Readonly[List[int]] = []
            y = x
            z: List[int] = x
        """
        self.type_error(
            codestr, "type mismatch: Readonly\\[list\\] cannot be assigned to list", "x"
        )

    def test_readonly_parameter_0(self):
        codestr = """
        from typing import List
        def f(l: List[int]) -> None:
            pass
        def g():
            l = readonly([])
            f(l)
        """
        self.type_error(
            codestr,
            "type mismatch: Readonly\[list\] received for positional arg 'l'",
            "l",
        )

    def test_readonly_parameter_1(self):
        codestr = """
        from typing import List
        def f(l: List[int], x: Readonly[List[int]]) -> None:
            pass
        def g():
            l = readonly([])
            x = []
            f(l, x)
        """
        self.type_error(
            codestr,
            "type mismatch: Readonly\[list\] received for positional arg 'l'",
            "l",
        )

    def test_readonly_parameter_2(self):
        codestr = """
        from __future__ import annotations
        from typing import List
        def f(l: List[int], x: Readonly[List[int]]) -> None:
            pass
        def g():
            l = readonly([])
            x = []
            f(x, l)
        """
        with self.in_module(codestr) as mod:
            mod.g()

    def test_readonly_return_1(self):
        codestr = """
        from typing import List
        def f() -> int:
            return 1 + 1
        def g():
            x: Readonly[int] = f()
        """
        with self.in_module(codestr) as mod:
            mod.g()

    def test_readonly_return_2(self):
        codestr = """
        from typing import List
        def f() -> Readonly[int]:
            return readonly(1)
        def g():
            x: int = f()
        """
        self.type_error(
            codestr, "type mismatch: Readonly\[int\] cannot be assigned to int", "f()"
        )

    def test_readonly_nonexact_int_assign(self):
        codestr = """
        class C(int):
            pass

        def foo():
            x: int = C(1)
            y: int = readonly(x)
        """
        self.type_error(
            codestr,
            "type mismatch: Readonly\[<module>.C\] cannot be assigned to int",
            "readonly(x)",
        )

    def test_readonly_return_3(self):
        codestr = """
        def g() -> int:
            return 1
        def f() -> int:
            return readonly(g())
        """
        self.type_error(
            codestr,
            "return type must be int, not Readonly\[int\]",
            "return readonly(g())",
        )

    def test_readonly_override_1(self):
        codestr = """
        from __future__ import annotations
        class C:
            def f(self, x: int) -> None:
                pass

        class D(C):
            def f(self, x: Readonly[int]) -> None:
                pass
        """
        with self.in_module(codestr) as mod:
            mod.D().f(1)

    def test_readonly_override_2(self):
        codestr = """

        class C:
            def f(self, x: Readonly[int]) -> None:
                pass

        class D(C):
            def f(self, x: int) -> None:
                pass
        """
        self.type_error(
            codestr,
            "Parameter x of type `int` is not a supertype "
            "of the overridden parameter `Readonly\[int\]`",
            "def f(self, x: int)",
        )

    def test_readonly_override_3(self):
        codestr = """
        class C:
            def f(self, x: int) -> int:
                return 1

        class D(C):
            def f(self, x: int) -> Readonly[int]:
                return 1
        """
        self.type_error(
            codestr,
            "Returned type `Readonly\[int\]` is not a "
            "subtype of the overridden return `int`",
            "def f(self, x: int) -> Readonly[int]",
        )

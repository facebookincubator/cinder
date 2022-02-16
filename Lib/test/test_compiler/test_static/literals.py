from .common import StaticTestBase


class LiteralsTests(StaticTestBase):
    def test_literal_bool_annotation_error(self) -> None:
        codestr = """
            from typing import Literal

            def f(x: bool) -> Literal[False]:
                return x
        """
        self.type_error(codestr, r"return type must be Literal\[False\]", "return x")

    def test_literal_bool_annotation_runtime_cast(self) -> None:
        codestr = """
            from typing import Literal

            def f(x) -> Literal[False]:
                return x
        """
        with self.in_module(codestr) as mod:
            self.assertInBytecode(mod.f, "COMPARE_OP", "is")
            self.assertIs(mod.f(False), False)
            with self.assertRaises(TypeError):
                mod.f(True)

    def test_literal_bool_annotation_good(self) -> None:
        codestr = """
            from typing import Literal

            def f() -> Literal[True]:
                return True
        """
        with self.in_module(codestr) as mod:
            self.assertNotInBytecode(mod.f, "COMPARE_OP")
            self.assertIs(mod.f(), True)

    def test_literal_bool_assign_to_wrong_literal(self) -> None:
        codestr = """
            from typing import Literal

            def f():
                x: Literal[False] = True
        """
        self.type_error(
            codestr, r"Literal\[True\] cannot be assigned to Literal\[False\]", "True"
        )

    def test_reassign_inferred_bool_top_level(self) -> None:
        codestr = """
            x = True
            x = False
        """
        with self.in_module(codestr) as mod:
            self.assertIs(mod.x, False)

    def test_literal_bool_assign_to_optional(self) -> None:
        codestr = """
            def f():
                x: bool | None = False
                return x
        """
        with self.in_module(codestr) as mod:
            self.assertIs(mod.f(), False)

    def test_literal_int_assign_to_optional(self) -> None:
        codestr = """
            def f():
                x: int | None = 1
                return x
        """
        with self.in_module(codestr) as mod:
            self.assertIs(mod.f(), 1)

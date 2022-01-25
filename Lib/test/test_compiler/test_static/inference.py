import re
from .common import StaticTestBase


class InferenceTests(StaticTestBase):
    def test_if_exp_union(self) -> None:
        """If expressions can be inferred as the union of the branches."""
        codestr = """
            def f(x: int) -> None:
                y = x if x else None
                reveal_type(y)
        """
        self.type_error(
            codestr, rf"reveal_type\(y\): 'Optional\[int\]'", at="reveal_type"
        )

    def test_if_exp_same_type(self) -> None:
        codestr = """
            class C: pass

            x = C() if a else C()
            reveal_type(x)
        """
        self.type_error(codestr, rf"reveal_type\(x\): '<module>.C'", at="reveal_type")

    def test_type_widened_in_while_loop(self) -> None:
        for loc, typ in [("inside", "int"), ("after", "Optional[int]")]:
            with self.subTest(loc=loc, typ=typ):
                if loc == "inside":
                    inner = "reveal_type(x)"
                    after = ""
                else:
                    inner = ""
                    after = "reveal_type(x)"
                codestr = f"""
                    def f() -> int:
                        x: int | None = None
                        while True:
                            if x is not None:
                                {inner}
                                return x
                            x = 1
                        {after}
                """
                self.type_error(codestr, rf"reveal_type\(x\): '{re.escape(typ)}'")

    def test_type_widened_in_for_loop(self) -> None:
        for loc, typ in [("inside", "int"), ("after", "Optional[int]")]:
            with self.subTest(loc=loc, typ=typ):
                if loc == "inside":
                    inner = "reveal_type(x)"
                    after = ""
                else:
                    inner = ""
                    after = "reveal_type(x)"
                codestr = f"""
                    def f() -> int:
                        x: int | None = None
                        for i in [0, 1, 2]:
                            if x is not None:
                                {inner}
                                return x
                            x = 1
                        {after}
                """
                self.type_error(codestr, rf"reveal_type\(x\): '{re.escape(typ)}'")

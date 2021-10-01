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
            codestr, f"reveal_type\(y\): 'Optional\[int\]'", at="reveal_type"
        )

    def test_if_exp_same_type(self) -> None:
        codestr = """
            class C: pass

            x = C() if a else C()
            reveal_type(x)
        """
        self.type_error(codestr, f"reveal_type\(x\): '<module>.C'", at="reveal_type")

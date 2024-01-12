from compiler.errors import TypedSyntaxError

from .common import StaticTestBase


class WalrusOperatorTests(StaticTestBase):
    def test_walrus_operator(self):
        codestr = """
        def fn(a: int) -> bool:
            if b := a - 1:
                return True
            return False
        """
        with self.in_module(codestr) as mod:
            fn = mod.fn
            self.assertEqual(fn(2), True)
            self.assertEqual(fn(1), False)

    def test_walrus_operator_with_decl(self):
        codestr = """
        def fn(a: int) -> bool:
            b: int
            if b := a - 1:
                return True
            return False
        """
        with self.in_module(codestr) as mod:
            fn = mod.fn
            self.assertEqual(fn(2), True)
            self.assertEqual(fn(1), False)

    def test_walrus_operator_with_final_decl(self):
        codestr = """
        from typing import Final

        def fn(a: int) -> bool:
            b: Final[int]
            if b := a - 1:
                return True
            return False
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot assign to a Final variable"
        ):
            self.compile(codestr)

    def test_walrus_operator_type_assigned(self):
        codestr = """
        from typing import Final

        def fn() -> bool:
            if b := 2 - 1:
                reveal_type(b)
        """
        with self.assertRaisesRegex(TypedSyntaxError, r"Literal\[1\]"):
            self.compile(codestr)

    def test_walrus_operator_with_ctx(self):
        codestr = """
        from typing import Final

        def fn() -> None:
            x: int = not(b := 2 - 1)
            reveal_type(b)
        """
        with self.assertRaisesRegex(TypedSyntaxError, r"Literal\[1\]"):
            self.compile(codestr)

    def test_walrus_operator_post_type(self):
        codestr = """
        from typing import Final

        def fn() -> None:
            return (b:= 2)
        """
        with self.assertRaisesRegex(TypedSyntaxError, r"return type must be None, not"):
            self.compile(codestr)

    def test_walrus_operator_post_type_primitive(self):
        codestr = """
        from __static__ import int64

        def fn() -> int64:
            b: int64
            return (b:= 2)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.fn(), 2)

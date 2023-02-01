from compiler.errors import TypedSyntaxError

from .common import StaticTestBase


class SlotInferenceTests(StaticTestBase):
    def test_attribute_type_inferred_from_arg(self) -> None:
        codestr = """
            class C:
                def __init__(self, x: int) -> None:
                    self.y = x

            def foo(num: int) -> int:
                c = C(num)
                return c.y
        """
        with self.in_strict_module(codestr) as mod:
            self.assertNotInBytecode(mod.foo, "CAST")
            self.assertEqual(mod.foo(3), 3)

    def test_attribute_type_cannot_be_inferred_from_arg(self) -> None:
        codestr = """
            class C:
                def __init__(self, x) -> None:
                    self.y = x

            def foo(num: int) -> int:
                c = C(num)
                return c.y
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(mod.foo, "CAST", (("builtins", "int")))
            self.assertEqual(mod.foo(3), 3)

    def test_attribute_type_inference_ignored_if_slot_has_inherited_type(self) -> None:
        codestr = """
            from typing import Optional
            class C:
                def __init__(self, x: Optional[int]) -> None:
                    self.y = x
                    self.z = x

            class D(C):
                def __init__(self, x: int) -> None:
                    self.y = x 
                    self.z = x 

            def foo(num: int):
                d = D(num)
                d.z = None
                reveal_type(d.y)
        """
        self.revealed_type(codestr, "Optional[int]")

    def test_attribute_type_cannot_be_reassigned_to_incompatible_type(self) -> None:
        codestr = """
            from typing import Optional
            class C:
                def __init__(self, x: Optional[int]) -> None:
                    self.y = x

            class D(C):
                def __init__(self, x: str) -> None:
                    self.y = x
        """
        self.type_error(
            codestr,
            r"type mismatch: str cannot be assigned to Optional\[int\]",
            at="self.y = x",
        )

import dis
from compiler.static.types import _TMP_VAR_PREFIX, TypedSyntaxError
from unittest import skip

from .common import StaticTestBase


class RefineFieldsTests(StaticTestBase):
    def test_can_refine_loaded_field(self) -> None:
        codestr = """
            class C:
                def __init__(self) -> None:
                    self.x: int | None = None

                def f(self) -> None:
                   if self.x is not None:
                       reveal_type(self.x)
        """
        self.revealed_type(codestr, "int")

    def test_cannot_refine_property(self) -> None:
        codestr = """
            class C:
                @property
                def x(self) -> int | None:
                    return 42

                def f(self) -> None:
                   if self.x is not None:
                       reveal_type(self.x)
        """
        self.revealed_type(codestr, "Optional[int]")

    def test_refinements_are_invalidated_with_calls(self) -> None:
        codestr = """
            class C:
                def __init__(self) -> None:
                    self.x: int | None = None

                def f(self) -> None:
                   if self.x is not None:
                       open("a.py")
                       reveal_type(self.x)
        """
        self.revealed_type(codestr, "Optional[int]")

    def test_refinements_are_invalidated_with_stores(self) -> None:
        codestr = """
            class C:
                def __init__(self) -> None:
                    self.x: int | None = None

                def f(self) -> None:
                   if self.x is not None:
                       self.x = None
                       reveal_type(self.x)
        """
        self.revealed_type(codestr, "Exact[None]")

    def test_refinements_restored_after_write_with_interfering_calls(self) -> None:
        codestr = """
            class C:
                def __init__(self) -> None:
                    self.x: int | None = None

                def f(self) -> None:
                   if self.x is not None:
                       self.x = None
                       open("a.py")
                       reveal_type(self.x)
        """
        self.revealed_type(codestr, "Optional[int]")

    def test_refinements_are_not_invalidated_with_known_safe_attr_stores(self) -> None:
        codestr = """
            class C:
                def __init__(self) -> None:
                    self.x: int | None = None
                    self.y: None = None

                def f(self) -> None:
                   if self.x is not None:
                       self.y = None
                       reveal_type(self.x)
        """
        self.revealed_type(codestr, "int")

    def test_refinements_are_invalidated_with_unknown_attr_stores(self) -> None:
        codestr = """
            class C:
                def __init__(self) -> None:
                    self.x: int | None = None

                def f(self, other) -> None:
                   if self.x is not None:
                       other.y = None
                       reveal_type(self.x)
        """
        self.revealed_type(codestr, "Optional[int]")

    def test_refinements_are_preserved_with_simple_assignments(self) -> None:
        codestr = """
            class C:
                def __init__(self) -> None:
                    self.x: int | None = None
                    self.y: None = None

                def f(self) -> None:
                   if self.x is not None:
                       a = self.x
                       reveal_type(self.x)
        """
        self.revealed_type(codestr, "int")

    def test_isinstance_refinement(self) -> None:
        codestr = """
            class C:
                def __init__(self) -> None:
                    self.x: int | None = None
                    self.y: None = None

                def f(self) -> None:
                   if isinstance(self.x, int):
                       reveal_type(self.x)
        """
        self.revealed_type(codestr, "int")

    def test_refinements_cleared_when_merging_branches(self) -> None:
        codestr = """
            class C:
                def __init__(self) -> None:
                    self.x: int | None = None

                def f(self) -> None:
                   if self.x is not None:
                      pass
                   reveal_type(self.x)
        """
        self.revealed_type(codestr, "Optional[int]")

    def test_type_not_refined_outside_while_loop(self) -> None:
        codestr = """
            class C:
                def __init__(self) -> None:
                    self.x: int | None = None

                def f(self) -> None:
                   while self.x is None:
                       pass
                   reveal_type(self.x)
        """
        self.revealed_type(codestr, "Optional[int]")

    def test_type_not_refined_when_visiting_name(self) -> None:
        codestr = """
            class C:
                def __init__(self) -> None:
                    self.x: int | None = None

                def f(self) -> None:
                   if self.x:
                       reveal_type(self.x)
        """
        self.revealed_type(codestr, "Optional[int]")

    def test_type_not_refined_for_attribute_test_with_custom_bool(self) -> None:
        codestr = """
            class D:
                def __bool__(self) -> bool:
                    return True

            class C:
                def __init__(self) -> None:
                    self.x: D | None = None

                def f(self) -> None:
                   if self.x:
                       reveal_type(self.x)
        """
        self.revealed_type(codestr, "Optional[<module>.D]")

    def test_type_not_refined_for_attribute_test_without_custom_bool(self) -> None:
        # We might want to support this in the future for final classes.
        codestr = """
            class D:
                pass

            class C:
                def __init__(self) -> None:
                    self.x: D | None = None

                def f(self) -> None:
                   if self.x:
                       reveal_type(self.x)
        """
        self.revealed_type(codestr, "Optional[<module>.D]")

    def test_type_refined_after_if_branch(self) -> None:
        codestr = """
            class C:
                def __init__(self) -> None:
                    self.x: int | None = None

                def f(self) -> None:
                   if self.x is None:
                      self.x = 4
                   reveal_type(self.x)
        """
        self.revealed_type(codestr, "int")

    def test_refined_field_codegen(self) -> None:
        codestr = """
            class C:
                def __init__(self, x: int | None) -> None:
                    self.x: int | None = x

                def f(self) -> int | None:
                   if self.x is not None:
                       a = self.x
                       return a * 2
        """
        with self.in_module(codestr) as mod:
            # Write to the temp.
            self.assertInBytecode(mod.C.f, "STORE_FAST")
            # Load from the temp directly in the second read.
            self.assertInBytecode(mod.C.f, "LOAD_FAST")
            self.assertEqual(mod.C(21).f(), 42)

    def test_refinements_cleared_in_if_with_implicit_bool(self) -> None:
        codestr = """
            class C:
                def __init__(self) -> None:
                    self.x: int | None = None

                def f(self, y) -> None:
                   if self.x is not None:
                       if y:
                           reveal_type(self.x)
        """
        self.revealed_type(codestr, "Optional[int]")

    def test_refinements_cleared_in_assert_with_implicit_bool(self) -> None:
        codestr = """
            class C:
                def __init__(self) -> None:
                    self.x: int | None = None

                def f(self, y) -> None:
                   if self.x is not None:
                       assert(y)
                       reveal_type(self.x)
        """
        self.revealed_type(codestr, "Optional[int]")

    def test_refined_field_assert_unoptimized(self) -> None:
        codestr = """
            class C:
                def __init__(self, x: int | None) -> None:
                    self.x: int | None = x

                def f(self) -> int:
                   assert self.x is not None
                   return self.x
        """
        with self.in_module(codestr) as mod:
            # Write to the temp.
            self.assertInBytecode(mod.C.f, "STORE_FAST")
            # Load from the temp directly in the second read.
            self.assertInBytecode(mod.C.f, "LOAD_FAST")
            self.assertEqual(mod.C(21).f(), 21)

    def test_refined_field_assert_optimized(self) -> None:
        codestr = """
            class C:
                def __init__(self, x: int | None) -> None:
                    self.x: int | None = x

                def f(self) -> int:
                   assert self.x is not None
                   return self.x
        """
        with self.in_module(codestr, optimize=2) as mod:
            # Write to the temp.
            self.assertInBytecode(mod.C.f, "STORE_FAST")
            # Load from the temp directly in the second read.
            self.assertInBytecode(mod.C.f, "LOAD_FAST")
            self.assertInBytecode(mod.C.f, "CAST")
            self.assertEqual(mod.C(21).f(), 21)

    def test_field_not_refined_if_one_branch_is_unrefined(self) -> None:
        codestr = """
            class C:
                def __init__(self, x: int | None) -> None:
                    self.x: int | None = x

                def f(self, b) -> int:
                   if b:
                       assert self.x is not None
                   reveal_type(self.x)
        """
        self.revealed_type(codestr, "Optional[int]")

    def test_refined_field_if_merge_branch_to_default(self) -> None:
        codestr = """
            class C:
                def __init__(self, x: int | None) -> None:
                    self.x: int | None = x

                def f(self, b: bool) -> int:
                   assert self.x is not None
                   if b:
                       assert self.x is not None
                   reveal_type(self.x)
        """
        self.revealed_type(codestr, "int")

    def test_fields_not_refined_if_dunder_bool_called_in_if(self) -> None:
        codestr = """
            class C:
                def __init__(self, x: int | None) -> None:
                    self.x: int | None = x

                def f(self, b) -> int:
                   assert self.x is not None
                   if b:
                       assert self.x is not None
                   reveal_type(self.x)
        """
        self.revealed_type(codestr, "Optional[int]")

    def test_refined_field_if_merge_branch_to_orelse(self) -> None:
        codestr = """
            class C:
                def __init__(self, x: int | None) -> None:
                    self.x: int | None = x

                def f(self, b) -> int:
                   if b:
                       assert self.x is not None
                   else:
                       assert self.x is not None
                   reveal_type(self.x)
        """
        self.revealed_type(codestr, "int")

    def test_refined_field_if_merge_branch_to_default_codegen(self) -> None:
        codestr = """
            class C:
                def __init__(self, x: int | None) -> None:
                    self.x: int | None = x

                def f(self, b: bool) -> int:
                   if self.x is None:
                       open("a.py") # Add a call to clear refinements.
                       assert self.x is not None
                   return self.x
        """
        with self.in_module(codestr) as mod:
            refined_write_count = 0
            tmp_name = f"{_TMP_VAR_PREFIX}.__refined_field__.0"
            for instr in dis.get_instructions(mod.C.f):
                if instr.opname == "STORE_FAST" and instr.argval == tmp_name:
                    refined_write_count += 1
            # Ensure that we have a refined write in both branches.
            self.assertEqual(refined_write_count, 2)
            self.assertInBytecode(mod.C.f, "LOAD_FAST", tmp_name)

    def test_refined_field_if_merge_branch_to_orelse_codegen(self) -> None:
        codestr = """
            class C:
                def __init__(self, x: int | None) -> None:
                    self.x: int | None = x

                def f(self, b) -> int:
                   if b:
                       assert self.x is not None
                   else:
                       assert self.x is not None
                   return self.x
        """
        with self.in_module(codestr) as mod:
            refined_write_count = 0
            tmp_name = f"{_TMP_VAR_PREFIX}.__refined_field__.0"
            for instr in dis.get_instructions(mod.C.f):
                if instr.opname == "STORE_FAST" and instr.argval == tmp_name:
                    refined_write_count += 1
            # Ensure that we have a refined write in both branches.
            self.assertEqual(refined_write_count, 2)
            self.assertInBytecode(mod.C.f, "LOAD_FAST", tmp_name)

    def test_refined_field_if_merge_branch_to_orelse_no_refinement(self) -> None:
        codestr = """
            from typing import Optional
            class C:

                def __init__(self, x: int | None) -> None:
                    self.x: int | None = x

                def f(self, b) -> Optional[int]:
                   if b:
                       assert self.x is not None
                   else:
                       assert self.x is not None
                   open("a.py")
                   return self.x
        """
        with self.in_module(codestr) as mod:
            refined_write_count = 0
            tmp_name = f"{_TMP_VAR_PREFIX}.__refined_field__.1"
            for instr in dis.get_instructions(mod.C.f):
                if instr.opname == "STORE_FAST" and instr.argval == tmp_name:
                    refined_write_count += 1
            # Ensure that we don't have any refinements without usees.
            self.assertEqual(refined_write_count, 0)
            self.assertNotInBytecode(mod.C.f, "LOAD_FAST", tmp_name)

    def test_refined_field_while_merge_branch(self) -> None:
        codestr = """
            class C:
                def __init__(self, x: int | None) -> None:
                    self.x: int | None = x

                def f(self, b) -> int:
                   assert self.x is not None
                   while b is not None:
                       b = not b
                       assert self.x is not None
                   reveal_type(self.x)
        """
        self.revealed_type(codestr, "int")

    def test_refined_field_when_storing(self) -> None:
        codestr = """
            class C:
                def __init__(self, x: int | None) -> None:
                    self.x: int | None = x

                def f(self, x: int) -> int:
                   self.x = x
                   return self.x
        """
        with self.in_module(codestr) as mod:
            c = mod.C(21)
            self.assertEqual(c.x, 21)
            self.assertEqual(c.f(42), 42)
            self.assertEqual(c.x, 42)

    def test_refined_field_at_source_codegen(self) -> None:
        codestr = """
            class C:
                def __init__(self, x: int | None) -> None:
                    self.x: int | None = x
                    self.y: int | None = None

                def f(self) -> int:
                   if self.x is None or self.y is None:
                      return 2
                   return 3
        """
        with self.in_module(codestr) as mod:
            c = mod.C(None)
            self.assertEqual(c.f(), 2)
            # Ensure that we don't emit a store for the refined field since there's no use.
            self.assertNotInBytecode(mod.C.f, "STORE_FAST")

    def test_refined_field_at_source_used_codegen(self) -> None:
        codestr = """
            class C:
                def __init__(self, x: int | None) -> None:
                    self.x: int | None = x
                    self.y: int | None = None

                def f(self) -> int:
                   if self.x is not None and self.y is None:
                      return self.x
                   return 3
        """
        with self.in_module(codestr) as mod:
            c = mod.C(42)
            self.assertEqual(c.f(), 42)
            # Ensure that we don't emit a store for the refined field since there's no use.
            self.assertInBytecode(mod.C.f, "STORE_FAST")
            self.assertInBytecode(mod.C.f, "LOAD_FAST")

from compiler.static.types import TypedSyntaxError, _TMP_VAR_PREFIX

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
        self.revealed_type(codestr, "Optional[int]")

    def test_refinements_are_invalidated_with_unrelated_attr_stores(self) -> None:
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

    def test_type_not_refined_after_if_branch(self) -> None:
        # TODO(T116955021): We want to infer int here for self.x
        codestr = """
            class C:
                def __init__(self) -> None:
                    self.x: int | None = None

                def f(self) -> None:
                   if self.x is None:
                      self.x = 4
                   reveal_type(self.x)
        """
        self.revealed_type(codestr, "Optional[int]")

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

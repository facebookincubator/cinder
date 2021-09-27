import ast
from compiler.static import StaticCodeGenerator
from compiler.static.declaration_visitor import DeclarationVisitor
from compiler.static.symbol_table import SymbolTable
from compiler.static.types import INT_TYPE, MODULE_TYPE, TypedSyntaxError
from textwrap import dedent
from typing import List, Tuple

from .common import StaticTestBase


class ModuleTests(StaticTestBase):
    def build_symbol_table(self, modules: List[Tuple[str, str, str]]) -> SymbolTable:
        symtable = SymbolTable(StaticCodeGenerator)
        for module_name, module_path, module_code in modules:
            module_visit = DeclarationVisitor(module_name, module_path, symtable)
            module_visit.visit(ast.parse(dedent(module_code)))
            module_visit.module.finish_bind()
        return symtable

    def test_import_name(self) -> None:
        acode = """
            def foo(x: int) -> int:
               return x
        """
        bcode = """
            import a
        """
        symtable = self.build_symbol_table(
            [
                ("a", "a.py", acode),
                ("b", "b.py", bcode),
            ]
        )

        self.assertIn("b", symtable.modules)
        self.assertIn("a", symtable.modules["b"].children)
        self.assertEqual(symtable.modules["b"].children["a"].klass, MODULE_TYPE)
        self.assertEqual(symtable.modules["b"].children["a"].module_name, "a")

    def test_import_name_as(self) -> None:
        acode = """
            def foo(x: int) -> int:
               return x
        """
        bcode = """
            import a as foo
        """
        symtable = self.build_symbol_table(
            [
                ("a", "a.py", acode),
                ("b", "b.py", bcode),
            ]
        )

        self.assertIn("foo", symtable.modules["b"].children)
        self.assertEqual(symtable.modules["b"].children["foo"].klass, MODULE_TYPE)
        self.assertEqual(symtable.modules["b"].children["foo"].module_name, "a")

    def test_import_module_within_directory(self) -> None:
        abcode = """
            def foo(x: int) -> int:
               return x
        """
        ccode = """
            import a.b
        """
        symtable = self.build_symbol_table(
            [
                ("a.b", "a/b.py", abcode),
                ("c", "c.py", ccode),
            ]
        )

        self.assertIn("a", symtable.modules["c"].children)
        self.assertEqual(symtable.modules["c"].children["a"].klass, MODULE_TYPE)
        self.assertEqual(symtable.modules["c"].children["a"].module_name, "a")

    def test_import_module_within_directory_as(self) -> None:
        abcode = """
            def foo(x: int) -> int:
               return x
        """
        ccode = """
            import a.b as m
        """
        symtable = self.build_symbol_table(
            [
                ("a.b", "a/b.py", abcode),
                ("c", "c.py", ccode),
            ]
        )

        self.assertIn("m", symtable.modules["c"].children)
        self.assertEqual(symtable.modules["c"].children["m"].klass, MODULE_TYPE)
        self.assertEqual(symtable.modules["c"].children["m"].module_name, "a.b")

    def test_import_module_within_directory_from(self) -> None:
        acode = """
            pass
        """
        abcode = """
            def foo(x: int) -> int:
               return x
        """
        ccode = """
            from a import b
        """
        symtable = self.build_symbol_table(
            [
                ("a", "a/__init__.py", acode),
                ("a.b", "a/b.py", abcode),
                ("c", "c.py", ccode),
            ]
        )

        self.assertIn("b", symtable.modules["c"].children)
        self.assertEqual(symtable.modules["c"].children["b"].klass, MODULE_TYPE)
        self.assertEqual(symtable.modules["c"].children["b"].module_name, "a.b")

    def test_import_module_within_directory_from_as(self) -> None:
        acode = """
            pass
        """
        abcode = """
            def foo(x: int) -> int:
               return x
        """
        ccode = """
            from a import b as zoidberg
        """
        symtable = self.build_symbol_table(
            [
                ("a", "a/__init__.py", acode),
                ("a.b", "a/b.py", abcode),
                ("c", "c.py", ccode),
            ]
        )

        self.assertIn("zoidberg", symtable.modules["c"].children)
        self.assertEqual(symtable.modules["c"].children["zoidberg"].klass, MODULE_TYPE)
        self.assertEqual(symtable.modules["c"].children["zoidberg"].module_name, "a.b")

    def test_import_module_within_directory_from_where_value_exists(self) -> None:
        acode = """
            b: int = 1
        """
        abcode = """
            def foo(x: int) -> int:
               return x
        """
        ccode = """
            from a import b
        """
        symtable = self.build_symbol_table(
            [
                ("a", "a/__init__.py", acode),
                ("a.b", "a/b.py", abcode),
                ("c", "c.py", ccode),
            ]
        )

        self.assertIn("b", symtable.modules["c"].children)
        self.assertEqual(symtable.modules["c"].children["b"].klass, INT_TYPE)

    def test_import_module_within_directory_from_where_untyped_value_exists(
        self,
    ) -> None:
        acode = """
            b = 1
        """
        abcode = """
            def foo(x: int) -> int:
               return x
        """
        ccode = """
            from a import b
        """
        symtable = self.build_symbol_table(
            [
                ("a", "a/__init__.py", acode),
                ("a.b", "a/b.py", abcode),
                ("c", "c.py", ccode),
            ]
        )

        self.assertIn("b", symtable.modules["c"].children)
        # Note that since the declaration visitor doesn't distinguish between
        # untyped values and missing ones, we resolve to the module type where that might
        # not have been the intention.
        self.assertEqual(symtable.modules["c"].children["b"].klass, MODULE_TYPE)

    def test_import_chaining(self) -> None:
        acode = """
            def foo(x: int) -> int: return x
        """
        bcode = """
            import a
        """
        ccode = """
            import b

            def f():
               return b.a.foo(1)
        """
        symtable = self.build_symbol_table(
            [
                ("a", "a.py", acode),
                ("b", "b.py", bcode),
            ]
        )
        ccomp = symtable.compile("c", "c.py", ast.parse(dedent(ccode)))
        f = self.find_code(ccomp, "f")
        self.assertInBytecode(f, "INVOKE_FUNCTION", (("a", "foo"), 1))

    def test_module_special_name_access(self) -> None:
        acode = """
            def foo(x: int) -> int: return x
        """
        bcode = """
            import a
        """
        ccode = """
            import b

            def f():
               reveal_type(b.a.__class__)
        """
        symtable = self.build_symbol_table(
            [
                ("a", "a.py", acode),
                ("b", "b.py", bcode),
            ]
        )
        with self.assertRaisesRegex(TypedSyntaxError, "types.ModuleType"):
            ccomp = symtable.compile("c", "c.py", ast.parse(dedent(ccode)))

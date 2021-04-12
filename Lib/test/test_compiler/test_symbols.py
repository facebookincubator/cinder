import ast
from ast import FunctionDef
from compiler import walk
from compiler.consts import SC_GLOBAL_IMPLICIT
from compiler.symbols import SymbolVisitor

from .common import CompilerTest


class SymbolVisitorTests(CompilerTest):
    def test_simple_assignments(self):
        stmts = [
            "foo = 42",
            "foo += 42",
            "class foo: pass",
            "def foo(): pass",
            "async def foo(): pass",
            "del foo",
            "import foo",
            "from foo import foo",
            "import bar as foo",
            "from bar import x as foo",
            "try:\n    pass\nexcept Exception as foo:\n    pass",
        ]
        for stmt in stmts:
            module = ast.parse(stmt)
            visitor = SymbolVisitor()
            walk(module, visitor)
            self.assertIn("foo", visitor.scopes[module].defs)

    def test_comp_assignments(self):
        stmts = [
            "(42 for foo in 'abc')",
            "[42 for foo in 'abc']",
            "{42 for foo in 'abc'}",
            "{42:42 for foo in 'abc'}",
        ]
        for stmt in stmts:
            module = ast.parse(stmt)
            visitor = SymbolVisitor()
            walk(module, visitor)
            gen = module.body[0].value
            self.assertIn("foo", visitor.scopes[gen].defs)

    def test_class_kwarg_in_nested_scope(self):
        code = """def f():
            def g():
                class C(x=foo):
                    pass"""
        module = ast.parse(code)
        visitor = SymbolVisitor()
        walk(module, visitor)
        for node, scope in visitor.scopes.items():
            if isinstance(node, FunctionDef) and node.name == "f":
                self.assertEqual(scope.check_name("foo"), SC_GLOBAL_IMPLICIT)
                break
        else:
            self.fail("scope not found")

    def test_class_annotation_in_nested_scope(self):
        code = """def f():
            def g():
                @foo
                class C:
                    pass"""
        module = ast.parse(code)
        visitor = SymbolVisitor()
        walk(module, visitor)
        for node, scope in visitor.scopes.items():
            if isinstance(node, FunctionDef) and node.name == "f":
                self.assertEqual(scope.check_name("foo"), SC_GLOBAL_IMPLICIT)
                break
        else:
            self.fail("scope not found")


if __name__ == "__main__":
    unittest.main()

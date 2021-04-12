import ast
from compiler.unparse import to_expr
from compiler.visitor import ASTRewriter
from unittest import TestCase


class VisitorTests(TestCase):
    def test_rewrite_node(self):
        class TestRewriter(ASTRewriter):
            def visitName(self, node):
                if isinstance(node, ast.Name) and node.id == "z":
                    return ast.Name("foo", ast.Load())
                return node

        tree = ast.parse("x + y + z").body[0].value

        rewriter = TestRewriter()
        new_tree = rewriter.visit(tree)

        self.assertIsNotNone(new_tree)
        self.assertNotEqual(new_tree, tree, "tree should be rewritten")
        self.assertEqual(to_expr(new_tree), "x + y + foo")
        self.assertEqual(tree.left, new_tree.left, "Unchanged nodes should be the same")

    def test_rewrite_stmt(self):
        class TestRewriter(ASTRewriter):
            def visitAssign(self, node):
                return ast.AnnAssign(
                    ast.Name("foo", ast.Store), ast.Str("foo"), ast.Num(1), True
                )

        tree = ast.parse("x = 1\nf()\n")

        rewriter = TestRewriter()
        new_tree = rewriter.visit(tree)

        self.assertIsNotNone(new_tree)
        self.assertNotEqual(new_tree, tree, "tree should be rewritten")
        self.assertNotEqual(tree.body[0], new_tree.body[0])
        self.assertEqual(tree.body[1], new_tree.body[1])

    def test_remove_node(self):
        class TestRewriter(ASTRewriter):
            def visitAssign(self, node):
                return None

        tree = ast.parse("x = 1\nf()\n")

        rewriter = TestRewriter()
        new_tree = rewriter.visit(tree)

        self.assertIsNotNone(new_tree)
        self.assertEqual(tree.body[1], new_tree.body[0])
        self.assertEqual(len(new_tree.body), 1)

    def test_change_child_and_list(self):
        class TestRewriter(ASTRewriter):
            def visitarguments(self, node: ast.arguments):
                node = self.clone_node(node)
                node.vararg = ast.arg("args", None)
                return node

            def visitAssign(self, node):
                return ast.Pass()

        tree = ast.parse("def f():\n    x = 1")

        rewriter = TestRewriter()
        new_tree = rewriter.visit(tree)
        func = new_tree.body[0]
        self.assertEqual(type(func.body[0]), ast.Pass)
        self.assertIsNotNone(func.args.vararg)

    def test_change_list_and_child(self):
        class TestRewriter(ASTRewriter):
            def visitStr(self, node: ast.Str):
                return ast.Str("bar")

            def visitAssign(self, node):
                return ast.Pass()

        tree = ast.parse("def f() -> 'foo':\n    x = 1")

        rewriter = TestRewriter()
        new_tree = rewriter.visit(tree)
        func = new_tree.body[0]
        self.assertIsNotNone(func.returns)
        self.assertEqual(type(func.body[0]), ast.Pass)

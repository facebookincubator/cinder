import __future__

import ast
import inspect
import re
import unittest
from compiler import compile

from .common import CompilerTest


class ApiTests(CompilerTest):
    def test_compile_single(self):
        code = compile("42", "foo", "single")
        self.assertInBytecode(code, "LOAD_CONST", 42)
        self.assertInBytecode(code, "PRINT_EXPR")

    def test_compile_eval(self):
        code = compile("42", "foo", "eval")
        self.assertInBytecode(code, "LOAD_CONST", 42)
        self.assertInBytecode(code, "RETURN_VALUE")

    def test_bad_mode(self):
        expected = re.escape("compile() mode must be 'exec', 'eval' or 'single'")
        with self.assertRaisesRegex(ValueError, expected):
            compile("42", "foo", "foo")

    def test_compile_with_only_ast_returns_ast(self):
        code = compile("42", "foo", "exec", 0x400)  # PyCF_ONLY_AST
        self.assertIsInstance(code, ast.AST)

    def test_compile_with_unrecognized_flag_raises_value_error(self):
        expected = re.escape("compile(): unrecognised flags")
        with self.assertRaisesRegex(ValueError, expected):
            compile("42", "foo", "exec", 0x80000000000)

    def test_compile_with_future_annotations_stringifies_annotation(self):
        code = compile(
            "a: List[int] = []", "foo", "exec", __future__.CO_FUTURE_ANNOTATIONS
        )
        self.assertInBytecode(code, "LOAD_CONST", "List[int]")
        self.assertNotInBytecode(code, "LOAD_NAME", "List")
        self.assertNotInBytecode(code, "LOAD_NAME", "int")
        self.assertNotInBytecode(code, "BINARY_SUBSCR")

    def test_compile_without_future_annotations_does_type_subscript(self):
        code = compile("a: List[int] = []", "foo", "exec", 0)
        self.assertNotInBytecode(code, "LOAD_CONST", "List[int]")
        self.assertInBytecode(code, "LOAD_NAME", "List")
        self.assertInBytecode(code, "LOAD_NAME", "int")
        self.assertInBytecode(code, "BINARY_SUBSCR")

    def test_compile_with_annotation_in_except_handler_emits_store_annotation(self):
        source = inspect.cleandoc(
            """
try:
    pass
except:
    x: int = 1
"""
        )

        code = compile(
            source,
            "foo",
            "exec",
            0,
        )
        self.assertInBytecode(code, "SETUP_ANNOTATIONS")

    def test_compile_with_barry_as_bdfl_emits_ne(self):
        code = compile("a <> b", "foo", "exec", __future__.CO_FUTURE_BARRY_AS_BDFL)
        self.assertInBytecode(code, "COMPARE_OP", "!=")

    def test_compile_unoptimized(self):
        src_code = "assert True"
        code = compile(src_code, "foo", "single", optimize=0)
        self.assertInBytecode(code, "LOAD_GLOBAL", "AssertionError")
        self.assertInBytecode(code, "RAISE_VARARGS")

    def test_compile_optimized(self):
        src_code = "assert True"
        code = compile(src_code, "foo", "single", optimize=1)
        self.assertNotInBytecode(code, "LOAD_GLOBAL", "AssertionError")
        self.assertNotInBytecode(code, "RAISE_VARARGS")

    def test_compile_optimized_docstrings(self):
        """
        Ensure we strip docstrings with optimize=2, and retain them for
        optimize=1
        """

        src_code = "def f(): '''hi'''\n"
        code = compile(src_code, "foo", "single", optimize=1)
        consts = {k: v for k, v in zip(code.co_names, code.co_consts)}
        self.assertIn("hi", consts["f"].co_consts)

        code = compile(src_code, "foo", "single", optimize=2)
        consts = {k: v for k, v in zip(code.co_names, code.co_consts)}
        self.assertNotIn("hi", consts["f"].co_consts)


if __name__ == "__main__":
    unittest.main()

import __future__

import ast
import dis
import math
import sys
from compiler.consts import (
    CO_NEWLOCALS,
    CO_NOFREE,
    CO_OPTIMIZED,
)
from compiler.optimizer import AstOptimizer
from compiler.pyassem import PyFlowGraph
from compiler.pycodegen import CodeGenerator, Python37CodeGenerator
from compiler.unparse import to_expr

from .common import CompilerTest


LOAD_METHOD = "LOAD_METHOD"
if LOAD_METHOD not in dis.opmap:
    LOAD_METHOD = "<160>"

CALL_METHOD = "CALL_METHOD"
if CALL_METHOD not in dis.opmap:
    CALL_METHOD = "<161>"
STORE_ANNOTATION = "STORE_ANNOTATION"
if STORE_ANNOTATION not in dis.opmap:
    STORE_ANNOTATION = "<127>"


class Python37Tests(CompilerTest):
    def test_compile_method(self):
        code = self.compile("x.f()")
        self.assertInBytecode(code, LOAD_METHOD)
        self.assertInBytecode(code, CALL_METHOD, 0)

        code = self.compile("x.f(42)")
        self.assertInBytecode(code, LOAD_METHOD)
        self.assertInBytecode(code, CALL_METHOD, 1)

    def test_compile_method_varargs(self):
        code = self.compile("x.f(*foo)")
        self.assertNotInBytecode(code, LOAD_METHOD)

    def test_compile_method_kwarg(self):
        code = self.compile("x.f(kwarg=1)")
        self.assertNotInBytecode(code, LOAD_METHOD)

    def test_compile_method_normal(self):
        code = self.compile("f()")
        self.assertNotInBytecode(code, LOAD_METHOD)

    def test_future_gen_stop(self):
        code = self.compile("from __future__ import generator_stop")
        self.assertEqual(
            code.co_flags,
            CO_NOFREE,
        )

    def test_future_annotations_flag(self):
        code = self.compile("from __future__ import annotations")
        self.assertEqual(
            code.co_flags,
            CO_NOFREE | __future__.CO_FUTURE_ANNOTATIONS,
        )

    def test_async_aiter(self):
        # Make sure GET_AITER isn't followed by LOAD_CONST None as Python 3.7 doesn't support async __aiter__
        outer_graph = self.to_graph(
            """
            async def f():
                async for x in y:
                    pass
        """
        )
        for outer_instr in self.graph_to_instrs(outer_graph):
            if outer_instr.opname == "LOAD_CONST" and isinstance(
                outer_instr.oparg, CodeGenerator
            ):
                saw_aiter = False
                for instr in self.graph_to_instrs(outer_instr.oparg.graph):
                    if saw_aiter:
                        self.assertNotEqual(instr.opname, "LOAD_CONST")
                        break

                    if instr.opname == "GET_AITER":
                        saw_aiter = True
                break

    def test_try_except_pop_except(self):
        """POP_EXCEPT moved after END_FINALLY in Python 3.7"""
        graph = self.to_graph(
            """
            try:
                pass
            except Exception as e:
                pass
        """
        )
        prev_instr = None
        for instr in self.graph_to_instrs(graph):
            if instr.opname == "POP_EXCEPT":
                self.assertEqual(prev_instr.opname, "END_FINALLY", prev_instr.opname)
            prev_instr = instr

    def test_func_doc_str_lnotab(self):
        test_code = """
            def f():

                '''hello there

                '''
            """

        code = self.find_code(self.compile(test_code))
        if sys.version_info >= (3, 8):
            expected = b"\x00\x02"
        elif sys.version_info >= (3, 7):
            expected = b"\x00\x04"
        else:
            expected = b""
        self.assertEqual(code.co_lnotab, expected)

    def test_future_annotations(self):
        annotations = ["42"]
        for annotation in annotations:
            code = self.compile(
                f"from __future__ import annotations\ndef f() -> {annotation}:\n    pass"
            )
            self.assertInBytecode(code, "LOAD_CONST", annotation)
        self.assertEqual(
            code.co_flags,
            CO_NOFREE | __future__.CO_FUTURE_ANNOTATIONS,
        )

    def test_circular_import_as(self):
        """verifies that we emit an IMPORT_FROM to enable circular imports
        when compiling an absolute import to verify that they can support
        circular imports"""
        code = self.compile(f"import x.y as b")
        self.assertInBytecode(code, "IMPORT_FROM")
        self.assertNotInBytecode(code, "LOAD_ATTR")

    def test_store_annotation_removed(self):
        code = self.compile(f"class C:\n    x: int = 42")
        class_code = self.find_code(code)
        self.assertNotInBytecode(class_code, STORE_ANNOTATION)

    def test_compile_opt_unary_jump(self):
        graph = self.to_graph("if not abc: foo")
        self.assertNotInGraph(graph, "POP_JUMP_IF_FALSE")

    def test_compile_opt_bool_or_jump(self):
        graph = self.to_graph("if abc or bar: foo")
        self.assertNotInGraph(graph, "JUMP_IF_TRUE_OR_POP")

    def test_compile_opt_bool_and_jump(self):
        graph = self.to_graph("if abc and bar: foo")
        self.assertNotInGraph(graph, "JUMP_IF_FALSE_OR_POP")

    def test_compile_opt_assert_or_bool(self):
        graph = self.to_graph("assert abc or bar")
        self.assertNotInGraph(graph, "JUMP_IF_TRUE_OR_POP")

    def test_compile_opt_assert_and_bool(self):
        graph = self.to_graph("assert abc and bar")
        self.assertNotInGraph(graph, "JUMP_IF_FALSE_OR_POP")

    def test_compile_opt_if_exp(self):
        graph = self.to_graph("assert not a if c else b")
        self.assertNotInGraph(graph, "UNARY_NOT")

    def test_compile_opt_cmp_op(self):
        graph = self.to_graph("assert not a > b")
        self.assertNotInGraph(graph, "UNARY_NOT")

    def test_compile_opt_chained_cmp_op(self):
        graph = self.to_graph("assert not a > b > c")
        self.assertNotInGraph(graph, "UNARY_NOT")

    def test_const_fold(self):
        code = "{" + "**{}, " * 256 + "}"
        graph = self.to_graph(code)
        self.assertInGraph(graph, "BUILD_MAP_UNPACK", 256)

import ast
import dis
import math
import sys
import unittest
from compiler.consts import (
    CO_NEWLOCALS,
    CO_NOFREE,
    CO_OPTIMIZED,
)
from compiler.optimizer import AstOptimizer
from compiler.pyassem import PyFlowGraph
from compiler.pycodegen import CodeGenerator, Python37CodeGenerator
from compiler.unparse import to_expr
from unittest import skipIf

from .common import CompilerTest


@unittest.skipIf(sys.version_info < (3, 7), "AST optimizer introduced in 3.7")
class AstOptimizerTests(CompilerTest):
    class _Comparer:
        def __init__(self, code, test):
            self.code = code
            self.test = test
            self.opt = self.test.to_graph(code)
            self.notopt = self.test.to_graph_no_opt(code)

        def assert_both(self, *args):
            self.test.assertInGraph(
                self.notopt, *args
            )  # should be present w/o peephole
            self.test.assertInGraph(self.opt, *args)  # should be present w/ peephole

        def assert_neither(self, *args):
            self.test.assertNotInGraph(
                self.notopt, *args
            )  # should be absent w/o peephole
            self.test.assertNotInGraph(self.opt, *args)  # should be absent w/ peephole

        def assert_removed(self, *args):
            self.test.assertInGraph(
                self.notopt, *args
            )  # should be present w/o peephole
            self.test.assertNotInGraph(self.opt, *args)  # should be removed w/ peephole

        def assert_added(self, *args):
            self.test.assertInGraph(self.opt, *args)  # should be added w/ peephole
            self.test.assertNotInGraph(
                self.notopt, *args
            )  # should be absent w/o peephole

        def get_instructions(self, graph):
            return [
                instr
                for block in graph.getBlocks()
                for instr in block.getInstructions()
            ]

        def assert_all_removed(self, *args):
            for instr in self.get_instructions(self.opt):
                for arg in args:
                    self.test.assertFalse(instr.opname.startswith(arg))
            for instr in self.get_instructions(self.notopt):
                for arg in args:
                    if instr.opname.startswith(arg):
                        return
            disassembly = self.test.dump_graph(self.notopt)
            self.test.fail(
                "no args were present: " + ", ".join(args) + "\n" + disassembly
            )

        def assert_in_opt(self, *args):
            self.test.assertInGraph(self.opt, *args)

        def assert_not_in_opt(self, *args):
            self.test.assertNotInGraph(self.opt, *args)

        def assert_instr_count(self, opcode, before, after):
            before_instrs = [
                instr
                for instr in self.get_instructions(self.notopt)
                if instr.opname == opcode
            ]
            self.test.assertEqual(len(before_instrs), before)
            after_instrs = [
                instr
                for instr in self.get_instructions(self.opt)
                if instr.opname == opcode
            ]
            self.test.assertEqual(len(after_instrs), after)

    def compare_graph(self, code):
        return AstOptimizerTests._Comparer(code, self)

    def to_graph_no_opt(self, code):
        return self.to_graph(code, ast_optimizer_enabled=False)

    def test_compile_opt_enabled(self):
        graph = self.to_graph("x = -1")
        self.assertNotInGraph(graph, "UNARY_NEGATIVE")

        graph = self.to_graph_no_opt("x = -1")
        self.assertInGraph(graph, "UNARY_NEGATIVE")

    def test_opt_debug(self):
        graph = self.to_graph("if not __debug__:\n    x = 42")
        self.assertNotInGraph(graph, "STORE_NAME")

        graph = self.to_graph_no_opt("if not __debug__:\n    x = 42")
        self.assertInGraph(graph, "STORE_NAME")

    def test_opt_debug_del(self):
        code = "def f(): del __debug__"
        outer_graph = self.to_graph(code)
        for outer_instr in self.graph_to_instrs(outer_graph):
            if outer_instr.opname == "LOAD_CONST" and isinstance(
                outer_instr.oparg, CodeGenerator
            ):
                graph = outer_instr.oparg.graph
                self.assertInGraph(graph, "LOAD_CONST", True)
                self.assertNotInGraph(graph, "DELETE_FAST", "__debug__")

        outer_graph = self.to_graph_no_opt(code)
        for outer_instr in self.graph_to_instrs(outer_graph):
            if outer_instr.opname == "LOAD_CONST" and isinstance(
                outer_instr.oparg, CodeGenerator
            ):
                graph = outer_instr.oparg.graph
                self.assertNotInGraph(graph, "LOAD_CONST", True)
                self.assertInGraph(graph, "DELETE_FAST", "__debug__")

    def test_const_fold(self):
        code = self.compile("x = 0.0\ny=-0.0")
        self.assertEqual(code.co_consts, (0.0, -0.0, None))
        self.assertEqual(math.copysign(1, code.co_consts[0]), 1)
        self.assertEqual(math.copysign(1, code.co_consts[1]), -1)

    def test_const_fold_tuple(self):
        code = self.compile("x = (0.0, )\ny=(-0.0, )")
        self.assertEqual(code.co_consts, ((0.0,), (-0.0,), None))
        self.assertEqual(math.copysign(1, code.co_consts[0][0]), 1)
        self.assertEqual(math.copysign(1, code.co_consts[1][0]), -1)

    def test_ast_optimizer(self):
        cases = [
            ("+1", "1"),
            ("--1", "1"),
            ("~1", "-2"),
            ("not 1", "False"),
            ("not x is y", "x is not y"),
            ("not x is not y", "x is y"),
            ("not x in y", "x not in y"),
            ("~1.1", "~1.1"),
            ("+'str'", "+'str'"),
            ("1 + 2", "3"),
            ("1 + 3", "4"),
            ("'abc' + 'def'", "'abcdef'"),
            ("b'abc' + b'def'", "b'abcdef'"),
            ("b'abc' + 'def'", "b'abc' + 'def'"),
            ("b'abc' + --2", "b'abc' + 2"),
            ("--2 + 'abc'", "2 + 'abc'"),
            ("5 - 3", "2"),
            ("6 - 3", "3"),
            ("2 * 2", "4"),
            ("2 * 3", "6"),
            ("'abc' * 2", "'abcabc'"),
            ("b'abc' * 2", "b'abcabc'"),
            ("1 / 2", "0.5"),
            ("6 / 2", "3.0"),
            ("6 // 2", "3"),
            ("5 // 2", "2"),
            ("2 >> 1", "1"),
            ("6 >> 1", "3"),
            ("1 | 2", "3"),
            ("1 | 1", "1"),
            ("1 ^ 3", "2"),
            ("1 ^ 1", "0"),
            ("1 & 2", "0"),
            ("1 & 3", "1"),
            ("'abc' + 1", "'abc' + 1"),
            ("1 / 0", "1 / 0"),
            ("1 + None", "1 + None"),
            ("True + None", "True + None"),
            ("True + 1", "2"),
            ("(1, 2)", "(1, 2)"),
            ("(1, 2) * 2", "(1, 2, 1, 2)"),
            ("(1, --2, abc)", "(1, 2, abc)"),
            ("(1, 2)[0]", "1"),
            ("1[0]", "1[0]"),
            ("x[+1]", "x[1]"),
            ("(+1)[x]", "1[x]"),
            ("[x for x in [1,2,3]]", "[x for x in (1, 2, 3)]"),
            ("(x for x in [1,2,3])", "(x for x in (1, 2, 3))"),
            ("{x for x in [1,2,3]}", "{x for x in (1, 2, 3)}"),
            ("{x for x in [--1,2,3]}", "{x for x in (1, 2, 3)}"),
            ("{--1 for x in [1,2,3]}", "{1 for x in (1, 2, 3)}"),
            ("x in [1,2,3]", "x in (1, 2, 3)"),
            ("x in x in [1,2,3]", "x in x in (1, 2, 3)"),
            ("x in [1,2,3] in x", "x in [1, 2, 3] in x"),
        ]
        for inp, expected in cases:
            optimizer = AstOptimizer()
            tree = ast.parse(inp)
            optimized = to_expr(optimizer.visit(tree).body[0].value)
            self.assertEqual(expected, optimized, "Input was: " + inp)

    def test_ast_optimizer_for(self):
        optimizer = AstOptimizer()
        tree = ast.parse("for x in [1,2,3]: pass")
        optimized = optimizer.visit(tree).body[0]
        self.assertEqual(to_expr(optimized.iter), "(1, 2, 3)")

    @skipIf(sys.version_info < (3, 8), "This optimization is only for Python 3.8+")
    def test_fold_nonconst_list_to_tuple_in_comparisons(self):
        optimizer = AstOptimizer()
        tree = ast.parse("[a for a in b if a.c in [e, f]]")
        optimized = optimizer.visit(tree)
        self.assertEqual(
            to_expr(optimized.body[0].value.generators[0].ifs[0].comparators[0]),
            "(e, f)",
        )

    def test_assert_statements(self):
        optimizer = AstOptimizer(optimize=True)
        non_optimizer = AstOptimizer(optimize=False)
        code = """def f(a, b): assert a == b, 'lol'"""
        tree = ast.parse(code)
        optimized = optimizer.visit(tree)
        # Function body should be empty
        self.assertListEqual(optimized.body[0].body, [])

        unoptimized = non_optimizer.visit(tree)
        # Function body should contain the assert
        self.assertIsInstance(unoptimized.body[0].body[0], ast.Assert)

    @unittest.skipIf(sys.version_info < (3, 7), "3.6 does this in peephole")
    def test_folding_of_tuples_of_constants(self):
        for line, elem in (
            ("a = 1,2,3", (1, 2, 3)),
            ('a = ("a","b","c")', ("a", "b", "c")),
            ("a,b,c = 1,2,3", (1, 2, 3)),
            ("a = (None, 1, None)", (None, 1, None)),
            ("a = ((1, 2), 3, 4)", ((1, 2), 3, 4)),
        ):
            code = self.compare_graph(line)
            code.assert_added("LOAD_CONST", elem)
            code.assert_removed("BUILD_TUPLE")

        # Long tuples should be folded too.
        code = self.compare_graph("x=" + repr(tuple(range(10000))))
        code.assert_removed("BUILD_TUPLE")
        # One LOAD_CONST for the tuple, one for the None return value
        code.assert_instr_count("LOAD_CONST", 10001, 2)

        # Bug 1053819:  Tuple of constants misidentified when presented with:
        # . . . opcode_with_arg 100   unary_opcode   BUILD_TUPLE 1  . . .
        # The following would segfault upon compilation
        def crater():
            (
                ~[
                    0,
                    1,
                    2,
                    3,
                    4,
                    5,
                    6,
                    7,
                    8,
                    9,
                    0,
                    1,
                    2,
                    3,
                    4,
                    5,
                    6,
                    7,
                    8,
                    9,
                    0,
                    1,
                    2,
                    3,
                    4,
                    5,
                    6,
                    7,
                    8,
                    9,
                    0,
                    1,
                    2,
                    3,
                    4,
                    5,
                    6,
                    7,
                    8,
                    9,
                    0,
                    1,
                    2,
                    3,
                    4,
                    5,
                    6,
                    7,
                    8,
                    9,
                    0,
                    1,
                    2,
                    3,
                    4,
                    5,
                    6,
                    7,
                    8,
                    9,
                    0,
                    1,
                    2,
                    3,
                    4,
                    5,
                    6,
                    7,
                    8,
                    9,
                    0,
                    1,
                    2,
                    3,
                    4,
                    5,
                    6,
                    7,
                    8,
                    9,
                    0,
                    1,
                    2,
                    3,
                    4,
                    5,
                    6,
                    7,
                    8,
                    9,
                    0,
                    1,
                    2,
                    3,
                    4,
                    5,
                    6,
                    7,
                    8,
                    9,
                ],
            )

    @unittest.skipIf(sys.version_info < (3, 7), "3.6 does this in peephole")
    def test_folding_of_lists_of_constants(self):
        for line, elem in (
            # in/not in constants with BUILD_LIST should be folded to a tuple:
            ("a in [1,2,3]", (1, 2, 3)),
            ('a not in ["a","b","c"]', ("a", "b", "c")),
            ("a in [None, 1, None]", (None, 1, None)),
            ("a not in [(1, 2), 3, 4]", ((1, 2), 3, 4)),
        ):
            code = self.compare_graph(line)
            code.assert_added("LOAD_CONST", elem)
            code.assert_removed("BUILD_LIST")

    @unittest.skipIf(sys.version_info < (3, 7), "3.6 does this in peephole")
    def test_folding_of_sets_of_constants(self):
        for line, elem in (
            # in/not in constants with BUILD_SET should be folded to a frozenset:
            ("a in {1,2,3}", frozenset({1, 2, 3})),
            ('a not in {"a","b","c"}', frozenset({"a", "c", "b"})),
            ("a in {None, 1, None}", frozenset({1, None})),
            ("a not in {(1, 2), 3, 4}", frozenset({(1, 2), 3, 4})),
            ("a in {1, 2, 3, 3, 2, 1}", frozenset({1, 2, 3})),
        ):
            code = self.compare_graph(line)
            code.assert_removed("BUILD_SET")
            code.assert_added("LOAD_CONST", elem)

        # Ensure that the resulting code actually works:
        d = self.run_code(
            """
        def f(a):
            return a in {1, 2, 3}

        def g(a):
            return a not in {1, 2, 3}"""
        )
        f, g = d["f"], d["g"]
        self.assertTrue(f(3))
        self.assertTrue(not f(4))

        self.assertTrue(not g(3))
        self.assertTrue(g(4))

    @unittest.skipIf(sys.version_info < (3, 7), "3.6 does this in peephole")
    def test_folding_of_binops_on_constants(self):
        for line, elem in (
            ("a = 2+3+4", 9),  # chained fold
            ('a = "@"*4', "@@@@"),  # check string ops
            ('a="abc" + "def"', "abcdef"),  # check string ops
            ("a = 3**4", 81),  # binary power
            ("a = 3*4", 12),  # binary multiply
            ("a = 13//4", 3),  # binary floor divide
            ("a = 14%4", 2),  # binary modulo
            ("a = 2+3", 5),  # binary add
            ("a = 13-4", 9),  # binary subtract
            # ('a = (12,13)[1]', 13),             # binary subscr
            ("a = 13 << 2", 52),  # binary lshift
            ("a = 13 >> 2", 3),  # binary rshift
            ("a = 13 & 7", 5),  # binary and
            ("a = 13 ^ 7", 10),  # binary xor
            ("a = 13 | 7", 15),  # binary or
            ("a = 2 ** -14", 6.103515625e-05),  # binary power neg rhs
        ):
            code = self.compare_graph(line)
            code.assert_added("LOAD_CONST", elem)
            code.assert_all_removed("BINARY_")

        # Verify that unfoldables are skipped
        code = self.compare_graph('a=2+"b"')
        code.assert_both("LOAD_CONST", 2)
        code.assert_both("LOAD_CONST", "b")

        # Verify that large sequences do not result from folding
        code = self.compare_graph('a="x"*10000')
        code.assert_both("LOAD_CONST", 10000)
        consts = code.opt.getConsts()
        self.assertNotIn("x" * 10000, consts)
        code = self.compare_graph("a=1<<1000")
        code.assert_both("LOAD_CONST", 1000)
        self.assertNotIn(1 << 1000, consts)
        code = self.compare_graph("a=2**1000")
        code.assert_both("LOAD_CONST", 1000)
        self.assertNotIn(2 ** 1000, consts)

    @unittest.skipIf(sys.version_info < (3, 7), "3.6 does this in peephole")
    def test_binary_subscr_on_unicode(self):
        # valid code get optimized
        code = self.compare_graph('x = "foo"[0]')
        code.assert_added("LOAD_CONST", "f")
        code.assert_removed("BINARY_SUBSCR")
        code = self.compare_graph('x = "\u0061\uffff"[1]')
        code.assert_added("LOAD_CONST", "\uffff")
        code.assert_removed("BINARY_SUBSCR")

        # With PEP 393, non-BMP char get optimized
        code = self.compare_graph('x = "\U00012345"[0]')
        code.assert_both("LOAD_CONST", "\U00012345")
        code.assert_removed("BINARY_SUBSCR")

        # invalid code doesn't get optimized
        # out of range
        code = self.compare_graph('x = "fuu"[10]')
        code.assert_both("BINARY_SUBSCR")

    @unittest.skipIf(sys.version_info < (3, 7), "3.6 does this in peephole")
    def test_folding_of_unaryops_on_constants(self):
        for line, elem in (
            ("x = -0.5", -0.5),  # unary negative
            ("x = -0.0", -0.0),  # -0.0
            ("x = -(1.0-1.0)", -0.0),  # -0.0 after folding
            ("x = -0", 0),  # -0
            ("x = ~-2", 1),  # unary invert
            ("x = +1", 1),  # unary positive
        ):
            code = self.compare_graph(line)
            # can't assert added here because -0/0 compares equal
            code.assert_in_opt("LOAD_CONST", elem)
            code.assert_all_removed("UNARY_")

        # Verify that unfoldables are skipped
        for line, elem, opname in (
            ('-"abc"', "abc", "UNARY_NEGATIVE"),
            ('~"abc"', "abc", "UNARY_INVERT"),
        ):
            code = self.compare_graph(line)
            code.assert_both("LOAD_CONST", elem)
            code.assert_both(opname)

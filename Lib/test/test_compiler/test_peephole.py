import ast
import compiler.pycodegen
import dis
import opcode
import unittest
from compiler.opcodes import opcode as opcodes
from compiler.peephole import Optimizer
from dis import opmap, opname
from types import CodeType
from unittest import TestCase

from .common import CompilerTest


class PeepHoleTests(CompilerTest):
    class PeepholeComparer:
        def __init__(self, test, opt, notopt):
            self.test = test
            self.opt = opt
            self.notopt = notopt

        def assert_both(self, *args):
            self.test.assertInBytecode(
                self.notopt, *args
            )  # should be present w/o peephole
            self.test.assertInBytecode(self.opt, *args)  # should be present w/ peephole

        def assert_neither(self, *args):
            self.test.assertNotInBytecode(
                self.notopt, *args
            )  # should be absent w/o peephole
            self.test.assertNotInBytecode(
                self.opt, *args
            )  # should be absent w/ peephole

        def assert_removed(self, *args):
            self.test.assertInBytecode(
                self.notopt, *args
            )  # should be present w/o peephole
            self.test.assertNotInBytecode(
                self.opt, *args
            )  # should be removed w/ peephole

        def assert_added(self, *args):
            self.test.assertInBytecode(self.opt, *args)  # should be added w/ peephole
            self.test.assertNotInBytecode(
                self.notopt, *args
            )  # should be absent w/o peephole

        def assert_all_removed(self, *args):
            for instr in dis.get_instructions(self.opt):
                for arg in args:
                    self.test.assertFalse(instr.opname.startswith(arg))
            for instr in dis.get_instructions(self.notopt):
                for arg in args:
                    if instr.opname.startswith(arg):
                        return
            disassembly = self.test.get_disassembly_as_string(self.notopt)
            self.test.fail(
                "no args were present: " + ", ".join(args) + "\n" + disassembly
            )

        def assert_in_opt(self, *args):
            self.test.assertInBytecode(self.opt, *args)

        def assert_not_in_opt(self, *args):
            self.test.assertNotInBytecode(self.opt, *args)

        def assert_instr_count(self, opcode, before, after):
            before_instrs = [
                instr
                for instr in dis.get_instructions(self.notopt)
                if instr.opname == opcode
            ]
            self.test.assertEqual(len(before_instrs), before)
            after_instrs = [
                instr
                for instr in dis.get_instructions(self.opt)
                if instr.opname == opcode
            ]
            self.test.assertEqual(len(after_instrs), after)

    class PeepholeRunner:
        def __init__(self, test, code):
            self.test = test
            self.opt = test.run_code(code)
            self.notopt = test.run_code(code, peephole_enabled=False)

        def __getitem__(self, func):
            return PeepHoleTests.PeepholeComparer(
                self.test, self.opt[func], self.notopt[func]
            )

    def peephole_run(self, code, func=None):
        runner = PeepHoleTests.PeepholeRunner(self, code)
        if func:
            return runner[func]
        return runner

    def peephole_compile(self, code):
        return PeepHoleTests.PeepholeComparer(
            self, self.compile(code), self.compile(code, peephole_enabled=False)
        )

    def test_global_as_constant(self):
        # LOAD_GLOBAL None/True/False  -->  LOAD_CONST None/True/False
        source = """
        def f():
            x = None
            x = None
            return x
        def g():
            x = True
            return x
        def h():
            x = False
            return x"""

        d = self.peephole_run(source)
        f, g, h = d["f"], d["g"], d["h"]

        for func, elem in ((f, None), (g, True), (h, False)):
            func.assert_neither("LOAD_GLOBAL")
            func.assert_both("LOAD_CONST", elem)

        source = """
        def f():
            'Adding a docstring made this test fail in Py2.5.0'
            return None
        """
        f = self.peephole_run(source, "f")

        f.assert_neither("LOAD_GLOBAL")
        f.assert_both("LOAD_CONST", None)

    def test_while_one(self):
        # Skip over:  LOAD_CONST trueconst  POP_JUMP_IF_FALSE xx
        source = """
        def f():
            while 1:
                pass
            return list"""

        f = self.peephole_run(source, "f")
        f.assert_neither("LOAD_CONST")
        f.assert_neither("POP_JUMP_IF_FALSE")
        f.assert_both("JUMP_ABSOLUTE")

    def make_byte_code(
        self,
        *ops,
    ):
        res = bytearray()
        for op, oparg in ops:
            if oparg >= 256:
                raise NotImplementedError()
            res.append(op)
            res.append(oparg)
        return bytes(res)

    def new_code(
        self,
        code,
        argcount=0,
        posonlyargcount=0,
        kwonlyargcount=0,
        nlocals=0,
        stacksize=0,
        flags=0,
        constants=(),
        names=(),
        varnames=(),
        filename="foo.py",
        name="foo",
        firstlineno=1,
        lnotab=b"",
        freevars=(),
        cellvars=(),
    ):
        return CodeType(
            argcount,
            posonlyargcount,
            kwonlyargcount,
            nlocals,
            stacksize,
            flags,
            code,
            constants,
            names,
            varnames,
            filename,
            name,
            firstlineno,
            lnotab,
            freevars,
            cellvars,
        )

    def test_mark_blocks_one_block(self):
        byte_code = self.make_byte_code(
            (opmap["LOAD_CONST"], 0), (opmap["RETURN_VALUE"], 0), constants=(None,)
        )
        opt = Optimizer(byte_code, (None,), b"", opcodes)
        self.assertEqual(opt.blocks, [0, 0])

    def test_mark_blocks_one_block(self):
        byte_code = self.make_byte_code(
            (opmap["LOAD_CONST"], 0),
            (opmap["POP_JUMP_IF_TRUE"], 8),
            (opmap["LOAD_CONST"], 0),
            (opmap["RETURN_VALUE"], 0),
            (opmap["LOAD_CONST"], 0),
            (opmap["RETURN_VALUE"], 0),
        )
        opt = Optimizer(byte_code, (None,), b"", opcodes)
        self.assertEqual(opt.blocks, [0, 0, 0, 0, 1, 1])

    def test_mark_blocks_abs_jump_2(self):
        byte_code = self.make_byte_code(
            (opmap["NOP"], 0),
            (opmap["LOAD_CONST"], 0),
            (opmap["POP_JUMP_IF_TRUE"], 10),
            (opmap["LOAD_CONST"], 0),
            (opmap["RETURN_VALUE"], 0),
            (opmap["LOAD_CONST"], 0),
            (opmap["RETURN_VALUE"], 0),
        )
        opt = Optimizer(byte_code, (None,), b"", opcodes)
        self.assertEqual(opt.blocks, [0, 0, 0, 0, 0, 1, 1])

    def test_mark_blocks_abs_jump(self):
        byte_code = self.make_byte_code(
            (opmap["LOAD_CONST"], 0),
            (opmap["POP_JUMP_IF_TRUE"], 8),
            (opmap["LOAD_CONST"], 0),
            (opmap["RETURN_VALUE"], 0),
            (opmap["LOAD_CONST"], 0),
            (opmap["RETURN_VALUE"], 0),
        )
        opt = Optimizer(byte_code, (None,), b"", opcodes)
        self.assertEqual(opt.blocks, [0, 0, 0, 0, 1, 1])

    def test_mark_blocks_rel_jump(self):
        byte_code = self.make_byte_code(
            (opmap["JUMP_FORWARD"], 6),
            (opmap["LOAD_CONST"], 0),
            (opmap["RETURN_VALUE"], 0),
            (opmap["LOAD_CONST"], 0),
            (opmap["RETURN_VALUE"], 0),
        )
        opt = Optimizer(byte_code, (None,), b"", opcodes)
        self.assertEqual(opt.blocks, [0, 0, 0, 0, 1])

    def test_mark_blocks_rel_jump_2(self):
        byte_code = self.make_byte_code(
            (opmap["NOP"], 0),
            (opmap["JUMP_FORWARD"], 6),
            (opmap["LOAD_CONST"], 0),
            (opmap["RETURN_VALUE"], 0),
            (opmap["LOAD_CONST"], 0),
            (opmap["RETURN_VALUE"], 0),
        )
        opt = Optimizer(byte_code, (None,), b"", opcodes)
        self.assertEqual(opt.blocks, [0, 0, 0, 0, 0, 1])

    def test_fix_blocks(self):
        """fix blocks should update instruction offsets for removed NOPs"""
        byte_code = self.make_byte_code(
            (opmap["NOP"], 0),
            (opmap["JUMP_FORWARD"], 6),
            (opmap["LOAD_CONST"], 0),
            (opmap["RETURN_VALUE"], 0),
            (opmap["LOAD_CONST"], 0),
            (opmap["RETURN_VALUE"], 0),
        )
        opt = Optimizer(byte_code, (None,), b"\x01\x01", opcodes)
        opt.fix_blocks()
        self.assertEqual(opt.blocks, [0, 0, 1, 2, 3, 4])

    def test_fix_lnotab(self):
        """basic smoke test that fix_lnotab removes NOPs"""
        byte_code = self.make_byte_code(
            (opmap["NOP"], 0),
            (opmap["JUMP_FORWARD"], 6),
            (opmap["LOAD_CONST"], 0),
            (opmap["RETURN_VALUE"], 0),
            (opmap["LOAD_CONST"], 0),
            (opmap["RETURN_VALUE"], 0),
        )
        opt = Optimizer(byte_code, (None,), b"\x02\x01", opcodes)
        opt.fix_blocks()
        lnotab = bytes(opt.fix_lnotab())

        self.assertEqual(lnotab, b"\x00\x01")

    def test_fix_jump_rel(self):
        """basic smoke test that fix_lnotab removes NOPs"""
        byte_code = self.make_byte_code(
            (opmap["JUMP_FORWARD"], 6),
            (opmap["NOP"], 0),
            (opmap["LOAD_CONST"], 0),
            (opmap["RETURN_VALUE"], 0),
            (opmap["LOAD_CONST"], 0),
            (opmap["RETURN_VALUE"], 0),
        )
        self.assertInBytecode(
            self.new_code(byte_code, constants=(None,)), "JUMP_FORWARD", 8
        )
        opt = Optimizer(byte_code, (None,), b"", opcodes)
        opt.fix_blocks()
        code = self.new_code(bytes(opt.fix_jumps()), constants=(None,))
        self.assertInBytecode(code, "JUMP_FORWARD", 6)

    def test_fix_jump_abs(self):
        """basic smoke test that fix_lnotab removes NOPs"""
        byte_code = self.make_byte_code(
            (opmap["LOAD_CONST"], 0),
            (opmap["POP_JUMP_IF_TRUE"], 10),
            (opmap["NOP"], 0),
            (opmap["LOAD_CONST"], 0),
            (opmap["RETURN_VALUE"], 0),
            (opmap["LOAD_CONST"], 0),
            (opmap["RETURN_VALUE"], 0),
        )
        self.assertInBytecode(
            self.new_code(byte_code, constants=(None,)), "POP_JUMP_IF_TRUE", 10
        )
        opt = Optimizer(byte_code, (None,), b"", opcodes)
        opt.fix_blocks()
        code = self.new_code(bytes(opt.fix_jumps()), constants=(None,))
        self.assertInBytecode(code, "POP_JUMP_IF_TRUE", 8)

    def test_fix_jump_drop_extended(self):
        """Handle EXTENDED_ARG removal correctly"""
        ops = [
            (opmap["LOAD_CONST"], 0),
            (opmap["EXTENDED_ARG"], 1),
            (opmap["POP_JUMP_IF_TRUE"], 3),
            *(((opmap["NOP"], 0),) * 256),
            (opmap["LOAD_CONST"], 0),
            (opmap["RETURN_VALUE"], 0),
            (opmap["LOAD_CONST"], 0),
            (opmap["RETURN_VALUE"], 0),
        ]
        byte_code = self.make_byte_code(*ops)
        self.assertInBytecode(
            self.new_code(byte_code, constants=(None,)), "POP_JUMP_IF_TRUE", 259
        )
        opt = Optimizer(byte_code, (None,), b"", opcodes)
        opt.fix_blocks()
        code = self.new_code(bytes(opt.fix_jumps()), constants=(None,))
        self.assertInBytecode(code, "EXTENDED_ARG", 0)
        self.assertInBytecode(code, "POP_JUMP_IF_TRUE", 6)

    def test_load_const_branch(self):
        code = "x = 1 if 1 else 2"
        optcode = self.compile(code)
        self.assertNotInBytecode(optcode, "POP_JUMP_IF_FALSE")

    def test_pack_unpack(self):
        for line, elem in (
            ("a, = a,", "LOAD_CONST"),
            ("a, b = a, b", "ROT_TWO"),
            ("a, b, c = a, b, c", "ROT_THREE"),
        ):
            code = self.peephole_compile(line)
            code.assert_in_opt(elem)
            code.assert_removed("BUILD_TUPLE")
            code.assert_removed("UNPACK_SEQUENCE")

    def test_return(self):
        code = "def f():\n    return 42\n    x = 1"
        code = self.peephole_run(code, "f")
        code.assert_removed("LOAD_CONST", 1)

    def test_elim_extra_return(self):
        # RETURN LOAD_CONST None RETURN  -->  RETURN
        f = self.peephole_run(
            """
        def f(x):
            return x""",
            "f",
        )
        f.assert_neither("LOAD_CONST", None)
        f.assert_instr_count("RETURN_VALUE", 1, 1)

    def test_elim_jump_to_return(self):
        # JUMP_FORWARD to RETURN -->  RETURN
        source = """
        def f(cond, true_value, false_value):
            return true_value if cond else false_value"""

        f = self.peephole_run(source, "f")
        f.assert_removed("JUMP_FORWARD")
        f.assert_not_in_opt("JUMP_ABSOLUTE")
        f.assert_instr_count("RETURN_VALUE", 1, 2)

    def test_elim_jump_after_return1(self):
        # Eliminate dead code: jumps immediately after returns can't be reached
        source = """
        def f(cond1, cond2):
            if cond1: return 1
            if cond2: return 2
            while 1:
                return 3
            while 1:
                if cond1: return 4
                return 5
            return 6"""
        f = self.peephole_run(source, "f")
        f.assert_removed("JUMP_ABSOLUTE")

    def test_bug_11510(self):
        self.run_code(
            """
        def f():
            x, y = {1, 1}
            return x, y
        try:
            f()
            raise Exception()
        except ValueError:
            pass"""
        )

    def test_no_rehash(self):
        source = """
        def f():
            for name in {'gi_running', 'gi_frame', 'gi_code', 'gi_yieldfrom',
                 'cr_running', 'cr_frame', 'cr_code', 'cr_await'}:
                 print(name)
         """

        f = self.peephole_run(source, "f")
        for func in (f.opt, f.notopt):
            code = func.__code__
            for const in code.co_consts:
                if isinstance(const, frozenset):
                    # We create the initial set in the peep holer based upon the order
                    # of the variables.
                    exp = frozenset(
                        (
                            "gi_running",
                            "gi_frame",
                            "gi_code",
                            "gi_yieldfrom",
                            "cr_running",
                            "cr_frame",
                            "cr_code",
                            "cr_await",
                        )
                    )
                    # Constructing the code object then creates a tuple from that, and then
                    # interns the strings, and then creates a new frozen set
                    exp = frozenset(tuple(exp))

                    self.assertEqual(list(const), list(exp))


if __name__ == "__main__":
    unittest.main()

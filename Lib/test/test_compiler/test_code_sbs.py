import dis
import os
import sys
from collections import deque
from compiler import pyassem
from compiler.pyassem import Instruction
from compiler.pycodegen import CodeGenerator
from functools import partial
from io import SEEK_END
from re import escape

from .common import CompilerTest, glob_test


def format_oparg(instr):
    if instr.target is not None:
        return f"Block({instr.target.bid})"
    elif isinstance(instr.oparg, CodeGenerator):
        return f"Code(({instr.oparg.tree.lineno},{instr.oparg.tree.col_offset}))"
    elif isinstance(instr.oparg, (str, int, tuple, frozenset, type(None))):
        return repr(instr.oparg)

    raise NotImplementedError("Unsupported oparg type: " + type(instr.oparg).__name__)


DEFAULT_MARKER = "# This is the default script and should be updated to the minimal byte codes to be verified"
SCRIPT_EXPECTED = "# EXPECTED:"
SCRIPT_EXT = ".scr.py"
SCRIPT_OPCODE_CODE = "CODE_START"


class _AnyType:
    """Singleton to indicate that we match any opcode or oparg."""

    def __new__(self):
        return Any

    def __repr__(self):
        return "Any"


Any = object.__new__(_AnyType)


def is_oparg_equal(test, expected, instr):
    if instr.target:
        return instr.target == expected
    else:
        return instr.oparg == expected


def is_instr_match(test, opname, oparg, instr):
    if opname != Any and opname != instr.opname:
        return False

    if oparg != Any and not is_oparg_equal(test, oparg, instr):
        return False

    return True


def graph_instrs(graph, name=None):
    if name:
        yield Instruction(SCRIPT_OPCODE_CODE, name)
    for block in graph.getBlocks():
        for instr in block.getInstructions():
            yield instr


class CodeTests(CompilerTest):
    def check_instrs(self, instrs, script):
        if not script:
            self.fail("Script file is empty")

        cur_scr = 0
        queue = deque([instrs])
        while queue:
            instrs = tuple(queue.popleft())
            for i, instr in enumerate(instrs):
                if instr.opname == "SET_LINENO":
                    continue

                if isinstance(instr.oparg, CodeGenerator):
                    queue.append(graph_instrs(instr.oparg.graph, instr.oparg.name))
                if cur_scr == len(script):
                    self.fail("Extra bytecodes not expected")

                op = script[cur_scr]
                inc, error = op.is_match(self, instrs, i, script, cur_scr)
                if error:
                    self.fail(error)
                cur_scr += inc

        # make sure we exhausted the script or ended on a ...
        if cur_scr == len(script):
            return

        script[cur_scr].end(self, script, cur_scr)

    def gen_default_script(self, scr, instrs):
        # generate a default script which matches exactly...  This should
        # be fixed up to a minimal match
        scr.write(SCRIPT_EXPECTED + "\n")
        scr.write(DEFAULT_MARKER + "\n")
        scr.write("[\n")
        queue = deque([instrs])
        while queue:
            instrs = queue.popleft()
            for instr in instrs:
                if instr.opname == "SET_LINENO":
                    continue
                elif isinstance(instr.oparg, CodeGenerator):
                    queue.append(graph_instrs(instr.oparg.graph, instr.oparg.name))
                scr.write(f"    {instr.opname}({format_oparg(instr)}),\n")
        scr.write("]\n")

        self.fail(
            "script file not present, script generated, fixup to be minimal repo and check it in"
        )

    def test_self_empty_script(self):
        with self.assertRaises(AssertionError):
            self.check_instrs([], [])

    def test_self_match_all(self):
        self.check_instrs([], [SkipAny()])
        self.check_instrs([Instruction("STORE_NAME", "foo")], [SkipAny()])
        self.check_instrs(
            [Instruction("STORE_NAME", "foo"), Instruction("STORE_NAME", "foo")],
            [SkipAny()],
        )

    def test_self_trailing_instrs(self):
        with self.assertRaisesRegex(AssertionError, "Extra bytecodes not expected"):
            self.check_instrs(
                [Instruction("STORE_NAME", "foo"), Instruction("STORE_NAME", "foo")],
                [Op("STORE_NAME", "foo")],
            )

    def test_self_bad_oparg(self):
        return
        with self.assertRaisesRegex(
            AssertionError, "Failed to eval expected oparg: foo"
        ):
            self.check_instrs(
                [Instruction("STORE_NAME", "foo")], [Op("STORE_NAME", "foo")]
            )

    def test_self_trailing_script_wildcard(self):
        with self.assertRaisesRegex(
            AssertionError, escape("Trailing script elements: [Op(STORE_NAME, 'bar')]")
        ):
            self.check_instrs(
                [Instruction("STORE_NAME", "foo"), Instruction("STORE_NAME", "foo")],
                [SkipAny(), Op("STORE_NAME", "bar")],
            )

    def test_self_trailing_script(self):
        with self.assertRaisesRegex(
            AssertionError, escape("Trailing script elements: [Op(STORE_NAME, 'bar')]")
        ):
            self.check_instrs(
                [Instruction("STORE_NAME", "foo")],
                [Op("STORE_NAME", "foo"), Op("STORE_NAME", "bar")],
            )

    def test_self_mismatch_oparg(self):
        with self.assertRaises(AssertionError):
            self.check_instrs(
                [Instruction("STORE_NAME", "foo")], [Op("STORE_NAME", "bar")]
            )

    def test_self_mismatch_opname(self):
        with self.assertRaises(AssertionError):
            self.check_instrs(
                [Instruction("LOAD_NAME", "foo")], [Op("STORE_NAME", "foo")]
            )

    def test_self_wildcard_oparg(self):
        self.check_instrs([Instruction("LOAD_NAME", "foo")], [Op("LOAD_NAME", Any)])

    def test_self_wildcard_opname(self):
        self.check_instrs([Instruction("LOAD_NAME", "foo")], [Op(Any, "foo")])

    def test_self_match_after_wildcard(self):
        self.check_instrs(
            [Instruction("LOAD_NAME", "foo"), Instruction("CALL_FUNCTION", 0)],
            [SkipAny(), Op("CALL_FUNCTION", 0)],
        )

    def test_self_match_block(self):
        block = pyassem.Block()
        block.bid = 1
        self.check_instrs(
            [
                Instruction("JUMP_ABSOLUTE", None, target=block),
                Instruction("LOAD_CONST", None),
                Instruction("RETURN_VALUE", 0),
            ],
            [Op("JUMP_ABSOLUTE", Block(1)), Op("LOAD_CONST", None), SkipAny()],
        )

    def test_self_match_wrong_block(self):
        block = pyassem.Block()
        block.bid = 1
        with self.assertRaisesRegex(
            AssertionError,
            escape(
                "mismatch, expected JUMP_ABSOLUTE Block(2), got JUMP_ABSOLUTE Block(1)"
            ),
        ):
            self.check_instrs(
                [
                    Instruction("JUMP_ABSOLUTE", None, target=block),
                    Instruction("LOAD_CONST", None),
                    Instruction("RETURN_VALUE", 0),
                ],
                [Op("JUMP_ABSOLUTE", Block(2)), Op("LOAD_CONST", None), SkipAny()],
            )

    def test_self_neg_wildcard_match(self):
        with self.assertRaisesRegex(
            AssertionError, "unexpected match LOAD_NAME foo, got LOAD_NAME 'foo'"
        ):
            self.check_instrs(
                [Instruction("LOAD_NAME", "foo"), Instruction("CALL_FUNCTION", 0)],
                [~Op("LOAD_NAME", "foo")],
            )

    def test_self_neg_wildcard_no_match(self):
        self.check_instrs(
            [Instruction("LOAD_NAME", "foo"), Instruction("CALL_FUNCTION", 0)],
            [~Op("LOAD_NAME", "bar")],
        )

    def test_self_neg_wildcard_multimatch(self):
        with self.assertRaisesRegex(
            AssertionError, "unexpected match CALL_FUNCTION 0, got CALL_FUNCTION 0"
        ):
            self.check_instrs(
                [Instruction("LOAD_NAME", "foo"), Instruction("CALL_FUNCTION", 0)],
                [SkipBut(Op("LOAD_NAME", "bar"), Op("CALL_FUNCTION", 0))],
            )

    def test_self_neg_wildcard_no_multimatch(self):
        self.check_instrs(
            [Instruction("LOAD_NAME", "foo"), Instruction("CALL_FUNCTION", 0)],
            [SkipBut(Op("LOAD_NAME", "bar"), Op("CALL_FUNCTION", 1))],
        )


def add_test(modname, fname):
    if "/cinder/" in fname and "cinder" not in sys.version:
        return
    if "/3.6/" in fname and sys.version_info[:2] != (3, 6):
        return
    elif "/3.7/" in fname and sys.version_info[:2] != (3, 7):
        return
    elif "/3.8/" in fname and sys.version_info[:2] != (3, 8):
        return

    def test_code(self):
        with open(fname) as f:
            test = f.read()

        parts = test.split(SCRIPT_EXPECTED, 1)
        graph = self.to_graph(parts[0])

        if len(parts) == 1:
            with open(fname, "a") as f:
                f.seek(0, SEEK_END)
                if not parts[0].endswith("\n"):
                    test.write("\n")

                self.gen_default_script(f, graph_instrs(graph))
                self.fail(
                    "test script not present, script generated, fixup to be minimal repo and check it in"
                )
        elif parts[1].find(DEFAULT_MARKER) != -1:
            self.fail(
                "generated script present, fixup to be a minimal repo and check it in"
            )

        script = eval(parts[1], globals(), SCRIPT_CONTEXT)
        for i, value in enumerate(script):
            if value == ...:
                script[i] = SkipAny()

        self.check_instrs(graph_instrs(graph), script)

    test_code.__name__ = "test_" + modname.replace("/", "_")[:-3]
    setattr(CodeTests, test_code.__name__, test_code)


glob_test("sbs_code_tests", "**/*.py", add_test)


class Matcher:
    def is_match(self, test, instrs, cur_instr, script, cur_scr):
        raise NotImplementedError()

    def end(self, test, script, cur_scr):
        test.fail(f"Trailing script elements: {script[cur_scr:]}")

    def __invert__(self):
        return SkipBut(self)


class Op(Matcher):
    """Matches a single opcode.  Can be used as:
    OPNAME([oparg])       - OPNAME is injected into eval namespace as a callable for all known opcodes.
    ANY([oparg])          - Match any opcode that matches the given oparg.
    Op(OPNAME[, oparg])   - Op will recognize the callable of a known opcode.
    Op('OPNAME'[, oparg]) - Explicitly provide opcode name as str (can match opcodes not defined in dis)
    Op(Any[, oparg])      - Match any opcode with the given oparg.

    If oparg is not provided then the oparg will not be checked."""

    def __init__(self, opname=Any, oparg=Any):
        if isinstance(opname, partial) and issubclass(opname.func, Op):
            # Op(LOAD_NAME, "foo") form instead of
            # LOAD_NAME("foo")
            opname = opname.args[0]
        self.opname = opname
        self.oparg = oparg

    def is_match(self, test, instrs, cur_instr, script, cur_scr):
        instr = instrs[cur_instr]
        if is_instr_match(test, self.opname, self.oparg, instr):
            return 1, None

        return (
            0,
            f"mismatch, expected {self.opname} {self.oparg}, got {instr.opname} {format_oparg(instr)}",
        )

    def __repr__(self):
        if self.oparg == Any:
            return f"Op({self.opname})"
        return f"Op({self.opname}, {self.oparg!r})"

    def __str__(self):
        return f"{self.opname} {self.oparg}"


CODE_START = partial(Op, SCRIPT_OPCODE_CODE)


class Block:
    """Matches an oparg with block target"""

    def __init__(self, bid):
        self.bid = bid

    def __eq__(self, other):
        if not isinstance(other, pyassem.Block):
            return False

        return other.bid == self.bid

    def __repr__(self):
        return f"Block({self.bid})"


class Code:
    def __init__(self, loc):
        if not isinstance(loc, (str, tuple, int)):
            raise TypeError("expected code name, line/offset tuple, or line no")
        self.loc = loc

    def __eq__(self, other):
        if not isinstance(other, CodeGenerator):
            return False

        if isinstance(self.loc, str):
            return self.loc == other.tree.name
        elif isinstance(self.loc, tuple):
            return self.loc == other.tree.lineno, other.tree.col_offset
        elif isinstance(self.loc, int):
            return self.loc == other.tree.lineno
        return False


class SkipAny(Matcher):
    """Skips any number of instructions until the next instruction matches. Can
    be used explicitly via SkipAny() or short hand usage of ... is supported.

    Skipping terminates when the next instrution matches."""

    def is_match(self, test, instrs, cur_instr, script, cur_scr):
        if cur_scr + 1 != len(script):
            next = script[cur_scr + 1]
            inc, error = next.is_match(test, instrs, cur_instr, script, cur_scr + 1)
            if not error:
                # skip the ... and the match
                return inc + 1, None
        return 0, None

    def end(self, test, script, cur_scr):
        if cur_scr != len(script) - 1:
            script[cur_scr + 1].end(test, script, cur_scr + 1)

    def __repr__(self):
        return "SkipAny()"


class SkipBut(SkipAny):
    """Skips instructions making sure there are no matches against 1 or more
    instructions along the way.  Can be used as:
        ~OPNAME()           - Asserts one opcode isn't present
        SkipBut(
            OPNAME(...),
            OPNAME(...)
        )                   - Asserts multiple instrutions aren't present.

    Skipping terminates when the next instruction matches."""

    def __init__(self, *args):
        self.args = args

    def is_match(self, test, instrs, cur_instr, script, cur_scr):
        instr = instrs[cur_instr]
        for arg in self.args:
            inc, err = arg.is_match(test, instrs, cur_instr, script, cur_scr)
            if not err:
                return (
                    0,
                    f"unexpected match {arg}, got {instr.opname} {format_oparg(instr)}",
                )

        return super().is_match(test, instrs, cur_instr, script, cur_scr)

    def __repr__(self):
        return f"SkipBut({self.opname!r}, {self.oparg!r})"


SCRIPT_CONTEXT = {
    "ANY": partial(Op, Any),
    **{opname: partial(Op, opname) for opname in dis.opmap},
}

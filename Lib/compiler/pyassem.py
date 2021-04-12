# pyre-unsafe
"""A flow graph representation for Python bytecode"""
from __future__ import annotations

import sys
from contextlib import contextmanager
from types import CodeType
from typing import Generator, List, Optional

from . import opcode36, opcode37, opcode38, opcode38cinder
from .consts import CO_NEWLOCALS, CO_OPTIMIZED
from .consts38 import CO_SUPPRESS_JIT
from .peephole import Optimizer
from .py38.peephole import Optimizer38

try:
    import cinder  # pyre-ignore # noqa: F401

    MAX_BYTECODE_OPT_ITERS = 5
except ImportError:
    MAX_BYTECODE_OPT_ITERS = 1


def sign(a):
    if not isinstance(a, float):
        raise TypeError(f"Must be a real number, not {type(a)}")
    if a != a:
        return 1.0  # NaN case
    return 1.0 if str(a)[0] != "-" else -1.0


def instrsize(oparg):
    if oparg <= 0xFF:
        return 1
    elif oparg <= 0xFFFF:
        return 2
    elif oparg <= 0xFFFFFF:
        return 3
    else:
        return 4


def cast_signed_byte_to_unsigned(i):
    if i < 0:
        i = 255 + i + 1
    return i


FVC_MASK = 0x3
FVC_NONE = 0x0
FVC_STR = 0x1
FVC_REPR = 0x2
FVC_ASCII = 0x3
FVS_MASK = 0x4
FVS_HAVE_SPEC = 0x4


class Instruction:
    __slots__ = ("opname", "oparg", "target", "ioparg")

    def __init__(
        self,
        opname: str,
        oparg: object,
        ioparg: int = 0,
        target: Optional[Block] = None,
    ):
        self.opname = opname
        self.oparg = oparg
        self.ioparg = ioparg
        self.target = target

    def __repr__(self):
        args = [f"{self.opname!r}", f"{self.oparg!r}", f"{self.ioparg!r}"]
        if self.target is not None:
            args.append(f"{self.target!r}")

        return f"Instruction({', '.join(args)})"


class CompileScope:
    START_MARKER = "compile-scope-start-marker"
    __slots__ = "blocks"

    def __init__(self, blocks):
        self.blocks = blocks


class FlowGraph:
    def __init__(self):
        self.block_count = 0
        # List of blocks in the order they should be output for linear
        # code. As we deal with structured code, this order corresponds
        # to the order of source level constructs. (The original
        # implementation from Python2 used a complex ordering algorithm
        # which more buggy and erratic than useful.)
        self.ordered_blocks = []
        # Current block being filled in with instructions.
        self.current = None
        self.entry = Block("entry")
        self.exit = Block("exit")
        self.startBlock(self.entry)

        # Source line number to use for next instruction.
        self.lineno = 0
        # Whether line number was already output (set to False to output
        # new line number).
        self.lineno_set = False
        # First line of this code block. This field is expected to be set
        # externally and serve as a reference for generating all other
        # line numbers in the code block. (If it's not set, it will be
        # deduced).
        self.firstline = 0
        # Line number of first instruction output. Used to deduce .firstline
        # if it's not set explicitly.
        self.first_inst_lineno = 0
        # If non-zero, do not emit bytecode
        self.do_not_emit_bytecode = 0

    @contextmanager
    def new_compile_scope(self) -> Generator[CompileScope, None, None]:
        prev_current = self.current
        prev_ordered_blocks = self.ordered_blocks
        prev_line_no = self.first_inst_lineno
        try:
            self.ordered_blocks = []
            self.current = self.newBlock(CompileScope.START_MARKER)
            yield CompileScope(self.ordered_blocks)
        finally:
            self.current = prev_current
            self.ordered_blocks = prev_ordered_blocks
            self.first_inst_lineno = prev_line_no

    def apply_from_scope(self, scope: CompileScope):
        # link current block with the block from out of order result
        block: Block = scope.blocks[0]
        assert block.prev is not None
        assert block.prev.label == CompileScope.START_MARKER
        block.prev = None

        self.current.addNext(block)
        self.ordered_blocks.extend(scope.blocks)
        self.current = scope.blocks[-1]

    def startBlock(self, block):
        if self._debug:
            if self.current:
                print("end", repr(self.current))
                print("    next", self.current.next)
                print("    prev", self.current.prev)
                print("   ", self.current.get_children())
            print(repr(block))
        block.bid = self.block_count
        self.block_count += 1
        self.current = block
        if block and block not in self.ordered_blocks:
            if block is self.exit:
                if self.ordered_blocks and self.ordered_blocks[-1].has_return():
                    return
            self.ordered_blocks.append(block)

    def nextBlock(self, block=None, label=""):
        if self.do_not_emit_bytecode:
            return
        # XXX think we need to specify when there is implicit transfer
        # from one block to the next.  might be better to represent this
        # with explicit JUMP_ABSOLUTE instructions that are optimized
        # out when they are unnecessary.
        #
        # I think this strategy works: each block has a child
        # designated as "next" which is returned as the last of the
        # children.  because the nodes in a graph are emitted in
        # reverse post order, the "next" block will always be emitted
        # immediately after its parent.
        # Worry: maintaining this invariant could be tricky
        if block is None:
            block = self.newBlock(label=label)

        # Note: If the current block ends with an unconditional control
        # transfer, then it is techically incorrect to add an implicit
        # transfer to the block graph. Doing so results in code generation
        # for unreachable blocks.  That doesn't appear to be very common
        # with Python code and since the built-in compiler doesn't optimize
        # it out we don't either.
        self.current.addNext(block)
        self.startBlock(block)

    def newBlock(self, label=""):
        b = Block(label)
        return b

    def startExitBlock(self):
        self.nextBlock(self.exit)

    _debug = 0

    def _enable_debug(self):
        self._debug = 1

    def _disable_debug(self):
        self._debug = 0

    def emit(self, opcode: str, oparg: object = 0):
        self.maybeEmitSetLineno()

        if opcode != "SET_LINENO" and isinstance(oparg, Block):
            if not self.do_not_emit_bytecode:
                self.current.addOutEdge(oparg)
                self.current.emit(Instruction(opcode, 0, 0, target=oparg))
            return

        ioparg = self.convertArg(opcode, oparg)

        if not self.do_not_emit_bytecode:
            self.current.emit(Instruction(opcode, oparg, ioparg))

        if opcode == "SET_LINENO" and not self.first_inst_lineno:
            self.first_inst_lineno = ioparg

    def maybeEmitSetLineno(self):
        if not self.do_not_emit_bytecode and not self.lineno_set and self.lineno:
            self.lineno_set = True
            self.emit("SET_LINENO", self.lineno)

    def convertArg(self, opcode: str, oparg: object) -> int:
        if isinstance(oparg, int):
            return oparg
        raise ValueError(f"invalid oparg {oparg!r} for {opcode!r}")

    def getBlocksInOrder(self):
        """Return the blocks in the order they should be output."""
        return self.ordered_blocks

    def getBlocks(self):
        if self.exit not in self.ordered_blocks:
            return self.ordered_blocks + [self.exit]
        return self.ordered_blocks

    def getRoot(self):
        """Return nodes appropriate for use with dominator"""
        return self.entry

    def getContainedGraphs(self):
        result = []
        for b in self.getBlocks():
            result.extend(b.getContainedGraphs())
        return result


class Block:
    def __init__(self, label=""):
        self.insts = []
        self.outEdges = set()
        self.label = label
        self.bid = None
        self.next = None
        self.prev = None
        self.returns = False
        self.offset = 0
        self.seen = False  # visited during stack depth calculation
        self.startdepth = -1

    def __repr__(self):
        data = []
        data.append(f"id={self.bid}")
        if self.next:
            data.append(f"next={self.next.bid}")
        extras = ", ".join(data)
        if self.label:
            return f"<block {self.label} {extras}>"
        else:
            return f"<block {extras}>"

    def __str__(self):
        insts = map(str, self.insts)
        insts = "\n".join(insts)
        return f"<block label={self.label} bid={self.bid} startdepth={self.startdepth}: {insts}>"

    def emit(self, instr: Instruction) -> None:
        if instr.opname == "RETURN_VALUE":
            self.returns = True

        self.insts.append(instr)

    def getInstructions(self):
        return self.insts

    def addOutEdge(self, block):
        self.outEdges.add(block)

    def addNext(self, block):
        assert self.next is None, next
        self.next = block
        assert block.prev is None, block.prev
        block.prev = self

    _uncond_transfer = (
        "RETURN_INT",
        "RETURN_VALUE",
        "RAISE_VARARGS",
        "JUMP_ABSOLUTE",
        "JUMP_FORWARD",
        "CONTINUE_LOOP",
    )

    def has_unconditional_transfer(self):
        """Returns True if there is an unconditional transfer to an other block
        at the end of this block. This means there is no risk for the bytecode
        executer to go past this block's bytecode."""
        if self.insts and self.insts[-1][0] in self._uncond_transfer:
            return True

    def has_return(self):
        return self.insts and self.insts[-1].opname == "RETURN_VALUE"

    def get_children(self):
        return list(self.outEdges) + [self.next]

    def get_followers(self):
        """Get the whole list of followers, including the next block."""
        followers = {self.next}
        # Blocks that must be emitted *after* this one, because of
        # bytecode offsets (e.g. relative jumps) pointing to them.
        for inst in self.insts:
            if inst[0] in self.opcode.hasjrel:
                followers.add(inst[1])
        return followers

    def getContainedGraphs(self):
        """Return all graphs contained within this block.

        For example, a MAKE_FUNCTION block will contain a reference to
        the graph for the function body.
        """
        contained = []
        for inst in self.insts:
            if len(inst) == 1:
                continue
            op = inst[1]
            if hasattr(op, "graph"):
                contained.append(op.graph)
        return contained


# flags for code objects

# the FlowGraph is transformed in place; it exists in one of these states
ACTIVE = "ACTIVE"  # accepting calls to .emit()
CLOSED = "CLOSED"  # closed to new instructions, ready for codegen
FLAT = "FLAT"  # flattened
DONE = "DONE"


class IndexedSet:
    __delitem__ = None

    def __init__(self, iterable=()):
        self.keys = {}
        for item in iterable:
            self.get_index(item)

    def __add__(self, iterable):
        result = IndexedSet()
        for item in self.keys.keys():
            result.get_index(item)
        for item in iterable:
            result.get_index(item)
        return result

    def __contains__(self, item):
        return item in self.keys

    def __iter__(self):
        return iter(self.keys.keys())

    def __len__(self):
        return len(self.keys)

    def get_index(self, item):
        """Return index of name in collection, appending if necessary"""
        assert type(item) is str
        idx = self.keys.get(item)
        if idx is not None:
            return idx
        idx = len(self.keys)
        self.keys[item] = idx
        return idx

    def index(self, item):
        assert type(item) is str
        idx = self.keys.get(item)
        if idx is not None:
            return idx
        raise ValueError()


class PyFlowGraph(FlowGraph):

    super_init = FlowGraph.__init__
    opcode = opcode36.opcode

    def __init__(
        self,
        name: str,
        filename: str,
        scope,
        flags: int = 0,
        args=(),
        kwonlyargs=(),
        starargs=(),
        optimized: int = 0,
        klass: bool = False,
        docstring: Optional[str] = None,
        firstline: int = 0,
        peephole_enabled: bool = True,
    ) -> None:
        self.super_init()
        self.name = name
        self.filename = filename
        self.scope = scope
        self.docstring = None
        self.args = args
        self.kwonlyargs = kwonlyargs
        self.starargs = starargs
        self.klass = klass
        self.stacksize = 0
        self.docstring = docstring
        self.peephole_enabled = peephole_enabled
        self.flags = flags
        if optimized:
            self.setFlag(CO_OPTIMIZED | CO_NEWLOCALS)
        self.consts = {}
        self.names = []
        # Free variables found by the symbol table scan, including
        # variables used only in nested scopes, are included here.
        self.freevars = list(self.scope.get_free_vars())
        self.cellvars = list(self.scope.get_cell_vars())
        # The closure list is used to track the order of cell
        # variables and free variables in the resulting code object.
        # The offsets used by LOAD_CLOSURE/LOAD_DEREF refer to both
        # kinds of variables.
        self.closure = self.cellvars + self.freevars
        self.varnames = list(args) + list(kwonlyargs) + list(starargs)
        self.stage = ACTIVE
        self.firstline = firstline
        self.first_inst_lineno = 0
        self.lineno_set = False
        self.lineno = 0
        # Add any extra consts that were requested to the const pool
        self.extra_consts = []
        self.initializeConsts()

    def setFlag(self, flag: int) -> None:
        self.flags |= flag

    def checkFlag(self, flag: int) -> Optional[int]:
        if self.flags & flag:
            return 1

    def initializeConsts(self) -> None:
        # Docstring is first entry in co_consts for normal functions
        # (Other types of code objects deal with docstrings in different
        # manner, e.g. lambdas and comprehensions don't have docstrings,
        # classes store them as __doc__ attribute.
        if self.name == "<lambda>":
            self.consts[self.get_const_key(None)] = 0
        elif not self.name.startswith("<") and not self.klass:
            if self.docstring is not None:
                self.consts[self.get_const_key(self.docstring)] = 0
            else:
                self.consts[self.get_const_key(None)] = 0

    def convertArg(self, opcode: str, oparg: object) -> int:
        assert self.stage == ACTIVE, self.stage

        if self.do_not_emit_bytecode and opcode in self._quiet_opcodes:
            # return -1 so this errors if it ever ends up in non-dead-code due
            # to a bug.
            return -1

        conv = self._converters.get(opcode)
        if conv is not None:
            return conv(self, oparg)

        return super().convertArg(opcode, oparg)

    def getCode(self):
        """Get a Python code object"""
        assert self.stage == ACTIVE, self.stage
        self.stage = CLOSED
        self.computeStackDepth()
        self.flattenGraph()
        assert self.stage == FLAT, self.stage
        self.makeByteCode()
        assert self.stage == DONE, self.stage
        code = self.newCodeObject()
        return code

    def dump(self, io=None):
        if io:
            save = sys.stdout
            sys.stdout = io
        pc = 0
        for block in self.getBlocks():
            print(repr(block))
            for instr in block.getInstructions():
                opname = instr.opname
                if opname == "SET_LINENO":
                    continue
                if instr.target is None:
                    print("\t", "%3d" % pc, opname, instr.oparg)
                elif instr.target.label:
                    print(
                        "\t",
                        f"{pc:3} {opname} {instr.target.bid} ({instr.target.label})",
                    )
                else:
                    print("\t", f"{pc:3} {opname} {instr.target.bid}")
                pc += self.opcode.CODEUNIT_SIZE
        if io:
            sys.stdout = save

    def stackdepth_walk(self, block, depth=0, maxdepth=0):
        assert block is not None
        if block.seen or block.startdepth >= depth:
            return maxdepth
        block.seen = True
        block.startdepth = depth
        for instr in block.getInstructions():
            effect = self.opcode.stack_effect_Raw(instr.opname, instr.oparg)
            depth += effect

            assert depth >= 0

            if depth > maxdepth:
                maxdepth = depth

            if instr.target:
                target_depth = depth
                if instr.opname == "FOR_ITER":
                    target_depth = depth - 2
                elif instr.opname in ("SETUP_FINALLY", "SETUP_EXCEPT"):
                    target_depth = depth + 3
                    if target_depth > maxdepth:
                        maxdepth = target_depth
                elif instr.opname in ("JUMP_IF_TRUE_OR_POP", "JUMP_IF_FALSE_OR_POP"):
                    depth -= 1
                maxdepth = self.stackdepth_walk(instr.target, target_depth, maxdepth)
                if instr.opname in ("JUMP_ABSOLUTE", "JUMP_FORWARD"):
                    # Remaining code is dead
                    block.seen = False
                    return maxdepth

        if block.next:
            maxdepth = self.stackdepth_walk(block.next, depth, maxdepth)

        block.seen = False
        return maxdepth

    def computeStackDepth(self):
        """Compute the max stack depth.

        Find the flow path that needs the largest stack.  We assume that
        cycles in the flow graph have no net effect on the stack depth.
        """
        assert self.stage == CLOSED, self.stage
        for block in self.getBlocksInOrder():
            # We need to get to the first block which actually has instructions
            if block.getInstructions():
                self.stacksize = self.stackdepth_walk(block)
                break

    def flattenGraph(self):
        """Arrange the blocks in order and resolve jumps"""
        assert self.stage == CLOSED, self.stage
        # This is an awful hack that could hurt performance, but
        # on the bright side it should work until we come up
        # with a better solution.
        #
        # The issue is that in the first loop blocksize() is called
        # which calls instrsize() which requires i_oparg be set
        # appropriately. There is a bootstrap problem because
        # i_oparg is calculated in the second loop.
        #
        # So we loop until we stop seeing new EXTENDED_ARGs.
        # The only EXTENDED_ARGs that could be popping up are
        # ones in jump instructions.  So this should converge
        # fairly quickly.
        extended_arg_recompile = True
        while extended_arg_recompile:
            extended_arg_recompile = False
            self.insts = insts = []
            pc = 0
            for b in self.getBlocksInOrder():
                b.offset = pc

                for inst in b.getInstructions():
                    insts.append(inst)
                    if inst.opname != "SET_LINENO":
                        pc += instrsize(inst.ioparg)

            pc = 0
            for inst in insts:
                if inst.opname == "SET_LINENO":
                    continue

                pc += instrsize(inst.ioparg)
                op = self.opcode.opmap[inst.opname]
                if self.opcode.has_jump(op):
                    oparg = inst.ioparg
                    target = inst.target

                    offset = target.offset
                    if op in self.opcode.hasjrel:
                        offset -= pc

                    offset *= 2
                    if instrsize(oparg) != instrsize(offset):
                        extended_arg_recompile = True

                    assert offset >= 0, "Offset value: %d" % offset
                    inst.ioparg = offset
        self.stage = FLAT

    def sort_cellvars(self):
        self.closure = self.cellvars + self.freevars

    def _lookupName(self, name, list):
        """Return index of name in list, appending if necessary

        This routine uses a list instead of a dictionary, because a
        dictionary can't store two different keys if the keys have the
        same value but different types, e.g. 2 and 2L.  The compiler
        must treat these two separately, so it does an explicit type
        comparison before comparing the values.
        """
        t = type(name)
        for i in range(len(list)):
            if t is type(list[i]) and list[i] == name:  # noqa: E721
                return i
        end = len(list)
        list.append(name)
        return end

    def _convert_LOAD_CONST(self, arg: object) -> int:
        getCode = getattr(arg, "getCode", None)
        if getCode is not None:
            arg = getCode()
        key = self.get_const_key(arg)
        res = self.consts.get(key, self)
        if res is self:
            res = self.consts[key] = len(self.consts)
        return res

    def get_const_key(self, value: object):
        if isinstance(value, float):
            return type(value), value, sign(value)
        elif isinstance(value, complex):
            return type(value), value, sign(value.real), sign(value.imag)
        elif isinstance(value, (tuple, frozenset)):
            return (
                type(value),
                value,
                tuple(self.get_const_key(const) for const in value),
            )

        return type(value), value

    def _convert_LOAD_FAST(self, arg: object) -> int:
        return self._lookupName(arg, self.varnames)

    def _convert_LOAD_LOCAL(self, arg: object) -> int:
        assert isinstance(arg, tuple), "invalid oparg {arg!r}"
        return self._convert_LOAD_CONST(
            (self._lookupName(arg[0], self.varnames), arg[1])
        )

    def _convert_NAME(self, arg: object) -> int:
        return self._lookupName(arg, self.names)

    def _convert_LOAD_SUPER(self, arg: object) -> int:
        assert isinstance(arg, tuple), "invalid oparg {arg!r}"
        return self._convert_LOAD_CONST((self._convert_NAME(arg[0]), arg[1]))

    def _convert_DEREF(self, arg: object) -> int:
        # Sometimes, both cellvars and freevars may contain the same var
        # (e.g., for class' __class__). In this case, prefer freevars.
        if arg in self.freevars:
            return self._lookupName(arg, self.freevars) + len(self.cellvars)
        return self._lookupName(arg, self.closure)

    # similarly for other opcodes...
    _converters = {
        "LOAD_CONST": _convert_LOAD_CONST,
        "INVOKE_FUNCTION": _convert_LOAD_CONST,
        "INVOKE_METHOD": _convert_LOAD_CONST,
        "LOAD_FIELD": _convert_LOAD_CONST,
        "STORE_FIELD": _convert_LOAD_CONST,
        "CAST": _convert_LOAD_CONST,
        "CHECK_ARGS": _convert_LOAD_CONST,
        "BUILD_CHECKED_MAP": _convert_LOAD_CONST,
        "PRIMITIVE_LOAD_CONST": _convert_LOAD_CONST,
        "LOAD_FAST": _convert_LOAD_FAST,
        "STORE_FAST": _convert_LOAD_FAST,
        "DELETE_FAST": _convert_LOAD_FAST,
        "LOAD_LOCAL": _convert_LOAD_LOCAL,
        "STORE_LOCAL": _convert_LOAD_LOCAL,
        "LOAD_NAME": _convert_NAME,
        "LOAD_CLOSURE": lambda self, arg: self._lookupName(arg, self.closure),
        "COMPARE_OP": lambda self, arg: self.opcode.CMP_OP.index(arg),
        "LOAD_GLOBAL": _convert_NAME,
        "STORE_GLOBAL": _convert_NAME,
        "DELETE_GLOBAL": _convert_NAME,
        "CONVERT_NAME": _convert_NAME,
        "STORE_NAME": _convert_NAME,
        "STORE_ANNOTATION": _convert_NAME,
        "DELETE_NAME": _convert_NAME,
        "IMPORT_NAME": _convert_NAME,
        "IMPORT_FROM": _convert_NAME,
        "STORE_ATTR": _convert_NAME,
        "LOAD_ATTR": _convert_NAME,
        "DELETE_ATTR": _convert_NAME,
        "LOAD_METHOD": _convert_NAME,
        "LOAD_DEREF": _convert_DEREF,
        "STORE_DEREF": _convert_DEREF,
        "DELETE_DEREF": _convert_DEREF,
        "LOAD_CLASSDEREF": _convert_DEREF,
        "REFINE_TYPE": _convert_LOAD_CONST,
        "LOAD_METHOD_SUPER": _convert_LOAD_SUPER,
        "LOAD_ATTR_SUPER": _convert_LOAD_SUPER,
    }

    # Opcodes which do not add names to co_consts/co_names/co_varnames in dead code (self.do_not_emit_bytecode)
    _quiet_opcodes = {
        "LOAD_CONST",
        "IMPORT_NAME",
        "STORE_ATTR",
        "LOAD_ATTR",
        "DELETE_ATTR",
        "LOAD_METHOD",
        "STORE_FAST",
        "LOAD_FAST",
    }

    def makeByteCode(self):
        assert self.stage == FLAT, self.stage
        self.lnotab = lnotab = LineAddrTable(self.opcode)
        lnotab.setFirstLine(self.firstline or self.first_inst_lineno or 1)

        for t in self.insts:
            if t.opname == "SET_LINENO":
                lnotab.nextLine(t.oparg)
                continue

            oparg = t.ioparg
            assert 0 <= oparg <= 0xFFFFFFFF, oparg
            if oparg > 0xFFFFFF:
                lnotab.addCode(self.opcode.EXTENDED_ARG, (oparg >> 24) & 0xFF)
            if oparg > 0xFFFF:
                lnotab.addCode(self.opcode.EXTENDED_ARG, (oparg >> 16) & 0xFF)
            if oparg > 0xFF:
                lnotab.addCode(self.opcode.EXTENDED_ARG, (oparg >> 8) & 0xFF)
            lnotab.addCode(self.opcode.opmap[t.opname], oparg & 0xFF)
        self.stage = DONE

    def newCodeObject(self):
        assert self.stage == DONE, self.stage
        if (self.flags & CO_NEWLOCALS) == 0:
            nlocals = 0
        else:
            nlocals = len(self.varnames)

        firstline = self.firstline
        # For module, .firstline is initially not set, and should be first
        # line with actual bytecode instruction (skipping docstring, optimized
        # out instructions, etc.)
        if not firstline:
            firstline = self.first_inst_lineno
        # If no real instruction, fallback to 1
        if not firstline:
            firstline = 1

        consts = self.getConsts()
        code = self.lnotab.getCode()
        lnotab = self.lnotab.getTable()
        if self.peephole_enabled:
            for _i in range(MAX_BYTECODE_OPT_ITERS):
                opt = self.make_optimizer(code, consts, lnotab).optimize()
                if opt is not None:
                    if code == opt.byte_code:
                        break
                    code, consts, lnotab = opt.byte_code, opt.consts, opt.lnotab

        consts = consts + tuple(self.extra_consts)
        return self.make_code(nlocals, code, consts, firstline, lnotab)

    def make_optimizer(self, code, consts, lnotab) -> Optimizer:
        return Optimizer(code, consts, lnotab, self.opcode)

    def make_code(self, nlocals, code, consts, firstline, lnotab):
        return CodeType(
            len(self.args),
            len(self.kwonlyargs),
            nlocals,
            self.stacksize,
            self.flags,
            code,
            consts,
            tuple(self.names),
            tuple(self.varnames),
            self.filename,
            self.name,
            firstline,
            lnotab,
            tuple(self.freevars),
            tuple(self.cellvars),
        )

    def getConsts(self):
        """Return a tuple for the const slot of the code object"""
        # Just return the constant value, removing the type portion. Order by const index.
        return tuple(
            const[1] for const, idx in sorted(self.consts.items(), key=lambda o: o[1])
        )


class PyFlowGraph37(PyFlowGraph):
    opcode = opcode37.opcode

    def push_block(self, worklist: List[Block], block: Block, depth: int):
        assert (
            block.startdepth < 0 or block.startdepth >= depth
        ), f"{block!r}: {block.startdepth} vs {depth}"
        if block.startdepth < depth:
            block.startdepth = depth
            worklist.append(block)

    def stackdepth_walk(self, block):
        maxdepth = 0
        worklist = []
        self.push_block(worklist, block, 0)
        while worklist:
            block = worklist.pop()
            next = block.next
            depth = block.startdepth
            assert depth >= 0

            for instr in block.getInstructions():
                if instr.opname == "SET_LINENO":
                    continue

                delta = self.opcode.stack_effect_raw(instr.opname, instr.oparg, False)
                new_depth = depth + delta
                if new_depth > maxdepth:
                    maxdepth = new_depth

                assert depth >= 0

                op = self.opcode.opmap[instr.opname]
                if self.opcode.has_jump(op):
                    delta = self.opcode.stack_effect_raw(
                        instr.opname, instr.oparg, True
                    )

                    target_depth = depth + delta
                    if target_depth > maxdepth:
                        maxdepth = target_depth

                    assert target_depth >= 0
                    if instr.opname == "CONTINUE_LOOP":
                        # Pops a variable number of values from the stack,
                        # but the target should be already proceeding.
                        assert instr.target.startdepth >= 0
                        assert instr.target.startdepth <= depth
                        # remaining code is dead
                        next = None
                        break

                    self.push_block(worklist, instr.target, target_depth)

                depth = new_depth

                if instr.opname in (
                    "JUMP_ABSOLUTE",
                    "JUMP_FORWARD",
                    "RETURN_VALUE",
                    "RAISE_VARARGS",
                    "BREAK_LOOP",
                ):
                    # Remaining code is dead
                    next = None
                    break

            # TODO(dinoviehland): we could save the delta we came up with here and
            # reapply it on subsequent walks rather than having to walk all of the
            # instructions again.
            if next:
                self.push_block(worklist, next, depth)

        return maxdepth


class PyFlowGraph38(PyFlowGraph37):
    opcode = opcode38.opcode

    def __init__(
        self,
        name: str,
        filename: str,
        scope,
        flags: int = 0,
        args=(),
        kwonlyargs=(),
        starargs=(),
        optimized: int = 0,
        klass: bool = False,
        docstring: Optional[str] = None,
        firstline: int = 0,
        peephole_enabled: bool = True,
        posonlyargs: int = 0,
    ):
        super().__init__(
            name,
            filename,
            scope,
            flags=flags,
            args=args,
            kwonlyargs=kwonlyargs,
            starargs=starargs,
            optimized=optimized,
            klass=klass,
            docstring=docstring,
            firstline=firstline,
            peephole_enabled=peephole_enabled,
        )
        self.posonlyargs = posonlyargs

    def make_optimizer(self, code, consts, lnotab) -> Optimizer:
        return Optimizer38(code, consts, lnotab, self.opcode)

    def make_code(self, nlocals, code, consts, firstline, lnotab) -> CodeType:
        return CodeType(
            len(self.args),
            self.posonlyargs,
            len(self.kwonlyargs),
            nlocals,
            self.stacksize,
            self.flags,
            code,
            consts,
            tuple(self.names),
            tuple(self.varnames),
            self.filename,
            self.name,
            firstline,
            lnotab,
            tuple(self.freevars),
            tuple(self.cellvars),
        )


class PyFlowGraphCinder(PyFlowGraph38):
    opcode = opcode38cinder.opcode

    def make_code(self, nlocals, code, consts, firstline, lnotab) -> CodeType:
        flags = self.flags | CO_SUPPRESS_JIT if self.scope.suppress_jit else self.flags
        return CodeType(
            len(self.args),
            self.posonlyargs,
            len(self.kwonlyargs),
            nlocals,
            self.stacksize,
            flags,
            code,
            consts,
            tuple(self.names),
            tuple(self.varnames),
            self.filename,
            self.name,
            firstline,
            lnotab,
            tuple(self.freevars),
            tuple(self.cellvars),
        )


class LineAddrTable:
    """lnotab

    This class builds the lnotab, which is documented in compile.c.
    Here's a brief recap:

    For each SET_LINENO instruction after the first one, two bytes are
    added to lnotab.  (In some cases, multiple two-byte entries are
    added.)  The first byte is the distance in bytes between the
    instruction for the last SET_LINENO and the current SET_LINENO.
    The second byte is offset in line numbers.  If either offset is
    greater than 255, multiple two-byte entries are added -- see
    compile.c for the delicate details.
    """

    def __init__(self, opcode):
        self.code = []
        self.codeOffset = 0
        self.firstline = 0
        self.lastline = 0
        self.lastoff = 0
        self.lnotab = []
        self.opcode = opcode

    def setFirstLine(self, lineno):
        self.firstline = lineno
        self.lastline = lineno

    def addCode(self, opcode, oparg):
        self.code.append(opcode)
        self.code.append(oparg)
        self.codeOffset += self.opcode.CODEUNIT_SIZE

    def nextLine(self, lineno):
        if self.firstline == 0:
            self.firstline = lineno
            self.lastline = lineno
        else:
            # compute deltas
            addr_delta = self.codeOffset - self.lastoff
            line_delta = lineno - self.lastline
            if not addr_delta and not line_delta:
                return

            push = self.lnotab.append
            while addr_delta > 255:
                push(255)
                push(0)
                addr_delta -= 255
            if line_delta < -128 or 127 < line_delta:
                if line_delta < 0:
                    k = -128
                    ncodes = (-line_delta) // 128
                else:
                    k = 127
                    ncodes = line_delta // 127
                line_delta -= ncodes * k
                push(addr_delta)
                push(cast_signed_byte_to_unsigned(k))
                addr_delta = 0
                for _ in range(ncodes - 1):
                    push(0)
                    push(cast_signed_byte_to_unsigned(k))

            assert -128 <= line_delta and line_delta <= 127
            push(addr_delta)
            push(cast_signed_byte_to_unsigned(line_delta))

            self.lastline = lineno
            self.lastoff = self.codeOffset

    def getCode(self):
        return bytes(self.code)

    def getTable(self):
        return bytes(self.lnotab)

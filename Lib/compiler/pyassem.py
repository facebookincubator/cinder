# Portions copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
# pyre-unsafe

"""A flow graph representation for Python bytecode"""
from __future__ import annotations

import sys
from contextlib import contextmanager
from types import CodeType
from typing import ClassVar, Generator, List, Optional

from . import opcode_cinder, opcodes
from .consts import (
    CO_ASYNC_GENERATOR,
    CO_COROUTINE,
    CO_GENERATOR,
    CO_NEWLOCALS,
    CO_OPTIMIZED,
    CO_SUPPRESS_JIT,
)
from .flow_graph_optimizer import FlowGraphOptimizer
from .opcodebase import Opcode


MAX_COPY_SIZE = 4


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
    __slots__ = ("opname", "oparg", "target", "ioparg", "lineno")

    def __init__(
        self,
        opname: str,
        oparg: object,
        ioparg: int = 0,
        lineno: int = -1,
        target: Optional[Block] = None,
    ):
        self.opname = opname
        self.oparg = oparg
        self.lineno = lineno
        self.ioparg = ioparg
        self.target = target

    def __repr__(self):
        args = [
            f"{self.opname!r}",
            f"{self.oparg!r}",
            f"{self.ioparg!r}",
            f"{self.lineno!r}",
        ]
        if self.target is not None:
            args.append(f"{self.target!r}")

        return f"Instruction({', '.join(args)})"

    def is_jump(self, opcode: Opcode) -> bool:
        op = opcode.opmap[self.opname]
        return opcode.has_jump(op)

    def copy(self) -> Instruction:
        return Instruction(
            self.opname, self.oparg, self.ioparg, self.lineno, self.target
        )


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
        self.startBlock(self.entry)

        # Source line number to use for next instruction.
        self.lineno = 0
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

    def blocks_in_reverse_allocation_order(self):
        for block in sorted(
            self.ordered_blocks, key=lambda b: b.alloc_id, reverse=True
        ):
            yield block

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

    _debug = 0

    def _enable_debug(self):
        self._debug = 1

    def _disable_debug(self):
        self._debug = 0

    def emit(self, opcode: str, oparg: object = 0, lineno: int | None = None) -> None:
        if lineno is None:
            lineno = self.lineno
        if isinstance(oparg, Block):
            if not self.do_not_emit_bytecode:
                self.current.addOutEdge(oparg)
                self.current.emit(Instruction(opcode, 0, 0, lineno, target=oparg))
            return

        ioparg = self.convertArg(opcode, oparg)

        if not self.do_not_emit_bytecode:
            self.current.emit(Instruction(opcode, oparg, ioparg, lineno))

    def emit_noline(self, opcode: str, oparg: object = 0):
        self.emit(opcode, oparg, -1)

    def emitWithBlock(self, opcode: str, oparg: object, target: Block):
        if not self.do_not_emit_bytecode:
            self.current.addOutEdge(target)
            self.current.emit(Instruction(opcode, oparg, target=target))

    def set_lineno(self, lineno: int) -> None:
        if not self.first_inst_lineno:
            self.first_inst_lineno = lineno
        self.lineno = lineno

    def convertArg(self, opcode: str, oparg: object) -> int:
        if isinstance(oparg, int):
            return oparg
        raise ValueError(f"invalid oparg {oparg!r} for {opcode!r}")

    def getBlocksInOrder(self):
        """Return the blocks in the order they should be output."""
        return self.ordered_blocks

    def getBlocks(self):
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
    allocated_block_count: ClassVar[int] = 0

    def __init__(self, label=""):
        self.insts: List[Instruction] = []
        self.outEdges = set()
        self.label: str = label
        self.bid: int | None = None
        self.next: Block | None = None
        self.prev: Block | None = None
        self.returns: bool = False
        self.offset: int = 0
        self.seen: bool = False  # visited during stack depth calculation
        self.startdepth: int = -1
        self.is_exit: bool = False
        self.no_fallthrough: bool = False
        self.num_predecessors: int = 0
        self.alloc_id: int = Block.allocated_block_count
        Block.allocated_block_count += 1

    def __repr__(self):
        data = []
        data.append(f"id={self.bid}")
        data.append(f"startdepth={self.startdepth}")
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
        # TODO(T128853358): The RETURN_PRIMITIVE logic should live in the Static flow graph.
        if instr.opname in ("RETURN_VALUE", "RETURN_PRIMITIVE"):
            self.returns = True

        self.insts.append(instr)

    def getInstructions(self):
        return self.insts

    def addOutEdge(self, block):
        self.outEdges.add(block)

    def addNext(self, block):
        assert self.next is None, self.next
        self.next = block
        assert block.prev is None, block.prev
        block.prev = self

    def removeNext(self):
        assert self.next is not None
        next = self.next
        next.prev = None
        self.next = None

    def has_return(self):
        # TODO(T128853358): The RETURN_PRIMITIVE logic should live in the Static flow graph.
        return self.insts and self.insts[-1].opname in (
            "RETURN_VALUE",
            "RETURN_PRIMITIVE",
        )

    def get_children(self):
        return list(self.outEdges) + ([self.next] if self.next is not None else [])

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

    def copy(self):
        # Cannot copy block if it has fallthrough, since a block can have only one
        # fallthrough predecessor
        assert self.no_fallthrough
        result = Block()
        result.insts = [instr.copy() for instr in self.insts]
        result.is_exit = self.is_exit
        result.no_fallthrough = True
        return result


# flags for code objects

# the FlowGraph is transformed in place; it exists in one of these states
ACTIVE = "ACTIVE"  # accepting calls to .emit()
CLOSED = "CLOSED"  # closed to new instructions
CONSTS_CLOSED = "CONSTS_CLOSED"  # closed to new consts
OPTIMIZED = "OPTIMIZED"  # optimizations have been run
ORDERED = "ORDERED"  # basic block ordering is set
FINAL = "FINAL"  # all optimization and normalization of flow graph is done
FLAT = "FLAT"  # flattened
DONE = "DONE"


class IndexedSet:
    """Container that behaves like a `set` that assigns stable dense indexes
    to each element. Put another way: This behaves like a `list` where you
    check `x in <list>` before doing any insertion to avoid duplicates. But
    contrary to the list this does not require an O(n) member check."""

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
        # This relies on `dict` maintaining insertion order.
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

    def update(self, iterable):
        for item in iterable:
            self.get_index(item)


class PyFlowGraph(FlowGraph):

    super_init = FlowGraph.__init__
    opcode = opcodes.opcode

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
        posonlyargs: int = 0,
    ) -> None:
        self.super_init()
        self.name = name
        self.filename = filename
        self.scope = scope
        self.docstring = None
        self.args = args
        self.kwonlyargs = kwonlyargs
        self.posonlyargs = posonlyargs
        self.starargs = starargs
        self.klass = klass
        self.stacksize = 0
        self.docstring = docstring
        self.flags = flags
        if optimized:
            self.setFlag(CO_OPTIMIZED | CO_NEWLOCALS)
        self.consts = {}
        self.names = IndexedSet()
        # Free variables found by the symbol table scan, including
        # variables used only in nested scopes, are included here.
        if scope is not None:
            self.freevars = IndexedSet(scope.get_free_vars())
            self.cellvars = IndexedSet(scope.get_cell_vars())
        else:
            self.freevars = IndexedSet([])
            self.cellvars = IndexedSet([])
        # The closure list is used to track the order of cell
        # variables and free variables in the resulting code object.
        # The offsets used by LOAD_CLOSURE/LOAD_DEREF refer to both
        # kinds of variables.
        self.closure = self.cellvars + self.freevars
        varnames = IndexedSet()
        varnames.update(args)
        varnames.update(kwonlyargs)
        varnames.update(starargs)
        self.varnames = varnames
        self.stage = ACTIVE
        self.firstline = firstline
        self.first_inst_lineno = 0
        self.lineno = 0
        # Add any extra consts that were requested to the const pool
        self.extra_consts = []
        self.initializeConsts()
        self.fast_vars = set()
        self.gen_kind = None
        if flags & CO_COROUTINE:
            self.gen_kind = 1
        elif flags & CO_ASYNC_GENERATOR:
            self.gen_kind = 2
        elif flags & CO_GENERATOR:
            self.gen_kind = 0

    def emit_gen_start(self) -> None:
        if self.gen_kind is not None:
            self.emit("GEN_START", self.gen_kind, -1)

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
        assert self.stage in {ACTIVE, CLOSED}, self.stage

        if self.do_not_emit_bytecode and opcode in self._quiet_opcodes:
            # return -1 so this errors if it ever ends up in non-dead-code due
            # to a bug.
            return -1

        conv = self._converters.get(opcode)
        if conv is not None:
            return conv(self, oparg)

        return super().convertArg(opcode, oparg)

    def finalize(self) -> None:
        """Perform final optimizations and normalization of flow graph."""
        assert self.stage == ACTIVE, self.stage
        self.stage = CLOSED

        for block in self.ordered_blocks:
            self.normalize_basic_block(block)
        for block in self.blocks_in_reverse_allocation_order():
            self.extend_block(block)
        self.optimizeCFG()
        self.duplicate_exits_without_lineno()

        self.stage = CONSTS_CLOSED
        self.trim_unused_consts()
        self.propagate_line_numbers()
        self.firstline = self.firstline or self.first_inst_lineno or 1
        self.guarantee_lineno_for_exits()

        self.stage = ORDERED
        self.normalize_jumps()
        self.stage = FINAL

    def getCode(self):
        """Get a Python code object"""
        self.finalize()
        assert self.stage == FINAL, self.stage

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
                if instr.target is None:
                    print("\t", f"{pc:3} {instr.lineno} {opname} {instr.oparg}")
                elif instr.target.label:
                    print(
                        "\t",
                        f"{pc:3} {instr.lineno} {opname} {instr.target.bid} ({instr.target.label})",
                    )
                else:
                    print("\t", f"{pc:3} {instr.lineno} {opname} {instr.target.bid}")
                pc += self.opcode.CODEUNIT_SIZE
        if io:
            sys.stdout = save

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
        self.push_block(worklist, block, 0 if self.gen_kind is None else 1)
        while worklist:
            block = worklist.pop()
            next = block.next
            depth = block.startdepth
            assert depth >= 0

            for instr in block.getInstructions():
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

                    self.push_block(worklist, instr.target, target_depth)

                depth = new_depth

                # TODO(T128853358): The RETURN_PRIMITIVE logic should live in the Static flow graph.
                if instr.opname in (
                    "JUMP_ABSOLUTE",
                    "JUMP_FORWARD",
                    "RETURN_VALUE",
                    "RETURN_PRIMITIVE",
                    "RAISE_VARARGS",
                    "RERAISE",
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

    def computeStackDepth(self):
        """Compute the max stack depth.

        Find the flow path that needs the largest stack.  We assume that
        cycles in the flow graph have no net effect on the stack depth.
        """
        assert self.stage == FINAL, self.stage
        for block in self.getBlocksInOrder():
            # We need to get to the first block which actually has instructions
            if block.getInstructions():
                self.stacksize = self.stackdepth_walk(block)
                break

    def flattenGraph(self):
        """Arrange the blocks in order and resolve jumps"""
        assert self.stage == FINAL, self.stage
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
                    pc += instrsize(inst.ioparg)

            pc = 0
            for inst in insts:
                pc += instrsize(inst.ioparg)
                op = self.opcode.opmap[inst.opname]
                if self.opcode.has_jump(op):
                    oparg = inst.ioparg
                    target = inst.target

                    offset = target.offset
                    if op in self.opcode.hasjrel:
                        offset -= pc

                    if instrsize(oparg) != instrsize(offset):
                        extended_arg_recompile = True

                    assert offset >= 0, "Offset value: %d" % offset
                    inst.ioparg = offset

        self.stage = FLAT

    def sort_cellvars(self):
        self.closure = self.cellvars + self.freevars

    # TODO(T128853358): pull out all converters for static opcodes into
    # StaticPyFlowGraph

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
                type(value)(self.get_const_key(const) for const in value),
            )

        return type(value), value

    def _convert_LOAD_FAST(self, arg: object) -> int:
        self.fast_vars.add(arg)
        return self.varnames.get_index(arg)

    def _convert_LOAD_LOCAL(self, arg: object) -> int:
        self.fast_vars.add(arg)
        assert isinstance(arg, tuple), "invalid oparg {arg!r}"
        return self._convert_LOAD_CONST((self.varnames.get_index(arg[0]), arg[1]))

    def _convert_NAME(self, arg: object) -> int:
        return self.names.get_index(arg)

    def _convert_LOAD_SUPER(self, arg: object) -> int:
        assert isinstance(arg, tuple), "invalid oparg {arg!r}"
        return self._convert_LOAD_CONST((self._convert_NAME(arg[0]), arg[1]))

    def _convert_DEREF(self, arg: object) -> int:
        # Sometimes, both cellvars and freevars may contain the same var
        # (e.g., for class' __class__). In this case, prefer freevars.
        if arg in self.freevars:
            return self.freevars.get_index(arg) + len(self.cellvars)
        return self.closure.get_index(arg)

    # similarly for other opcodes...
    _converters = {
        "LOAD_CLASS": _convert_LOAD_CONST,
        "LOAD_CONST": _convert_LOAD_CONST,
        "INVOKE_FUNCTION": _convert_LOAD_CONST,
        "INVOKE_METHOD": _convert_LOAD_CONST,
        "INVOKE_NATIVE": _convert_LOAD_CONST,
        "LOAD_FIELD": _convert_LOAD_CONST,
        "STORE_FIELD": _convert_LOAD_CONST,
        "CAST": _convert_LOAD_CONST,
        "TP_ALLOC": _convert_LOAD_CONST,
        "CHECK_ARGS": _convert_LOAD_CONST,
        "BUILD_CHECKED_MAP": _convert_LOAD_CONST,
        "BUILD_CHECKED_LIST": _convert_LOAD_CONST,
        "PRIMITIVE_LOAD_CONST": _convert_LOAD_CONST,
        "LOAD_FAST": _convert_LOAD_FAST,
        "STORE_FAST": _convert_LOAD_FAST,
        "DELETE_FAST": _convert_LOAD_FAST,
        "LOAD_LOCAL": _convert_LOAD_LOCAL,
        "STORE_LOCAL": _convert_LOAD_LOCAL,
        "LOAD_NAME": _convert_NAME,
        "LOAD_CLOSURE": lambda self, arg: self.closure.get_index(arg),
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
        "LOAD_TYPE": _convert_LOAD_CONST,
    }

    # Converters which add an entry to co_consts
    _const_converters = {_convert_LOAD_CONST, _convert_LOAD_LOCAL, _convert_LOAD_SUPER}
    # Opcodes which reference an entry in co_consts
    _const_opcodes = set()
    for op, converter in _converters.items():
        if converter in _const_converters:
            _const_opcodes.add(op)

    # Opcodes which do not add names to co_consts/co_names/co_varnames in dead code (self.do_not_emit_bytecode)
    _quiet_opcodes = {
        "LOAD_GLOBAL",
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
        lnotab.setFirstLine(self.firstline)

        for t in self.insts:
            if lnotab.current_line != t.lineno:
                lnotab.nextLine(t.lineno)
            oparg = t.ioparg
            assert 0 <= oparg <= 0xFFFFFFFF, oparg
            if oparg > 0xFFFFFF:
                lnotab.addCode(self.opcode.EXTENDED_ARG, (oparg >> 24) & 0xFF)
            if oparg > 0xFFFF:
                lnotab.addCode(self.opcode.EXTENDED_ARG, (oparg >> 16) & 0xFF)
            if oparg > 0xFF:
                lnotab.addCode(self.opcode.EXTENDED_ARG, (oparg >> 8) & 0xFF)
            lnotab.addCode(self.opcode.opmap[t.opname], oparg & 0xFF)

        # Since the linetable format writes the end offset of bytecodes, we can't commit the
        # last write until all the instructions are iterated over.
        lnotab.emitCurrentLine()
        self.stage = DONE

    def newCodeObject(self):
        assert self.stage == DONE, self.stage
        if (self.flags & CO_NEWLOCALS) == 0:
            nlocals = len(self.fast_vars)
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
        consts = consts + tuple(self.extra_consts)
        return self.make_code(nlocals, code, consts, firstline, lnotab)

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

    def getConsts(self):
        """Return a tuple for the const slot of the code object"""
        # Just return the constant value, removing the type portion. Order by const index.
        return tuple(
            const[1] for const, idx in sorted(self.consts.items(), key=lambda o: o[1])
        )

    def propagate_line_numbers(self):
        """Propagate line numbers to instructions without."""
        for block in self.ordered_blocks:
            if not block.insts:
                continue
            prev_lineno = -1
            for instr in block.insts:
                if instr.lineno < 0:
                    instr.lineno = prev_lineno
                else:
                    prev_lineno = instr.lineno
            if not block.no_fallthrough and block.next.num_predecessors == 1:
                assert block.next.insts
                next_instr = block.next.insts[0]
                if next_instr.lineno < 0:
                    next_instr.lineno = prev_lineno
            last_instr = block.insts[-1]
            if last_instr.is_jump(self.opcode) and last_instr.opname not in {
                # Only actual jumps, not exception handlers
                "SETUP_ASYNC_WITH",
                "SETUP_WITH",
                "SETUP_FINALLY",
            }:
                target = last_instr.target
                if target.num_predecessors == 1:
                    assert target.insts
                    next_instr = target.insts[0]
                    if next_instr.lineno < 0:
                        next_instr.lineno = prev_lineno

    def guarantee_lineno_for_exits(self):
        lineno = self.firstline
        assert lineno > 0
        for block in self.ordered_blocks:
            if not block.insts:
                continue
            last_instr = block.insts[-1]
            if last_instr.lineno < 0:
                # TODO(T128853358): The RETURN_PRIMITIVE logic should live in the Static flow graph.
                if last_instr.opname in ("RETURN_VALUE", "RETURN_PRIMITIVE"):
                    for instr in block.insts:
                        assert instr.lineno < 0
                        instr.lineno = lineno
            else:
                lineno = last_instr.lineno

    def duplicate_exits_without_lineno(self):
        """
        PEP 626 mandates that the f_lineno of a frame is correct
        after a frame terminates. It would be prohibitively expensive
        to continuously update the f_lineno field at runtime,
        so we make sure that all exiting instruction (raises and returns)
        have a valid line number, allowing us to compute f_lineno lazily.
        We can do this by duplicating the exit blocks without line number
        so that none have more than one predecessor. We can then safely
        copy the line number from the sole predecessor block.
        """
        # Copy all exit blocks without line number that are targets of a jump.
        append_after = {}
        for block in self.blocks_in_reverse_allocation_order():
            if block.insts and (last := block.insts[-1]).is_jump(self.opcode):
                if last.opname in {"SETUP_ASYNC_WITH", "SETUP_WITH", "SETUP_FINALLY"}:
                    continue
                target = last.target
                assert target.insts
                if (
                    target.is_exit
                    and target.insts[0].lineno < 0
                    and target.num_predecessors > 1
                ):
                    new_target = target.copy()
                    new_target.insts[0].lineno = last.lineno
                    last.target = new_target
                    target.num_predecessors -= 1
                    new_target.num_predecessors = 1
                    new_target.next = target.next
                    target.next = new_target
                    new_target.prev = target
                    new_target.bid = self.block_count
                    self.block_count += 1
                    append_after.setdefault(target, []).append(new_target)
        for after, to_append in append_after.items():
            idx = self.ordered_blocks.index(after) + 1
            self.ordered_blocks[idx:idx] = reversed(to_append)

    def normalize_jumps(self):
        assert self.stage == ORDERED, self.stage

        seen_blocks = set()

        for block in self.ordered_blocks:
            seen_blocks.add(block.bid)
            if not block.insts:
                continue
            last = block.insts[-1]
            if last.opname == "JUMP_ABSOLUTE" and last.target.bid not in seen_blocks:
                last.opname = "JUMP_FORWARD"
            elif last.opname == "JUMP_FORWARD" and last.target.bid in seen_blocks:
                last.opname = "JUMP_ABSOLUTE"

    def optimizeCFG(self):
        """Optimize a well-formed CFG."""
        assert self.stage == CLOSED, self.stage

        optimizer = FlowGraphOptimizer(self)
        for block in self.ordered_blocks:
            optimizer.optimize_basic_block(block)
            optimizer.clean_basic_block(block, -1)

        for block in self.blocks_in_reverse_allocation_order():
            self.extend_block(block)

        prev_block = None
        for block in self.ordered_blocks:
            prev_lineno = -1
            if prev_block and prev_block.insts:
                prev_lineno = prev_block.insts[-1].lineno
            optimizer.clean_basic_block(block, prev_lineno)
            prev_block = None if block.no_fallthrough else block

        self.eliminate_empty_basic_blocks()
        self.remove_unreachable_basic_blocks()

        # Delete jump instructions made redundant by previous step. If a non-empty
        # block ends with a jump instruction, check if the next non-empty block
        # reached through normal flow control is the target of that jump. If it
        # is, then the jump instruction is redundant and can be deleted.
        maybe_empty_blocks = False
        for block in self.ordered_blocks:
            if not block.insts:
                continue
            last = block.insts[-1]
            if last.opname not in {"JUMP_ABSOLUTE", "JUMP_FORWARD"}:
                continue
            if last.target == block.next:
                block.no_fallthrough = False
                last.opname = "NOP"
                last.oparg = last.ioparg = 0
                last.target = None
                optimizer.clean_basic_block(block, -1)
                maybe_empty_blocks = True

        if maybe_empty_blocks:
            self.eliminate_empty_basic_blocks()

        self.stage = OPTIMIZED

    def eliminate_empty_basic_blocks(self):
        for block in self.ordered_blocks:
            next_block = block.next
            if next_block:
                while not next_block.insts and next_block.next:
                    next_block = next_block.next
                block.next = next_block
        for block in self.ordered_blocks:
            if not block.insts:
                continue
            last = block.insts[-1]
            if last.is_jump(self.opcode):
                target = last.target
                while not target.insts and target.next:
                    target = target.next
                last.target = target

    def remove_unreachable_basic_blocks(self):
        # mark all reachable blocks
        reachable_blocks = set()
        worklist = [self.entry]
        while worklist:
            entry = worklist.pop()
            if entry.bid in reachable_blocks:
                continue
            reachable_blocks.add(entry.bid)
            for instruction in entry.getInstructions():
                target = instruction.target
                if target is not None:
                    worklist.append(target)
                    target.num_predecessors += 1

            if not entry.no_fallthrough:
                worklist.append(entry.next)
                entry.next.num_predecessors += 1

        self.ordered_blocks = [
            block for block in self.ordered_blocks if block.bid in reachable_blocks
        ]
        prev = None
        for block in self.ordered_blocks:
            block.prev = prev
            if prev is not None:
                prev.next = block
            prev = block

    def normalize_basic_block(self, block: Block) -> None:
        """Sets the `fallthrough` and `exit` properties of a block, and ensures that the targets of
        any jumps point to non-empty blocks by following the next pointer of empty blocks."""
        for instr in block.getInstructions():
            # TODO(T128853358): The RETURN_PRIMITIVE logic should live in the Static flow graph.
            if instr.opname in (
                "RETURN_VALUE",
                "RETURN_PRIMITIVE",
                "RAISE_VARARGS",
                "RERAISE",
            ):
                block.is_exit = True
                block.no_fallthrough = True
                continue
            elif instr.opname in ("JUMP_ABSOLUTE", "JUMP_FORWARD"):
                block.no_fallthrough = True
            elif not instr.is_jump(self.opcode):
                continue
            while not instr.target.insts:
                instr.target = instr.target.next

    def extend_block(self, block: Block) -> None:
        """If this block ends with an unconditional jump to an exit block,
        then remove the jump and extend this block with the target.
        """
        if len(block.insts) == 0:
            return
        last = block.insts[-1]
        if last.opname not in ("JUMP_ABSOLUTE", "JUMP_FORWARD"):
            return
        target = last.target
        assert target is not None
        if not target.is_exit:
            return
        if len(target.insts) > MAX_COPY_SIZE:
            return
        last = block.insts[-1]
        last.opname = "NOP"
        last.oparg = last.ioparg = 0
        last.target = None
        for instr in target.insts:
            block.insts.append(instr.copy())
        block.next = None
        block.is_exit = True
        block.no_fallthrough = True

    def trim_unused_consts(self) -> None:
        """Remove trailing unused constants."""
        assert self.stage == CONSTS_CLOSED, self.stage

        max_const_index = 0
        for block in self.ordered_blocks:
            for instr in block.insts:
                if (
                    instr.opname in self._const_opcodes
                    and instr.ioparg > max_const_index
                ):
                    max_const_index = instr.ioparg
        self.consts = {
            key: index for key, index in self.consts.items() if index <= max_const_index
        }


class PyFlowGraphCinder(PyFlowGraph):
    opcode = opcode_cinder.opcode

    def make_code(self, nlocals, code, consts, firstline, lnotab) -> CodeType:
        if self.scope is not None and self.scope.suppress_jit:
            self.setFlag(CO_SUPPRESS_JIT)
        return super().make_code(nlocals, code, consts, firstline, lnotab)


class LineAddrTable:
    """linetable / lnotab

    This class builds the linetable, which is documented in
    Objects/lnotab_notes.txt. Here's a brief recap:

    For each new lineno after the first one, two bytes are added to the
    linetable.  (In some cases, multiple two-byte entries are added.)  The first
    byte is the distance in bytes between the instruction for the current lineno
    and the next lineno.  The second byte is offset in line numbers.  If either
    offset is greater than 255, multiple two-byte entries are added -- see
    lnotab_notes.txt for the delicate details.

    """

    def __init__(self, opcode):
        self.code = []
        self.current_start = 0
        self.current_end = 0
        self.current_line = 0
        self.prev_line = 0
        self.linetable = []
        self.opcode = opcode

    def setFirstLine(self, lineno):
        self.current_line = lineno
        self.prev_line = lineno

    def addCode(self, opcode, oparg):
        self.code.append(opcode)
        self.code.append(oparg)
        self.current_end += self.opcode.CODEUNIT_SIZE

    def nextLine(self, lineno):
        if not lineno:
            return
        self.emitCurrentLine()

        self.current_start = self.current_end
        if self.current_line >= 0:
            self.prev_line = self.current_line
        self.current_line = lineno

    def emitCurrentLine(self):
        # compute deltas
        addr_delta = self.current_end - self.current_start
        if not addr_delta:
            return
        if self.current_line < 0:
            line_delta = -128
        else:
            line_delta = self.current_line - self.prev_line
            while line_delta < -127 or 127 < line_delta:
                if line_delta < 0:
                    k = -127
                else:
                    k = 127
                self.push_entry(0, k)
                line_delta -= k

        while addr_delta > 254:
            self.push_entry(254, line_delta)
            line_delta = -128 if self.current_line < 0 else 0
            addr_delta -= 254

        assert -128 <= line_delta and line_delta <= 127
        self.push_entry(addr_delta, line_delta)

    def getCode(self):
        return bytes(self.code)

    def getTable(self):
        return bytes(self.linetable)

    def push_entry(self, addr_delta, line_delta):
        self.linetable.append(addr_delta)
        self.linetable.append(cast_signed_byte_to_unsigned(line_delta))

# pyre-unsafe
from __future__ import annotations

TYPE_CHECKING = False
if TYPE_CHECKING:
    from typing import Callable, Dict, Optional

    TOpcodeHandler = Callable[["Optimizer", int, int, int, int, int], Optional[int]]


PyCmp_LT = 0
PyCmp_LE = 1
PyCmp_EQ = 2
PyCmp_NE = 3
PyCmp_GT = 4
PyCmp_GE = 5
PyCmp_IN = 6
PyCmp_NOT_IN = 7
PyCmp_IS = 8
PyCmp_IS_NOT = 9
PyCmp_EXC_MATCH = 10

UNARY_OPS: Dict[str, object] = {
    "UNARY_INVERT": lambda v: ~v,
    "UNARY_NEGATIVE": lambda v: -v,
    "UNARY_POSITIVE": lambda v: +v,
}


class DefaultLimits:
    MAX_INT_SIZE = 128
    MAX_COLLECTION_SIZE = 20
    MAX_STR_SIZE = 20
    MAX_TOTAL_ITEMS = 1024


def check_complexity(obj, limit):
    if isinstance(obj, (frozenset, tuple)):
        limit -= len(obj)
        for item in obj:
            limit = check_complexity(item, limit)
            if limit < 0:
                break

    return limit


def safe_multiply(left, right, limits=DefaultLimits):
    if isinstance(left, int) and isinstance(right, int) and left and right:
        lbits = left.bit_length()
        rbits = right.bit_length()
        if lbits + rbits > limits.MAX_INT_SIZE:
            raise OverflowError()
    elif isinstance(left, int) and isinstance(right, (tuple, frozenset)):
        rsize = len(right)
        if rsize:
            if left < 0 or left > limits.MAX_COLLECTION_SIZE / rsize:
                raise OverflowError()
            if left:
                if check_complexity(right, limits.MAX_TOTAL_ITEMS / left) < 0:
                    raise OverflowError()
    elif isinstance(left, int) and isinstance(right, (str, bytes)):
        rsize = len(right)
        if rsize:
            if left < 0 or left > limits.MAX_STR_SIZE / rsize:
                raise OverflowError()
    elif isinstance(right, int) and isinstance(left, (tuple, frozenset, str, bytes)):
        return safe_multiply(right, left, limits)

    return left * right


def safe_power(left, right, limits=DefaultLimits):
    if isinstance(left, int) and isinstance(right, int) and left and right > 0:
        lbits = left.bit_length()
        if lbits > limits.MAX_INT_SIZE / right:
            raise OverflowError()

    return left ** right


def safe_mod(left, right, limits=DefaultLimits):
    if isinstance(left, (str, bytes)):
        raise OverflowError()

    return left % right


def safe_lshift(left, right, limits=DefaultLimits):
    if isinstance(left, int) and isinstance(right, int) and left and right:
        lbits = left.bit_length()
        if (
            right < 0
            or right > limits.MAX_INT_SIZE
            or lbits > limits.MAX_INT_SIZE - right
        ):
            raise OverflowError()

    return left << right


BINARY_OPS: Dict[str, object] = {
    "BINARY_POWER": safe_power,
    "BINARY_MULTIPLY": safe_multiply,
    "BINARY_TRUE_DIVIDE": lambda left, right: left / right,
    "BINARY_FLOOR_DIVIDE": lambda left, right: left // right,
    "BINARY_MODULO": safe_mod,
    "BINARY_ADD": lambda left, right: left + right,
    "BINARY_SUBTRACT": lambda left, right: left - right,
    "BINARY_SUBSCR": lambda left, right: left[right],
    "BINARY_LSHIFT": safe_lshift,
    "BINARY_RSHIFT": lambda left, right: left >> right,
    "BINARY_AND": lambda left, right: left & right,
    "BINARY_XOR": lambda left, right: left ^ right,
    "BINARY_OR": lambda left, right: left | right,
}


def cast_signed_byte_to_unsigned(i):
    if i < 0:
        i = 255 + i + 1
    return i


def instrsize(oparg):
    if oparg <= 0xFF:
        return 1
    elif oparg <= 0xFFFF:
        return 2
    elif oparg <= 0xFFFFFF:
        return 3

    assert oparg <= 0xFFFFFFFF
    return 4


def ophandler_registry(existing: Optional[Dict[str, TOpcodeHandler]] = None):
    registry = {} if existing is None else dict(existing)

    def register(*opcodes):
        def handler(f: TOpcodeHandler):
            for opcode in opcodes:
                registry[opcode] = f
            return f

        return handler

    return registry, register


class OptimizedCode:
    def __init__(self, byte_code, consts, lnotab):
        self.byte_code = byte_code
        self.consts = consts
        self.lnotab = lnotab


class Optimizer:
    def __init__(
        self,
        codestr: bytes,
        consts,
        lnotab: bytes,
        opcode,
    ) -> None:
        self.consts = list(consts)
        self.codestr = bytearray(codestr)
        self.lnotab = lnotab
        self.const_stack = []
        self.in_consts = False
        self.opcode = opcode
        self.CODEUNIT_SIZE: int = opcode.CODEUNIT_SIZE
        assert len(codestr) % self.CODEUNIT_SIZE == 0
        self.EXTENDED_ARG: int = opcode.EXTENDED_ARG
        opmap = opcode.opmap

        self.NOP: int = opmap["NOP"]

        self.BUILD_SET: int = opmap["BUILD_SET"]
        self.LOAD_CONST: int = opmap["LOAD_CONST"]

        self.POP_JUMP_IF_FALSE: int = opmap["POP_JUMP_IF_FALSE"]
        self.POP_JUMP_IF_TRUE: int = opmap["POP_JUMP_IF_TRUE"]

        self.CONDITIONAL_JUMPS: tuple[int] = (
            opcode.POP_JUMP_IF_FALSE,
            opcode.POP_JUMP_IF_TRUE,
            opcode.JUMP_IF_FALSE_OR_POP,
            opcode.JUMP_IF_TRUE_OR_POP,
        )

        self.UNCONDITIONAL_JUMPS: tuple[int] = (
            opcode.JUMP_ABSOLUTE,
            opcode.JUMP_FORWARD,
        )
        self.JUMPS_ON_TRUE: tuple[int] = (
            opcode.POP_JUMP_IF_TRUE,
            opcode.JUMP_IF_TRUE_OR_POP,
        )

        self.CMP_OP_IN: int = opcode.CMP_OP.index("in")
        self.CMP_OP_IS_NOT: int = opcode.CMP_OP.index("is not")

        assert self.CMP_OP_IN < self.CMP_OP_IS_NOT
        assert (
            opcode.CMP_OP.index("not in") > self.CMP_OP_IN
            and opcode.CMP_OP.index("not in") < self.CMP_OP_IS_NOT
        )
        assert (
            opcode.CMP_OP.index("is") > self.CMP_OP_IN
            and opcode.CMP_OP.index("is") < self.CMP_OP_IS_NOT
        )

        assert (self.CMP_OP_IS_NOT - self.CMP_OP_IN) == 3

        self.blocks = self.markblocks()

    def is_basic_block(self, start, end) -> bool:
        return self.blocks[start] == self.blocks[end]

    def fill_nops(self, start, end) -> None:
        for i in range(start, end):
            self.codestr[i * self.CODEUNIT_SIZE] = self.NOP

    def find_op(self, i=0) -> int:
        for i in range(i, len(self.codestr) // self.CODEUNIT_SIZE):
            if self.codestr[i * self.CODEUNIT_SIZE] != self.EXTENDED_ARG:
                break
        return i

    def push_const(self, const) -> None:
        self.const_stack.append(const)
        self.in_consts = True

    def get_op(self, code, i) -> int:
        return code[i * self.CODEUNIT_SIZE]

    def get_arg(self, code, i) -> int:
        index = i * self.CODEUNIT_SIZE + 1
        oparg = code[index]
        if i >= 1 and self.get_op(code, i - 1) == self.EXTENDED_ARG:
            oparg |= code[index - 2] << 8
            if i >= 2 and self.get_op(code, i - 2) == self.EXTENDED_ARG:
                oparg |= code[index - 4] << 16
                if i >= 3 and self.get_op(code, i - 3) == self.EXTENDED_ARG:
                    oparg |= code[index - 6] << 24
        return oparg

    def optimize(self) -> Optional[OptimizedCode]:
        i = 0
        while i < len(self.lnotab):
            if self.lnotab[i] == 0xFF:
                return None
            i += 2

        instr_index = self.find_op()
        num_operations = len(self.codestr) // self.CODEUNIT_SIZE

        while instr_index < num_operations:
            opcode = self.codestr[instr_index * self.CODEUNIT_SIZE]
            op_start = instr_index
            while (
                op_start >= 1
                and self.codestr[(op_start - 1) * self.CODEUNIT_SIZE]
                == self.EXTENDED_ARG
            ):
                op_start -= 1
            nexti = instr_index + 1
            while (
                nexti < num_operations
                and self.codestr[nexti * self.CODEUNIT_SIZE] == self.EXTENDED_ARG
            ):
                nexti += 1

            nextop = (
                self.codestr[nexti * self.CODEUNIT_SIZE]
                if nexti < num_operations
                else 0
            )

            if not self.in_consts:
                del self.const_stack[:]
            self.in_consts = False

            opname = self.opcode.opname[opcode]
            handler = self.OP_HANDLERS.get(opname)
            if handler is not None:
                instr_index = (
                    handler(self, instr_index, opcode, op_start, nextop, nexti) or nexti
                )
            else:
                instr_index = nexti

        self.fix_blocks()
        lnotab = self.fix_lnotab()
        codestr = self.fix_jumps()
        if codestr is None:
            return None

        return OptimizedCode(bytes(codestr), tuple(self.consts), bytes(lnotab))

    OP_HANDLERS, ophandler = ophandler_registry()

    @ophandler("UNARY_NOT")
    def opt_unary_not(self, instr_index, opcode, op_start, nextop, nexti):
        # Replace UNARY_NOT POP_JUMP_IF_FALSE
        #   with    POP_JUMP_IF_TRUE
        if nextop != self.POP_JUMP_IF_FALSE or not self.is_basic_block(
            op_start, instr_index + 1
        ):
            return

        self.fill_nops(op_start, instr_index + 1)
        self.codestr[nexti * self.CODEUNIT_SIZE] = self.POP_JUMP_IF_TRUE

    @ophandler("COMPARE_OP")
    def opt_compare_op(self, instr_index, opcode, op_start, nextop, nexti):
        # not a is b -->  a is not b
        # not a in b -->  a not in b
        # not a is not b -->  a is b
        # not a not in b -->  a in b
        cmp_type = self.get_arg(self.codestr, instr_index)
        if (
            cmp_type < self.CMP_OP_IN
            or cmp_type > self.CMP_OP_IS_NOT
            or nextop != self.opcode.UNARY_NOT
            or not self.is_basic_block(op_start, instr_index + 1)
        ):
            return

        self.codestr[instr_index * self.CODEUNIT_SIZE + 1] = cmp_type ^ 1
        self.fill_nops(instr_index + 1, nexti + 1)

    @ophandler("LOAD_CONST")
    def opt_load_const(self, instr_index, opcode, op_start, nextop, nexti):
        # Skip over LOAD_CONST trueconst
        # POP_JUMP_IF_FALSE xx.  This improves
        # "while 1" performance.
        # The above comment is from CPython.  This optimization is now performed
        # at the AST level and is also applied to if statements.  But it does
        # not get applied to conditionals, e.g. 1 if 2 else 3
        self.push_const(self.consts[self.get_arg(self.codestr, instr_index)])
        if (
            nextop != self.POP_JUMP_IF_FALSE
            or not self.is_basic_block(op_start, instr_index + 1)
            or not bool(self.consts[self.get_arg(self.codestr, instr_index)])
        ):
            return
        self.fill_nops(op_start, nexti + 1)

    @ophandler("RETURN_VALUE", "RETURN_INT")
    def opt_return_value(self, instr_index, opcode, op_start, nextop, nexti):
        block_end = instr_index + 1
        block_id = self.blocks[instr_index]
        while (
            # checks that we are still in the same basic block
            block_end < len(self.codestr) // self.CODEUNIT_SIZE
            and self.blocks[block_end] == block_id
        ):
            block_end += 1
        if block_end > instr_index + 1:
            self.fill_nops(instr_index + 1, block_end)

    @ophandler(*UNARY_OPS)
    def op_unary_constants(self, instr_index, opcode, op_start, nextop, nexti):
        # Fold unary ops on constants.
        #  LOAD_CONST c1  UNARY_OP --> LOAD_CONST unary_op(c)
        if not self.const_stack:
            return
        unary_ops_start = self.lastn_const_start(op_start, 1)
        if self.is_basic_block(unary_ops_start, op_start):
            last_instr = self.fold_unaryops_on_constants(
                unary_ops_start, instr_index + 1, opcode
            )
            if last_instr >= 0:
                self.const_stack[-1] = self.consts[
                    self.get_arg(self.codestr, instr_index)
                ]
                self.in_consts = True

    @ophandler(*BINARY_OPS)
    def op_binary_constants(self, instr_index, opcode, op_start, nextop, nexti):
        if len(self.const_stack) < 2:
            return
        bin_ops_start = self.lastn_const_start(op_start, 2)
        if self.is_basic_block(bin_ops_start, op_start):
            last_instr = self.fold_binops_on_constants(
                bin_ops_start, instr_index + 1, opcode
            )
            if last_instr >= 0:
                del self.const_stack[-1]
                self.const_stack[-1] = self.consts[
                    self.get_arg(self.codestr, instr_index)
                ]
                self.in_consts = True

    @ophandler("BUILD_TUPLE", "BUILD_LIST", "BUILD_SET")
    def op_fold_sequences(self, instr_index, opcode, op_start, nextop, nexti):
        coll_size = self.get_arg(self.codestr, instr_index)
        if coll_size > 0 and len(self.const_stack) >= coll_size:
            consts_instr_start = self.lastn_const_start(op_start, coll_size)
            if (
                opcode == self.opcode.BUILD_TUPLE
                and self.is_basic_block(consts_instr_start, op_start)
            ) or (
                (opcode == self.opcode.BUILD_LIST or opcode == self.BUILD_SET)
                and (
                    (
                        nextop == self.opcode.COMPARE_OP
                        and (
                            self.codestr[nexti * self.CODEUNIT_SIZE + 1] == PyCmp_IN
                            or self.codestr[nexti * self.CODEUNIT_SIZE + 1]
                            == PyCmp_NOT_IN
                        )
                    )
                    or nextop == self.opcode.GET_ITER
                )
                and self.is_basic_block(consts_instr_start, instr_index + 1)
            ):
                last_instr = self.fold_build_into_constant_tuple(
                    consts_instr_start, instr_index + 1, opcode, coll_size
                )
                if last_instr >= 0:
                    del self.const_stack[-coll_size:]
                    self.const_stack.append(
                        self.consts[self.get_arg(self.codestr, last_instr)]
                    )
                    self.in_consts = True
                return

        if (
            nextop != self.opcode.UNPACK_SEQUENCE
            or not self.is_basic_block(op_start, instr_index + 1)
            or coll_size != self.get_arg(self.codestr, nexti)
            or opcode == self.BUILD_SET
        ):
            return

        if coll_size < 2:
            self.fill_nops(op_start, nexti + 1)
        elif coll_size == 2:
            self.codestr[op_start * self.CODEUNIT_SIZE] = self.opcode.ROT_TWO
            self.codestr[op_start * self.CODEUNIT_SIZE + 1] = 0
            self.fill_nops(op_start + 1, nexti + 1)
            del self.const_stack[:]
        elif coll_size == 3:
            self.codestr[op_start * self.CODEUNIT_SIZE] = self.opcode.ROT_THREE
            self.codestr[op_start * self.CODEUNIT_SIZE + 1] = 0
            self.codestr[(op_start + 1) * self.CODEUNIT_SIZE] = self.opcode.ROT_TWO
            self.codestr[(op_start + 1) * self.CODEUNIT_SIZE + 1] = 0
            self.fill_nops(op_start + 2, nexti + 1)
            del self.const_stack[:]

    @ophandler("JUMP_IF_FALSE_OR_POP", "JUMP_IF_TRUE_OR_POP")
    def op_fold_cond_jumps(self, instr_index, opcode, op_start, nextop, nexti):
        """Simplify conditional jump to conditional jump where the
        result of the first test implies the success of a similar
        test or the failure of the opposite test.
        Arises in code like:
        "if a and b:"
        "if a or b:"
        "a and b or c"
        "(a and b) and c"
        x:JUMP_IF_FALSE_OR_POP y   y:JUMP_IF_FALSE_OR_POP z
           -->  x:JUMP_IF_FALSE_OR_POP z
        x:JUMP_IF_FALSE_OR_POP y   y:JUMP_IF_TRUE_OR_POP z
           -->  x:POP_JUMP_IF_FALSE y+1
        where y+1 is the instruction following the second test."""

        jmp_target = self.get_arg(self.codestr, instr_index) // self.CODEUNIT_SIZE
        tgt_start = self.find_op(jmp_target)
        tgt_instr = self.codestr[tgt_start * self.CODEUNIT_SIZE]
        if tgt_instr in self.CONDITIONAL_JUMPS:
            # NOTE: all possible jumps here are absolute.
            if (tgt_instr in self.JUMPS_ON_TRUE) == (opcode in self.JUMPS_ON_TRUE):
                # The second jump will be taken iff the first is.
                # The current opcode inherits its target's
                # stack effect
                new_end = self.set_arg(
                    instr_index, self.get_arg(self.codestr, tgt_start)
                )
            else:
                # The second jump is not taken if the first is (so
                # jump past it), and all conditional jumps pop their
                # argument when they're not taken (so change the
                # first jump to pop its argument when it's taken).
                new_end = self.set_arg(
                    instr_index, (tgt_start + 1) * self.CODEUNIT_SIZE
                )
                if opcode == self.opcode.JUMP_IF_TRUE_OR_POP:
                    tgt_instr = self.POP_JUMP_IF_TRUE
                else:
                    tgt_instr = self.POP_JUMP_IF_FALSE
            if new_end >= 0:
                nexti = new_end
                self.codestr[nexti * self.CODEUNIT_SIZE] = tgt_instr
                return new_end
        self.op_fold_jumps_to_uncond_jumps(instr_index, opcode, op_start, nextop, nexti)

    @ophandler(
        "POP_JUMP_IF_FALSE",
        "POP_JUMP_IF_TRUE",
        "FOR_ITER",
        "JUMP_FORWARD",
        "JUMP_ABSOLUTE",
        "CONTINUE_LOOP",
        "SETUP_LOOP",
        "SETUP_EXCEPT",
        "SETUP_FINALLY",
        "SETUP_WITH",
        "SETUP_ASYNC_WITH",
    )
    def op_fold_jumps_to_uncond_jumps(
        self, instr_index, opcode, op_start, nextop, nexti
    ):
        jmp_target = self.get_jmp_target(instr_index)
        tgt = self.find_op(jmp_target)
        if (
            opcode in self.UNCONDITIONAL_JUMPS
            and self.codestr[tgt * self.CODEUNIT_SIZE] == self.opcode.RETURN_VALUE
        ):
            self.codestr[op_start * self.CODEUNIT_SIZE] = self.opcode.RETURN_VALUE
            self.codestr[op_start * self.CODEUNIT_SIZE + 1] = 0
            self.fill_nops(op_start + 1, instr_index + 1)
        elif self.codestr[tgt * self.CODEUNIT_SIZE] in self.UNCONDITIONAL_JUMPS:
            jmp_target = self.get_jmp_target(tgt)
            if opcode == self.opcode.JUMP_FORWARD:  # JMP_ABS can go backwards
                opcode = self.opcode.JUMP_ABSOLUTE
            elif opcode not in self.opcode.hasjabs:
                if jmp_target < instr_index + 1:
                    return  # No backward relative jumps
                jmp_target -= instr_index + 1  # Calc relative jump addr
            jmp_target *= self.opcode.CODEUNIT_SIZE
            self.copy_op_arg(op_start, opcode, jmp_target, instr_index + 1)

    def get_jmp_target(self, index):
        code = self.codestr
        opcode = self.get_op(code, index)
        if opcode in self.opcode.hasjabs:
            oparg = self.get_arg(code, index)
            return oparg // self.CODEUNIT_SIZE
        elif opcode in self.opcode.hasjrel:
            oparg = self.get_arg(code, index)
            return oparg // self.CODEUNIT_SIZE + index + 1

    def set_arg(self, instr_index, oparg):
        """Given the index of the effective opcode, attempt to replace the
        argument, taking into account self.EXTENDED_ARG.
        Returns -1 on failure, or the new op index on success."""
        curarg = self.get_arg(self.codestr, instr_index)
        if curarg == oparg:
            return instr_index
        curilen = instrsize(curarg)
        newilen = instrsize(oparg)
        if curilen < newilen:
            return -1

        self.write_op_arg(
            instr_index + 1 - curilen,
            self.codestr[instr_index * self.CODEUNIT_SIZE],
            oparg,
            newilen,
        )
        self.fill_nops(instr_index + 1 - curilen + newilen, instr_index + 1)
        return instr_index - curilen + newilen

    def fold_build_into_constant_tuple(self, c_start, opcode_end, opcode, const_count):
        if opcode == self.BUILD_SET:
            newconst = frozenset(tuple(self.const_stack[-const_count:]))
        else:
            newconst = tuple(self.const_stack[-const_count:])

        self.consts.append(newconst)
        return self.copy_op_arg(
            c_start, self.LOAD_CONST, len(self.consts) - 1, opcode_end
        )

    def fold_binops_on_constants(self, c_start, opcode_end, opcode):
        left = self.const_stack[-2]
        right = self.const_stack[-1]
        try:
            opname = self.opcode.opname[opcode]
            v = BINARY_OPS[opname](left, right)
        except Exception:
            return -1

        # CPython does nothing to optimize these, and doing something like
        # -+-+-+-+-+-+-+-+42  just bloats the constant table with unused entries
        self.consts.append(v)

        return self.copy_op_arg(
            c_start, self.LOAD_CONST, len(self.consts) - 1, opcode_end
        )

    def fold_unaryops_on_constants(self, c_start, opcode_end, opcode):
        v = self.const_stack[-1]
        try:
            opname = self.opcode.opname[opcode]
            v = UNARY_OPS[opname](v)
        except TypeError:
            return -1

        # CPython does nothing to optimize these, and doing something like
        # -+-+-+-+-+-+-+-+42  just bloats the constant table with unused entries
        self.consts.append(v)

        return self.copy_op_arg(
            c_start, self.LOAD_CONST, len(self.consts) - 1, opcode_end
        )

    def copy_op_arg(self, i, op, oparg, maxi):
        ilen = instrsize(oparg)
        if ilen + i > maxi:
            return -1
        self.write_op_arg(maxi - ilen, op, oparg, ilen)
        self.fill_nops(i, maxi - ilen)
        return maxi - 1

    def lastn_const_start(self, instr_index, n):
        assert n > 0
        while True:
            instr_index -= 1
            assert instr_index >= 0
            opcode = self.codestr[instr_index * self.CODEUNIT_SIZE]
            if opcode == self.LOAD_CONST:
                n -= 1
                if not n:
                    while (
                        instr_index > 0
                        and self.codestr[instr_index * self.CODEUNIT_SIZE - 2]
                        == self.EXTENDED_ARG
                    ):
                        instr_index -= 1
                    return instr_index
            else:
                assert opcode == self.NOP or opcode == self.EXTENDED_ARG

    def markblocks(self):
        num_operations = len(self.codestr) // self.CODEUNIT_SIZE
        blocks = [0] * num_operations
        code = self.codestr

        for i in range(num_operations):
            opcode = self.get_op(code, i)
            if opcode in self.opcode.hasjabs:
                oparg = self.get_arg(code, i)
                blocks[oparg // self.CODEUNIT_SIZE] = 1
            elif opcode in self.opcode.hasjrel:
                oparg = self.get_arg(code, i)
                blocks[oparg // self.CODEUNIT_SIZE + i + 1] = 1

        blockcnt = 0
        for block in range(num_operations):
            blockcnt += blocks[block]
            blocks[block] = blockcnt

        return blocks

    def write_op_arg(self, i, opcode, oparg, size):
        codestr = self.codestr
        ofs = i * self.CODEUNIT_SIZE
        if size == 4:
            codestr[ofs] = self.EXTENDED_ARG
            codestr[ofs + 1] = (oparg >> 24) & 0xFF
            ofs += self.CODEUNIT_SIZE
        if size >= 3:
            codestr[ofs] = self.EXTENDED_ARG
            codestr[ofs + 1] = (oparg >> 16) & 0xFF
            ofs += self.CODEUNIT_SIZE
        if size >= 2:
            codestr[ofs] = self.EXTENDED_ARG
            codestr[ofs + 1] = (oparg >> 8) & 0xFF
            ofs += self.CODEUNIT_SIZE

        codestr[ofs] = opcode
        codestr[ofs + 1] = oparg & 0xFF

    def fix_blocks(self) -> None:
        """updates self.blocks to contain the the new instruction offset for
        each corresponding old instruction"""
        nops = 0
        for i in range(len(self.codestr) // self.CODEUNIT_SIZE):
            # original code offset => new code offset
            self.blocks[i] = i - nops
            if self.codestr[i * self.CODEUNIT_SIZE] == self.NOP:
                nops += 1

    def fix_lnotab(self):
        lnotab = bytearray(self.lnotab)
        tabsiz = len(lnotab)
        codelen = len(self.codestr) // self.CODEUNIT_SIZE
        cum_orig_offset = 0
        last_offset = 0
        for i in range(0, tabsiz, 2):
            cum_orig_offset += lnotab[i]
            assert cum_orig_offset % 2 == 0
            index = cum_orig_offset // self.CODEUNIT_SIZE
            if index >= codelen:
                # When running this function multiple times over the same bytecode
                # offset_delta* can be bigger than 255 when the index from the
                # origin is  exactly as big as the code. In that case we don't need to
                # adjust.
                continue

            new_offset = (
                self.blocks[cum_orig_offset // self.CODEUNIT_SIZE] * self.CODEUNIT_SIZE
            )
            offset_delta = new_offset - last_offset
            assert offset_delta <= 255
            lnotab[i] = cast_signed_byte_to_unsigned(offset_delta)
            last_offset = new_offset
        return lnotab

    def fix_jumps(self):
        op_start = i = last_instr_index = 0
        codestr = self.codestr
        blocks = self.blocks
        while i < len(codestr) // self.CODEUNIT_SIZE:
            oparg = codestr[i * self.CODEUNIT_SIZE + 1]
            while codestr[i * self.CODEUNIT_SIZE] == self.EXTENDED_ARG:
                i += 1
                oparg = oparg << 8 | codestr[i * self.CODEUNIT_SIZE + 1]
            opcode = codestr[i * self.CODEUNIT_SIZE]

            if opcode == self.NOP:
                i += 1
                op_start = i
                continue
            if opcode in self.opcode.hasjabs:
                oparg = blocks[oparg // self.CODEUNIT_SIZE] * self.CODEUNIT_SIZE
            elif opcode in self.opcode.hasjrel:
                oparg = blocks[oparg // self.CODEUNIT_SIZE + i + 1] - blocks[i] - 1
                oparg *= self.CODEUNIT_SIZE
            nexti = i - op_start + 1

            if instrsize(oparg) > nexti:
                return None

            # If instrsize(j) < nexti, we'll emit self.EXTENDED_ARG 0
            self.write_op_arg(last_instr_index, opcode, oparg, nexti)

            last_instr_index += nexti
            i += 1
            op_start = i

        del codestr[last_instr_index * self.CODEUNIT_SIZE :]
        return codestr

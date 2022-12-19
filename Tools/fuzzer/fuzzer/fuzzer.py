import argparse
import dis
import enum
import random
import string
import sys
import textwrap
import types
from compiler import compile, opcode_cinder, pyassem, pycodegen, symbols
from compiler.consts import (
    CO_ASYNC_GENERATOR,
    CO_COROUTINE,
    CO_GENERATOR,
    CO_NESTED,
    CO_VARARGS,
    CO_VARKEYWORDS,
    PyCF_MASK_OBSOLETE,
    PyCF_ONLY_AST,
    PyCF_SOURCE_IS_UTF8,
    SC_CELL,
    SC_FREE,
    SC_GLOBAL_EXPLICIT,
    SC_GLOBAL_IMPLICIT,
    SC_LOCAL,
)

from verifier import VerificationError, Verifier

try:
    import cinderjit
except ImportError:
    cinderjit = None


# Bounds for size of randomized strings and integers
# Can be changed as necessary
STR_LEN_UPPER_BOUND: int = 100
STR_LEN_LOWER_BOUND: int = 0
INT_UPPER_BOUND = sys.maxsize
INT_LOWER_BOUND = -sys.maxsize - 1
OPARG_LOWER_BOUND = 0
OPARG_UPPER_BOUND = 2**32 - 1
CMP_OP_LENGTH = len(opcode_cinder.opcode.CMP_OP) - 1  # has "BAD" entry at end

# % Chance of an instr being replaced (1-100)
INSTR_RANDOMIZATION_CHANCE = 50
# % Chance of a random basic block being added (1-100)
CHANCE_TO_EMIT_BLOCK = 10
# Size of a randomly generated basic block
GENERATED_BLOCK_SIZE = 4


class Fuzzer(pycodegen.CinderCodeGenerator):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.flow_graph = PyFlowGraphFuzzer
        self.oparg_randomizations = {}

    # overriding to set definitions
    def _setupGraphDelegation(self):
        self.emitWithBlock = self.graph.emitWithBlock
        self.newBlock = self.graph.newBlock
        self.nextBlock = self.graph.nextBlock

    # Overriding emit call to fuzz certain opargs stored in names, varnames, consts
    # Will update to fuzz more types of opargs, and fuzz instructions as well
    def emit(self, opcode: str, oparg: object = 0) -> None:
        self.graph.maybeEmitSetLineno()

        if opcode != "SET_LINENO" and isinstance(oparg, pyassem.Block):
            if not self.graph.do_not_emit_bytecode:
                self.graph.current.addOutEdge(oparg)
                self.graph.current.emit(pyassem.Instruction(opcode, 0, 0, target=oparg))
            return

        ioparg = self.graph.convertArg(opcode, oparg)
        randomized_opcode = randomize_opcode(opcode)
        """
        # We can fuzz opcodes if 3 conditions are met
        # 1. randomized_opcode != opcode (certain opcodes are left unrandomized, such as branch instructions)
        # 2. we can safely replace the original oparg with a new one (for the new instruction)
             without the length of a tuple (i.e. co_names, co_varnames) hitting zero (or it will fail assertions)
        # 3. random chance based on INSTR_RANDOMIZATION_CHANCE
        """
        if (
            random.randint(1, 100) <= INSTR_RANDOMIZATION_CHANCE
            and randomized_opcode != opcode
            and can_replace_oparg(
                opcode,
                self.graph.consts,
                self.graph.names,
                self.graph.varnames,
                self.graph.closure,
            )
        ):
            # if we are fuzzing this opcode
            # create a new oparg corresponding to that opcode
            # and emit
            new_oparg = generate_oparg_for_randomized_opcode(
                opcode,
                randomized_opcode,
                oparg,
                self.graph.consts,
                self.graph.names,
                self.graph.varnames,
                self.graph.freevars,
                self.graph.cellvars,
            )
            # get new ioparg
            ioparg = self.graph.convertArg(randomized_opcode, new_oparg)
            self.graph.current.emit(
                pyassem.Instruction(randomized_opcode, new_oparg, ioparg)
            )
        else:
            # otherwise, just randomize the oparg and emit
            self.randomize_oparg(opcode, oparg, ioparg)

        if opcode == "SET_LINENO" and not self.graph.first_inst_lineno:
            self.graph.first_inst_lineno = ioparg

    # randomizes an existing oparg and emits an instruction with the randomized oparg and ioparg
    def randomize_oparg(self, opcode: str, oparg: object, ioparg: int) -> None:
        if not self.graph.do_not_emit_bytecode:
            # storing oparg to randomized version as a key value pair
            if oparg in self.oparg_randomizations:
                randomized_oparg = self.oparg_randomizations[oparg]
            else:
                randomized_oparg = randomize_variable(oparg)
                self.oparg_randomizations[oparg] = randomized_oparg

            if opcode in Fuzzer.INSTRS_WITH_OPARG_IN_NAMES:
                ioparg = replace_name_var(oparg, randomized_oparg, self.graph.names)
                self.graph.current.emit(
                    pyassem.Instruction(opcode, randomized_oparg, ioparg)
                )
            elif opcode in Fuzzer.INSTRS_WITH_OPARG_IN_VARNAMES:
                ioparg = replace_name_var(oparg, randomized_oparg, self.graph.varnames)
                self.graph.current.emit(
                    pyassem.Instruction(opcode, randomized_oparg, ioparg)
                )
            elif (
                opcode in Fuzzer.INSTRS_WITH_OPARG_IN_CONSTS
                # LOAD_CONST often has embedded code objects or a code generator as its oparg
                # If I randomize the oparg to a LOAD_CONST the code object generation could fail
                # Therefore it is not being randomized at the moment
                and opcode != "LOAD_CONST"
            ):
                ioparg = replace_const_var(
                    self.graph.get_const_key(oparg),
                    self.graph.get_const_key(randomized_oparg),
                    self.graph.consts,
                )
                self.graph.current.emit(
                    pyassem.Instruction(opcode, randomized_oparg, ioparg)
                )
            elif opcode in Fuzzer.INSTRS_WITH_OPARG_IN_CLOSURE:
                ioparg = replace_closure_var(
                    oparg,
                    randomized_oparg,
                    ioparg,
                    self.graph.freevars,
                    self.graph.cellvars,
                )
                self.graph.current.emit(
                    pyassem.Instruction(opcode, randomized_oparg, ioparg)
                )
            else:
                ioparg = generate_random_ioparg(opcode, ioparg)
                self.graph.current.emit(pyassem.Instruction(opcode, oparg, ioparg))

    INSTRS_WITH_OPARG_IN_CONSTS = {
        "LOAD_CONST",
        "LOAD_CLASS",
        "INVOKE_FUNCTION",
        "INVOKE_METHOD",
        "LOAD_FIELD",
        "STORE_FIELD",
        "CAST",
        "PRIMITIVE_BOX",
        "PRIMITIVE_UNBOX",
        "TP_ALLOC",
        "CHECK_ARGS",
        "BUILD_CHECKED_MAP",
        "BUILD_CHECKED_LIST",
        "PRIMITIVE_LOAD_CONST",
        "LOAD_LOCAL",
        "STORE_LOCAL",
        "REFINE_TYPE",
        "LOAD_METHOD_SUPER",
        "LOAD_ATTR_SUPER",
    }

    INSTRS_WITH_OPARG_IN_VARNAMES = {
        "LOAD_FAST",
        "STORE_FAST",
        "DELETE_FAST",
    }

    INSTRS_WITH_OPARG_IN_NAMES = {
        "LOAD_NAME",
        "LOAD_GLOBAL",
        "STORE_GLOBAL",
        "DELETE_GLOBAL",
        "STORE_NAME",
        "DELETE_NAME",
        "IMPORT_NAME",
        "IMPORT_FROM",
        "STORE_ATTR",
        "LOAD_ATTR",
        "DELETE_ATTR",
        "LOAD_METHOD",
    }

    INSTRS_WITH_OPARG_IN_CLOSURE = {
        "LOAD_DEREF",
        "STORE_DEREF",
        "DELETE_DEREF",
        "LOAD_CLASSDEREF",
        "LOAD_CLOSURE",
    }

    INSTRS_WITH_BRANCHES = {
        "FOR_ITER",
        "JUMP_ABSOLUTE",
        "JUMP_FORWARD",
        "JUMP_IF_FALSE_OR_POP",
        "JUMP_IF_NOT_EXC_MATCH",
        "JUMP_IF_TRUE_OR_POP",
        "POP_JUMP_IF_FALSE",
        "POP_JUMP_IF_TRUE",
        "RETURN_VALUE",
        "RAISE_VARARGS",
        "RERAISE",
        "JUMP_ABSOLUTE",
        "JUMP_FORWARD",
    }

    def opcodes_with_stack_effect(n, without=()):
        all_opcodes = {
            op for op, eff in opcode_cinder.opcode.stack_effects.items() if eff == n
        }
        for opcode in without:
            assert opcode in all_opcodes, f"Opcode {opcode} not found in list"
        result = all_opcodes - set(without)
        assert (
            len(result) > 1
        ), "Not enough opcodes in list to prevent unbounded recursion"
        return result

    INSTRS_WITH_STACK_EFFECT_0 = opcodes_with_stack_effect(0)

    INSTRS_WITH_STACK_EFFECT_0_SEQ = tuple(INSTRS_WITH_STACK_EFFECT_0)

    # WITH_EXCEPT_START expects 7 things on the stack as a precondition.
    INSTRS_WITH_STACK_EFFECT_1 = opcodes_with_stack_effect(1, {"WITH_EXCEPT_START"})

    INSTRS_WITH_STACK_EFFECT_1_SEQ = tuple(INSTRS_WITH_STACK_EFFECT_1)

    INSTRS_WITH_STACK_EFFECT_2 = opcodes_with_stack_effect(2)

    INSTRS_WITH_STACK_EFFECT_2_SEQ = tuple(INSTRS_WITH_STACK_EFFECT_2)

    INSTRS_WITH_STACK_EFFECT_NEG_1 = opcodes_with_stack_effect(-1)

    INSTRS_WITH_STACK_EFFECT_NEG_1_SEQ = tuple(INSTRS_WITH_STACK_EFFECT_NEG_1)

    INSTRS_WITH_STACK_EFFECT_NEG_2 = opcodes_with_stack_effect(
        -2,
    )

    INSTRS_WITH_STACK_EFFECT_NEG_2_SEQ = tuple(INSTRS_WITH_STACK_EFFECT_NEG_2)

    # RERAISE has some preconditions about the blockstack.
    INSTRS_WITH_STACK_EFFECT_NEG_3 = opcodes_with_stack_effect(-3, {"RERAISE"})

    INSTRS_WITH_STACK_EFFECT_NEG_3_SEQ = tuple(INSTRS_WITH_STACK_EFFECT_NEG_3)

    INSTRS_WITH_OPARG_AFFECTING_STACK = {
        op
        for op, eff in opcode_cinder.opcode.stack_effects.items()
        if not isinstance(eff, int)
    } - {
        # TODO(emacs): Figure out why BUILD_SLICE is excluded.
        "BUILD_SLICE",
        # TODO(emacs): Figure out why FOR_ITER is excluded.
        "FOR_ITER",
        # TODO(emacs): Figure out why FORMAT_VALUE is excluded.
        "FORMAT_VALUE",
        # TODO(emacs): Figure out why INVOKE_METHOD' is excluded.
        "INVOKE_METHOD",
        # TODO(emacs): Figure out why JUMP_IF_X_OR_POP group is excluded.
        "JUMP_IF_FALSE_OR_POP",
        "JUMP_IF_TRUE_OR_POP",
        # Exclude instructions that modify the blockstack.
        "SETUP_ASYNC_WITH",
        "SETUP_FINALLY",
        "SETUP_WITH",
    }
    assert (
        len(INSTRS_WITH_OPARG_AFFECTING_STACK) > 1
    ), "Not enough opcodes in list to prevent unbounded recursion"


class FuzzerReturnTypes(enum.Enum):
    SYNTAX_ERROR = 0
    FUZZER_CODEGEN_ERROR = 2
    ERROR_CAUGHT_BY_JIT = 3
    VERIFICATION_ERROR = 4
    SUCCESS = 5


class PyFlowGraphFuzzer(pyassem.PyFlowGraphCinder):
    # Overriding nextBlock from FlowGraph in pyassem
    # Modified to insert a new block based on CHANCE_TO_EMIT_BLOCK
    def nextBlock(self, block=None, label=""):
        if self.do_not_emit_bytecode:
            return

        if block is None:
            block = self.newBlock(label=label)

        self.current.addNext(block)
        self.startBlock(block)

        if random.randint(1, 100) <= CHANCE_TO_EMIT_BLOCK:
            self.nextBlock(
                generate_random_block(
                    self.consts, self.names, self.varnames, self.freevars
                )
            )


def fuzzer_compile(code_str: str) -> tuple:
    # wrap code in a wrapper function for jit compilation
    wrapped_code_str = "def wrapper_function():\n" + textwrap.indent(code_str, "  ")
    # compile code with the Fuzzer as its codegenerator
    try:
        code = compile(wrapped_code_str, "", "exec", compiler=Fuzzer)
    except SyntaxError:
        return (None, FuzzerReturnTypes.SYNTAX_ERROR)
    except (AssertionError, AttributeError, IndexError, KeyError, ValueError):
        # indicates an error during code generation
        # meaning the fuzzer has modified the code in a way which
        # does not allow the creation of a code object
        # ideally these types of errors are minimal
        return (None, FuzzerReturnTypes.FUZZER_CODEGEN_ERROR)
    # print code object to stdout so it is present in the output file
    print(code_object_to_string(code))
    # Run through the verifier
    try:
        Verifier.validate_code(code)
    except VerificationError:
        return (code, FuzzerReturnTypes.VERIFICATION_ERROR)

    # create a function object from the code object
    func = types.FunctionType(code.co_consts[0], {})

    # jit compile the function
    try:
        jit_compiled_function = cinderjit.force_compile(func)
    except RuntimeError:
        return (code, FuzzerReturnTypes.ERROR_CAUGHT_BY_JIT)

    return (code, FuzzerReturnTypes.SUCCESS)


# Can be used for debugging
def code_object_to_string(code: types.CodeType) -> str:
    res = ""
    res += "CODE OBJECT:\n"
    stack = [(code, 0)]
    while stack:
        code_obj, level = stack.pop()
        res += f"Code object at level {level}\n"
        res += f"Bytecode: {code_obj.co_code}\n"
        res += f"Formatted bytecode:\n"
        bytecode = code_obj.co_code
        i = 0
        while i < len(bytecode):
            if i % 2 == 0:
                res += f"{dis.opname[bytecode[i]]} : "
            else:
                res += f"{bytecode[i]}, "
            i += 1
        res += "\n"
        res += f"Consts: {code_obj.co_consts}\n"
        res += f"Names: {code_obj.co_names}\n"
        res += f"Varnames: {code_obj.co_varnames}\n"
        res += f"Cellvars: {code_obj.co_cellvars}\n"
        res += f"Freevars: {code_obj.co_freevars}\n"
        for i in code_obj.co_consts:
            if isinstance(i, types.CodeType):
                stack.append((i, level + 1))
    return res


def replace_closure_var(
    name: str,
    randomized_name: str,
    ioparg: int,
    freevars: pyassem.IndexedSet,
    cellvars: pyassem.IndexedSet,
) -> int:
    if name in freevars:
        del freevars.keys[name]
        return freevars.get_index(randomized_name)
    else:
        del cellvars.keys[name]
        return cellvars.get_index(randomized_name)


def replace_name_var(
    name: str, randomized_name: str, location: pyassem.IndexedSet
) -> int:
    if name in location:
        del location.keys[name]
    return location.get_index(randomized_name)


def replace_const_var(
    old_key: tuple,
    new_key: tuple,
    consts: dict,
) -> int:
    oparg_index = consts[old_key]
    del consts[old_key]
    consts[new_key] = oparg_index
    return oparg_index


def generate_random_ioparg(opcode: str, ioparg: int):
    if (
        opcode in Fuzzer.INSTRS_WITH_BRANCHES
        or opcode in Fuzzer.INSTRS_WITH_OPARG_AFFECTING_STACK
        or opcode in Fuzzer.INSTRS_WITH_OPARG_IN_CONSTS
    ):
        return ioparg
    elif opcode == "COMPARE_OP":
        return generate_random_integer(ioparg, 0, CMP_OP_LENGTH)
    return generate_random_integer(ioparg, OPARG_LOWER_BOUND, OPARG_UPPER_BOUND)


def randomize_variable(var: object) -> object:
    if isinstance(var, str):
        return generate_random_string(var, STR_LEN_LOWER_BOUND, STR_LEN_UPPER_BOUND)
    elif isinstance(var, int):
        return generate_random_integer(var, INT_LOWER_BOUND, INT_UPPER_BOUND)
    elif isinstance(var, tuple):
        return tuple(randomize_variable(i) for i in var)
    elif isinstance(var, frozenset):
        return frozenset(randomize_variable(i) for i in var)
    else:
        return var


def generate_random_string(original: str, lower: int, upper: int) -> str:
    random_str = original
    newlen = random.randint(lower, upper)
    # ensuring random str is not the same as original
    while random_str == original:
        random_str = "".join(
            random.choice(string.ascii_letters + string.digits + string.punctuation)
            for i in range(newlen)
        )
    return random_str


def generate_random_integer(original: int, lower: int, upper: int) -> int:
    random_int = original
    while random_int == original:
        random_int = random.randint(lower, upper)
    return random_int


# return random opcode with same stack effect as original
def randomize_opcode(opcode: str) -> str:
    if (
        opcode in Fuzzer.INSTRS_WITH_BRANCHES
        or opcode in Fuzzer.INSTRS_WITH_OPARG_AFFECTING_STACK
        # LOAD_CONST often has embedded code objects or a code generator as its oparg
        # If I replace LOAD_CONST instructions the code object generation can fail
        # Therefore it is not being replaced at the moment
        or opcode == "LOAD_CONST"
    ):
        return opcode

    stack_depth_sets = (
        Fuzzer.INSTRS_WITH_STACK_EFFECT_0,
        Fuzzer.INSTRS_WITH_STACK_EFFECT_1,
        Fuzzer.INSTRS_WITH_STACK_EFFECT_2,
        Fuzzer.INSTRS_WITH_STACK_EFFECT_NEG_1,
        Fuzzer.INSTRS_WITH_STACK_EFFECT_NEG_2,
        Fuzzer.INSTRS_WITH_STACK_EFFECT_NEG_3,
    )
    for stack_depth_set in stack_depth_sets:
        if opcode in stack_depth_set:
            return generate_random_opcode(opcode, stack_depth_set)
    return opcode


# generate random opcode given a set of possible options
def generate_random_opcode(opcode: str, options: set) -> str:
    new_op = random.choice(tuple(options))
    if new_op == opcode:
        return generate_random_opcode(opcode, options)
    return new_op


# ensures that consts, names, varnames, closure don't reach length 0
# when randomizing an opcode and replacing the oparg
# otherwise they will fail certain assertions and/or jit checks
def can_replace_oparg(
    opcode: str,
    consts: dict,
    names: pyassem.IndexedSet,
    varnames: pyassem.IndexedSet,
    closure: pyassem.IndexedSet,
):
    if opcode in Fuzzer.INSTRS_WITH_OPARG_IN_CONSTS:
        return len(consts) > 1
    if opcode in Fuzzer.INSTRS_WITH_OPARG_IN_NAMES:
        return len(names) > 1
    elif opcode in Fuzzer.INSTRS_WITH_OPARG_IN_VARNAMES:
        return len(varnames) > 1
    elif opcode in Fuzzer.INSTRS_WITH_OPARG_IN_CLOSURE:
        return len(closure) > 1
    else:
        return True


# generates a new oparg for a newly generated random opcode
# and removes the old oparg
def generate_oparg_for_randomized_opcode(
    original_opcode: str,
    randomized_opcode: str,
    oparg: object,
    consts: dict,
    names: pyassem.IndexedSet,
    varnames: pyassem.IndexedSet,
    freevars: pyassem.IndexedSet,
    cellvars: pyassem.IndexedSet,
) -> object:
    # delete the original oparg
    if original_opcode in Fuzzer.INSTRS_WITH_OPARG_IN_CONSTS:
        del consts[get_const_key(oparg)]
    elif original_opcode in Fuzzer.INSTRS_WITH_OPARG_IN_NAMES:
        del names.keys[oparg]
    elif original_opcode in Fuzzer.INSTRS_WITH_OPARG_IN_VARNAMES:
        del varnames.keys[oparg]
    elif original_opcode in Fuzzer.INSTRS_WITH_OPARG_IN_CLOSURE:
        if oparg in freevars:
            del freevars.keys[oparg]
        else:
            del cellvars.keys[oparg]

    # replace with a new oparg that corresponds with the new instruction
    if randomized_opcode in Fuzzer.INSTRS_WITH_OPARG_IN_CONSTS:
        new_oparg = randomize_variable(oparg)
        consts[get_const_key(new_oparg)] = len(consts)
        return new_oparg
    elif randomized_opcode in Fuzzer.INSTRS_WITH_OPARG_IN_NAMES:
        new_oparg = randomize_variable("")  # random string
        names.get_index(new_oparg)
        return new_oparg
    elif randomized_opcode in Fuzzer.INSTRS_WITH_OPARG_IN_VARNAMES:
        new_oparg = randomize_variable("")
        varnames.get_index(new_oparg)
        return new_oparg
    elif randomized_opcode in Fuzzer.INSTRS_WITH_OPARG_IN_CLOSURE:
        new_oparg = randomize_variable("")
        freevars.get_index(new_oparg)
        return new_oparg
    elif randomized_opcode == "GEN_START":
        # oparg must be < 3 according to an assert in ceval.c
        return generate_random_integer(-1, 0, 3)
    else:
        # if it isn't in one of the tuples, just return a random integer within oparg bounds
        return generate_random_integer(-1, OPARG_LOWER_BOUND, OPARG_UPPER_BOUND)


# get_const_key from pyassem
# modified to possess no state
def get_const_key(value: object):
    if isinstance(value, float):
        return type(value), value, pyassem.sign(value)
    elif isinstance(value, complex):
        return type(value), value, pyassem.sign(value.real), pyassem.sign(value.imag)
    elif isinstance(value, (tuple, frozenset)):
        return (
            type(value),
            value,
            tuple(get_const_key(const) for const in value),
        )
    return type(value), value


def generate_random_block(
    consts: dict,
    names: pyassem.IndexedSet,
    varnames: pyassem.IndexedSet,
    freevars: pyassem.IndexedSet,
) -> pyassem.Block:
    block = pyassem.Block("random")
    # possible stack depths that are available, and mapping to correct set of instruction
    stack_depth_to_instr_seq = {
        0: Fuzzer.INSTRS_WITH_STACK_EFFECT_0_SEQ,
        1: Fuzzer.INSTRS_WITH_STACK_EFFECT_1_SEQ,
        2: Fuzzer.INSTRS_WITH_STACK_EFFECT_2_SEQ,
        -1: Fuzzer.INSTRS_WITH_STACK_EFFECT_NEG_1_SEQ,
        -2: Fuzzer.INSTRS_WITH_STACK_EFFECT_NEG_2_SEQ,
        -3: Fuzzer.INSTRS_WITH_STACK_EFFECT_NEG_3_SEQ,
    }
    # generating all stack depth combinations of size BLOCK_SIZE that have net 0 stack effect
    combinations = generate_stackdepth_combinations([0, 1, 2, -1, -2, -3])
    # picking a random combination out of all that were generated
    random_combination = combinations[random.randint(0, len(combinations) - 1)]
    # sorting so that we don't pick a negative instruction first (preventing dip below 0 stack depth)
    random_combination.sort(reverse=True)
    # emit random instructions and corresponding opargs
    for i in random_combination:
        oparg, ioparg, instr = None, None, None
        instr = random.choice(stack_depth_to_instr_seq[i])
        if instr in Fuzzer.INSTRS_WITH_OPARG_IN_CONSTS:
            oparg = randomize_variable(0)
            ioparg = len(consts)
            consts[get_const_key(oparg)] = ioparg
        elif instr in Fuzzer.INSTRS_WITH_OPARG_IN_NAMES:
            oparg = randomize_variable("")
            ioparg = names.get_index(oparg)
        elif instr in Fuzzer.INSTRS_WITH_OPARG_IN_VARNAMES:
            oparg = randomize_variable("")
            ioparg = varnames.get_index(oparg)
        elif instr in Fuzzer.INSTRS_WITH_OPARG_IN_CLOSURE:
            oparg = randomize_variable("")
            ioparg = freevars.get_index(oparg)
        else:
            random_int_oparg = generate_random_integer(
                0, OPARG_LOWER_BOUND, OPARG_UPPER_BOUND
            )
            oparg = random_int_oparg
            ioparg = random_int_oparg
        block.emit(pyassem.Instruction(instr, oparg, ioparg))
    return block


# generates all possible stack depth subsets of length BLOCK_SIZE that add up to 0 (to maintain a net 0 stack effect)
def generate_stackdepth_combinations(possible_stack_depths):
    result_list = []
    _generate_stackdepth_combinations(
        possible_stack_depths, current_idx=0, current_list=[], result_list=result_list
    )
    return result_list


def _generate_stackdepth_combinations(
    possible_stack_depths: list,
    current_idx: int,
    current_list: list,
    result_list: list,
) -> None:
    if sum(current_list) == 0 and len(current_list) == GENERATED_BLOCK_SIZE:
        result_list.append(current_list)
        return
    if len(current_list) > GENERATED_BLOCK_SIZE:
        return
    if current_idx > len(possible_stack_depths) - 1:
        return
    # take current stack depth but do not move up an index to the next one
    _generate_stackdepth_combinations(
        possible_stack_depths,
        current_idx,
        current_list + [possible_stack_depths[current_idx]],
        result_list,
    )
    # take current stack depth and move up an index to next stack depth option
    _generate_stackdepth_combinations(
        possible_stack_depths,
        current_idx + 1,
        current_list + [possible_stack_depths[current_idx]],
        result_list,
    )
    # do not take current stack depth, just move up an index
    _generate_stackdepth_combinations(
        possible_stack_depths, current_idx + 1, current_list, result_list
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--codestr", help="code string to be passed into the fuzzer")
    args = parser.parse_args()
    if args.codestr:
        # printing return type to STDOUT so it can be seen in the output file
        print(fuzzer_compile(args.codestr)[1])

import argparse
import enum
import random
import string
import sys
import textwrap
import types
from compiler import compile, pyassem, pycodegen, symbols
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
STR_LEN_LOWER_BOUND: int = 2
INT_UPPER_BOUND = sys.maxsize
INT_LOWER_BOUND = -sys.maxsize - 1


class Fuzzer(pycodegen.CinderCodeGenerator):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
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

        if not self.graph.do_not_emit_bytecode:
            # storing oparg to randomized version as a key value pair
            if oparg in self.oparg_randomizations:
                randomized_oparg = self.oparg_randomizations[oparg]
            else:
                randomized_oparg = self.randomize_variable(oparg)
                self.oparg_randomizations[oparg] = randomized_oparg

            if opcode in Fuzzer.INSTRS_WITH_OPARG_IN_NAMES:
                ioparg = self.randomize_name(
                    opcode, oparg, randomized_oparg, self.graph.names
                )
                self.graph.current.emit(
                    pyassem.Instruction(opcode, randomized_oparg, ioparg)
                )
            elif opcode in Fuzzer.INSTRS_WITH_OPARG_IN_VARNAMES:
                ioparg = self.randomize_name(
                    opcode, oparg, randomized_oparg, self.graph.varnames
                )
                self.graph.current.emit(
                    pyassem.Instruction(opcode, randomized_oparg, ioparg)
                )
            elif (
                opcode in Fuzzer.INSTRS_WITH_OPARG_IN_CONSTS
                and oparg != randomized_oparg
            ):
                ioparg = self.randomize_const(opcode, oparg, randomized_oparg)
                self.graph.current.emit(
                    pyassem.Instruction(opcode, randomized_oparg, ioparg)
                )
            else:
                self.graph.current.emit(pyassem.Instruction(opcode, oparg, ioparg))

        if opcode == "SET_LINENO" and not self.graph.first_inst_lineno:
            self.graph.first_inst_lineno = ioparg

    # swaps a name with a randomized one, returns index of new name
    def randomize_name(
        self, opcode: str, name: str, randomized_name: str, location: pyassem.IndexedSet
    ) -> int:
        if name in location:
            del location.keys[name]
        location.get_index(randomized_name)
        return self.graph.convertArg(opcode, randomized_name)

    def randomize_const(
        self, opcode: str, oparg: object, randomized_oparg: object
    ) -> int:
        key = self.graph.get_const_key(oparg)
        oparg_index = self.graph.consts[key]
        del self.graph.consts[key]
        self.graph.consts[self.graph.get_const_key(randomized_oparg)] = oparg_index
        return oparg_index

    def randomize_variable(self, var: object) -> object:
        if isinstance(var, str):
            return self.generate_random_string(
                var, STR_LEN_LOWER_BOUND, STR_LEN_UPPER_BOUND
            )
        elif isinstance(var, int):
            return self.generate_random_integer(var, INT_LOWER_BOUND, INT_UPPER_BOUND)
        elif isinstance(var, tuple):
            return tuple(self.randomize_variable(i) for i in var)
        else:
            return var

    # generates random string within bounds
    def generate_random_string(self, original: str, lower: int, upper: int) -> str:
        newlen = random.randint(lower, upper)
        random_str = "".join(
            random.choice(string.ascii_letters + string.digits + string.punctuation)
            for i in range(newlen)
        )
        # ensuring random str is not the same as original
        if random_str == original:
            return self.generate_random_string(original, lower, upper)
        return random_str

    def generate_random_integer(self, original: int, lower: int, upper: int) -> int:
        random_int = random.randint(lower, upper)
        if random_int == original:
            return self.generate_random_integer(original, lower, upper)
        return random_int

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
        "FUNC_CREDENTIAL",
        "READONLY_OPERATION",
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

    DEREF_INSTRS = {
        "LOAD_DEREF",
        "STORE_DEREF",
        "DELETE_DEREF",
        "LOAD_CLASSDEREF",
    }


class FuzzerReturnTypes(enum.Enum):
    SYNTAX_ERROR = 0
    ERROR_CAUGHT_BY_JIT = 1
    VERIFICATION_ERROR = 2
    SUCCESS = 3


def fuzzer_compile(code_str: str) -> tuple:
    # wrapping all code in a function for JIT compilation
    # since the cinderjit "force_compile" method requires a function object
    wrapped_code_str = "def wrapper_function():\n" + textwrap.indent(code_str, "  ")
    # if the code_str is already a function, keep it as is
    if len(code_str) > 3 and code_str[:3] == "def":
        wrapped_code_str = code_str
    try:
        code = compile(wrapped_code_str, "", "exec", compiler=Fuzzer)
        # validating the code object
        Verifier.validate_code(code)
        # the original code is wrapped in a function, extracting it for jit compilation
        func = types.FunctionType(code.co_consts[0], globals())
        if cinderjit:
            try:
                jit_compiled_function = cinderjit.force_compile(func)
            except RuntimeError:
                return (code, FuzzerReturnTypes.ERROR_CAUGHT_BY_JIT)
    except SyntaxError:
        return (None, FuzzerReturnTypes.SYNTAX_ERROR)
    except VerificationError:
        return (code, FuzzerReturnTypes.VERIFICATION_ERROR)
    return (code, FuzzerReturnTypes.SUCCESS)


# Can be used for debugging
def print_code_object(code: types.CodeType) -> None:
    stack = [(code, 0)]
    while stack:
        code_obj, level = stack.pop()
        print(f"Code object at level {level}")
        print(f"Bytecode: {code_obj.co_code}")
        print(f"Consts: {code_obj.co_consts}")
        print(f"Names: {code_obj.co_names}")
        print(f"Varnames: {code_obj.co_varnames}")
        print(f"Cellvars: {code_obj.co_cellvars}")
        print(f"Freevars: {code_obj.co_freevars}\n")
        for i in code_obj.co_consts:
            if isinstance(i, types.CodeType):
                stack.append((i, level + 1))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--codestr", help="code string to be passed into the fuzzer")
    args = parser.parse_args()
    if args.codestr:
        fuzzer_compile(args.codestr)

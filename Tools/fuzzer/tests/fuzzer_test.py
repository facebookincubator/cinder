import os
import sys
import unittest
from compiler import compile, pyassem
from inspect import cleandoc

sys.path.append(os.path.join(sys.path[0], "..", "fuzzer"))
import fuzzer
from fuzzer import Fuzzer, FuzzerReturnTypes

try:
    import cinderjit
except ImportError:
    cinderjit = None


class FuzzerSyntaxTest(unittest.TestCase):
    def test_code_str_invalid_syntax_returns_SYNTAX_ERROR(self):
        codestr = cleandoc(
            """
        def f():
            return 4+
        """
        )
        self.assertEqual(
            fuzzer.fuzzer_compile(codestr)[1], FuzzerReturnTypes.SYNTAX_ERROR
        )


class FuzzerOpargsFuzzingTest(unittest.TestCase):
    def test_randomized_string_is_different_from_original(self):
        original_string = "hello"
        self.assertNotEqual(original_string, fuzzer.randomize_variable(original_string))

    def test_randomized_int_is_different_from_original(self):
        original_int = 5
        self.assertNotEqual(original_int, fuzzer.randomize_variable(original_int))

    def test_randomized_tuple_is_different_from_original(self):
        original_tuple = ("hello", 6)
        randomized_tuple = fuzzer.randomize_variable(original_tuple)
        # checking that every element of the tuple is fuzzed
        self.assertNotEqual(original_tuple, randomized_tuple)
        for i in range(len(original_tuple)):
            self.assertNotEqual(original_tuple[i], randomized_tuple[i])

    def test_randomized_frozen_set_is_different_from_original(self):
        original_frozenset = frozenset(["hello", 6])
        randomized_frozenset = fuzzer.randomize_variable(original_frozenset)
        self.assertNotEqual(original_frozenset, randomized_frozenset)
        # checking that they have no elements in common
        self.assertEqual(
            original_frozenset.intersection(randomized_frozenset), frozenset()
        )

    def test_replace_name_var_replaces_str(self):
        name = "foo"
        randomized_name = fuzzer.randomize_variable(name)
        names = pyassem.IndexedSet(["hello", name])
        idx = fuzzer.replace_name_var("foo", randomized_name, names)
        self.assertEqual(names.index(randomized_name), 1)
        self.assertEqual(idx, 1)

    def test_replace_const_var_replaces_const(self):
        const = "HELLO"
        consts = {(str, const): 0}
        randomized_const = fuzzer.randomize_variable(const)
        idx = fuzzer.replace_const_var((str, const), (str, randomized_const), consts)
        self.assertEqual(consts, {(str, randomized_const): 0})
        self.assertEqual(idx, 0)

    def test_replace_closure_var_replaces_str(self):
        var = "foo"
        randomized_var = fuzzer.randomize_variable(var)
        freevars = pyassem.IndexedSet(["hello", var])
        cellvars = pyassem.IndexedSet()
        idx = fuzzer.replace_closure_var(var, randomized_var, 1, freevars, cellvars)
        self.assertEqual(freevars.index(randomized_var), 1)
        self.assertEqual(idx, 1)

    def test_int_opargs_that_impact_stack_not_changed(self):
        # oparg to BUILD_LIST affects stack directly, and thus should not be fuzzed
        opcode = "BUILD_LIST"
        ioparg = 5
        self.assertEqual(ioparg, fuzzer.generate_random_ioparg(opcode, ioparg))

    def test_int_opargs_changed(self):
        # oparg to SET_ADD does not impact stack at all, and should be fuzzed
        opcode = "SET_ADD"
        ioparg = 5
        self.assertNotEqual(ioparg, fuzzer.generate_random_ioparg(opcode, ioparg))

if __name__ == "__main__":
    unittest.main()

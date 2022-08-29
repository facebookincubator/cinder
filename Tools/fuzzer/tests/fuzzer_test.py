import os
import sys
import unittest
from compiler import compile, opcode_cinder, pyassem
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

    def test_COMPARE_OP_ioparg_changed_and_within_bounds(self):
        opcode = "COMPARE_OP"
        ioparg = 5
        self.assertNotEqual(ioparg, fuzzer.generate_random_ioparg(opcode, ioparg))
        self.assertGreaterEqual(ioparg, 0)
        self.assertLess(ioparg, len(opcode_cinder.opcode.CMP_OP))


class FuzzerInstrFuzzingTest(unittest.TestCase):
    def test_randomized_opcode_is_different_from_original_and_maintains_stack_for_opcode_with_stack_effect_0(
        self,
    ):
        original_opcode = "ROT_TWO"
        randomized_opcode = fuzzer.randomize_opcode(original_opcode)
        # check opcodes are different
        self.assertNotEqual(original_opcode, randomized_opcode)
        # check stack effect is same
        self.assertEqual(
            opcode_cinder.opcode.stack_effects.get(original_opcode),
            opcode_cinder.opcode.stack_effects.get(randomized_opcode),
        )

    def test_randomized_opcode_is_different_from_original_and_maintains_stack_for_opcode_with_stack_effect_1(
        self,
    ):
        original_opcode = "DUP_TOP"
        randomized_opcode = fuzzer.randomize_opcode(original_opcode)
        self.assertNotEqual(original_opcode, randomized_opcode)
        self.assertEqual(
            opcode_cinder.opcode.stack_effects.get(original_opcode),
            opcode_cinder.opcode.stack_effects.get(randomized_opcode),
        )

    def test_randomized_opcode_is_different_from_original_and_maintains_stack_for_opcode_with_stack_effect_2(
        self,
    ):
        original_opcode = "DUP_TOP_TWO"
        randomized_opcode = fuzzer.randomize_opcode(original_opcode)
        self.assertNotEqual(original_opcode, randomized_opcode)
        self.assertEqual(
            opcode_cinder.opcode.stack_effects.get(original_opcode),
            opcode_cinder.opcode.stack_effects.get(randomized_opcode),
        )

    def test_randomized_opcode_is_different_from_original_and_maintains_stack_for_opcode_with_stack_effect_negative_1(
        self,
    ):
        original_opcode = "POP_TOP"
        randomized_opcode = fuzzer.randomize_opcode(original_opcode)
        self.assertNotEqual(original_opcode, randomized_opcode)
        self.assertEqual(
            opcode_cinder.opcode.stack_effects.get(original_opcode),
            opcode_cinder.opcode.stack_effects.get(randomized_opcode),
        )

    def test_randomized_opcode_is_different_from_original_and_maintains_stack_for_opcode_with_stack_effect_negative_2(
        self,
    ):
        original_opcode = "DELETE_SUBSCR"
        randomized_opcode = fuzzer.randomize_opcode(original_opcode)
        self.assertNotEqual(original_opcode, randomized_opcode)
        self.assertEqual(
            opcode_cinder.opcode.stack_effects.get(original_opcode),
            opcode_cinder.opcode.stack_effects.get(randomized_opcode),
        )

    def test_randomized_opcode_is_different_from_original_and_maintains_stack_for_opcode_with_stack_effect_negative_3(
        self,
    ):
        original_opcode = "STORE_SUBSCR"
        randomized_opcode = fuzzer.randomize_opcode(original_opcode)
        self.assertNotEqual(original_opcode, randomized_opcode)
        self.assertEqual(
            opcode_cinder.opcode.stack_effects[original_opcode],
            opcode_cinder.opcode.stack_effects[randomized_opcode],
        )

    def test_branch_opcode_not_randomized(self):
        original_opcode = "JUMP_ABSOLUTE"
        randomized_opcode = fuzzer.randomize_opcode(original_opcode)
        self.assertEqual(original_opcode, randomized_opcode)

    def test_opcode_with_oparg_affecting_stack_not_randomized(self):
        original_opcode = "BUILD_LIST"
        randomized_opcode = fuzzer.randomize_opcode(original_opcode)
        self.assertEqual(original_opcode, randomized_opcode)

    def test_load_const_not_randomized(self):
        original_opcode = "LOAD_CONST"
        randomized_opcode = fuzzer.randomize_opcode(original_opcode)
        self.assertEqual(original_opcode, randomized_opcode)

    def test_can_replace_oparg_returns_false_when_tuple_size_less_or_equal_to_1(self):
        # all tuples have size <= 1, so can_replace_oparg should return False
        consts = {(str, "hello"): 0}
        names = pyassem.IndexedSet(("hello",))
        varnames = pyassem.IndexedSet(("hello",))
        closure = pyassem.IndexedSet(("hello",))

        self.assertFalse(
            # oparg stored in consts
            fuzzer.can_replace_oparg(
                "BUILD_CHECKED_LIST", consts, names, varnames, closure
            )
        )
        self.assertFalse(
            # oparg stored in names
            fuzzer.can_replace_oparg("LOAD_NAME", consts, names, varnames, closure)
        )
        self.assertFalse(
            # oparg stored in varnames
            fuzzer.can_replace_oparg("LOAD_FAST", consts, names, varnames, closure)
        )
        self.assertFalse(
            # oparg stored in closure
            fuzzer.can_replace_oparg("LOAD_CLOSURE", consts, names, varnames, closure)
        )

    def test_can_replace_oparg_returns_true_when_tuple_size_greater_than_1_or_oparg_not_in_tuples(
        self,
    ):
        # all tuples have size > 1, so can_replace_oparg should return True
        consts = {(str, "hello"): 0, (str, "world"): 1}
        names = pyassem.IndexedSet(("hello", "world"))
        varnames = pyassem.IndexedSet(("hello", "world"))
        closure = pyassem.IndexedSet(("hello", "world"))

        self.assertTrue(
            fuzzer.can_replace_oparg(
                "BUILD_CHECKED_LIST", consts, names, varnames, closure
            )
        )
        self.assertTrue(
            fuzzer.can_replace_oparg("LOAD_NAME", consts, names, varnames, closure)
        )
        self.assertTrue(
            fuzzer.can_replace_oparg("LOAD_FAST", consts, names, varnames, closure)
        )
        self.assertTrue(
            fuzzer.can_replace_oparg("LOAD_CLOSURE", consts, names, varnames, closure)
        )
        self.assertTrue(
            fuzzer.can_replace_oparg("ROT_TWO", consts, names, varnames, closure)
        )

    def test_generate_oparg_for_randomized_opcode_removes_original_oparg_and_creates_new_one(
        self,
    ):
        original_opcode = "LOAD_NAME"
        original_oparg = "hi"
        new_opcode = "LOAD_FAST"
        freevars = pyassem.IndexedSet()
        cellvars = pyassem.IndexedSet()
        names = pyassem.IndexedSet(["hello", original_oparg])
        varnames = pyassem.IndexedSet()
        consts = {}

        fuzzer.generate_oparg_for_randomized_opcode(
            original_opcode,
            new_opcode,
            original_oparg,
            consts,
            names,
            varnames,
            freevars,
            cellvars,
        )
        # check that original oparg for LOAD_NAME ("hi") is removed from names
        self.assertNotIn(original_oparg, names)
        # check that a new oparg for LOAD_FAST is inserted into varnames
        self.assertEqual(len(varnames), 1)


class FuzzerBlockInsertionTest(unittest.TestCase):
    def test_generate_stackdepth_combinations_generates_combinations_with_net_0_stack_effect(
        self,
    ):
        possible_stack_depths = [0, 1, 2, -1, -2, -3]
        result = []
        result = fuzzer.generate_stackdepth_combinations(possible_stack_depths)
        # asserting the sum of all stack depths for each combination is zero
        for i in result:
            self.assertEqual(
                sum(i),
                0,
                f"net stack depth is {sum(i)} instead of zero for combination {i}",
            )

    def test_generate_stackdepth_combination_generates_combinations_of_correct_size(
        self,
    ):
        possible_stack_depths = [0, 1, 2, -1, -2, -3]
        result = fuzzer.generate_stackdepth_combinations(possible_stack_depths)
        # asserting the length of each combination is equal to GENERATED_BLOCK_SIZE
        for i in result:
            self.assertEqual(
                len(i),
                fuzzer.GENERATED_BLOCK_SIZE,
                f"combination size is {len(i)} instead of {fuzzer.GENERATED_BLOCK_SIZE} for combination {i}",
            )

    def test_random_blocks_have_zero_stack_effect(self):
        consts = {}
        names = pyassem.IndexedSet()
        varnames = pyassem.IndexedSet()
        freevars = pyassem.IndexedSet()
        block = fuzzer.generate_random_block(consts, names, varnames, freevars)
        block_stack_effect = sum(
            [opcode_cinder.opcode.stack_effects[i.opname] for i in block.insts]
        )
        err_message = f"Block stack effect is {block_stack_effect}, should be zero\n Each instruction with individual stack effect listed below:\n"
        for i in block.insts:
            err_message += f"{i.opname} with stack effect {opcode_cinder.opcode.stack_effects.get(i.opname)}\n"
        self.assertEqual(block_stack_effect, 0, err_message)

    def test_random_blocks_insert_generated_opargs_into_tuples(self):
        consts = {}
        names = pyassem.IndexedSet()
        varnames = pyassem.IndexedSet()
        freevars = pyassem.IndexedSet()
        block = fuzzer.generate_random_block(consts, names, varnames, freevars)
        for i in block.insts:
            name = i.opname
            arg = i.oparg
            if name in fuzzer.Fuzzer.INSTRS_WITH_OPARG_IN_CONSTS:
                self.assertIn(fuzzer.get_const_key(arg), consts)
            elif name in fuzzer.Fuzzer.INSTRS_WITH_OPARG_IN_NAMES:
                self.assertIn(arg, names)
            elif name in fuzzer.Fuzzer.INSTRS_WITH_OPARG_IN_VARNAMES:
                self.assertIn(arg, varnames)
            elif name in fuzzer.Fuzzer.INSTRS_WITH_OPARG_IN_CLOSURE:
                self.assertIn(arg, freevars)


if __name__ == "__main__":
    unittest.main()

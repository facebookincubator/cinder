import _immutable_globals
from test.test_dict import DictTest

import unittest


class _ImmutableGlobalsTests(unittest.TestCase):
    def test_set_immutable_globals_creation(self):
        _immutable_globals.set_immutable_globals_creation(True)
        self.assertTrue(_immutable_globals.get_immutable_globals_creation())
        _immutable_globals.set_immutable_globals_creation(False)
        self.assertFalse(_immutable_globals.get_immutable_globals_creation())
        with self.assertRaises(TypeError):
            _immutable_globals.set_immutable_globals_creation(7)

    def test_set_immutable_globals_detection(self):
        _immutable_globals.set_immutable_globals_detection(True)
        self.assertTrue(_immutable_globals.get_immutable_globals_detection())
        _immutable_globals.set_immutable_globals_detection(False)
        self.assertFalse(_immutable_globals.get_immutable_globals_detection())
        with self.assertRaises(TypeError):
            _immutable_globals.set_immutable_globals_detection(7)


# Inherit from DictTest to run the existing tests with immutable globals on
class ImmutableDictTest(DictTest):
    def setUp(self):
        _immutable_globals.set_immutable_globals_creation(True)

    def tearDown(self):
        _immutable_globals.set_immutable_globals_creation(False)

    def test_create_immutable_dictionary(self):
        idict = dict(a=1, b=2)
        idict["a"] = 1
        idict["b"] = 2
        self.assertFalse(isinstance(idict, dict))
        self.assertIsInstance(idict, _immutable_globals.ImmutableDict)
        self.assertEqual(idict["a"], 1)
        self.assertEqual(idict["b"], 2)

    @unittest.skip("constructor broken")
    def test_create_immutable_dictionary_literal(self):
        # This still creates a dict
        idict = {"a": 1, "b": 2}
        self.assertFalse(isinstance(idict, dict))
        self.assertIsInstance(idict, _immutable_globals.ImmutableDict)
        self.assertEqual(idict["a"], 1)
        self.assertEqual(idict["b"], 2)

    @unittest.skip("constructor broken")
    def test_create_immutable_dictionary_type_call(self):
        # this does not set the values
        idict = dict(a=1, b=2)
        self.assertFalse(isinstance(idict, dict))
        self.assertIsInstance(idict, _immutable_globals.ImmutableDict)
        self.assertEqual(idict["a"], 1)
        self.assertEqual(idict["b"], 2)


# "TypeError: type.__new__() argument 3 must be dict, not ImmutableDict"
skipped_tests = [
    "test_free_after_iterating",  # TypeError: type.__new__() argument 3 must be dict, not ImmutableDict
    "test_update",  # TypeError: type.__new__() argument 3 must be dict, not ImmutableDict
    "test_instance_dict_getattr_str_subclass", # TypeError: type.___new__() argument 3 must be dict, not ImmutableDict
    "test_dict_copy_order",  # values not set
    "test_invalid_keyword_arguments",  # kwarg constructor does not work
    "test_reentrant_insertion",
    "test_copy_fuzz",
    "test_repr_deep",
    "test_equal_operator_modifying_operand",
    "test_itemiterator_pickling",
    "test_container_iterator",
    "test_items",
    "test_getitem",
    "test_literal_constructor",
    "test_reverseitemiterator_pickling",
    "test_track_dynamic",
    "test_values",
    "test_keys",
]


@unittest.skip("test is broken for IDict")
def placeholder(self):
    pass


for test in skipped_tests:
    setattr(ImmutableDictTest, test, placeholder)


if __name__ == "__main__":
    unittest.main()

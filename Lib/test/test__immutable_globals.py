import _immutable_globals

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


class ImmutableDictTest(unittest.TestCase):
    def test_create_immutable_dictionary(self):
        idict = _immutable_globals.ImmutableDict(a=1, b=2)
        self.assertFalse(isinstance(idict, dict))
        self.assertEqual(idict["a"], 1)
        self.assertEqual(idict["b"], 2)


if __name__ == '__main__':
    unittest.main()

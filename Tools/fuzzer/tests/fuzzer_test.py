import os
import sys
import unittest
from inspect import cleandoc

sys.path.append(os.path.join(sys.path[0], "..", "fuzzer"))
import fuzzer
from fuzzer import FuzzerReturnTypes

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

    def test_code_str_valid_syntax_returns_SUCCESS(self):
        codestr = cleandoc(
            """
        def f():
            return 4+5
        """
        )
        self.assertEqual(fuzzer.fuzzer_compile(codestr)[1], FuzzerReturnTypes.SUCCESS)


class FuzzerNameFuzzingTest(unittest.TestCase):
    def test_function_name_is_changed(self):
        codestr = cleandoc(
            """
        def f():
            return 4+5
        """
        )
        code_obj, fuzzer_return_type = fuzzer.fuzzer_compile(codestr)
        self.assertEqual(fuzzer_return_type, FuzzerReturnTypes.SUCCESS)
        self.assertNotEqual(code_obj.co_names[0], "f")

    def test_variable_name_is_changed(self):
        codestr = cleandoc(
            """
        x = 6
        """
        )
        code_obj, fuzzer_return_type = fuzzer.fuzzer_compile(codestr)
        self.assertEqual(fuzzer_return_type, FuzzerReturnTypes.SUCCESS)
        self.assertNotEqual(code_obj.co_consts[0].co_varnames[0], "x")

    def test_embedded_function_name_is_changed(self):
        codestr = cleandoc(
            """
            def foo():
                x = 6
                def f():
                    return 4+5
        """
        )
        code_obj, fuzzer_return_type = fuzzer.fuzzer_compile(codestr)
        self.assertEqual(fuzzer_return_type, FuzzerReturnTypes.SUCCESS)
        self.assertNotEqual(code_obj.co_consts[0].co_varnames[1], "f")

    def test_embedded_variable_name_is_changed(self):
        codestr = cleandoc(
            """
            def foo():
                x = 6
        """
        )
        code_obj, fuzzer_return_type = fuzzer.fuzzer_compile(codestr)
        self.assertEqual(fuzzer_return_type, FuzzerReturnTypes.SUCCESS)
        self.assertNotEqual(code_obj.co_consts[0].co_varnames[0], "x")


class FuzzerConstFuzzingTest(unittest.TestCase):
    def test_const_int_is_changed(self):
        codestr = cleandoc(
            """
            x = 6
        """
        )
        code_obj, fuzzer_return_type = fuzzer.fuzzer_compile(codestr)
        self.assertEqual(fuzzer_return_type, FuzzerReturnTypes.SUCCESS)
        self.assertNotEqual(code_obj.co_consts[0].co_consts[1], 6)

    def test_const_str_is_changed(self):
        codestr = cleandoc(
            """
            x = "hello"
        """
        )
        code_obj, fuzzer_return_type = fuzzer.fuzzer_compile(codestr)
        self.assertEqual(fuzzer_return_type, FuzzerReturnTypes.SUCCESS)
        self.assertNotEqual(code_obj.co_consts[0].co_consts[1], "hello")

    def test_const_tuple_is_changed(self):
        codestr = cleandoc(
            """
            x = ("hello", 6)
        """
        )
        code_obj, fuzzer_return_type = fuzzer.fuzzer_compile(codestr)
        self.assertEqual(fuzzer_return_type, FuzzerReturnTypes.SUCCESS)
        self.assertNotEqual(code_obj.co_consts[0].co_consts[1], ("hello", 6))


if __name__ == "__main__":
    unittest.main()

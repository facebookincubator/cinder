import os
import re
import sys
import unittest
from inspect import cleandoc
from tempfile import NamedTemporaryFile

from cinderx.compiler import compile

sys.path.append(os.path.join(sys.path[0], "..", "fuzzer"))
from executor import extract_functions_from_file


class ExecutorExtractFunctionFromFileTest(unittest.TestCase):
    test_string = cleandoc(
        """
        def foo():
            x = 7
            return x

        class Bar:
            def xyz():
                a = 5
                a += 2
                return a
        """
    )

    def test_correct_number_of_functions_extracted(self):
        with NamedTemporaryFile(mode=("w+")) as temp_file:
            temp_file.write(self.test_string)
            temp_file.flush()
            funcs = extract_functions_from_file(temp_file.name)
            self.assertEqual(len(funcs), 2)

    def test_extracted_strings_are_all_compilable_functions(self):
        with NamedTemporaryFile(mode=("w+")) as temp_file:
            temp_file.write(self.test_string)
            temp_file.flush()
            funcs = extract_functions_from_file(temp_file.name)
            for i in funcs:
                self.assertEqual(i.strip()[:3], "def")
                code = compile(i, "", "exec")

    def test_function_indentation_level_is_consistent(self):
        with NamedTemporaryFile(mode=("w+")) as temp_file:
            temp_file.write(self.test_string)
            temp_file.flush()
            funcs = extract_functions_from_file(temp_file.name)
            for i in funcs:
                indentation = len(re.findall("^ *", i)[0])
                self.assertEqual(indentation, 0)


if __name__ == "__main__":
    unittest.main()

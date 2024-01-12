"""
Allow running side-by-side compiler tests against arbitrary external codebase.

Set EXT_COMPILE_TEST_PATH env var to the directory containing Python code source
files you want to do a side-by-side compile test on. Optionally set
EXT_COMPILE_TEST_IGNORE to a colon-separated list of ignore patterns (string-contains
matched against the file paths.) Then run this file using e.g.
`./python -m unittest test.test_compiler.test_sbs_external`.

"""
import ast
import dis
import os
import unittest
from compiler.dis_stable import Disassembler
from compiler.pycodegen import compile as py_compile
from io import StringIO
from os import path
from tokenize import detect_encoding

from .common import glob_test


IGNORE_PATTERNS = list(
    filter(lambda p: p, os.environ.get("EXT_COMPILE_TEST_IGNORE", "").split(":"))
)


class SbsCompileTests(unittest.TestCase):
    pass


# Add a test case for each external file to SbsCompileTests.
def add_test(modname, fname):
    for p in IGNORE_PATTERNS:
        if p in fname:
            return

    def test_external(self):
        with open(fname, "rb") as inp:
            encoding, _lines = detect_encoding(inp.readline)
            code = b"".join(_lines + inp.readlines()).decode(encoding)
            node = ast.parse(code, modname, "exec")
            node.filename = modname

            orig = compile(node, modname, "exec")
            origdump = StringIO()
            Disassembler().dump_code(orig, origdump)

            codeobj = py_compile(node, modname, "exec")
            newdump = StringIO()
            Disassembler().dump_code(codeobj, newdump)

            try:
                self.assertEqual(
                    origdump.getvalue().split("\n"), newdump.getvalue().split("\n")
                )
            except AssertionError:
                with open("c_compiler_output.txt", "w") as f:
                    f.write(origdump.getvalue())
                with open("py_compiler_output.txt", "w") as f:
                    f.write(newdump.getvalue())
                raise

    name = "test_" + modname.replace("/", "_")[:-3]
    test_external.__name__ = name
    setattr(SbsCompileTests, test_external.__name__, test_external)


libpath = os.environ.get("EXT_COMPILE_TEST_PATH", None)
if libpath and path.exists(libpath):
    glob_test(libpath, "**/*.py", add_test)

if __name__ == "__main__":
    unittest.main()

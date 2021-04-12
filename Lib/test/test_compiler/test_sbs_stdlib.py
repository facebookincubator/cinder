import ast
import dis
import glob
import re
import sys
from compiler.pycodegen import compile as py_compile
from io import StringIO, TextIOWrapper
from os import path
from tokenize import detect_encoding
from unittest import TestCase

from .common import get_repo_root, glob_test
from .dis_stable import Disassembler


IGNORE_PATTERNS = (
    # Not a valid Python3 syntax
    "lib2to3/tests/data",
    "test/badsyntax_",
    "test/bad_coding",
    # These are syntax errors in Py3.8, so disable them
    "test/test_compiler/sbs_code_tests/3.6/58_yield_from_gen_comp.py",
    "test/test_compiler/sbs_code_tests/3.6/58_yield_gen_comp.py",
    # run separately via test_corpus.py
    "test/test_compiler/testcorpus",
)

if sys.version_info < (3, 8):
    IGNORE_PATTERNS += ("test/test_compiler/sbs_code_tests/3.8",)


class SbsCompileTests(TestCase):
    pass


# Add a test case for each standard library file to SbsCompileTests.  Individual
# tests can be run with:
#  python -m test.test_compiler SbsCompileTests.test_Lib_test_test_unary
def add_test(modname, fname):
    assert fname.startswith(libpath + "/")
    for p in IGNORE_PATTERNS:
        if p in fname:
            return

    modname = path.relpath(fname, REPO_ROOT)

    def test_stdlib(self):
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

    test_stdlib.__name__ = "test_stdlib_" + modname.replace("/", "_")[:-3]
    setattr(SbsCompileTests, test_stdlib.__name__, test_stdlib)


REPO_ROOT = path.join(path.dirname(__file__), "..", "..", "..")
libpath = path.join(REPO_ROOT, "Lib")
if path.exists(libpath):
    glob_test(libpath, "**/*.py", add_test)
else:
    libpath = LIB_PATH = path.dirname(dis.__file__)
    glob_test(LIB_PATH, "**/*.py", add_test)
    IGNORE_PATTERNS = tuple(
        pattern.replace("test/test_compiler/", "test_compiler/")
        for pattern in IGNORE_PATTERNS
    )

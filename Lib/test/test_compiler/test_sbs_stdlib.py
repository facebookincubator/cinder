import ast
import dis
import glob
import re
import sys
from compiler.dis_stable import Disassembler
from compiler.pycodegen import compile as py_compile
from io import StringIO, TextIOWrapper
from os import path
from tokenize import detect_encoding
from unittest import TestCase

from .common import get_repo_root, glob_test


# This must match the same global in runtest.py. Unfortuantely there isn't
# a place to put this which can be imported by both users without either
# disrupting tests with unexpected imports, or being unavailable in some of our
# CI setups.
N_SBS_TEST_CLASSES = 10

IGNORE_PATTERNS = (
    # Not a valid Python3 syntax
    "lib2to3/tests/data",
    "test/badsyntax_",
    "test/bad_coding",
    # run separately via test_corpus.py
    "test/test_compiler/testcorpus",
    # Burn-down for full Python 3.10 compatibility
    "test/test_cinderjit.py",
    "test/test_grammar.py",
    "test/test_aifc.py",
    "test/test_tarfile.py",
    "test/test_socket.py",
    "test/test_compile.py",
    "test/test_patma.py",
    "test/test_sunau.py",
    "tempfile.py",
    "test/test_doctest.py",
    "lib2to3/pytree.py",
    "subprocess.py",
    "test/test_sys_settrace.py",
    "test/test_coroutines.py",
    "test/test_json/__init__.py",
    "nntplib.py",
    "test/test_functools.py",
    "tarfile.py",
    "test/test_wave.py",
    "test/test_asyncio/test_tasks.py",
    "test/test_nntplib.py",
    "test/test_poplib.py",
    "test/test_urllib.py",
)

SbsCompileTests = []

for i in range(N_SBS_TEST_CLASSES):
    class_name = f"SbsCompileTests{i}"
    new_class = type(class_name, (TestCase,), {})
    new_class.maxDiff = None
    SbsCompileTests.append(new_class)
    globals()[class_name] = new_class


# Add a test case for each standard library file to SbsCompileTests.  Individual
# tests can be run with:
#  python -m test.test_compiler SbsCompileTestsN.test_Lib_test_test_unary
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

    name = "test_stdlib_" + modname.replace("/", "_")[:-3]
    test_stdlib.__name__ = name
    n = hash(name) % N_SBS_TEST_CLASSES
    setattr(SbsCompileTests[n], test_stdlib.__name__, test_stdlib)


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

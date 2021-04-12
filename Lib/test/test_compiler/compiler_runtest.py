#
# Helper script for testsuite - generally, run a file thru compiler and
# disassemble using dis_stable.
#
import ast
import re
import sys
from compiler.pycodegen import compile as py_compile

import dis_stable


# https://www.python.org/dev/peps/pep-0263/
coding_re = re.compile(rb"^[ \t\f]*#.*?coding[:=][ \t]*([-_.a-zA-Z0-9]+)")


def open_with_coding(fname):
    with open(fname, "rb") as f:
        l = f.readline()
        m = coding_re.match(l)
        if not m:
            l = f.readline()
            m = coding_re.match(l)
        encoding = "utf-8"
        if m:
            encoding = m.group(1).decode()
    return open(fname, encoding=encoding)


if len(sys.argv) < 2:
    print("no filename provided")
    sys.exit(1)

peephole = True
if sys.argv[1] == "--peephole":
    peephole = True
    del sys.argv[1]

text = open_with_coding(sys.argv[1]).read()

codeobj = py_compile(text, sys.argv[1], "exec")

dis_stable.Disassembler().dump_code(codeobj, file=sys.stdout)

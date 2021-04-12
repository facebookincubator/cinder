# pyre-unsafe
import argparse
import importlib.util
import marshal
import os
import re
from dis import dis

from . import pycodegen, static

# https://www.python.org/dev/peps/pep-0263/
coding_re = re.compile(rb"^[ \t\f]*#.*?coding[:=][ \t]*([-_.a-zA-Z0-9]+)")


def open_with_coding(fname):
    with open(fname, "rb") as f:
        line = f.readline()
        m = coding_re.match(line)
        if not m:
            line = f.readline()
            m = coding_re.match(line)
        encoding = "utf-8"
        if m:
            encoding = m.group(1).decode()
    return open(fname, encoding=encoding)


argparser = argparse.ArgumentParser(
    prog="compiler",
    description="Compile/execute a Python3 source file",
    epilog="""\
By default, compile source code into in-memory code object and execute it.
If -c is specified, instead of executing write .pyc file.
""",
)
argparser.add_argument(
    "-c", action="store_true", help="compile into .pyc file instead of executing"
)
argparser.add_argument("--dis", action="store_true", help="disassemble compiled code")
argparser.add_argument("--output", help="path to the output .pyc file")
argparser.add_argument("input", help="source .py file")
group = argparser.add_mutually_exclusive_group()
group.add_argument(
    "--static", action="store_true", help="compile using static compiler"
)
group.add_argument(
    "--builtin", action="store_true", help="compile using built-in C compiler"
)
group.add_argument(
    "--opt",
    action="store",
    type=int,
    default=-1,
    help="set optimization level to compile with",
)
args = argparser.parse_args()

with open_with_coding(args.input) as f:
    fileinfo = os.stat(args.input)
    source = f.read()

if args.builtin:
    codeobj = compile(source, args.input, "exec")
else:
    compiler = pycodegen.CinderCodeGenerator
    if args.static:
        compiler = static.StaticCodeGenerator

    codeobj = pycodegen.compile(
        source,
        args.input,
        "exec",
        optimize=args.opt,
        compiler=compiler,
        modname="__main__",
    )

if args.dis:
    dis(codeobj)

if args.c:
    if args.output:
        name = args.output
    else:
        name = args.input.rsplit(".", 1)[0] + ".pyc"
    with open(name, "wb") as f:
        hdr = pycodegen.make_header(int(fileinfo.st_mtime), fileinfo.st_size)
        f.write(importlib.util.MAGIC_NUMBER)
        f.write(hdr)
        marshal.dump(codeobj, f)
else:
    exec(codeobj)

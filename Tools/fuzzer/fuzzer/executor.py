import argparse
import ast
import io
import re
import subprocess
import sys
import textwrap
from typing import List, Tuple

import unparse


def run_fuzzer(code_str: str, subprocesses: int) -> None:
    subprocess_arr = []
    for i in range(subprocesses):
        subprocess_arr.append(
            subprocess.Popen(
                [
                    sys.executable,
                    "-X",
                    "jit",
                    __file__.replace("executor.py", "fuzzer.py"),
                    "--codestr",
                    code_str,
                ]
            )
        )
    for i in subprocess_arr:
        i.wait()


# calls run_fuzzer on every function in a test file
def run_fuzzer_on_test_file(file_location: str, subprocesses: int) -> None:
    funcs = extract_functions_from_file(file_location)
    subprocess_arr = []
    for i in funcs:
        run_fuzzer(i, subprocesses)


# extracts function strings from a file
# by parsing a file into an AST
# and unparsing embedded function objects
def extract_functions_from_file(file_location: str) -> List[str]:
    funcs = []
    with open(file_location, "r") as code_file:
        text = code_file.read()
        node = ast.parse(text)
        s = [i for i in node.body]
        # extract all embedded function objects by recursing with a stack
        while s:
            curr = s.pop()
            str_io = io.StringIO()
            # unparse function objects and append them to funcs
            if isinstance(curr, ast.FunctionDef):
                unparsed_func = unparse.Unparser(curr, str_io)
                funcs.append(str_io.getvalue())
            if hasattr(curr, "body"):
                s += [
                    i
                    for i in curr.body
                    if isinstance(i, ast.FunctionDef) or isinstance(i, ast.ClassDef)
                ]
    return funcs


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--codestr", help="code string to be passed into the fuzzer")

    parser.add_argument("--subprocesses", help="number of subprocesses")
    parser.add_argument("--file", help="location of file to run fuzzer on")
    args = parser.parse_args()
    if args.codestr and args.subprocesses:
        run_fuzzer(args.codestr, int(args.subprocesses))
    elif args.file and args.subprocesses:
        run_fuzzer_on_test_file(args.file, int(args.subprocesses))

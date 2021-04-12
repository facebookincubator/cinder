#!/bin/sh
#
# Test compiler by compiling its own source code with itself.
#
set -e

run_compare() {
    echo $1
    ./python3.5-nopeephole dis_stable.py $1 >out.ref
    ./python3.5-nopeephole compiler_runtest.py $1 >out.compiler
    diff -u out.ref out.compiler
    rm out.ref out.compiler
}

run_compare compiler/__init__.py
run_compare compiler/consts.py
run_compare compiler/future.py
run_compare compiler/misc.py
run_compare compiler/visitor.py

run_compare compiler/symbols.py
run_compare compiler/pyassem.py
run_compare compiler/pycodegen.py

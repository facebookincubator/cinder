#!/bin/bash

OUTPUT=$(mktemp)
buck2 --isolation-dir test-python3.10-bin-$1 run @fbcode//mode/$1 fbcode//cinderx/PythonBin:python3.10 -- -c 'print("test")' | tee $OUTPUT
[[ $(tail -1 $OUTPUT) = "test" ]] || exit 1

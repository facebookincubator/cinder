#!/bin/bash -e
# Copyright (c) Meta Platforms, Inc. and affiliates.

cd "$(dirname "$0")"/../..
files=$(find Jit RuntimeTests -name '*.c' -o -name '*.cpp' -o -name '*.h')
if command -v parallel &>/dev/null; then
    echo "$files" | parallel -m -- clang-format -i
else
    echo -e "This script is faster with GNU parallel! Consider running:\nsudo yum install -y parallel && parallel --bibtex"
    clang-format -i $files
fi

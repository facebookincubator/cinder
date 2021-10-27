#!/usr/bin/env bash

if ! git diff --quiet HEAD; then
    echo "Your checkout has dirty files. Please commit your changes."
    exit 1
fi

# Check that Include/opcode.h is in sync with Lib/opcode.py
make regen-opcode > /dev/null || exit 1
if ! git diff --quiet HEAD; then
    echo "Include/opcode.h is out of sync with Lib/opcode.py"
    echo "Please run 'make regen-opcode' and commit the result."
    git checkout -- .
    exit 1
fi

# Check that Python/opcode_targets.h is in sync with Lib/opcode.py
make regen-opcode-targets > /dev/null || exit 1
if ! git diff --quiet HEAD; then
    echo "Python/opcode_targets.h is out of sync with Lib/opcode.py"
    echo "Please run 'make regen-opcode-targets' and commit the result."
    git checkout -- .
    exit 1
fi

# Check that all JIT generated files are up-to-date
make regen-jit > /dev/null || exit 1
if ! git diff --quiet HEAD; then
    echo "One or more generated JIT files are out of date"
    echo "Please run 'make regen-jit' and commit the result."
    git checkout -- .
    exit 1
fi

#!/bin/bash
# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)

set -xe

env

SRC_ROOT="$(readlink -f "$(dirname "${BASH_SOURCE[0]}")")"

mkdir build
cd build
"$SRC_ROOT/configure"
make -j 4 VERBOSE=1 testcinder_jit
make -j 4 VERBOSE=1 testruntime
make -j 4 VERBOSE=1 testcinder
make -j 4 VERBOSE=1 test_strict_module

#!/bin/bash

set -xe

SRC_ROOT="$(readlink -f "$(dirname "${BASH_SOURCE[0]}")")"
export CC=gcc
export CXX=g++
export LDSHARED="gcc -shared"
export CFLAGS="-I\"$SRC_ROOT\" -fno-omit-frame-pointer -momit-leaf-frame-pointer -Wp,-D_FORTIFY_SOURCE=2 -Wno-cast-function-type -Wno-type-limits"
export CPPFLAGS="-I\"$SRC_ROOT\" -Wno-cast-function-type -Wno-type-limits"
export CXXFLAGS="-I\"$SRC_ROOT\" -fno-omit-frame-pointer -momit-leaf-frame-pointer -Wp,-D_FORTIFY_SOURCE=2"

env

mkdir build
cd build
"$SRC_ROOT/configure" --with-cxx-main
make -j 4 VERBOSE=1 testcinder_jit
make -j 4 VERBOSE=1 testruntime
make -j 4 VERBOSE=1 testcinder
make -j 4 VERBOSE=1 test_strict_module

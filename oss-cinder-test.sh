#!/bin/bash
# Copyright (c) Meta Platforms, Inc. and affiliates.

set -xe

env

./python -VV
make -j 4 VERBOSE=1 testcinder_jit
make -j 4 VERBOSE=1 testruntime
make -j 4 VERBOSE=1 testcinder
make -j 4 VERBOSE=1 test_strict_module

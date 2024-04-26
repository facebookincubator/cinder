#!/bin/bash

set -xe

cd "$(dirname $(readlink -f $0))"/../PythonLib

rm -f "test_cinderjit_profile.types"

TEST_CINDERJIT_PROFILE_MAKE_PROFILE=1 \
    buck2 run @//mode/opt fbcode//cinderx:python3.10 -- \
        -X jit-profile-interp -X jit-profile-interp-period=1 -X jit-write-profile="test_cinderjit_profile.types" \
        -munittest -v test_cinderx.test_cinderjit_profile

TEST_CINDERJIT_PROFILE_TEST_PROFILE=1 \
    buck2 run @//mode/opt fbcode//cinderx:python3.10-jit-all -- \
        -X jit-read-profile="test_cinderjit_profile.types" \
        -munittest -v test_cinderx.test_cinderjit_profile

rm -f "test_cinderjit_profile.types"

#!/bin/bash

set -xe
set -o pipefail

cd "$(dirname $(readlink -f $0))"/../

# # Only debug builds have sys.gettotalrefcount()
buck2 run @fbcode//mode/dbg fbcode//cinderx:python3.10 -- -c 'import sys; sys.gettotalrefcount()'
buck2 run @fbcode//mode/dev fbcode//cinderx:python3.10 -- -c 'import sys; sys.gettotalrefcount()'
! buck2 run @fbcode//mode/opt fbcode//cinderx:python3.10 -- -c 'import sys; sys.gettotalrefcount()'

# # Only ASAN builds should have the ASAN API avilable through ctypes
! buck2 run @fbcode//mode/dbg fbcode//cinderx:python3.10 -- -c 'import ctypes; ctypes.pythonapi.__asan_default_options()'
buck2 run @fbcode//mode/dev fbcode//cinderx:python3.10 -- -c 'import ctypes; ctypes.pythonapi.__asan_default_options()'
! buck2 run @fbcode//mode/opt fbcode//cinderx:python3.10 -- -c 'import ctypes; ctypes.pythonapi.__asan_default_options()'

# We embed build flags in our objects
strings $(buck2 run @//mode/dev fbcode//cinderx:dist-path)/lib/libpython3.10.so | ( ! grep -- "-O3 -fno-omit-frame-pointer" > /dev/null )
strings $(buck2 run @//mode/dbg fbcode//cinderx:dist-path)/lib/libpython3.10.so | ( ! grep -- "-O3 -fno-omit-frame-pointer" > /dev/null )
strings $(buck2 run @//mode/opt fbcode//cinderx:dist-path)/lib/libpython3.10.so | grep -- "-O3 -fno-omit-frame-pointer" > /dev/null

echo All tests passed!

#!/bin/bash

# In order to build the cinderx .so we need a working version of Python to run
# the setup.py script. Using the system Python doesn't work as it will try to
# use headers etc. from the system install to build the module.
#
# So this script builds a version of Python from our own sources. We then use
# this to create a venv with setuptools installed which can be used to build the
# cinderx .so.

set -xe

ROOT=$(git rev-parse --show-toplevel)
MODULE_DIR=$(readlink -f "$(dirname "$0")")
PYTHON_FOR_CINDERX_BUILD_DIR="$ROOT/build_cinderx_venv"
VENV_DIR="$PYTHON_FOR_CINDERX_BUILD_DIR/venv"

if [ -f "$VENV_DIR"/bin/activate ]; then
  echo "Bootstrap venv already exists"
  exit 0
fi

rm -rf "$PYTHON_FOR_CINDERX_BUILD_DIR"
mkdir -p "$PYTHON_FOR_CINDERX_BUILD_DIR"
cd "$PYTHON_FOR_CINDERX_BUILD_DIR"
"$ROOT"/Tools/scripts/facebook/configure_with_fb_toolchain.py --debug
make VERBOSE=1 -j

./python -mvenv --without-pip "$VENV_DIR"
. "$VENV_DIR"/bin/activate
# Uses Python from our venv
python3 "$MODULE_DIR"/pip.pyz install "$MODULE_DIR"/setuptools-65.6.0-py3-none-any.whl

#!/bin/bash

ROOT=$(git rev-parse --show-toplevel)
MODULE_DIR=$(readlink -f "$(dirname "$0")")
PYTHON_FOR_CINDERVM_BUILD_DIR="$ROOT/build_cindervm_venv"
VENV_DIR="$PYTHON_FOR_CINDERVM_BUILD_DIR/venv"

if ! [ -f "$VENV_DIR"/bin/activate ]; then
  >&2 echo "Run $MODULE_DIR/bootstrap.sh with a clean checkout."
  >&2 echo "This only needs to be done once."
  exit 1
fi

set -xe

. "$VENV_DIR"/bin/activate

# Build the cindervm "module" .so
cd "$MODULE_DIR"
rm -rf "$MODULE_DIR"/build
# Uses Python from our venv
python3 setup.py build
ln -s "$MODULE_DIR"/build/lib.*/*so "$MODULE_DIR"/build

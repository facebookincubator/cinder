#!/bin/bash

set -xe

MODULE_SRC_DIR=$(readlink -f "$(dirname "$0")")

BUILD_ROOT=""
PYTHON_BIN=""
OUTPUT_DIR=""

# Parse command line arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-root)
      BUILD_ROOT="$2"
      shift # past value
      ;;
    --python-bin)
      PYTHON_BIN="$2"
      shift # past value
      ;;
    --output-dir)
      OUTPUT_DIR="$2"
      shift # past value
      ;;
    *)
      echo "Unknown option: $key"
      exit 1
      ;;
  esac
  shift # past argument
done

if [ -z "$BUILD_ROOT" ] ; then
  BUILD_ROOT=$(
    git rev-parse --is-inside-work-tree >/dev/null 2>&1 &&
    git rev-parse --show-toplevel ||
    hg root)
fi

if [ -z "$PYTHON_BIN" ] ; then
  PYTHON_BIN="$BUILD_ROOT/python"
fi

# Make venv for setuptools
VENV_DIR="$BUILD_ROOT/_cinderx_venv/"
if ! [ -f "$VENV_DIR/bin/activate" ] ; then
  rm -rf "$VENV_DIR"
  "$PYTHON_BIN" -mvenv --without-pip "$VENV_DIR"
  . "$VENV_DIR"/bin/activate
  # Uses Python from our venv
  python3 "$MODULE_SRC_DIR"/pip.pyz install "$MODULE_SRC_DIR"/setuptools-65.6.0-py3-none-any.whl
else
  . "$VENV_DIR"/bin/activate
fi

# Build the cinderx module
MODULE_BUILD_DIR="$BUILD_ROOT/_cinderx_build/"
cd "$MODULE_SRC_DIR"
# Uses Python from our venv
python3 setup.py build -b "$MODULE_BUILD_DIR"

if ! [ -z "$OUTPUT_DIR" ] ; then
  ln -sf "$MODULE_BUILD_DIR"/lib.*/*so "$OUTPUT_DIR/"
fi

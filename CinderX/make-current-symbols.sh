#!/bin/bash

# Generates a list of symbols exported from "core" (non-CinderX) Python.
#
# To be run after a CinderX .so build which additionally has --enable-shared
# configured. E.g to match Sandcatle:
#
#   OPT=-O2 python3 Tools/scripts/facebook/configure_with_fb_toolchain.py --shared --debug -- --enable-cinderx-so
#   make -j
#
# Note the "OPT=-O2".

set -xe

ROOT=$(git rev-parse --show-toplevel)

# The greps make sure to only include exported text and data symbols, and strip
# out empty lines respectively.
nm "$ROOT"/libpython3.10d.so | grep -E '^[0-9a-f]* T|D' | cut -f 3 -d " " | grep -vE '^$' | sort -u

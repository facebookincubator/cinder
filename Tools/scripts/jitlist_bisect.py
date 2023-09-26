#!/usr/bin/env python3
# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)

import argparse
import logging
import os
import re
import shlex
import subprocess
import sys
import tempfile

JITLIST_FILENAME = "jitlist.txt"


def write_jitlist(jitlist):
    with open(JITLIST_FILENAME, "w") as file:
        for func in jitlist:
            print(func, file=file)


def read_jitlist(jit_list_file):
    with open(jit_list_file, "r") as file:
        return [line.strip() for line in file.readlines()]


def run_with_jitlist(command, jitlist):
    write_jitlist(jitlist)

    environ = dict(os.environ)
    environ.update({"PYTHONJITLISTFILE": JITLIST_FILENAME})

    logging.debug(
        f"Running '{shlex.join(command)}' with jitlist of size {len(jitlist)}"
    )
    proc = subprocess.run(
        command,
        env=environ,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding=sys.stdout.encoding,
    )
    logging.debug(
        f"Finished with exit status {proc.returncode}\n\nstdout:\n{proc.stdout}\n\nstderr:\n{proc.stderr}"
    )
    return proc.returncode == 0


COMPILED_FUNC_RE = re.compile(r" -- Compiling ([^ ]+) @ 0x[0-9a-f]+$")


def get_compiled_funcs(command):
    environ = dict(os.environ)
    environ.update({"PYTHONJITDEBUG": "1"})

    logging.info("Generating initial jit-list")
    proc = subprocess.run(
        command,
        env=environ,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        encoding=sys.stderr.encoding,
    )
    if proc.returncode == 0:
        sys.exit(f"Command succeeded during jit-list generation")

    funcs = set()
    for line in proc.stderr.splitlines():
        m = COMPILED_FUNC_RE.search(line)
        if m is None:
            continue
        funcs.add(m[1])
    if len(funcs) == 0:
        sys.exit(f"No compiled functions found")
    # We want a deterministic jitlist, unaffected by the order functions happen
    # to be compiled in.
    return sorted(funcs)


# Return two halves of the given list. For odd-length lists, the second half
# will be larger.
def split_list(l):
    half = len(l) // 2
    return l[0:half], l[half:]


# Attempt to reduce the `jitlist` argument as much as possible, returning the
# shorter version. `fixed` will always be used as part of the jitlist when
# running `command`.
def bisect_impl(command, fixed, jitlist, indent=""):
    logging.info(f"{indent}step fixed[{len(fixed)}] and jitlist[{len(jitlist)}]")

    while len(jitlist) > 1:
        logging.info(f"{indent}{len(fixed) + len(jitlist)} candidates")

        left, right = split_list(jitlist)
        if not run_with_jitlist(command, fixed + left):
            jitlist = left
            continue
        if not run_with_jitlist(command, fixed + right):
            jitlist = right
            continue

        # We need something from both halves to trigger the failure. Try
        # holding each half fixed and bisecting the other half to reduce the
        # candidates.
        new_right = bisect_impl(command, fixed + left, right, indent + "< ")
        new_left = bisect_impl(command, fixed + new_right, left, indent + "> ")
        return new_left + new_right

    return jitlist


def run_bisect(command, jit_list_file):
    prev_arg = ""
    for arg in command:
        if arg.startswith("-Xjit-log-file") or (
            prev_arg == "-X" and arg.startswith("jit-log-file")
        ):
            sys.exit(
                "Your command includes -X jit-log-file, which is incompatible "
                "with this script. Please remove it and try again."
            )
        prev_arg = arg

    if jit_list_file is None:
        jitlist = get_compiled_funcs(command)
    else:
        jitlist = read_jitlist(jit_list_file)

    logging.info("Verifying jit-list")
    if run_with_jitlist(command, jitlist):
        sys.exit(f"Command succeeded with full jit-list")
    if not run_with_jitlist(command, []):
        sys.exit(f"Command failed with empty jit-list")

    jitlist = bisect_impl(command, [], jitlist)
    write_jitlist(jitlist)

    print(f"Bisect finished with {len(jitlist)} functions in {JITLIST_FILENAME}")


def parse_args():
    parser = argparse.ArgumentParser(
        description="When given a command that fails with the jit enabled (including -X jit as appropriate), bisects to find a minimal jit-list that preserves the failure"
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true", help="Enable verbose logging"
    )
    parser.add_argument("--initial-jit-list-file", help="initial jitlist file (default: auto-detect the initial jit list)", default=None)
    parser.add_argument("command", nargs=argparse.REMAINDER)

    return parser.parse_args()


def main():
    args = parse_args()
    log_level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(level=log_level)
    run_bisect(args.command, args.initial_jit_list_file)


if __name__ == "__main__":
    sys.exit(main())

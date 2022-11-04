#!/usr/bin/env python3
# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)

import argparse
import collections
import os
import re
import subprocess
import sys
from enum import Enum


def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "command",
        help="Path to runtime_tests binary, optionally with extra arguments to filter which tests run",
        nargs=argparse.REMAINDER,
    )
    parser.add_argument(
        "--text-input",
        "-t",
        help="File containing output from a previous run of runtime_tests",
    )

    return parser.parse_args()


TEST_RUN_RE = re.compile(r"^\[ RUN +\] ([^.]+)\.(.+)$")
ACTUAL_TEXT_RE = re.compile(r'^    Which is: "(.+)\\n"$')
EXPECTED_VAR_RE = re.compile(r"^  ([^ ]+)$")

# special-case common abbrieviations like HIR and CFG when converting
# camel-cased suite name to its snake-cased file name
SUITE_NAME_RE = re.compile(r"(HIR|CFG|[A-Z][a-z0-9]+)")
FINISHED_LINE = "[----------] Global test environment tear-down"


def unescape_gtest_string(s):
    result = []
    s_iter = iter(s)
    try:
        while True:
            c = next(s_iter)
            if c != "\\":
                result.append(c)
                continue
            c = next(s_iter)
            if c == "n":
                result.append("\n")
                continue
            result.append(c)
    except StopIteration:
        pass
    return "".join(result)


def get_failed_tests(args):
    if args.text_input:
        with open(args.text_input, "r") as f:
            stdout = f.read()
    elif args.command:
        proc = subprocess.run(
            args.command + ["--gtest_color=no"],
            stdout=subprocess.PIPE,
            encoding=sys.stdout.encoding,
        )
        if proc.returncode == 0:
            raise RuntimeError("No tests failed!")
        elif proc.returncode != 1:
            raise RuntimeError(
                f"Command exited with {proc.returncode}, suggesting tests did not run to completion"
            )
        stdout = proc.stdout
    else:
        raise RuntimeError("Must give either --text-input or a command")

    failed_tests = collections.defaultdict(lambda: {})
    line_iter = iter(stdout.split("\n"))
    while True:
        line = next(line_iter)
        if line == FINISHED_LINE:
            break

        match = TEST_RUN_RE.match(line)
        if match:
            test_name = (match[1], match[2])
            test_dict = dict()
            continue

        match = ACTUAL_TEXT_RE.match(line)
        if not match:
            continue

        actual_text = unescape_gtest_string(match[1]).split("\n")
        line = next(line_iter)
        match = EXPECTED_VAR_RE.match(line)
        if not match:
            raise RuntimeError(f"Unexpected line '{line}' after actual text")
        varname = match[1]
        if varname in test_dict:
            raise RuntimeError(
                f"Duplicate expect variable name '{varname}' in {test_name[0]}.{test_name[1]}"
            )
        test_dict[varname] = actual_text
        failed_tests[test_name[0]][test_name[1]] = test_dict

        # Skip the "Which is: ..." line after the expect variable name.
        next(line_iter)

    return failed_tests


TESTS_DIR = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "..", "RuntimeTests")
)


def map_suite_to_file_basename(suite_name):
    return "_".join(map(str.lower, SUITE_NAME_RE.findall(suite_name)))


assert map_suite_to_file_basename("CleanCFGTest") == "clean_cfg_test"
assert map_suite_to_file_basename("HIRBuilderTest") == "hir_builder_test"
assert (
    map_suite_to_file_basename("ProfileDataStaticHIRTest")
    == "profile_data_static_hir_test"
)
assert (
    map_suite_to_file_basename("SomethingEndingWithHIR") == "something_ending_with_hir"
)


def map_suite_to_file(suite_name):
    snake_name = map_suite_to_file_basename(suite_name)
    return os.path.join(TESTS_DIR, "hir_tests", snake_name + ".txt")


def update_text_test(old_lines, suite_name, failed_tests):
    line_iter = iter(old_lines)
    new_lines = []

    def expect(exp):
        line = next(line_iter)
        if line != exp:
            raise RuntimeError(f"Expected '{exp}', got '{line}'")
        new_lines.append(line)

    expect(suite_name)
    expect("---")
    # Optional pass names
    line = next(line_iter)
    new_lines.append(line)
    while line != "---":
        line = next(line_iter)
        new_lines.append(line)

    try:
        while True:
            test_case = next(line_iter)
            if test_case == "@disabled":
                test_case += "\n" + next(line_iter)
            new_lines.append(test_case)
            expect("---")
            while True:
                line = next(line_iter)
                new_lines.append(line)
                if line == "---":
                    break

            hir_lines = []
            line = next(line_iter)
            while line != "---":
                hir_lines.append(line)
                line = next(line_iter)

            if test_case in failed_tests:
                # For text HIR tests, there should only be one element in the
                # failed test dict.
                hir_lines = next(iter(failed_tests[test_case].values()))
            new_lines += hir_lines
            new_lines.append("---")
    except StopIteration:
        pass

    return new_lines


def write_if_changed(filename, old_lines, new_lines):
    if new_lines == old_lines:
        return
    with open(filename, "w") as f:
        print(f"Rewriting {filename}")
        f.write("\n".join(new_lines))


CPP_TEST_NAME_RE = re.compile(r"^TEST(_F)?\(([^,]+), ([^)]+)\) {")
CPP_EXPECTED_START_RE = re.compile(r"^(  const char\* ([^ ]+) =)")
CPP_EXPECTED_END = ')";'
CPP_TEST_END = "}"


def find_cpp_files(root):
    for dirpath, dirnames, filenames in os.walk(root):
        for filename in filenames:
            if filename.endswith(".cpp"):
                yield os.path.join(dirpath, filename)


class State(Enum):
    # Active before the first test is found, or during a passing test.
    WAIT_FOR_TEST = 1

    # Active while reading a test that has failed, either waiting for an
    # expected variable or a new test.
    PROCESS_FAILED_TEST = 2

    # Active while skipping lines of an expected variable.
    SKIP_EXPECTED = 3


def update_cpp_tests(failed_suites, failed_cpp_tests):
    def expect_state(estate):
        nonlocal state, lineno, cpp_filename
        if state is not estate:
            sys.exit(
                f"Expected state {estate} at {cpp_filename}:{lineno}, actual {state}"
            )

    def expect_empty_test_dict():
        if test_dict is not None and len(test_dict) > 0:
            print(
                f"Coudln't find {len(test_dict)} expected variables in {suite_name}.{test_name}:"
            )
            print(list(test_dict.keys()))

    for cpp_filename in find_cpp_files(TESTS_DIR):
        with open(cpp_filename, "r") as f:
            old_lines = f.read().split("\n")

        state = State.WAIT_FOR_TEST
        test_dict = None
        new_lines = []
        for lineno, line in enumerate(old_lines, 1):
            m = CPP_TEST_NAME_RE.match(line)
            if m is not None:
                new_lines.append(line)

                expect_empty_test_dict()
                test_dict = None

                suite_name = m[2]
                test_name = m[3]
                try:
                    failed_cpp_tests.remove((suite_name, test_name))
                except KeyError:
                    state = State.WAIT_FOR_TEST
                    continue

                test_dict = failed_suites[suite_name][test_name]
                state = State.PROCESS_FAILED_TEST
                continue

            if state is State.WAIT_FOR_TEST:
                new_lines.append(line)
                continue

            m = CPP_EXPECTED_START_RE.match(line)
            if m is not None:
                expect_state(State.PROCESS_FAILED_TEST)
                decl = m[1]
                varname = m[2]
                try:
                    actual_lines = test_dict[varname]
                    del test_dict[varname]
                except KeyError:
                    # This test has multiple expected variables, and this one
                    # is OK.
                    new_lines.append(line)
                    continue

                # This may collapse a two-line start to the variable onto one
                # line, which clang-format will clean up.
                new_lines.append(decl + ' R"(' + actual_lines[0])
                new_lines += actual_lines[1:]
                state = State.SKIP_EXPECTED
                continue

            if state is State.SKIP_EXPECTED:
                if line == CPP_EXPECTED_END:
                    new_lines.append(line)
                    state = State.PROCESS_FAILED_TEST
                continue

            new_lines.append(line)

        expect_empty_test_dict()
        write_if_changed(cpp_filename, old_lines, new_lines)

    if len(failed_cpp_tests) > 0:
        print(f"\nCouldn't find {len(failed_cpp_tests)} failed test(s):")
        for test in failed_cpp_tests:
            print(f"{test[0]}.{test[1]}")


def main():
    args = parse_arguments()
    failed_cpp_tests = set()
    failed_suites = get_failed_tests(args)

    for suite_name, failed_tests in failed_suites.items():
        suite_file = map_suite_to_file(suite_name)
        try:
            with open(suite_file, "r") as f:
                old_lines = f.read().split("\n")
        except FileNotFoundError:
            for test_name in failed_tests:
                failed_cpp_tests.add((suite_name, test_name))
            continue

        new_lines = update_text_test(old_lines, suite_name, failed_tests)
        write_if_changed(suite_file, old_lines, new_lines)

    if len(failed_cpp_tests) > 0:
        update_cpp_tests(failed_suites, failed_cpp_tests)


if __name__ == "__main__":
    sys.exit(main())

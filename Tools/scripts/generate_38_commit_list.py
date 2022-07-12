#!/usr/bin/env python3

import argparse
import re
import subprocess
import sys
from enum import Enum


COMMIT_START_RE = re.compile(r"^commit ([a-f0-9]{40})$")
REVISION_RE = re.compile(r"    Differential Revision: https.+(D[0-9]+)$")


class State(Enum):
    WAIT_FOR_COMMIT = 1
    WAIT_FOR_TITLE = 2
    WAIT_FOR_REVISION = 3


def find_commits(args):
    stdout = subprocess.run(
        [
            "git",
            "log",
            "--reverse",
            f"{args.base_commit}..{args.end_commit}",
            "--",
            *args.files,
        ],
        check=True,
        encoding=sys.stdin.encoding,
        stdout=subprocess.PIPE,
    ).stdout

    commits = {}
    commit_hash = None
    title = None
    state = State.WAIT_FOR_COMMIT

    for line in stdout.splitlines():
        if state is State.WAIT_FOR_COMMIT:
            if m := COMMIT_START_RE.match(line):
                state = State.WAIT_FOR_TITLE
                commit_hash = m[1]
        elif state is State.WAIT_FOR_TITLE:
            if line.startswith("    "):
                title = line.strip()
                state = State.WAIT_FOR_REVISION
        elif state is State.WAIT_FOR_REVISION:
            if m := REVISION_RE.match(line):
                commits[commit_hash] = f"{m[1]}: {title}"
                commit_hash = None
                title = None
                state = State.WAIT_FOR_COMMIT

    return commits


BLAME_LINE_RE = re.compile(r"^([0-9a-f]{40}) ")


def collect_blamed_commits(args, filename, commits):
    stdout = subprocess.run(
        ["git", "blame", "-l", args.end_commit, "--", filename],
        check=True,
        encoding=sys.stdin.encoding,
        stdout=subprocess.PIPE,
    ).stdout

    blamed_commits = set()
    for line in stdout.splitlines():
        m = BLAME_LINE_RE.match(line)
        if not m:
            raise RuntimeError(f"Don't understand line '{line}'")
        commit_hash = m[1]
        if commit_hash in commits:
            blamed_commits.add(commit_hash)

    return blamed_commits


def parse_args():
    parser = argparse.ArgumentParser(
        description="Generate an abbreviated list of commits from Cinder 3.8, optionally limited to commits that contributed to the current version of a given set of files (as determined by `git blame`)."
    )
    parser.add_argument(
        "--base-commit", default="origin/3.8", help="Beginning of commit search range"
    )
    parser.add_argument(
        "--end-commit",
        default="cinder/310-phase1-target",
        help="End of commit search range",
    )
    parser.add_argument(
        "files", nargs="*", help="Restrict search to the given files, if any"
    )
    return parser.parse_args()


def main():
    args = parse_args()
    all_commits = find_commits(args)

    if len(args.files) == 0:
        for title_line in all_commits.values():
            print(title_line)
        return

    blamed_commits = set()
    for filename in args.files:
        blamed_commits |= collect_blamed_commits(args, filename, all_commits)

    for commit_hash, title_line in all_commits.items():
        if commit_hash in blamed_commits:
            print(title_line)


if __name__ == "__main__":
    sys.exit(main())

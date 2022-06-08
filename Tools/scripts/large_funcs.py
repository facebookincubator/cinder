# Parses a perf-<pid>.map file and outputs a list of functions
# ordered by size (largest first).

import sys
from dataclasses import dataclass


@dataclass
class Entry:
    base: int
    size: int
    name: str


def load_perf_map(filepath):
    result = []
    with open(filepath, "r") as perfmap:
        for line in perfmap.readlines():
            line = line.strip()
            b, s, n = line.split(" ")
            entry = Entry(int(b, 16), int(s, 16), n)
            result.append(entry)
    return result


def main(filepath):

    entries = load_perf_map(filepath)
    for e in sorted(entries, key=lambda e: e.size, reverse=True):
        print(e.name, e.size)

if __name__ == "__main__":

    main(sys.argv[1])

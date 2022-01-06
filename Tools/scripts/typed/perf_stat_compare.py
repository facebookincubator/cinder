#!/usr/bin/env python3

import argparse
import collections
import os
import re
import shlex
import subprocess
import sys
from typing import NamedTuple, Iterable, Sequence, Dict


class Stat(NamedTuple):
    hits: int
    stddev: float
    min: float
    max: float


def measure_binary(
    binary: str, repetitions: int, events: Iterable[str], common_args: Iterable[str]
) -> str:
    events_args = []
    for event in events:
        events_args += ["-e", event]
    env = dict(os.environ)
    env["PYTHONHASHSEED"] = "0"

    try:
        return subprocess.run(
            [
                "perf",
                "stat",
                "--field-separator",
                ";",
                "--repeat",
                repetitions,
                *events_args,
                *shlex.split(binary),
                *common_args,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            encoding=sys.stderr.encoding,
            env=env,
            check=True,
        ).stderr
    except subprocess.CalledProcessError as cpe:
        print(
            f"Command {cpe.cmd} failed with exit status {cpe.returncode}:\n\nstdout:\n{cpe.stdout}\n\nstderr:\n{cpe.stderr}",
            file=sys.stderr,
        )
        raise


class RetryMeasurement(Exception):
    pass


IGNORE_LINES = {
    "Using perf wrapper that supports hot-text. Try perf.real if you encounter any issues.",
    "",
}


def parse_perf_stat_output(output: str) -> Dict[str, Stat]:
    stats = {}
    for line in output.split("\n"):
        line = line.strip()
        if line in IGNORE_LINES:
            continue
        parts = line.split(";")
        if len(parts) != 8 and len(parts) != 10:
            continue

        run_ratio = float(parts[5].replace("%", ""))
        if run_ratio != 100.0:
            raise RetryMeasurement()

        count = int(parts[0])
        event = parts[2]
        stddev = float(parts[3].replace("%", "")) / 100 * count
        stats[event] = Stat(count, stddev, count - stddev, count + stddev)

    return stats


def diff_perf_stats(binaries: Sequence[str], stats: Sequence[Dict[str, Stat]]) -> None:
    max_stat_len = 0
    for stats_dict in stats:
        for counter in stats_dict.keys():
            max_stat_len = max(max_stat_len, len(counter))
    stat_format = (
        f"  {{:{max_stat_len}}}" + " : {:+5.1f}% to {:+5.1f}%, mean {:+5.1f}%{}"
    )

    extra_line = ""
    for i in range(len(binaries)):
        for j in range(i + 1, len(binaries)):
            print(extra_line, end="")
            extra_line = "\n"

            prog1, stats1 = binaries[i], stats[i]
            prog2, stats2 = binaries[j], stats[j]

            print(f"{prog1} -> {prog2}")
            for counter, stat1 in stats1.items():
                stat2 = stats2[counter]

                min_change = (stat2.min / stat1.max - 1) * 100
                max_change = (stat2.max / stat1.min - 1) * 100
                change = (stat2.hits / stat1.hits - 1) * 100

                # Indicate results that have a very small delta or include 0 in
                # their range.
                tail = ""
                if (
                    abs(min_change) < 0.1
                    or abs(max_change) < 0.1
                    or (min_change < 0 and max_change > 0)
                ):
                    tail = " ✗"
                print(stat_format.format(counter, min_change, max_change, change, tail))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="""
Run the given binaries under `perf stat` and print pairwise comparisons of the
results. Delta ranges are printed using one standard deviation from the mean in
either direction. When either side of the interval is within 0.1% of 0, an '✗'
is used to indicate a probably-insignificant result.
"""
    )

    parser.add_argument("--binary", "-b", action="append", default=[])
    parser.add_argument(
        "-r", "--repeat", default="12", help="Number of repetitions for each binary."
    )
    parser.add_argument(
        "common_args", nargs=argparse.REMAINDER, help="Arguments to pass to each binary"
    )
    parser.add_argument(
        "-e",
        "--event",
        action="append",
        help="Events to pass to `perf stat`. Defaults to cycles, instructions, "
        "branches, and branch-misses if not given.",
    )

    return parser.parse_args()


def collect_events(
    binary: str, repeat: int, events: Iterable[str], common_args: Iterable[str]
) -> Dict[str, Stat]:
    tries = 10
    while True:
        output = measure_binary(binary, repeat, events, common_args)
        if "nmi_watchdog" in output:
            # Results reported when the nmi_watchdog interfered are useless.
            sys.stderr.write("\n\nError: perf stat complained about nmi_watchdog\n")
            sys.stderr.write(output)
            sys.stderr.write("\n\nAborting\n")
            sys.exit(1)
        try:
            return parse_perf_stat_output(output)
        except RetryMeasurement:
            if tries == 1:
                print(f"Failed to measure {events} for {binary}", file=sys.stderr)
                sys.exit(1)
            tries -= 1
            print(
                f"Re-measuring {events} for {binary}, {tries} attempts left",
                file=sys.stderr,
            )


def main() -> None:
    args = parse_args()

    if not args.event:
        args.event = ["cycles", "instructions", "branches", "branch-misses"]
    common_args = args.common_args
    if len(common_args) > 0 and common_args[0] == "--":
        common_args = common_args[1:]

    group_size = 2
    event_groups = [
        args.event[i : i + group_size] for i in range(0, len(args.event), group_size)
    ]
    results = []
    for binary in args.binary:
        print(f"Measuring {' '.join([binary] + common_args)}", file=sys.stderr)
        stats = {}
        for events in event_groups:
            stats.update(collect_events(binary, args.repeat, events, common_args))
        results.append(stats)

    diff_perf_stats(args.binary, results)


if __name__ == "__main__":
    sys.exit(main())

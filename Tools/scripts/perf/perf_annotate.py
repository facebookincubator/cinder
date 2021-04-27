#!/usr/bin/env python3
# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)

# Until this and prod_collect.py are integrated better, the steps to get an
# annotated disassembly dump are roughly:

# Add the following lines to instagram-server/conf/uwsgi.ini and commit the
# result:
#
# env = PYTHONJITDISASFUNCS=1
# env = PYTHONJITLOGFILE=/tmp/cinder-disas-{pid}.log
#
# > cd instagram-server && ig-igsrv build .
# > maryctl run-canary --duration 1800 --num-hosts 1 --fbpkg-id <hash from prev step>
#
# Wait ~10 minutes for server to warm up
#
# > Tools/scripts/perf/prod_collect.py --host <canary host>
# > tar xvf data.tgz
# > Tools/scripts/perf/perf_annotate.py -p perf.samples -d tmp/cinder-disas-*.log -o perf-out
#
# Then look in perf-out/ for the results of the analysis.


import argparse
import logging
import os
import re
import shutil
import subprocess
import sys
import types
from collections import defaultdict

from typing import List, IO, Any, Dict, Tuple, Optional, Set, NamedTuple, Pattern


class Frame(NamedTuple):
    address: int
    symbol: str


class Sample(NamedTuple):
    process: str
    pid: int
    stacktrace: List[Frame]
    event: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--perf-file",
        "-p",
        required=True,
        help="Perf data to read. May be a raw perf file or the output of `perf script -c uwsgi -F comm,pid,time,event,period,ip,sym`.",
    )
    parser.add_argument(
        "--disas-file",
        "-d",
        required=True,
        help="Log file from Cinder with HIR-annotated disassembly.",
    )
    parser.add_argument(
        "--output",
        "-o",
        required=True,
        help="Output directory. Must not exist, or --force must be given.",
    )
    parser.add_argument(
        "--force",
        "-f",
        action="store_true",
        help="If the output directory exists, erase it before writing anything to it.",
    )

    parser.add_argument(
        "--top-funcs",
        type=int,
        default=50,
        help="Number of functions to print annotated disassembly for (sorted by descending number of hits per function).",
    )

    return parser.parse_args()


HEADER_LINE_RE: Pattern[str] = re.compile(
    r"([^ ]+) +([0-9]+) +([0-9]+\.[0-9]+): +([0-9]+) +([^:]+)(:p+)?:"
)
TRACE_LINE_RE: Pattern[str] = re.compile(r"([0-9a-f]+) (.+)")

# Parse and return a list of Samples from the given file.
def parse_perf_data(filename: str) -> Dict[str, List[Sample]]:
    proc = None

    with open(filename, "rb") as script_file_raw:
        magic = script_file_raw.read(8)
    if magic == b"PERFILE2":
        proc = subprocess.Popen(
            [
                "perf",
                "script",
                "-i",
                filename,
                "-c",
                "uwsgi",
                "-F",
                "comm,pid,time,event,period,ip,sym",
            ],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            encoding="utf-8",
        )
        stdout = proc.stdout
        if stdout is None:
            raise RuntimeError("Proc has no stdout")
        script_file = stdout
    else:
        # If the file isn't a perf file, assume it's the output of the correct
        # perf.script command.
        script_file = open(filename, "r")

    samples: Dict[str, List[Sample]] = defaultdict(list)
    process = pid = event = None
    stacktrace: List[Frame] = []

    for line in script_file:
        line = line.strip()
        if line == "":
            if process and pid and event and stacktrace:
                samples[event].append(
                    Sample(process=process, pid=pid, stacktrace=stacktrace, event=event)
                )
            else:
                logging.info(f"Ignoring empty sample ({process}, {pid}, {event})")

            process = pid = event = None
            stacktrace = []
            sample_lines = []
            continue

        if process is None:
            m = HEADER_LINE_RE.match(line)
            if not m:
                raise RuntimeError(f"Bad header line '{line}'")
            process, pid, event = m[1], int(m[2]), sys.intern(m[5])
            continue

        # We only care about the first entry in the stacktrace at the moment,
        # and ignoring subsequent entries speeds up processing considerably.
        if len(stacktrace) == 0:
            m = TRACE_LINE_RE.match(line)
            if not m:
                raise RuntimeError(f"Bad trace line '{line}'")
            stacktrace.append(Frame(address=int(m[1], 16), symbol=sys.intern(m[2])))

    script_file.close()

    if proc is not None:
        returncode = proc.wait(timeout=5) != 0
        if returncode != 0:
            logging.info(f"perf script exited with status {returncode}")

    return samples


JIT_PREFIX = "__CINDER_JIT:"


# Return an ASCII table of statistics about the items in the given dict, sorted
# in descending order.
def dict_stats(d: Dict[str, int], grand_total: int) -> str:
    flat_items = sorted(d.items(), key=lambda t: t[1], reverse=True)
    total = 0
    max_count = 0
    for key, count in flat_items:
        total += count
        max_count = max(max_count, count)

    max_count_len = max(7, len(f"{max_count}"))
    header_row_fmt = f"{{:>{max_count_len}}} {{:>6}} | {{}}\n"
    stats_row_fmt = f"{{:>{max_count_len}}} {{:>6.2f}} | {{}}\n"

    stats = header_row_fmt.format("count", "pct", "bucket")
    stats += "-" * (len(stats) - 1) + "\n"
    for bucket, hits in flat_items:
        percent = hits / grand_total * 100
        stats += stats_row_fmt.format(hits, percent, bucket)

    return stats


# Categorize every sample into one of a few coarse buckets, and collect sample
# counts for all JIT functions, returning both dicts.
def generate_stats(samples: List[Sample]) -> Tuple[Dict[str, int], Dict[str, int]]:
    buckets = {bucket: 0 for bucket in ["interpreter", "jit", "other"]}
    jit_funcs: Dict[str, int] = defaultdict(int)

    for sample in samples:
        top_symbol = sample.stacktrace[0].symbol
        if top_symbol == "_PyEval_EvalFrameDefault":
            bucket = "interpreter"
        elif top_symbol.startswith(JIT_PREFIX):
            bucket = "jit"
            jit_func = sys.intern(top_symbol[len(JIT_PREFIX) :])
            jit_funcs[jit_func] += 1
        else:
            bucket = "other"

        buckets[bucket] += 1

    return buckets, jit_funcs


# Transform a dict with lists of Samples for different events into a dict
# mapping addresses to dicts of hit counts for each event.
def collect_address_hits(samples: Dict[str, List[Sample]]) -> Dict[int, Dict[str, int]]:
    # pyre-ignore[9]: Can't annotate lambda return types
    hits: Dict[int, Dict[str, int]] = defaultdict(lambda: defaultdict(int))

    for event, event_samples in samples.items():
        for sample in event_samples:
            hits[sample.stacktrace[0].address][event] += 1
    return hits


# Compact event names used to print multiple events inline.
EVENT_ABBREVS = {
    "cycles": "cy",
    "branch-misses": "bm",
    "L1-icache-misses": "ic",
    "L1-dcache-misses": "dc",
    "iTLB-misses": "it",
    "dTLB-misses": "dt",
}

# Return a list of all events present in the given set, sorted by order of
# appearancee in EVENT_ABBREVS.
def collect_events(events: Set[str]) -> List[str]:
    return [e for e in EVENT_ABBREVS.keys() if e in events]


FUNC_START_RE: Pattern[str] = re.compile(r"-- Disassembly for (.+)")
CODE_LINE_RE: Pattern[str] = re.compile(r" +(0x[0-9a-f]+)")

# Annotate a disassembly log file to include perf event hit counts, writing the
# results to output_dir/funcNNN.txt for the hottest --top-funcs functions.
def annotate_disas(
    args: argparse.Namespace,
    samples: Dict[str, List[Sample]],
    jit_func_cycles: Dict[str, int],
) -> None:
    addr_hits = collect_address_hits(samples)
    all_events = collect_events(set(samples.keys()))
    disas_file = open(args.disas_file, "r")

    jit_top_funcs_order = sorted(
        jit_func_cycles.keys(), key=lambda key: jit_func_cycles[key], reverse=True
    )[: args.top_funcs]
    jit_top_funcs = set(jit_top_funcs_order)

    funcs: Dict[str, List[str]] = dict()
    cur_func = None
    cur_func_lines = []
    for line in disas_file:
        match = FUNC_START_RE.search(line)
        if match:
            # Save current func, if any.
            if cur_func is not None:
                funcs[cur_func] = cur_func_lines
                cur_func = None
                cur_func_lines = []

            # Check if we care about the new func.
            if match[1] in jit_top_funcs:
                cur_func = match[1]
                cur_func_lines.append(line)
            continue

        # Skip any lines until we find a function we care about.
        if cur_func is None:
            continue

        hits_str = ""
        match = CODE_LINE_RE.match(line)
        if match:
            addr = int(match[1], 16)
            if addr in addr_hits:
                hits = addr_hits[addr]
                for event in all_events:
                    if event in hits:
                        hits_str += f"{EVENT_ABBREVS[event]}: {hits[event]:2} "
                    else:
                        hits_str += "       "

        cur_func_lines.append(f"{hits_str:42}{line}")

    disas_file.close()

    logging.info(
        f"Writing annotated disassembly for {args.top_funcs} hottest functions to {args.output}/func*.txt"
    )

    for i, func_name in enumerate(jit_top_funcs_order):
        if func_name not in funcs:
            # TODO(bsimmers): Figure out why these functions are missing.
            logging.warning(
                f"Not annotating disassembly for missing function {func_name}"
            )
            continue

        with open(os.path.join(args.output, f"func{i:03}.txt"), "w") as func_file:
            func_file.writelines(funcs[func_name])


# Create the output directory, deleting it first if --force was given.
def prepare_output_dir(args: argparse.Namespace) -> None:
    if os.path.exists(args.output) and not args.force:
        raise RuntimeError(
            f"Output directory {args.output} exists and --force was not given"
        )
    shutil.rmtree(args.output, ignore_errors=True)
    os.mkdir(args.output)


def main() -> None:
    logging.basicConfig(level=logging.INFO)

    args = parse_args()
    prepare_output_dir(args)

    samples = parse_perf_data(args.perf_file)
    buckets, jit_funcs = generate_stats(samples["cycles"])
    cycle_count = len(samples["cycles"])

    with open(os.path.join(args.output, "stats.txt"), "w") as stats_file:
        stats_file.write(dict_stats(buckets, cycle_count))
        stats_file.write("\n")
        stats_file.write(dict_stats(jit_funcs, cycle_count))

    annotate_disas(args, samples, jit_funcs)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3

import argparse
import logging
import os
import random
import subprocess
import sys

from typing import Sequence

# Run the given command, returning its stdout. Exits the process with an error
# if the command fails.
def cmd_stdout(*cmd: str) -> bytes:
    result = subprocess.run(cmd, stdout=subprocess.PIPE)
    if result.returncode != 0:
        sys.exit(f"Command {cmd} failed with status {result.returncode}")
    return result.stdout


# Return a random host in the given smc tier.
def randbox(tier: str) -> str:
    hosts = cmd_stdout("smcc", "ls-hosts", "-r", tier).decode("utf-8").splitlines()
    return random.choice(hosts)


COLLECT_SCRIPT = "prod_collect.sh"


# Collect perf data from the given host.
def collect_profile(args: argparse.Namespace) -> None:
    host = args.host
    output_filename = args.output

    logging.info(f"Running perf on {host}...")

    cmd_stdout(
        "tw", "scp", os.path.dirname(__file__) + "/" + COLLECT_SCRIPT, f"{host}:~/"
    )
    data = cmd_stdout("tw", "ssh", host, f"./{COLLECT_SCRIPT} {args.sample_time}")
    with open(output_filename, "wb") as file:
        file.write(data)
        logging.info(f"Wrote data to {output_filename}")

    cmd_stdout("tw", "ssh", host, f"rm {COLLECT_SCRIPT}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run perf on a random or specified host, copying the resulting data to the local machine."
    )

    parser.add_argument(
        "--tier",
        help="SMC tier to select a host from.",
    )
    parser.add_argument(
        "--host", help="Host to profile from. Should not be given if --tier is given."
    )

    parser.add_argument(
        "--sample-time",
        default=6,
        type=int,
        help="Time to run `perf record` per event, in seconds`.",
    )

    parser.add_argument("--output", "-o", default="data.tgz")

    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.tier and args.host:
        sys.exit("Can't specify both a tier and a host")

    host = args.host
    if not args.host:
        if not args.tier:
            args.tier = "instagram.django.oregon"
        args.host = randbox(args.tier)

    collect_profile(args)


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    sys.exit(main())

# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
"""
Benchmark script for recursive async tree workloads. This script includes the
following microbenchmark scenarios:

1) "no_suspension": No suspension in the async tree.
2) "suspense_all": Suspension (simulating IO) at all leaf nodes in the async tree.

Use the commandline flag or pass microbenchmark scenario name to run_microbenchmark()
to determine which microbenchmark scenario to run.
"""


import asyncio
import time
from argparse import ArgumentParser


NUM_RECURSE_LEVELS = 6
NUM_RECURSE_BRANCHES = 6
IO_SLEEP_TIME = 0.05


def parse_args():
    parser = ArgumentParser(
        description="""\
Benchmark script for recursive async tree workloads. It can be run as a standalone
script, in which case you can specify the microbenchmark scenario to run and whether
to print the results.
"""
    )
    parser.add_argument(
        "-s",
        "--scenario",
        choices=["no_suspension", "suspense_all"],
        default="no_suspension",
        help="""\
Determines which microbenchmark scenario to run. Defaults to no_suspension. Options:
1) "no_suspension": No suspension in the async tree.
2) "suspense_all": Suspension (simulating IO) at all leaf nodes in the async tree.
""",
    )
    parser.add_argument(
        "-p",
        "--print",
        action="store_true",
        default=False,
        help="Print the results (runtime and number of Tasks created).",
    )
    return parser.parse_args()


class AsyncTree:
    def __init__(self):
        self.task_count = 0

    def create_task(self, loop, coro):
        self.task_count += 1
        return asyncio.Task(coro, loop=loop)

    async def recurse(self, recurse_level, suspense_func):
        if recurse_level == 0:
            if suspense_func is not None:
                await suspense_func()
            return

        await asyncio.gather(
            *[
                self.recurse(recurse_level - 1, suspense_func)
                for _ in range(NUM_RECURSE_BRANCHES)
            ]
        )

    async def suspense_all_suspense_func(self):
        await asyncio.sleep(IO_SLEEP_TIME)

    def run_microbenchmark(self, scenario="no_suspension"):
        suspense_funcs = {
            "no_suspension": None,
            "suspense_all": self.suspense_all_suspense_func,
        }
        suspense_func = suspense_funcs[scenario]

        loop = asyncio.new_event_loop()
        loop.set_task_factory(self.create_task)
        loop.run_until_complete(self.recurse(NUM_RECURSE_LEVELS, suspense_func))


if __name__ == "__main__":
    args = parse_args()
    scenario = args.scenario
    async_tree = AsyncTree()

    start_time = time.perf_counter()
    async_tree.run_microbenchmark(scenario)
    end_time = time.perf_counter()

    if args.print:
        print(f"Scenario: {scenario}")
        print(f"Time: {end_time - start_time} s")
        print(f"{async_tree.task_count} tasks created")


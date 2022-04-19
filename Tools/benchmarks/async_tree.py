# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
"""
Benchmark script for recursive async tree workloads. This script includes the
following microbenchmark scenarios:

1) "no_suspension": No suspension in the async tree.
2) "suspense_all": Suspension (simulating IO) at all leaf nodes in the async tree.
3) "memoization": Simulated IO calls at all leaf nodes, but with memoization. Only
                  un-memoized IO calls will result in suspensions.
4) "cpu_io_mixed": A mix of CPU-bound workload and IO-bound workload (with
                   memoization) at the leaf nodes.

Use the commandline flag or pass microbenchmark scenario name to run_microbenchmark()
to determine which microbenchmark scenario to run.
"""


import asyncio
import math
import random
import time
from argparse import ArgumentParser


NUM_RECURSE_LEVELS = 6
NUM_RECURSE_BRANCHES = 6
IO_SLEEP_TIME = 0.05
DEFAULT_MEMOIZABLE_PERCENTAGE = 90
DEFAULT_CPU_PROBABILITY = 0.5
FACTORIAL_N = 500


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
        choices=["no_suspension", "suspense_all", "memoization", "cpu_io_mixed"],
        default="no_suspension",
        help="""\
Determines which microbenchmark scenario to run. Defaults to no_suspension. Options:
1) "no_suspension": No suspension in the async tree.
2) "suspense_all": Suspension (simulating IO) at all leaf nodes in the async tree.
3) "memoization": Simulated IO calls at all leaf nodes, but with memoization. Only
                  un-memoized IO calls will result in suspensions.
4) "cpu_io_mixed": A mix of CPU-bound workload and IO-bound workload (with
                   memoization) at the leaf nodes.
""",
    )
    parser.add_argument(
        "-m",
        "--memoizable-percentage",
        type=int,
        default=DEFAULT_MEMOIZABLE_PERCENTAGE,
        help="""\
Sets the percentage (0-100) of the data that should be memoized, defaults to 90. For
example, at the default 90 percent, data 1-90 will be memoized and data 91-100 will not.
""",
    )
    parser.add_argument(
        "-c",
        "--cpu-probability",
        type=float,
        default=DEFAULT_CPU_PROBABILITY,
        help="""\
Sets the probability (0-1) that a leaf node will execute a cpu-bound workload instead
of an io-bound workload. Defaults to 0.5. Only applies to the "cpu_io_mixed"
microbenchmark scenario.
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
    def __init__(
        self,
        memoizable_percentage=DEFAULT_MEMOIZABLE_PERCENTAGE,
        cpu_probability=DEFAULT_CPU_PROBABILITY,
    ):
        self.suspense_count = 0
        self.task_count = 0
        self.memoizable_percentage = memoizable_percentage
        self.cpu_probability = cpu_probability
        self.cache = {}
        # set to deterministic random, so that the results are reproducible
        random.seed(0)

    async def mock_io_call(self):
        self.suspense_count += 1
        await asyncio.sleep(IO_SLEEP_TIME)

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
        await self.mock_io_call()

    async def memoization_suspense_func(self):
        # deterministic random (seed preset)
        data = random.randint(1, 100)

        if data <= self.memoizable_percentage:
            if self.cache.get(data):
                return data

            self.cache[data] = True

        await self.mock_io_call()
        return data

    async def cpu_io_mixed_suspense_func(self):
        if random.random() < self.cpu_probability:
            # mock cpu-bound call
            return math.factorial(FACTORIAL_N)
        else:
            return await self.memoization_suspense_func()

    def run_microbenchmark(self, scenario="no_suspension"):
        suspense_funcs = {
            "no_suspension": None,
            "suspense_all": self.suspense_all_suspense_func,
            "memoization": self.memoization_suspense_func,
            "cpu_io_mixed": self.cpu_io_mixed_suspense_func,
        }
        suspense_func = suspense_funcs[scenario]

        loop = asyncio.new_event_loop()
        loop.set_task_factory(self.create_task)
        loop.run_until_complete(self.recurse(NUM_RECURSE_LEVELS, suspense_func))


if __name__ == "__main__":
    args = parse_args()
    scenario = args.scenario
    async_tree = AsyncTree(args.memoizable_percentage, args.cpu_probability)

    start_time = time.perf_counter()
    async_tree.run_microbenchmark(scenario)
    end_time = time.perf_counter()

    if args.print:
        print(f"Scenario: {scenario}")
        print(f"Time: {end_time - start_time} s")
        print(f"Tasks created: {async_tree.task_count}")
        print(f"Suspense called: {async_tree.suspense_count}")


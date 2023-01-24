# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

# NOTE: This file is intended to test JIT behavior in the presence of profile
# data. The non-standard filename is so it's not run by default as part of the
# normal test suite; instead, it is run in a special configuration by the
# 'testcinder_jit_profile' make target.

import os
import threading
import unittest
from collections import Counter
from contextlib import contextmanager

try:
    import cinderjit
except ModuleNotFoundError:
    cinderjit = None

# Some tests want to make sure the type-specialized code behaves correctly when
# given a type that was not seen during profiling. The simplest and most
# reliable way to do this (given the current system) is to allow the code under
# test to behave differently during profiling and testing.
#
# We expose this within this file through the PROFILING and TESTING globals,
# which will never both be set at the same time (they might both be false, if
# the JIT is disabled). This assumes that calling code will set
# CINDER_JIT_PROFILE_TEST_PROFILING as appropriate.
def check_profiling():
    var = os.getenv("CINDER_JIT_PROFILE_TEST_PROFILING", None)
    return var not in (None, "", "0")


PROFILING = check_profiling()
TESTING = not (PROFILING or cinderjit is None)


class ProfileTest(unittest.TestCase):
    @contextmanager
    def assertDeopts(self, deopt_specs):
        """Assert that the protected region of code has exactly as many deopts as the
        given specs. Specs are given as dict keys in deopt_specs, with the
        values indicating how many deopts of that kind to expect.

        Specs should be tuples of key-value pair tuples, and can reference any
        of the keys and values in
        cinderjit.get_and_clear_runtime_stats()["deopt"]. For example, to match
        two GuardType guard failures, pass:

        {(("reason", "GuardFailure"), ("description", "GuardType)): 2}

        or to assert no deopts, pass an empty dict:

        {}
        """

        cinderjit.get_and_clear_runtime_stats()
        yield
        deopts = cinderjit.get_and_clear_runtime_stats()["deopt"]

        def deopt_matches_spec(deopt, spec):
            for key, val in spec:
                if deopt.get(key) != val:
                    return False
            return True

        found_specs = Counter()
        for deopt in deopts:
            # The data from Cinder is type-segregated for Scuba; flatten it
            # into one dict.
            deopt = {**deopt["normal"], **deopt["int"]}

            # Ignore deopt events not from this file (the unittest machinery
            # often triggers a few).
            if deopt["filename"] != __file__:
                continue

            for spec in deopt_specs:
                if deopt_matches_spec(deopt, spec):
                    found_specs[spec] += deopt["count"]
                    break
            else:
                self.fail(f"Deopt event '{deopt}' doesn't match any given specs")

        for spec, expected_count in deopt_specs.items():
            found_count = found_specs[spec]
            self.assertEqual(found_count, expected_count, f"Deopt spec {spec}")


class BinaryOpTests(ProfileTest):
    def test_long_power(self):
        def do_pow(a, b):
            return a**b

        self.assertEqual(do_pow(2, 2), 4)

        if TESTING:
            with self.assertDeopts({}):
                self.assertEqual(do_pow(2, 8), 256)


class NewThreadTests(ProfileTest):
    def test_create_thread(self):
        x = 5

        def thread_body():
            nonlocal x
            x = 10

        self.assertEqual(x, 5)
        t = threading.Thread(target=thread_body)
        t.start()
        t.join()
        self.assertEqual(x, 10)

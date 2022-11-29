# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

# NOTE: This test is run normally as part of the regular test suite, and it is
# also run again separately, using profile data recorded for consumption by the
# JIT. That means all of the tests in here are targeted at various JIT features
# that only activate in the presence of profile data.

import unittest


class BinaryOpTests(unittest.TestCase):
    def test_long_power(self):
        def do_pow(a, b):
            return a **b
        self.assertEqual(do_pow(2, 8), 256)

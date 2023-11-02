# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

import gc
import unittest

try:
    import cinder
except ImportError:
    cinder = None

# TODO(T168696028): Remove this once parallel gc functions are moved to the
# cinderx module
@unittest.skipIf(cinder is None, "Tests CinderX features")
class ParallelGCTests(unittest.TestCase):
    def setUp(self):
        self.par_gc_settings = cinder.get_parallel_gc_settings()

    def tearDown(self):
        if self.par_gc_settings is not None:
            cinder.disable_parallel_gc()
            cinder.enable_parallel_gc(
                self.par_gc_settings["min_generation"],
                self.par_gc_settings["num_threads"],
            )
        else:
            cinder.disable_parallel_gc()

    def test_get_settings_when_disabled(self):
        cinder.disable_parallel_gc()
        self.assertEqual(cinder.get_parallel_gc_settings(), None)

    def test_get_settings_when_enabled(self):
        cinder.disable_parallel_gc()
        cinder.enable_parallel_gc(2, 8)
        settings = cinder.get_parallel_gc_settings()
        expected = {
            "min_generation": 2,
            "num_threads": 8,
        }
        self.assertEqual(settings, expected)

    def test_set_invalid_generation(self):
        with self.assertRaisesRegex(ValueError, "invalid generation"):
            cinder.enable_parallel_gc(4, 8)

    def test_set_invalid_num_threads(self):
        with self.assertRaisesRegex(ValueError, "invalid num_threads"):
            cinder.enable_parallel_gc(2, -1)

    def test_collection(self):
        collected = False

        class Cycle:
            def __init__(self):
                self.ref = self

            def __del__(self):
                nonlocal collected
                collected = True

        cinder.enable_parallel_gc()
        gc.collect()
        c = Cycle()
        del c
        gc.collect()
        self.assertTrue(collected)


if __name__ == "__main__":
    unittest.main()

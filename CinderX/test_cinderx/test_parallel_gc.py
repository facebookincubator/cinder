# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

import gc
import test.test_gc
import unittest

try:
    import cinder
except ImportError:
    # TODO(T168696028): Remove this once parallel gc functions are moved to the
    # cinderx module
    raise unittest.SkipTest("Tests CinderX features")


def _restore_parallel_gc(settings):
    cinder.disable_parallel_gc()
    if settings is not None:
        cinder.enable_parallel_gc(
            settings["min_generation"],
            settings["num_threads"],
        )


class ParallelGCAPITests(unittest.TestCase):
    def setUp(self):
        self.old_par_gc_settings = cinder.get_parallel_gc_settings()
        cinder.disable_parallel_gc()

    def tearDown(self):
        _restore_parallel_gc(self.old_par_gc_settings)

    def test_get_settings_when_disabled(self):
        self.assertEqual(cinder.get_parallel_gc_settings(), None)

    def test_get_settings_when_enabled(self):
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


# Run all the GC tests with parallel GC enabled


class ParallelGCTests(test.test_gc.GCTests):
    pass


class ParallelGCCallbackTests(test.test_gc.GCCallbackTests):
    @unittest.skip("Tests implementation details of serial collector")
    def test_refcount_errors(self):
        pass


class ParallelGCFinalizationTests(test.test_gc.PythonFinalizationTests):
    pass


def setUpModule():
    test.test_gc.setUpModule()

    global old_par_gc_settings
    old_par_gc_settings = cinder.get_parallel_gc_settings()
    cinder.enable_parallel_gc(0, 8)


def tearDownModule():
    test.test_gc.tearDownModule()

    global old_par_gc_settings
    _restore_parallel_gc(old_par_gc_settings)


if __name__ == "__main__":
    unittest.main()

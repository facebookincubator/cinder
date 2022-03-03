# This script exercises multi-threaded JIT compilation by importing a bunch of
# things and then calling a magic function on the cinderjit module to do a
# multi-threaded (re)-compile every JITable function encountered.
#
# To be useful this script must be run with:
#   -X jit-test-multithreaded-compile
#   -X jit-batch-compile-workers=<some number > 1>
#   -X jit
#
# To be most useful this script should be run with a TSAN build, in which case
# it will show up most if not all threading errors. Without TSAN there is still
# a fair chance of a crash if something is broken.

import sys
import unittest
import warnings

from cinder import StrictModule


def run_static_tests():
    import test_compiler.test_static as test_static
    from test_compiler.test_static.common import StaticTestBase
    from test_compiler.test_static.compile import init_xxclassloader

    CODE_SAMPLES_IN_MODULE = []
    CODE_SAMPLES_IN_STRICT_MODULE = []
    CODE_SAMPLES_RUN = []

    class CompileCaptureOverrides:
        def _finalize_module(self, name, mod_dict=None):
            pass

        def _in_module(self, *args):
            d, m = super()._in_module(*args)
            args = list(args)
            args[0] = d["__name__"]
            CODE_SAMPLES_IN_MODULE.append(args)
            return d, m

        def _in_strict_module(self, *args):
            d, m = super()._in_strict_module(*args)
            args = list(args)
            args[0] = d["__name__"]
            CODE_SAMPLES_IN_STRICT_MODULE.append(args)
            return d, m

        def _run_code(self, *args):
            modname, r = super()._run_code(*args)
            args = list(args)
            args[2] = modname
            CODE_SAMPLES_RUN.append(args)
            return modname, r

    class StaticCompilationTests(
            CompileCaptureOverrides,
            test_static.StaticCompilationTests):
        @classmethod
        def tearDownClass(cls):
            pass

    class StaticRuntimeTests(
            CompileCaptureOverrides,
            test_static.StaticRuntimeTests):
        pass

    suite = unittest.TestLoader().loadTestsFromTestCase(StaticCompilationTests)
    unittest.TextTestRunner().run(suite)

    suite = unittest.TestLoader().loadTestsFromTestCase(StaticRuntimeTests)
    unittest.TextTestRunner().run(suite)

    print("Regenerate Static Python tests Python code")
    class StaticTestCodeRegenerator(StaticTestBase):
        def __init__(self):
            init_xxclassloader()

            for args in CODE_SAMPLES_IN_MODULE:
                self._in_module(*args)

            for args in CODE_SAMPLES_IN_STRICT_MODULE:
                self._in_strict_module(*args)

            for args in CODE_SAMPLES_RUN:
                _, d = self._run_code(*args)
                sys.modules[args[2]] = d

    StaticTestCodeRegenerator()


def main():
    import cinderjit

    # The Cinder JIT tests will generally introduce functions which exercise JIT
    # compilation corner-cases.
    import test_cinderjit

    # The Cinder Static Python tests introduce functions which exercise features
    # of the JIT compiler for Static Python. These tests need to be executed as
    # many functions are created dynamically from strings.
    run_static_tests()

    cinderjit.multithreaded_compile_test()


if __name__ == "__main__":
    main()

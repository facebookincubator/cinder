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

if __name__ == "__main__":
    import cinderjit
    import unittest
    import warnings

    try:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            from .test_compiler import test_static
    except ImportError:
        import test_compiler.test_static

    # The Cinder JIT tests will generally introduce functions which exercise JIT
    # compilation corner-cases.
    import test_cinderjit

    # The Cinder Static Python tests introduce functions which exercise features
    # of the JIT compiler for Static Python. These tests need to be executed as
    # many functions are created dynamically from strings.
    suite = unittest.TestLoader().loadTestsFromModule(test_compiler.test_static)
    unittest.TextTestRunner().run(suite)

    cinderjit.test_multithreaded_compile()

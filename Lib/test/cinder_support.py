# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

import multiprocessing
import sys
import tempfile
import unittest
from contextlib import contextmanager
from pathlib import Path

try:
    from cinderjit import force_compile, is_jit_compiled, jit_frame_mode

    CINDERJIT_ENABLED = True
except ImportError:

    def is_jit_compiled(f):
        return False

    def force_compile(f):
        return False

    CINDERJIT_ENABLED = False


try:
    import cinder

    def hasCinderX():
        return True

except ImportError:

    def hasCinderX():
        return False


def get_await_stack(coro):
    """Return the chain of coroutines reachable from coro via its awaiter"""

    stack = []
    awaiter = cinder._get_coro_awaiter(coro)
    while awaiter is not None:
        stack.append(awaiter)
        awaiter = cinder._get_coro_awaiter(awaiter)
    return stack


def verify_stack(testcase, stack, expected):
    n = len(expected)
    frames = stack[-n:]
    testcase.assertEqual(len(frames), n, "Callstack had less frames than expected")

    for actual, expected in zip(frames, expected):
        testcase.assertTrue(
            actual.endswith(expected),
            f"The actual frame {actual} doesn't refer to the expected function {expected}",
        )


def skipUnderJIT(reason):
    if CINDERJIT_ENABLED:
        return unittest.skip(reason)
    return unittest.case._id


def skipUnlessJITEnabled(reason):
    if not CINDERJIT_ENABLED:
        return unittest.skip(reason)
    return unittest.case._id


def failUnlessJITCompiled(func):
    """
    Fail a test if the JIT is enabled but the test body wasn't JIT-compiled.
    """
    if not CINDERJIT_ENABLED:
        return func

    try:
        # force_compile raises a RuntimeError if compilation fails. If it does,
        # defer raising an exception to when the decorated function runs.
        force_compile(func)
    except RuntimeError as re:
        if re.args == ("PYJIT_RESULT_NOT_ON_JITLIST",):
            # We generally only run tests with a jitlist under
            # Tools/scripts/jitlist_bisect.py. In that case, we want to allow
            # the decorated function to run under the interpreter to determine
            # if it's the function the JIT is handling incorrectly.
            return func

        # re is cleared at the end of the except block but we need the value
        # when wrapper() is eventually called.
        exc = re

        def wrapper(*args):
            raise RuntimeError(
                f"JIT compilation of {func.__qualname__} failed with {exc}"
            )

        return wrapper

    return func


# This is pretty long because ASAN + JIT + subprocess + the Python compiler can
# be pretty slow in CI.
SUBPROCESS_TIMEOUT_SEC = 5


@contextmanager
def temp_sys_path():
    with tempfile.TemporaryDirectory() as tmpdir:
        _orig_sys_modules = sys.modules
        sys.modules = _orig_sys_modules.copy()
        _orig_sys_path = sys.path[:]
        sys.path.insert(0, tmpdir)
        try:
            yield Path(tmpdir)
        finally:
            sys.path[:] = _orig_sys_path
            sys.modules = _orig_sys_modules


def runInSubprocess(func):
    queue = multiprocessing.Queue()

    def wrapper(queue, *args):
        result = func(*args)
        queue.put(result, timeout=SUBPROCESS_TIMEOUT_SEC)

    def wrapped(*args):
        p = multiprocessing.Process(target=wrapper, args=(queue, *args))
        p.start()
        value = queue.get(timeout=SUBPROCESS_TIMEOUT_SEC)
        p.join(timeout=SUBPROCESS_TIMEOUT_SEC)
        return value

    return wrapped

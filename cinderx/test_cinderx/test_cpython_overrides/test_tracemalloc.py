import contextlib
import os
import sys
import tracemalloc
import unittest
from unittest.mock import patch
from test.support.script_helper import (assert_python_ok, assert_python_failure,
                                        interpreter_requires_environment)
from test import support
from test.support import os_helper

try:
    import _testcapi
except ImportError:
    _testcapi = None

try:
    import cinderjit
except:
    cinderjit = None

EMPTY_STRING_SIZE = sys.getsizeof(b'')
INVALID_NFRAME = (-1, 2**30)


def get_frames(nframe, lineno_delta):
    frames = []
    frame = sys._getframe(1)
    for index in range(nframe):
        code = frame.f_code
        lineno = frame.f_lineno + lineno_delta
        frames.append((code.co_filename, lineno))
        lineno_delta = 0
        frame = frame.f_back
        if frame is None:
            break
    return tuple(frames)

def allocate_bytes(size):
    nframe = tracemalloc.get_traceback_limit()
    bytes_len = (size - EMPTY_STRING_SIZE)
    frames = get_frames(nframe, 1)
    data = b'x' * bytes_len
    return data, tracemalloc.Traceback(frames, min(len(frames), nframe))

def create_snapshots():
    traceback_limit = 2

    # _tracemalloc._get_traces() returns a list of (domain, size,
    # traceback_frames) tuples. traceback_frames is a tuple of (filename,
    # line_number) tuples.
    raw_traces = [
        (0, 10, (('a.py', 2), ('b.py', 4)), 3),
        (0, 10, (('a.py', 2), ('b.py', 4)), 3),
        (0, 10, (('a.py', 2), ('b.py', 4)), 3),

        (1, 2, (('a.py', 5), ('b.py', 4)), 3),

        (2, 66, (('b.py', 1),), 1),

        (3, 7, (('<unknown>', 0),), 1),
    ]
    snapshot = tracemalloc.Snapshot(raw_traces, traceback_limit)

    raw_traces2 = [
        (0, 10, (('a.py', 2), ('b.py', 4)), 3),
        (0, 10, (('a.py', 2), ('b.py', 4)), 3),
        (0, 10, (('a.py', 2), ('b.py', 4)), 3),

        (2, 2, (('a.py', 5), ('b.py', 4)), 3),
        (2, 5000, (('a.py', 5), ('b.py', 4)), 3),

        (4, 400, (('c.py', 578),), 1),
    ]
    snapshot2 = tracemalloc.Snapshot(raw_traces2, traceback_limit)

    return (snapshot, snapshot2)

def frame(filename, lineno):
    return tracemalloc._Frame((filename, lineno))

def traceback(*frames):
    return tracemalloc.Traceback(frames)

def traceback_lineno(filename, lineno):
    return traceback((filename, lineno))

def traceback_filename(filename):
    return traceback_lineno(filename, 0)

class CinderX_TestTracemallocEnabled(unittest.TestCase):
    def setUp(self):
        if tracemalloc.is_tracing():
            self.skipTest("tracemalloc must be stopped before the test")

        tracemalloc.start(1)

    def tearDown(self):
        tracemalloc.stop()

    @unittest.skipIf(
        cinderjit and cinderjit.is_inline_cache_stats_collection_enabled(),
        "#TODO(T150421262): Traced memory does not work well with JIT's inline cache stats collection.",
    )
    def test_get_traced_memory(self):
        # Python allocates some internals objects, so the test must tolerate
        # a small difference between the expected size and the real usage
        max_error = 2048

        # allocate one object
        obj_size = 1024 * 1024
        tracemalloc.clear_traces()
        obj, obj_traceback = allocate_bytes(obj_size)
        size, peak_size = tracemalloc.get_traced_memory()
        self.assertGreaterEqual(size, obj_size)
        self.assertGreaterEqual(peak_size, size)

        self.assertLessEqual(size - obj_size, max_error)
        self.assertLessEqual(peak_size - size, max_error)

        # destroy the object
        obj = None
        size2, peak_size2 = tracemalloc.get_traced_memory()
        self.assertLess(size2, size)
        self.assertGreaterEqual(size - size2, obj_size - max_error)
        self.assertGreaterEqual(peak_size2, peak_size)

        # clear_traces() must reset traced memory counters
        tracemalloc.clear_traces()
        self.assertEqual(tracemalloc.get_traced_memory(), (0, 0))

        # allocate another object
        obj, obj_traceback = allocate_bytes(obj_size)
        size, peak_size = tracemalloc.get_traced_memory()
        self.assertGreaterEqual(size, obj_size)

        # stop() also resets traced memory counters
        tracemalloc.stop()
        self.assertEqual(tracemalloc.get_traced_memory(), (0, 0))

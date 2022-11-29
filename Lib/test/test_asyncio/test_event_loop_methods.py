# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

import _testcapi
import asyncio
import unittest


class EventLoopMethodsTestCase(unittest.TestCase):
    def test_call_soon_calls(self):
        get_debug_called = False

        args_info = []
        capture_arg = False
        class Loop:
            def get_debug_impl(self):
                nonlocal get_debug_called
                get_debug_called = True
                return False

            def call_soon_impl(self, args, kwargs):
                if capture_arg:
                    self.captured = (args, kwargs)
                args_info.append((id(args), id(kwargs)))
        Loop.get_debug = _testcapi.make_get_debug_descriptor(Loop)
        Loop.call_soon = _testcapi.make_call_soon_descriptor(Loop)

        loop = Loop()
        loop.__class__
        fut = asyncio.Future(loop=loop)  # calls get_debug
        self.assertTrue(get_debug_called)

        fut.set_result(10)
        fut.add_done_callback(lambda *args: 0)  # calls call_soon
        fut.add_done_callback(lambda *args: 0)  # calls call_soon
        # verify that args were represented by the same tuple/dict in both cases
        # as it was not leaked
        self.assertEqual(args_info[0], args_info[1])
        capture_arg = True
        args_info = []
        fut.add_done_callback(lambda *args: 0)  # calls call_soon
        fut.add_done_callback(lambda *args: 0)  # calls call_soon
        # verify that args were represented by different tuple/dict in both cases
        # as it were captured on the first call
        self.assertNotEqual(args_info[0][0], args_info[1][0])
        self.assertNotEqual(args_info[0][1], args_info[1][1])

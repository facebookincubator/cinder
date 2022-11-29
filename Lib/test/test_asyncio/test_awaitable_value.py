# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

import asyncio
import _asyncio
import unittest

@unittest.skipUnless(hasattr(_asyncio, 'AwaitableValue'), 'requires _asyncio.AwaitableValue')
class AwaitableValueTest(unittest.TestCase):
    def setUp(self) -> None:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        self.loop = loop

    def tearDown(self):
        self.loop.close()
        asyncio.set_event_loop_policy(None)

    def test_async_lazy_value(self):
        async def main():
            v = await _asyncio.create_awaitable_value(42)
            self.assertEqual(v, 42)

            v = await _asyncio.AwaitableValue(100)
            self.assertEqual(v, 100)

        self.loop.run_until_complete(main())

    @unittest.skipUnless(hasattr(_asyncio, '_start_immediate'), 'requires _asyncio._start_immediate')
    def test_start_immediate_finish_eagerly(self):
        async def run():
            return 42

        val = _asyncio._start_immediate(run(), self.loop)
        self.assertIsInstance(val, _asyncio.AwaitableValue)
        self.assertEqual(val.value, 42)

    @unittest.skipUnless(hasattr(_asyncio, '_start_immediate'), 'requires _asyncio._start_immediate')
    def test_start_immediate_deferred(self):
        started = False
        finished = False
        async def run():
            nonlocal started, finished
            started = True
            await asyncio.sleep(0)
            finished = True
            return 42

        async def wait(t):
            return await t

        val = _asyncio._start_immediate(run(), self.loop)
        self.assertTrue(started)
        self.assertFalse(finished)
        self.assertNotIsInstance(val, _asyncio.AwaitableValue)
        res = self.loop.run_until_complete(wait(val))
        self.assertTrue(finished)
        self.assertEqual(res, 42)

    @unittest.skipUnless(hasattr(_asyncio, '_start_immediate'), 'requires _asyncio._start_immediate')
    def test_start_immediate_exception_eager_part(self):
        class E(Exception): pass
        async def run():
            raise E
        try:
            _asyncio._start_immediate(run(), self.loop)
        except E:
            pass
        else:
            self.fail("Exception expected")

    @unittest.skipUnless(hasattr(_asyncio, '_start_immediate'), 'requires _asyncio._start_immediate')
    def test_start_immediate_exception_deferred_part(self):
        class E(Exception): pass
        async def run():
            await asyncio.sleep(0)
            raise E

        async def wait(t):
            return await t

        val = _asyncio._start_immediate(run(), self.loop)
        self.assertNotIsInstance(val, _asyncio.AwaitableValue)
        try:
            self.loop.run_until_complete(wait(val))
        except E:
            pass
        else:
            self.fail("Exception expected")

    @unittest.skipUnless(hasattr(_asyncio, '_start_immediate'), 'requires _asyncio._start_immediate')
    def test_start_immediate_no_loop(self):
        async def run():
            await asyncio.sleep(0)
            return 10
        async def main():
            val = _asyncio._start_immediate(run())
            self.assertIs(val._loop, asyncio.get_running_loop())
        asyncio.run(main())

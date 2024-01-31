import inspect
import types
import unittest

from test.support import gc_collect

from test.support.import_helper import import_module

asyncio = import_module("asyncio")


_no_default = object()


class AwaitException(Exception):
    pass

class CinderX_AsyncGenAsyncioTest(unittest.TestCase):

    def setUp(self):
        self.loop = asyncio.new_event_loop()
        asyncio.set_event_loop(None)

    def tearDown(self):
        self.loop.close()
        self.loop = None
        asyncio.set_event_loop_policy(None)

    def test_async_gen_asyncio_gc_aclose_09(self):
        DONE = 0

        async def gen():
            nonlocal DONE
            try:
                while True:
                    yield 1
            finally:
                await asyncio.sleep(0.01)
                await asyncio.sleep(0.01)
                DONE = 1

        async def run():
            g = gen()
            await g.__anext__()
            await g.__anext__()
            del g
            gc_collect()  # For PyPy or other GCs.

            # CinderX: This is changed from asyncio.sleep(1)
            await asyncio.sleep(0.1)

        self.loop.run_until_complete(run())
        self.assertEqual(DONE, 1)

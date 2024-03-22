import contextlib
import copy
import inspect
import pickle
import sys
import types
import unittest
import warnings

from test import cinder_support, support
from test.support import import_helper, maybe_get_event_loop_policy, warnings_helper
from test.support.script_helper import assert_python_ok

if cinder_support.hasCinderX():
    import cinder


def run_async(coro):
    assert coro.__class__ in {types.GeneratorType, types.CoroutineType}

    buffer = []
    result = None
    while True:
        try:
            buffer.append(coro.send(None))
        except StopIteration as ex:
            result = ex.args[0] if ex.args else None
            break
    return buffer, result


class CoroutineAwaiterTest(unittest.TestCase):
    def test_basic_await(self):
        async def coro():
            self.assertIs(cinder._get_coro_awaiter(coro_obj), awaiter_obj)
            return "success"

        async def awaiter():
            return await coro_obj

        coro_obj = coro()
        awaiter_obj = awaiter()
        self.assertIsNone(cinder._get_coro_awaiter(coro_obj))
        self.assertEqual(run_async(awaiter_obj), ([], "success"))
        self.assertIsNone(cinder._get_coro_awaiter(coro_obj))
        del awaiter_obj
        self.assertIsNone(cinder._get_coro_awaiter(coro_obj))

    class FakeFuture:
        def __await__(self):
            return iter(["future"])

    def test_eager_await(self):
        async def awaitee():
            nonlocal awaitee_frame
            awaitee_frame = sys._getframe()
            self.assertIsNone(cinder._get_frame_gen(awaitee_frame))
            await self.FakeFuture()

            # Our caller verified our awaiter while we were suspended; ensure
            # it's still set while running.
            awaitee_obj = cinder._get_frame_gen(awaitee_frame)
            self.assertIsInstance(awaitee_obj, types.CoroutineType)
            self.assertIs(cinder._get_coro_awaiter(awaitee_obj), awaiter_obj)
            return "good!"

        async def awaiter():
            nonlocal awaiter_frame
            awaiter_frame = sys._getframe()
            return await awaitee()

        awaitee_frame = None
        awaiter_frame = None
        awaiter_obj = awaiter()
        self.assertIsNone(awaiter_frame)
        self.assertIsNone(awaitee_frame)

        v1 = awaiter_obj.send(None)
        self.assertEqual(v1, "future")
        self.assertIsInstance(awaitee_frame, types.FrameType)
        self.assertIsInstance(awaiter_frame, types.FrameType)
        self.assertIs(cinder._get_frame_gen(awaiter_frame), awaiter_obj)
        self.assertIsNone(cinder._get_coro_awaiter(awaiter_obj))

        awaitee_obj = cinder._get_frame_gen(awaitee_frame)
        self.assertIsInstance(awaitee_obj, types.CoroutineType)
        self.assertIs(cinder._get_coro_awaiter(awaitee_obj), awaiter_obj)

        with self.assertRaises(StopIteration) as cm:
            awaiter_obj.send(None)
        self.assertEqual(cm.exception.value, "good!")
        self.assertIsNone(cinder._get_coro_awaiter(awaitee_obj))

        # Run roughly the same sequence again, with awaiter() executed eagerly.
        async def awaiter2():
            return await awaiter()

        awaitee_frame = None
        awaiter_frame = None
        awaiter2_obj = awaiter2()
        self.assertIsNone(cinder._get_coro_awaiter(awaiter2_obj))
        awaiter2_obj.send(None)

        self.assertIsInstance(awaitee_frame, types.FrameType)
        self.assertIsInstance(awaiter_frame, types.FrameType)
        awaitee_obj = cinder._get_frame_gen(awaitee_frame)
        awaiter_obj = cinder._get_frame_gen(awaiter_frame)
        self.assertIsInstance(awaitee_obj, types.CoroutineType)
        self.assertIsInstance(awaiter_obj, types.CoroutineType)
        self.assertIs(cinder._get_coro_awaiter(awaitee_obj), awaiter_obj)
        self.assertIs(cinder._get_coro_awaiter(awaiter_obj), awaiter2_obj)
        self.assertIsNone(cinder._get_coro_awaiter(awaiter2_obj))

        with self.assertRaises(StopIteration) as cm:
            awaiter2_obj.send(None)
        self.assertEqual(cm.exception.value, "good!")
        self.assertIsNone(cinder._get_coro_awaiter(awaitee_obj))
        self.assertIsNone(cinder._get_coro_awaiter(awaiter_obj))

    def test_coro_outlives_awaiter(self):
        async def coro():
            await self.FakeFuture()

        async def awaiter(cr):
            await cr

        coro_obj = coro()
        self.assertIsNone(cinder._get_coro_awaiter(coro_obj))
        awaiter_obj = awaiter(coro_obj)
        self.assertIsNone(cinder._get_coro_awaiter(coro_obj))

        v1 = awaiter_obj.send(None)
        self.assertEqual(v1, "future")
        self.assertIs(cinder._get_coro_awaiter(coro_obj), awaiter_obj)

        del awaiter_obj
        self.assertIsNone(cinder._get_coro_awaiter(coro_obj))

    def test_async_gen_doesnt_set(self):
        async def coro():
            await self.FakeFuture()

        async def async_gen(cr):
            await cr
            yield "hi"

        # ci_cr_awaiter should always be None or a coroutine object, and async
        # generators aren't coroutines.
        coro_obj = coro()
        self.assertIsNone(cinder._get_coro_awaiter(coro_obj))
        agen = async_gen(coro_obj)
        self.assertIsNone(cinder._get_coro_awaiter(coro_obj))

        v1 = agen.asend(None).send(None)
        self.assertEqual(v1, "future")
        self.assertIsNone(cinder._get_coro_awaiter(coro_obj))

        del agen
        self.assertIsNone(cinder._get_coro_awaiter(coro_obj))


class TestEagerExecution(unittest.TestCase):
    def setUp(self):
        self._asyncio = import_helper.import_module("asyncio")
        policy = maybe_get_event_loop_policy()
        self.addCleanup(lambda: self._asyncio.set_event_loop_policy(policy))

    async def _raise_IndexError_eager(self, x=None):
        try:
            raise IndexError
        except:
            pass

    async def _raise_IndexError_suspended(self, x=None):
        try:
            raise IndexError
        except:
            await self._asyncio.sleep(0)

    def _check(self, expected_coro, actual_coro):
        def run(coro):

            try:
                self._asyncio.run(coro)
                self.fail("Exception expected")
            except RuntimeError as e:
                return type(e.__context__)

        self.assertEqual(run(expected_coro), run(actual_coro))

    def _do_test_exc_handler(self, f):
        async def actual_1():
            try:

                raise ValueError
            except:
                await f()
                raise RuntimeError

        async def expected_1():

            try:
                raise ValueError
            except:

                coro = f()
                await coro
                raise RuntimeError

        async def actual_2():

            try:

                raise ValueError
            except:

                await f(x=1)
                raise RuntimeError

        async def expected_2():

            try:

                raise ValueError

            except:
                coro = f(x=1)
                await coro

                raise RuntimeError

        self._check(expected_1(), actual_1())
        self._check(expected_2(), actual_2())

    def _do_test_no_err(self, f):
        async def actual_1():
            await f()
            raise RuntimeError

        async def expected_1():
            coro = f()
            await coro
            raise RuntimeError

        async def actual_2():

            await f(x=1)

            raise RuntimeError

        async def expected_2():

            coro = f(x=1)

            await coro
            raise RuntimeError

        self._check(expected_1(), actual_1())

        self._check(expected_2(), actual_2())

    def test_eager_await_no_error_eager(self):

        self._do_test_no_err(self._raise_IndexError_eager)

    def test_suspended_await_no_error_suspended(self):
        self._do_test_no_err(self._raise_IndexError_suspended)

    def test_suspended_await_in_catch_eager(self):
        self._do_test_exc_handler(self._raise_IndexError_eager)

    def test_suspended_await_in_catch_suspended(self):
        self._do_test_exc_handler(self._raise_IndexError_suspended)


if __name__ == "__main__":
    unittest.main()

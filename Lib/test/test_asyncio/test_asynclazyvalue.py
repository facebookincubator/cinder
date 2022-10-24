# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
import asyncio
import cinder
import inspect
import unittest
from _asyncio import AsyncLazyValue
from functools import wraps
from time import time

from test.support.cinder import get_await_stack


def async_test(f):
    assert inspect.iscoroutinefunction(f)

    @wraps(f)
    def impl(*args, **kwargs):
        asyncio.run(f(*args, **kwargs))

    return impl


class AsyncLazyValueCoroTest(unittest.TestCase):
    def setUp(self) -> None:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        self.loop = loop

    def tearDown(self):
        self.loop.close()
        asyncio.set_event_loop_policy(None)

    @async_test
    async def test_close_not_started(self) -> None:
        async def g():
            pass

        # close non-started asynclazy value is no-op
        (AsyncLazyValue(g).__await__()).close()
        pass

    @async_test
    async def test_close_normal(self) -> None:
        async def g(fut):
            await fut

        # close non-started asynclazy value is no-op
        alv = AsyncLazyValue(g, asyncio.Future())
        i = alv.__await__()  # get main coro
        i1 = alv.__await__()  # get future
        i.send(None)
        i.close()
        try:
            # check future has gotten generator exit
            next(i1)
            self.fail("should not be here")
        except GeneratorExit:
            pass

    @async_test
    async def test_close_subgen_error(self) -> None:
        class Exc(Exception):
            pass

        async def g(fut):
            try:
                await fut
            except GeneratorExit:
                raise Exc("error")

        alv = AsyncLazyValue(g, asyncio.Future())
        i = alv.__await__()  # get main coro
        i1 = alv.__await__()  # get future
        i.send(None)
        try:
            i.close()
            self.fail("Error expected")
        except Exc:
            pass
        try:
            # check future has gotten Exc exit
            next(i1)
            self.fail("should not be here")
        except Exc as e:
            self.assertIs(type(e.__context__), GeneratorExit)
            pass

    @async_test
    async def test_throw_not_started(self) -> None:
        class Exc(Exception):
            pass

        async def g():
            pass

        alv = AsyncLazyValue(g)
        c = alv.__await__()
        try:
            c.throw(Exc)
            self.fail("Error expected")
        except Exc:
            pass

    @async_test
    async def test_throw_handled_in_subgen(self) -> None:
        class Exc(Exception):
            pass

        async def g(fut):
            try:
                await fut
            except Exc:
                return 10

        alv = AsyncLazyValue(g, asyncio.Future())
        c = alv.__await__()
        c1 = alv.__await__()
        c.send(None)
        try:
            c.throw(Exc)
        except StopIteration as e:
            self.assertEqual(e.args[0], 10)

        try:
            next(c1)
            self.fail("StopIteration expected")
        except StopIteration as e:
            self.assertEqual(e.args[0], 10)

    @async_test
    async def test_throw_unhandled_in_subgen(self) -> None:
        class Exc(Exception):
            pass

        async def g(fut):
            try:
                await fut
            except Exc:
                raise IndexError

        alv = AsyncLazyValue(g, asyncio.Future())
        c = alv.__await__()
        c1 = alv.__await__()
        c.send(None)
        try:
            c.throw(Exc)
            self.fail("IndexError expected")
        except IndexError as e:
            self.assertTrue(type(e.__context__) is Exc)
        try:
            next(c1)
        except IndexError as e:
            self.assertTrue(type(e.__context__) is Exc)

    @async_test
    async def test_get_awaiter(self) -> None:
        async def g(f):
            return await f

        async def h(f):
            return await AsyncLazyValue(g, f)

        coro = None
        await_stack = None

        async def f():
            nonlocal coro, await_stack
            # Force suspension. Otherwise the entire execution is eager and
            # awaiter is never set.
            await asyncio.sleep(0)
            await_stack = get_await_stack(coro)
            return 100

        coro = f()
        h_coro = h(coro)
        res = await h_coro
        self.assertEqual(res, 100)
        # awaiter of f is the coroutine running g. That's created by the
        # AsyncLazyValue machinery, so we can't check the awaiter's identity
        # directly, only that it corresponds to g
        self.assertIs(await_stack[0].cr_code, g.__code__)
        self.assertIs(await_stack[1], h_coro)

    @async_test
    async def test_get_awaiter_from_gathered(self) -> None:
        async def g(f):
            return await f

        async def h(f):
            return await AsyncLazyValue(g, f)

        async def gatherer(c0, c1):
            return await asyncio.gather(c0, c1)

        coros = [None, None]
        await_stacks = [None, None]

        async def f(idx, res):
            nonlocal coros, await_stacks
            # Force suspension. Otherwise the entire execution is eager and
            # awaiter is never set.
            await asyncio.sleep(0)
            await_stacks[idx] = get_await_stack(coros[idx])
            return res

        coros[0] = f(0, 10)
        coros[1] = f(1, 20)
        h0_coro = h(coros[0])
        h1_coro = h(coros[1])
        gatherer_coro = gatherer(h0_coro, h1_coro)
        results = await gatherer_coro
        self.assertEqual(results[0], 10)
        self.assertEqual(results[1], 20)
        # awaiter of f is the coroutine running g. That's created by the
        # AsyncLazyValue machinery, so we can't check the awaiter's identity
        # directly, only that it corresponds to g
        self.assertIs(await_stacks[0][0].cr_code, g.__code__)
        self.assertIs(await_stacks[0][1], h0_coro)
        self.assertIs(await_stacks[0][2], gatherer_coro)
        self.assertIs(await_stacks[1][0].cr_code, g.__code__)
        self.assertIs(await_stacks[1][1], h1_coro)
        self.assertIs(await_stacks[1][2], gatherer_coro)


class AsyncLazyValueTest(unittest.TestCase):
    def setUp(self) -> None:
        self.events = []
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        self.cancelled = asyncio.Event()
        self.coro_running = asyncio.Event()
        self.loop = loop

    def tearDown(self) -> None:
        self.loop.close()
        asyncio.set_event_loop_policy(None)

    def log(self, msg: str) -> None:
        self.events.append(msg)

    @async_test
    async def test_ok_path(self) -> None:
        async def async_func(arg1: int, arg2: int) -> int:
            return arg1 + arg2

        alv = AsyncLazyValue(async_func, 1000, 2000)
        self.assertEqual(await alv, 3000)

    @async_test
    async def test_two_tasks(self) -> None:
        call_count = 0

        async def async_func(arg1: int, arg2: int) -> int:
            nonlocal call_count
            call_count += 1
            return arg1 + arg2

        alv = AsyncLazyValue(async_func, 1, 2)
        ta = asyncio.ensure_future(alv)
        tb = asyncio.ensure_future(alv)

        res = await asyncio.gather(ta, tb)
        self.assertEqual(res, [3, 3])

        # should only be called once
        self.assertEqual(call_count, 1)

    @async_test
    async def test_single_task_cancelled(self) -> None:
        """
        Should raise CancelledError()
        """

        async def async_func(arg1: int, arg2: int) -> int:
            self.log("ran-coro")
            self.coro_running.set()
            await asyncio.sleep(3)
            raise RuntimeError("async_func never got cancelled")

        async def async_cancel(task: asyncio.Task, alv: AsyncLazyValue) -> None:
            await asyncio.wait_for(self.coro_running.wait(), timeout=3)
            self.log("cancelling")
            task.cancel()
            self.log("cancelled")

        alv = AsyncLazyValue(async_func, 1, 2)
        ta = asyncio.ensure_future(alv)
        tc = asyncio.ensure_future(async_cancel(ta, alv))

        ta_result, tc_result = await asyncio.gather(ta, tc, return_exceptions=True)
        self.assertSequenceEqual(self.events, ["ran-coro", "cancelling", "cancelled"])
        self.assertTrue(isinstance(ta_result, asyncio.CancelledError))
        self.assertEqual(tc_result, None)

    @async_test
    async def test_two_tasks_parent_cancelled(self) -> None:
        """
        Creates two tasks from the same AsyncLazyValue. Cancels the task which
        calls the coroutine first.
        """

        async def async_func(arg1: int, arg2: int) -> int:
            self.log("ran-coro")
            await asyncio.sleep(3)
            self.log("completed-coro")
            raise RuntimeError("async_func never got cancelled")

        async def async_cancel(task: asyncio.Task, alv: AsyncLazyValue) -> None:
            start = time()
            while alv._awaiting_tasks < 1:
                # Sleep until both tasks are awaiting on the future (one of them
                # is awaiting on async_func, so we only wait until the count
                # is 1). Also, we timeout if we wait more than 3 seconds
                now = time()
                if now - start > 3:
                    raise RuntimeError(
                        "cannot cancel since the tasks are not waiting on the future"
                    )
                await asyncio.sleep(0)

            self.log("cancelling")
            task.cancel()
            self.log("cancelled")

        alv = AsyncLazyValue(async_func, 1, 2)
        ta = asyncio.ensure_future(alv)
        tb = asyncio.ensure_future(alv)
        # pyre-fixme[6]: Expected `Task[Any]` for 1st param but got `Future[Any]`.
        tc = asyncio.ensure_future(async_cancel(ta, alv))

        ta_result, tb_result, tc_result = await asyncio.gather(
            ta, tb, tc, return_exceptions=True
        )

        self.assertSequenceEqual(self.events, ["ran-coro", "cancelling", "cancelled"])
        self.assertTrue(isinstance(ta_result, asyncio.CancelledError))
        self.assertTrue(isinstance(tb_result, asyncio.CancelledError))
        self.assertEqual(tc_result, None)

    @async_test
    async def test_two_tasks_child_cancelled(self) -> None:
        """
        Creates two tasks from the same AsyncLazyValue. Cancels the task which
        calls the coroutine second.
        """

        async def async_func(arg1: int, arg2: int) -> int:
            self.log("ran-coro")
            await asyncio.wait_for(self.cancelled.wait(), timeout=3)
            return arg1 + arg2

        async def async_cancel(task: asyncio.Task, alv: AsyncLazyValue) -> None:
            start = time()
            while alv._awaiting_tasks < 1:
                # Sleep until both tasks are awaiting on the future (one of them
                # is awaiting on async_func, so we only wait until the count
                # is 1). Also, we timeout if we wait more than 3 seconds
                now = time()
                if now - start > 3:
                    raise RuntimeError(
                        "cannot cancel since the tasks are not waiting on the future"
                    )
                await asyncio.sleep(0)
            self.log("cancelling")
            task.cancel()
            self.cancelled.set()
            self.log("cancelled")

        alv = AsyncLazyValue(async_func, 1, 2)
        ta = asyncio.ensure_future(alv)
        tb = asyncio.ensure_future(alv)
        tc = asyncio.ensure_future(async_cancel(tb, alv))

        ta_result, tb_result, tc_result = await asyncio.gather(
            ta, tb, tc, return_exceptions=True
        )

        self.assertSequenceEqual(self.events, ["ran-coro", "cancelling", "cancelled"])
        self.assertEqual(ta_result, 3)
        self.assertTrue(isinstance(tb_result, asyncio.CancelledError))
        self.assertEqual(tc_result, None)

    @async_test
    async def test_throw_1(self):
        async def l0(alv):
            return await l1(alv)

        async def l1(alv):
            return await l2(alv)

        async def l2(alv):
            return await alv

        async def val(f):
            try:
                await f
            except:
                pass
            return 42

        f = asyncio.Future()
        alv = AsyncLazyValue(val, f)

        l = asyncio.get_running_loop()
        l.call_later(1, lambda: f.set_exception(NotImplementedError))
        x = await l0(alv)
        self.assertEqual(x, 42)

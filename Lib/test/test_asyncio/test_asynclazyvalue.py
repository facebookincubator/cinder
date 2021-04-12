import asyncio
import inspect
import unittest
from _asyncio import AsyncLazyValue
from functools import wraps
from time import time


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

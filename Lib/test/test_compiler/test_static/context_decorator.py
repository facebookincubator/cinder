import asyncio
import inspect

from __static__ import ContextDecorator
from test.support.cinder import get_await_stack

from .common import StaticTestBase


class ContextDecoratorTests(StaticTestBase):
    def test_simple(self):
        codestr = """
            from __static__ import ContextDecorator
            class MyDecorator(ContextDecorator):
                pass

            class C:
                @MyDecorator()
                def f(self):
                    return 42

            def f(c: C):
                return c.f()
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertInBytecode(
                C.f,
                "INVOKE_METHOD",
                ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
            )
            self.assertEqual(C().f(), 42)

            f = mod.f
            self.assertInBytecode(
                f,
                "INVOKE_METHOD",
                (((mod.__name__, "C", "f"), 0)),
            )

    def test_simple_async(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            calls = 0
            class MyDecorator(ContextDecorator):
                def __enter__(self) -> MyDecorator:
                    global calls
                    calls += 1
                    return self

            class C:
                @MyDecorator()
                async def f(self):
                    return 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertInBytecode(
                C.f,
                "INVOKE_METHOD",
                ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
            )
            a = C().f()
            self.assertEqual(mod.calls, 0)
            try:
                a.send(None)
            except StopIteration as e:
                self.assertEqual(e.args[0], 42)
                self.assertEqual(mod.calls, 1)

    def test_property(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            class MyDecorator(ContextDecorator):
                pass

            class C:
                @property
                @MyDecorator()
                def f(self):
                    return 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertInBytecode(
                C.f.fget,
                "INVOKE_METHOD",
                ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
            )
            self.assertEqual(C().f, 42)

    def test_cached_property(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            from cinder import cached_property

            class MyDecorator(ContextDecorator):
                def __enter__(self) -> MyDecorator:
                    return self

                def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool:
                    return False

            X = 42
            class C:
                @cached_property
                @MyDecorator()
                def f(self):
                    global X
                    X += 1
                    return X
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertInBytecode(
                C.f.func,
                "INVOKE_METHOD",
                ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
            )
            c = C()
            self.assertEqual(c.f, 43)
            self.assertEqual(c.f, 43)

    def test_async_cached_property(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            from cinder import async_cached_property

            class MyDecorator(ContextDecorator):
                def __enter__(self) -> MyDecorator:
                    return self

                def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool:
                    return False

            X = 42
            class C:
                @async_cached_property
                @MyDecorator()
                async def f(self):
                    global X
                    X += 1
                    return X
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertInBytecode(
                C.f.func,
                "INVOKE_METHOD",
                ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
            )
            c = C()
            x = c.f.__await__()
            try:
                x.__next__()
            except StopIteration as e:
                self.assertEqual(e.args[0], 43)
            x = c.f.__await__()
            try:
                x.__next__()
            except StopIteration as e:
                self.assertEqual(e.args[0], 43)

    def test_static_method(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            class MyDecorator(ContextDecorator):
                def __enter__(self) -> MyDecorator:
                    return self

                def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool:
                    return False

            class C:
                @staticmethod
                @MyDecorator()
                def f():
                    return 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C().f(), 42)
            self.assertInBytecode(
                C.__dict__["f"].__func__,
                "INVOKE_METHOD",
                ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
            )
            self.assertEqual(C().f(), 42)
            self.assertEqual(C.f(), 42)

    def test_static_method_with_arg(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            class MyDecorator(ContextDecorator):
                def __enter__(self) -> MyDecorator:
                    return self

                def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool:
                    return False

            class C:
                @staticmethod
                @MyDecorator()
                def f(x):
                    return x
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C().f(42), 42)
            self.assertEqual(C.f(42), 42)
            self.assertInBytecode(
                C.__dict__["f"].__func__,
                "INVOKE_METHOD",
                ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
            )

    def test_static_method_compat_with_arg(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            class MyDecorator(ContextDecorator):
                def __enter__(self) -> MyDecorator:
                    return self

                def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool:
                    return False

            class C:
                @staticmethod
                @MyDecorator()
                def f(x: C):
                    return 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C().f(C()), 42)
            self.assertInBytecode(C.__dict__["f"].__func__, "LOAD_FAST", "x")
            self.assertInBytecode(
                C.__dict__["f"].__func__,
                "INVOKE_METHOD",
                ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
            )
            self.assertEqual(C().f(C()), 42)
            self.assertEqual(C.f(C()), 42)

    def test_class_method(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            from typing import final
            calls = 0
            class MyDecorator(ContextDecorator):
                def __enter__(self) -> MyDecorator:
                    global calls
                    calls += 1
                    return self

                def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool:
                    return False

            @final
            class C:
                @classmethod
                @MyDecorator()
                def f(cls):
                    return 42

            def f(c: C):
                return c.f()
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C().f(), 42)
            self.assertEqual(mod.calls, 1)
            self.assertInBytecode(
                C.__dict__["f"].__func__,
                "INVOKE_METHOD",
                ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
            )
            f = mod.f
            self.assertEqual(f(C()), 42)
            self.assertEqual(mod.calls, 2)

    def test_top_level(self):
        codestr = """
            from __static__ import ContextDecorator
            class MyDecorator(ContextDecorator):
                pass

            @MyDecorator()
            def f():
                return 42

            def g():
                return f()
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(
                f,
                "INVOKE_METHOD",
                ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
            )
            self.assertEqual(f(), 42)

            g = mod.g
            self.assertInBytecode(g, "INVOKE_FUNCTION", (((mod.__name__, "f"), 0)))

    def test_recreate_cm(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            class MyDecorator(ContextDecorator):
                def _recreate_cm(self) -> MyDecorator:
                    return self

            class C:
                @MyDecorator()
                def f(self):
                    return 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertInBytecode(
                C.f,
                "INVOKE_METHOD",
                (((mod.__name__, "MyDecorator", "_recreate_cm"), 0)),
            )
            self.assertEqual(C().f(), 42)

    def test_recreate_cm_final(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            from typing import final

            @final
            class MyDecorator(ContextDecorator):
                def _recreate_cm(self) -> MyDecorator:
                    return self

            class C:
                @MyDecorator()
                def f(self):
                    return 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertInBytecode(
                C.f,
                "INVOKE_FUNCTION",
                (((mod.__name__, "MyDecorator", "_recreate_cm"), 1)),
            )
            self.assertEqual(C().f(), 42)

    def test_stacked(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            calls = []
            class MyDecorator1(ContextDecorator):
                def __enter__(self) -> MyDecorator1:
                    calls.append("MyDecorator1.__enter__")
                    return self
                def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool:
                    calls.append("MyDecorator1.__exit__")
                    return False
                def _recreate_cm(self) -> MyDecorator1:
                    return self

            class MyDecorator2(ContextDecorator):
                def __enter__(self) -> MyDecorator2:
                    calls.append("MyDecorator2.__enter__")
                    return self

                def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool:
                    calls.append("MyDecorator2.__exit__")
                    return False

                def _recreate_cm(self) -> MyDecorator2:
                    return self

            class C:
                @MyDecorator1()
                @MyDecorator2()
                def f(self):
                    return 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C().f(), 42)
            self.assertInBytecode(
                C.f,
                "INVOKE_METHOD",
                (((mod.__name__, "MyDecorator1", "_recreate_cm"), 0)),
            )
            self.assertInBytecode(
                C.f,
                "INVOKE_METHOD",
                (((mod.__name__, "MyDecorator2", "_recreate_cm"), 0)),
            )
            self.assertEqual(
                mod.calls,
                [
                    "MyDecorator1.__enter__",
                    "MyDecorator2.__enter__",
                    "MyDecorator2.__exit__",
                    "MyDecorator1.__exit__",
                ],
            )

    def test_simple_func(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            calls = 0
            class MyDecorator(ContextDecorator):
                def __enter__(self) -> MyDecorator:
                    global calls
                    calls += 1
                    return self

            def wrapper() -> MyDecorator:
                return MyDecorator()

            class C:
                @wrapper()
                def f(self) -> int:
                    return 42

                def x(self) -> int:
                    return self.f()
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            a = C()
            self.assertEqual(a.f(), 42)
            self.assertEqual(mod.calls, 1)
            self.assertInBytecode(
                C.x,
                "INVOKE_METHOD",
                (((mod.__name__, "C", "f"), 0)),
            )

    def test_simple_method(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            calls = 0
            class MyDecorator(ContextDecorator):
                def __enter__(self) -> MyDecorator:
                    global calls
                    calls += 1
                    return self

            class WrapperFactory:
                def wrapper(self) -> MyDecorator:
                    return MyDecorator()

            wf: WrapperFactory = WrapperFactory()

            class C:
                @wf.wrapper()
                def f(self) -> int:
                    return 42

                def x(self) -> int:
                    return self.f()
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            a = C()
            self.assertEqual(a.f(), 42)
            self.assertEqual(mod.calls, 1)
            self.assertInBytecode(
                C.x,
                "INVOKE_METHOD",
                (((mod.__name__, "C", "f"), 0)),
            )

    def test_cross_module(self) -> None:
        acode = """
            from __future__ import annotations
            from __static__ import ContextDecorator

            def wrapper() -> MyDecorator:
                return MyDecorator()

            class MyDecorator(ContextDecorator):
                def __enter__(self) -> MyDecorator:
                    global calls
                    calls += 1
                    return self


                def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool:
                    return False
        """
        bcode = """
            from a import wrapper

            class C:
                @wrapper()
                def f(self) -> int:
                    return 42

                def x(self) -> int:
                    return self.f()

        """
        bcomp = self.compiler(a=acode, b=bcode).compile_module("b")
        f = self.find_code(self.find_code(bcomp, "C"), "f")
        self.assertInBytecode(
            f,
            "INVOKE_METHOD",
            ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
        )

    def test_nonstatic_base(self):
        class C(ContextDecorator):
            pass

        @C()
        def f():
            return 42

        self.assertEqual(f(), 42)

    def test_nonstatic_base_async(self):
        class C(ContextDecorator):
            pass

        @C()
        async def f():
            return 42

        x = f()
        with self.assertRaises(StopIteration):
            x.send(None)

    def test_nonstatic_async_eager(self):
        class C(ContextDecorator):
            pass

        @C()
        async def f():
            return 42

        async def caller():
            return await f()

        x = caller()
        with self.assertRaises(StopIteration) as si:
            x.send(None)
        self.assertEqual(si.exception.args[0], 42)

    def test_nonstatic_async_eager_exit_raises(self):
        class C(ContextDecorator):
            def __exit__(self, *args):
                raise ValueError()

        @C()
        async def f():
            return 42

        async def caller():
            return await f()

        x = caller()
        with self.assertRaises(ValueError):
            x.send(None)

    def test_nonstatic_base_async_exit(self):
        exit_called = False

        class C(ContextDecorator):
            def __exit__(self, *args):
                nonlocal exit_called
                exit_called = True

        @C()
        async def f():
            return 42

        x = f()
        with self.assertRaises(StopIteration):
            x.send(None)
            self.assertTrue(exit_called)

    def test_nonstatic_base_async_exit_raises(self):
        exit_called = False

        class C(ContextDecorator):
            def __exit__(self, *args):
                nonlocal exit_called
                exit_called = True

        @C()
        async def f():
            raise ValueError()

        x = f()
        with self.assertRaises(ValueError):
            x.send(None)
        self.assertTrue(exit_called)

    def test_nonstatic_async_steps(self):
        exit_called = False

        class C(ContextDecorator):
            def __exit__(self, *args):
                nonlocal exit_called
                exit_called = True

        loop = asyncio.new_event_loop()
        fut = asyncio.Future(loop=loop)

        @C()
        async def f():
            await fut
            return 42

        x = f()
        with self.assertRaises(StopIteration) as se:
            x.send(None)
            fut.set_result(None)
            x.send(None)

        self.assertEqual(se.exception.args[0], 42)
        self.assertTrue(exit_called)
        loop.close()

    def test_nonstatic_raise_and_suppress_async(self):
        class C(ContextDecorator):
            def _recreate_cm(self):
                return self

            def __enter__(self):
                return self

            def __exit__(self, *args):
                return True

        @C()
        async def f():
            raise Exception()

        async def g():
            return await f()

        self.assertEqual(asyncio.run(g()), None)

    def test_nonstatic_async_steps_raises(self):
        exit_called = False

        class C(ContextDecorator):
            def __exit__(self, *args):
                nonlocal exit_called
                exit_called = True

        loop = asyncio.new_event_loop()
        fut = asyncio.Future(loop=loop)

        @C()
        async def f():
            await fut
            raise ValueError()

        x = f()
        with self.assertRaises(ValueError):
            x.send(None)

            fut.set_result(None)
            x.send(None)
        self.assertTrue(exit_called)
        loop.close()

    def test_nonstatic_base_async_no_await(self):
        class C(ContextDecorator):
            pass

        @C()
        async def f():
            return 42

        # just checking to make sure this doesn't leak
        # in ref leak tests
        x = f()

    def test_nonstatic_override(self):
        class C(ContextDecorator):
            def _recreate_cm(self):
                return self

            def __enter__(self):
                return self

            def __exit__(self, *args):
                return None

        @C()
        def f():
            return 42

        self.assertEqual(f(), 42)

    def test_nonstatic_raise(self):
        class C(ContextDecorator):
            def _recreate_cm(self):
                return self

            def __enter__(self):
                return self

            def __exit__(self, *args):
                return False

        @C()
        def f():
            raise ValueError()

        with self.assertRaises(ValueError):
            f()

    def test_nonstatic_raise_bad_true(self):
        class B:
            def __bool__(self):
                raise ValueError()

        class C(ContextDecorator):
            def _recreate_cm(self):
                return self

            def __enter__(self):
                return self

            def __exit__(self, *args):
                return B()

        @C()
        def f():
            raise Exception()

        with self.assertRaises(ValueError):
            f()

    def test_nonstatic_raise_on_exit_error(self):
        class B:
            def __bool__(self):
                raise ValueError()

        class C(ContextDecorator):
            def _recreate_cm(self):
                return self

            def __enter__(self):
                return self

            def __exit__(self, *args):
                raise ValueError()

        @C()
        def f():
            raise Exception()

        with self.assertRaises(ValueError):
            f()

    def test_nonstatic_raise_on_exit_success(self):
        class B:
            def __bool__(self):
                raise ValueError()

        class C(ContextDecorator):
            def _recreate_cm(self):
                return self

            def __enter__(self):
                return self

            def __exit__(self, *args):
                raise ValueError()

        @C()
        def f():
            return 42

        with self.assertRaises(ValueError):
            f()

    def test_nonstatic_raise_and_suppress(self):
        class C(ContextDecorator):
            def _recreate_cm(self):
                return self

            def __enter__(self):
                return self

            def __exit__(self, *args):
                return True

        @C()
        def f():
            raise Exception()

        self.assertEqual(f(), None)

    def test_nonstatic_suppress_on_throw(self):
        class C(ContextDecorator):
            def _recreate_cm(self):
                return self

            def __enter__(self):
                return self

            def __exit__(self, *args):
                return True

        loop = asyncio.new_event_loop()
        fut = asyncio.Future(loop=loop)

        @C()
        async def f():
            await fut

        x = f()
        x.send(None)
        with self.assertRaises(StopIteration) as e:
            x.throw(Exception())

        self.assertEqual(e.exception.args, ())
        loop.close()

    def test_nonstatic_suppress_on_throw_no_send(self):
        class C(ContextDecorator):
            def _recreate_cm(self):
                return self

            def __Xcall__(self, func):
                async def _no_profile_inner(*args, **kwds):
                    with self._recreate_cm():
                        return await func(*args, **kwds)

                return _no_profile_inner

            def __enter__(self):
                return self

            def __exit__(self, *args):
                return True

        loop = asyncio.new_event_loop()
        fut = asyncio.Future(loop=loop)

        @C()
        async def f():
            await fut

        x = f()
        with self.assertRaises(ValueError) as e:
            x.throw(ValueError())

        loop.close()

    def test_nonstatic_recreate(self):
        class C(ContextDecorator):
            def _recreate_cm(self_):
                return C()

            def __enter__(self_):
                self.assertNotEqual(a, self_)
                return self_

            def __exit__(self_, *args):
                return True

        a = C()

        @a
        def f():
            raise Exception()

        self.assertEqual(f(), None)

    def test_nonstatic_change_recreate_cm(self):
        class C(ContextDecorator):
            def _recreate_cm(self):
                return self

            def __enter__(self):
                return self

            def __exit__(self, *args):
                return True

        @C()
        def f():
            raise Exception()

        self.assertEqual(f(), None)

        def raises(self):
            raise ValueError()

        C._recreate_cm = raises
        self.assertRaises(ValueError, f)

    def test_nonstatic_wraps(self):
        class C(ContextDecorator):
            pass

        @C()
        def f():
            raise Exception()

        self.assertEqual(f.__name__, "f")

    def test_nonstatic_custom_attr(self):
        class C(ContextDecorator):
            pass

        @C()
        def f():
            raise Exception()

        f.foo = 42
        self.assertEqual(f.foo, 42)

    def test_nonstatic_dict_copy(self):
        class C(ContextDecorator):
            pass

        def dec(f):
            f.bar = "abc"
            return f

        @C()
        @dec
        def f():
            raise Exception()

        f.foo = 42
        self.assertEqual(f.foo, 42)
        self.assertEqual(f.bar, "abc")

    def test_nonstatic_coroutine(self):
        class C(ContextDecorator):
            pass

        @C()
        async def f():
            pass

        self.assertTrue(asyncio.iscoroutinefunction(f))

    def test_nonstatic_signature(self):
        class C(ContextDecorator):
            pass

        @C()
        def f(x):
            pass

        def other(x):
            pass

        self.assertEqual(inspect.signature(f), inspect.signature(other))

    def test_nonstatic_async_enter_deferred(self):
        enter_called = False

        class C(ContextDecorator):
            def __enter__(self):
                nonlocal enter_called
                enter_called = True

        @C()
        async def f():
            pass

        x = f()
        self.assertFalse(enter_called)

    def test_nonstatic_async_return_tuple_on_throw(self):
        class C(ContextDecorator):
            pass

        loop = asyncio.new_event_loop()
        try:
            fut = asyncio.Future(loop=loop)

            @C()
            async def f():
                try:
                    await fut
                except ValueError:
                    return (1, 2, 3)

            x = f()
            x.send(None)
            with self.assertRaises(StopIteration) as e:
                x.throw(ValueError())

            self.assertEqual(e.exception.args, ((1, 2, 3),))
        finally:
            loop.close()

    def test_stack_trace(self):
        coro = None
        await_stack = None

        class C(ContextDecorator):
            pass

        @C()
        async def f():
            nonlocal await_stack
            await_stack = get_await_stack(coro)
            return 100

        async def g():
            nonlocal coro
            x = f()
            coro = x.__coro__
            return await x

        g_coro = g()
        asyncio.run(g_coro)
        self.assertEqual(await_stack, [g_coro])

    def test_stack_trace_non_eager(self):
        coro = None
        await_stack = None

        class C(ContextDecorator):
            pass

        @C()
        async def f():
            nonlocal await_stack
            await asyncio.sleep(0.1)
            await_stack = get_await_stack(coro)
            return 100

        async def g():
            nonlocal coro
            x = f()
            coro = x.__coro__
            return await x

        g_coro = g()
        asyncio.run(g_coro)
        self.assertEqual(await_stack, [g_coro])

    def test_repeated_import_with_contextdecorator(self) -> None:
        codestr = """
            from __static__ import ContextDecorator

            class C:
                @ContextDecorator()
                def meth(self):
                    pass
        """
        compiler = self.get_strict_compiler()

        compiler.load_compiled_module_from_source(
            self.clean_code(codestr), "mod.py", "mod", 1
        )
        compiler.load_compiled_module_from_source(
            self.clean_code(codestr), "mod.py", "mod", 1
        )

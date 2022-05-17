import asyncio

from .common import StaticTestBase


class AsyncTests(StaticTestBase):
    def test_async_for(self) -> None:
        codestr = """
        from typing import Awaitable, List
        async def foo(awaitables) -> int:
             sum = 0
             async for x in awaitables:
                 sum += x
             return sum
        """
        # Ensure that this compiles without hassle - we don't do any type analysis.
        self.compile(codestr)

    def test_async_for_name_error(self) -> None:
        codestr = """
        from typing import Awaitable, List
        async def foo(awaitables) -> int:
             sum = 0
             async for x in awaitables:
                 sum += y
             return sum
        """
        self.type_error(codestr, "Name `y` is not defined.")

    def test_async_for_primitive_error(self) -> None:
        codestr = """
        from __static__ import int64
        from typing import Awaitable, List
        async def foo() -> int:
             awaitables: int64 = 1
             async for x in awaitables:
                 sum += x
             return sum

        async def asyncify(x):
             return x
        """
        self.type_error(codestr, "cannot await a primitive value")

    def test_async_with(self) -> None:
        codestr = """
        from typing import Awaitable, List
        async def foo(acm) -> None:
             async with acm() as c:
                 c.m()
        """
        # Ensure that this compiles without hassle - we don't do any type analysis.
        self.compile(codestr)

    def test_async_with_name_error(self) -> None:
        codestr = """
        from typing import Awaitable, List
        async def foo(acm) -> int:
             async with acm() as c:
                 d.m()
        """
        self.type_error(codestr, "Name `d` is not defined.")

    def test_async_with_may_not_terminate(self) -> None:
        codestr = """
        from typing import Awaitable, List
        async def foo(acm) -> int:
             async with acm() as c:
                 return 42
        """
        self.type_error(
            codestr,
            "Function has declared return type 'int' but can implicitly return None.",
        )

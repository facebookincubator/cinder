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

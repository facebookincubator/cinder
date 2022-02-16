import asyncio
import unittest

from test.support import import_module
from test.support.cinder import verify_stack


_testinternalcapi = import_module("_testinternalcapi")


class WalkShadowFramesTest(unittest.TestCase):
    def setUp(self) -> None:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        self.loop = loop

    def tearDown(self):
        self.loop.close()
        asyncio.set_event_loop_policy(None)

    def test_walk_and_populate_stack(self):
        """
        Note - this test case is isolated in this file, because it tests line numbers,
        which change in other files and cause the test to fail.
        """

        stacks = None

        async def a1():
            nonlocal stacks
            stacks = _testinternalcapi.test_shadowframe_walk_and_populate()

        async def a2():
            await a1()

        async def a3():
            return await asyncio.ensure_future(a2())

        async def a4():
            return await a3()

        async def a5():
            return await a4()

        async def drive():
            await a5()

        asyncio.run(drive())

        async_stack, sync_stack = stacks

        class _StackEntry:
            def __init__(self, entry: str):
                self.filename, self.lineno, self.qualname = entry.split(":")

        async_entries = [_StackEntry(e) for e in async_stack]
        # async stack ends at `drive`
        self.assertEqual(len(async_entries), 6)

        # All entries in the async stack must have the correct filename
        self.assertTrue(
            all(e.filename.endswith("test_shadow_stack_walk.py") for e in async_entries)
        )

        # These are the line numbers corresponding to a1, a2, etc above.
        self.assertEqual(
            [e.lineno for e in async_entries], ['31', '34', '37', '40', '43', '46']
        )

        verify_stack(
            self,
            async_stack[::-1],
            ["drive", "a5", "a4", "a3", "a2", "a1"],
        )

        sync_entries = [_StackEntry(e) for e in sync_stack]

        # Must have at least 4 entries in the sync stack
        self.assertGreaterEqual(len(sync_entries), 4)

        # Sync stack has entries outside of this file (such as the event loop),
        # we only verify the filename for the known entries (first two).
        self.assertTrue(
            all(
                e.filename.endswith("test_shadow_stack_walk.py")
                for e in sync_entries[:2]
            )
        )

        # These are the line numbers corresponding to a1, a2 above.
        self.assertEqual([e.lineno for e in sync_entries[:2]], ["31", "34"])

        # the sync stack can't capture how a3 is awaiting on a2
        verify_stack(self, sync_stack[::-1], ["_run", "a2", "a1"])


if __name__ == "__main__":
    unittest.main()

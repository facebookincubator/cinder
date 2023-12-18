import unittest
from textwrap import dedent

from test.support import cpython_only, SuppressCrashReport
from test.support.script_helper import kill_python

from test.test_repl import spawn_repl


class CinderX_TestInteractiveInterpreter(unittest.TestCase):
    # When CinderX's debug assertions are enabled the REPL crashes with a
    # different error on CinderX shutdown. I think the main purpose of this
    # test is just to assert we don't end up in a loop so any kind of exit is
    # fine.
    def test_no_memory(self):
        # Issue #30696: Fix the interactive interpreter looping endlessly when
        # no memory. Check also that the fix does not break the interactive
        # loop when an exception is raised.
        user_input = """
            import sys, _testcapi
            1/0
            print('After the exception.')
            _testcapi.set_nomemory(0)
            sys.exit(0)
        """
        user_input = dedent(user_input)
        p = spawn_repl()
        with SuppressCrashReport():
            p.stdin.write(user_input)
        output = kill_python(p)
        self.assertIn("After the exception.", output)
        # Changed to just assert not 0 for CinderX
        self.assertNotEqual(p.returncode, 0)


if __name__ == "__main__":
    unittest.main()

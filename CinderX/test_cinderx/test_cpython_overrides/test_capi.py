import textwrap
import unittest

from test.support.script_helper import assert_python_failure

from test.test_capi import decode_stderr


class CinderX_CAPITest(unittest.TestCase):

    # The output in this test is different for CinderX as we now have the
    # _cinderx module loaded.
    def test_getitem_with_error(self):
        # Test _Py_CheckSlotResult(). Raise an exception and then calls
        # PyObject_GetItem(): check that the assertion catches the bug.
        # PyObject_GetItem() must not be called with an exception set.
        code = textwrap.dedent(
            """
            import _testcapi
            from test import support

            with support.SuppressCrashReport():
                _testcapi.getitem_with_error({1: 2}, 1)
        """
        )
        rc, out, err = assert_python_failure("-c", code)
        err = decode_stderr(err)
        if "SystemError: " not in err:
            self.assertRegex(
                err,
                r"Fatal Python error: _Py_CheckSlotResult: "
                r"Slot __getitem__ of type dict succeeded "
                r"with an exception set\n"
                r"Python runtime state: initialized\n"
                r"ValueError: bug\n"
                r"\n"
                r"Current thread .* \(most recent call first\):\n"
                r"  File .*, line 6 in <module>\n"
                r"\n"
                # Changes for CinderX
                r"Extension modules: .* \(total: .*\)\n",
            )
        else:
            # Python built with NDEBUG macro defined:
            # test _Py_CheckFunctionResult() instead.
            self.assertIn("returned a result with an exception set", err)


if __name__ == "__main__":
    unittest.main()

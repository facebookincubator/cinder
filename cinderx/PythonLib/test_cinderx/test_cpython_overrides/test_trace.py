import os
import sys
from test.support import captured_stdout
from test.support.os_helper import (TESTFN, rmtree, unlink)
from test.support.script_helper import assert_python_ok, assert_python_failure
import textwrap
import unittest

import trace
from trace import Trace

from test.tracedmodules import testmod

#------------------------------- Utilities -----------------------------------#

def fix_ext_py(filename):
    """Given a .pyc filename converts it to the appropriate .py"""
    if filename.endswith('.pyc'):
        filename = filename[:-1]
    return filename

def get_firstlineno(func):
    return func.__code__.co_firstlineno

#-------------------- Target functions for tracing ---------------------------#
#
# The relative line numbers of lines in these functions matter for verifying
# tracing. Please modify the appropriate tests if you change one of the
# functions. Absolute line numbers don't matter.
#

def traced_doubler(num):
    return num * 2

def traced_caller_list_comprehension():
    k = 10
    mylist = [traced_doubler(i) for i in range(k)]
    return mylist


#------------------------------ Test cases -----------------------------------#


class CinderX_TestLineCounts(unittest.TestCase):
    _inline_comprehensions = os.getenv("PYTHONINLINECOMPREHENSIONS")

    """White-box testing of line-counting, via runfunc"""
    def setUp(self):
        self.addCleanup(sys.settrace, sys.gettrace())
        self.tracer = Trace(count=1, trace=0, countfuncs=0, countcallers=0)
        self.my_py_filename = fix_ext_py(__file__)

    def test_trace_list_comprehension(self):
        # cinder modified for comprehension inlining
        self.tracer.runfunc(traced_caller_list_comprehension)

        firstlineno_calling = get_firstlineno(traced_caller_list_comprehension)
        firstlineno_called = get_firstlineno(traced_doubler)
        expected = {
            (self.my_py_filename, firstlineno_calling + 1): 1,
            # List comprehensions work differently in 3.x, so the count
            # below changed compared to 2.x.
            (self.my_py_filename, firstlineno_calling + 2): 11,
            (self.my_py_filename, firstlineno_called + 1): 10,
            (self.my_py_filename, firstlineno_calling + 3): 1,
        } if self._inline_comprehensions else {
            (self.my_py_filename, firstlineno_calling + 1): 1,
            # List comprehensions work differently in 3.x, so the count
            # below changed compared to 2.x.
            (self.my_py_filename, firstlineno_calling + 2): 12,
            (self.my_py_filename, firstlineno_calling + 3): 1,
            (self.my_py_filename, firstlineno_called + 1): 10,
        }

        self.assertEqual(self.tracer.results().counts, expected)

if __name__ == '__main__':
    unittest.main()

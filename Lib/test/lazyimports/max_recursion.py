"""
This case had ever failed in "RecursionError: maximum recursion depth exceeded"
Test there is no error or exception is raised when running
"""
from test.lazyimports.data import max_recursion
max_recursion._vendor.packaging._compat

# This checks no exception is raised

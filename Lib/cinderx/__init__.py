"""High-performance Python runtime extensions."""

# TODO(T171566018) remove
import sys
import os

try:
    # TODO(T171566018) remove
    sys.path.append(os.path.dirname(__file__) + "/../../CinderX/build")
    from _cinderx import *
except ImportError:
    pass

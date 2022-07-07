# TODO enable lazy imports tests and FIXME
# from __future__ import lazy_imports

import warnings

vars = {}
vars.update(globals())
print(repr(vars['warnings']))


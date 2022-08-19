from __future__ import eager_imports

import importlib

from test.lazyimports.future_eager import eagerly_imported_mod

assert(not importlib.is_lazy_import(globals(), "eagerly_imported_mod"))

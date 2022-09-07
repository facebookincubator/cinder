import importlib

from test.lazyimports.future_eager import lazily_imported_mod

assert importlib.is_lazy_import(globals(), "lazily_imported_mod")

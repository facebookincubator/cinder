import importlib

from test.lazyimports.importlib_apis.customized_modules import module_is_lazy_import_eager
assert(not importlib.is_lazy_import(globals(), "module_is_lazy_import_eager"))                  # should be eager

importlib.set_lazy_imports()                                                                    # enable lazy imports
from test.lazyimports.importlib_apis.customized_modules import module_is_lazy_import_lazy
assert(importlib.is_lazy_import(globals(), "module_is_lazy_import_lazy"))                       # should be lazy

with importlib.eager_imports():
    from test.lazyimports.importlib_apis.customized_modules import module_context_manager_eager
assert(not importlib.is_lazy_import(globals(), "module_context_manager_eager"))                 # should be eager

import importlib

importlib.set_lazy_imports(excluding=["test.lazyimports.importlib_apis.customized_modules.module_excluded"])

from test.lazyimports.importlib_apis.customized_modules import module_excluded
assert(importlib.is_lazy_import(globals(), "module_excluded"))                  # should be lazy

module_excluded                                                                 # force loaded
assert(not importlib.is_lazy_import(globals(), "module_excluded"))              # should be eager

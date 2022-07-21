import importlib

class Checker:
    def __contains__(self, name):
            return name in ["test.lazyimports.importlib_apis.customized_modules.module_excluded"]

checker = Checker()
importlib.set_lazy_imports(checker)

from test.lazyimports.importlib_apis.customized_modules import module_excluded
assert(importlib.is_lazy_import(globals(), "module_excluded"))                  # should be lazy

module_excluded                                                                 # force loaded
assert(not importlib.is_lazy_import(globals(), "module_excluded"))              # should be eager

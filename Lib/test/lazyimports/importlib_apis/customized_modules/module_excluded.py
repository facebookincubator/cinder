from test.lazyimports.importlib_apis.customized_modules import module_excluded_sub
import importlib

assert(not importlib.is_lazy_import(globals(), "module_excluded_sub")) # should be eager

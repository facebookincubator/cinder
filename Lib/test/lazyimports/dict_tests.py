import importlib
from test.lazyimports.customized_modules import module
assert importlib.is_lazy_import(globals(), "module")


g = globals()
gcopy = g.copy()
gcopy_resolved = gcopy.copy()
gcopy_resolved.items() # resolve

assert importlib.is_lazy_import(g, "module")
assert importlib.is_lazy_import(gcopy, "module")
assert not importlib.is_lazy_import(gcopy_resolved, "module")


"""
Test | of lazy dictionaries and lazy/non-lazy combinations.
"""
# if `gcopy` and `gcopy_resolved` have the same key
# the value should be `gcopy_resolved[key]`
dict_or_resolved = gcopy | gcopy_resolved
assert not importlib.is_lazy_import(dict_or_resolved, "module")
dict_or_unresolved = gcopy_resolved | gcopy
assert importlib.is_lazy_import(dict_or_unresolved, "module")


"""
Test merging lazy/non-lazy dictionaries (dict.update()).
"""
gcopy.update(gcopy_resolved)
assert not importlib.is_lazy_import(gcopy, "module") # should be eager
gcopy.update(g)
assert importlib.is_lazy_import(gcopy, "module") # should be lazy

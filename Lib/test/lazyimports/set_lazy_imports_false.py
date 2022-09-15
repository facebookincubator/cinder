from importlib import set_lazy_imports, is_lazy_import

set_lazy_imports(False)

import collections

assert not is_lazy_import(globals(), "collections")

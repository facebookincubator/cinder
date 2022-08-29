from __future__ import lazy_imports

try:
    import test.lazyimports.lazy_attribute_side_effect.versioned.__version__
finally:
    pass

print(repr(test.lazyimports.lazy_attribute_side_effect.versioned.__version__))

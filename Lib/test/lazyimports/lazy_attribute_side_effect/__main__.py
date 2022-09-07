"""
This tests the case where eager imports didn't take into account
when a lazy attribute had already been added to a parent module,
and thus mixing eager imports and lazy imports could produce an
additional side effect when the import ends, overwriting already
existing attributes with the child module object.
"""

try:
    import test.lazyimports.lazy_attribute_side_effect.versioned.__version__
finally:
    pass

assert test.lazyimports.lazy_attribute_side_effect.versioned.__version__ == "1.0"

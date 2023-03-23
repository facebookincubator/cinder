"""
This tests the case where eager imports didn't take into account
when a lazy attribute had already been added to a parent module,
and thus mixing eager imports and lazy imports could produce an
additional side effect when the import ends, overwriting already
existing attributes with the child module object.
"""

import self

try:
    import test.lazyimports.data.versioned as versioned
finally:
    pass

expected_version = "1.0"
expected_copyright = "Copyright (c) 2001-2022 Python Software Foundation."

self.assertEqual(versioned.__copyright__, expected_copyright)
self.assertEqual(versioned.__version__, expected_version)

import self
import test.lazyimports.data.versioned as versioned

expected_version = "1.0"
expected_copyright = "Copyright (c) 2001-2022 Python Software Foundation."

"""
When the package `requests` made use of submodule `__version__`,
it would trigger the side effect of overwriting `requests` module's own `__version__`

Test this trigger doesn't happen
"""
self.assertEqual(versioned.__copyright__, expected_copyright)
self.assertEqual(versioned.__version__, expected_version)

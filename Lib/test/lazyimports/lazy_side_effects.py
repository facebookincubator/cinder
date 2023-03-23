"""
Test the side effects when loading a lazy imports object
"""
import self

import test.lazyimports.data.metasyntactic.foo.bar
from test.lazyimports.data.metasyntactic.foo import ack
import test.lazyimports.data.metasyntactic.foo as foo

self.assertEqual(foo.bar.Bar, "Bar")

if self._lazy_imports:
    with self.assertRaises(AttributeError):
        foo.ack.Ack
else:
    self.assertEqual(foo.ack.Ack, "Ack")

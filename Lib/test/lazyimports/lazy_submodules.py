"""
Test the status of submodules
"""
import self
import sys

import test.lazyimports.data.metasyntactic
import test.lazyimports.data.metasyntactic.foo.bar as bar
import test.lazyimports.data.metasyntactic.foo.ack as ack

bar.Bar

self.assertIn("test.lazyimports.data.metasyntactic.foo.bar", set(sys.modules))
if self._lazy_imports:
    self.assertNotIn("test.lazyimports.data.metasyntactic.foo.ack", set(sys.modules))
else:
    self.assertIn("test.lazyimports.data.metasyntactic.foo.ack", set(sys.modules))

import test.lazyimports.data.metasyntactic.waldo.fred as fred

self.assertEqual(test.lazyimports.data.metasyntactic.waldo.Waldo, "Waldo")

import self
import sys

import test.lazyimports.data.metasyntactic.foo as foo

import test.lazyimports.data.metasyntactic.foo.bar.baz

first_bar = test.lazyimports.data.metasyntactic.foo.bar

del sys.modules["test.lazyimports.data.metasyntactic.foo.bar"]

import test.lazyimports.data.metasyntactic.foo.bar.thud

second_bar = test.lazyimports.data.metasyntactic.foo.bar

self.assertIn("test.lazyimports.data.metasyntactic.foo.bar", set(sys.modules))
sys_modules_bar = sys.modules["test.lazyimports.data.metasyntactic.foo.bar"]

self.assertIsNot(first_bar, second_bar)
self.assertIsNot(sys_modules_bar, first_bar)
self.assertIs(sys_modules_bar, second_bar)

self.assertIn("baz", dir(first_bar))
self.assertNotIn("thud", dir(first_bar))

self.assertNotIn("baz", dir(second_bar))
self.assertIn("thud", dir(second_bar))

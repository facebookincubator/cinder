"""
Test the lazy imports objects are not exposed when checking the values of dictionaries
"""
import self
import importlib
from test.lazyimports.data.metasyntactic import names
from test.lazyimports.data.metasyntactic.names import *

g = globals().copy()
g_copy1 = g.copy()
g_copy2 = g.copy()
g_copy3 = g.copy()
g_copy4 = g.copy()

def notExposeLazyPrefix(obj_repr):
    return not obj_repr.startswith("<lazy_import ")


# Test dict.values()
for value in g_copy1.values():
    self.assertNotRegex(repr(value), r"^<lazy_import ")

# Test dict.items()
for key, value in g_copy2.items():
    self.assertNotRegex(repr(value), r"^<lazy_import ")

# Test iter(dict.values())
it = iter(g_copy3.values())
for value in it:
    self.assertNotRegex(repr(value), r"^<lazy_import ")

# Test iter(dict.items())
it = iter(g_copy4.items())
for key, value in it:
    self.assertNotRegex(repr(value), r"^<lazy_import ")

# Test directly getting values by using keys
self.assertNotRegex(repr(g["names"]), r"^<lazy_import ")
self.assertNotRegex(repr(g["Foo"]), r"^<lazy_import ")
self.assertNotRegex(repr(g["Ack"]), r"^<lazy_import ")
self.assertNotRegex(repr(g["Bar"]), r"^<lazy_import ")
self.assertNotRegex(repr(g["Baz"]), r"^<lazy_import ")
self.assertNotRegex(repr(g["Thud"]), r"^<lazy_import ")
self.assertNotRegex(repr(g["Waldo"]), r"^<lazy_import ")
self.assertNotRegex(repr(g["Fred"]), r"^<lazy_import ")
self.assertNotRegex(repr(g["Plugh"]), r"^<lazy_import ")
self.assertNotRegex(repr(g["Metasyntactic"]), r"^<lazy_import ")

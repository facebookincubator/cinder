import unittest
from test.support.script_helper import assert_python_ok


class LazyImportsTest(unittest.TestCase):
    def test_dict_update(self):
        rc, out, err = assert_python_ok(
            "-X", "lazyimportsall",
            "-m", "test.lazyimports.dict_update",
        )
        self.assertFalse(err)
        self.assertNotIn(b"<deferred ", out)

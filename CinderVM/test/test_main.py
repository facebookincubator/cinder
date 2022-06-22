import cindervm
import unittest


class CinderVMTests(unittest.TestCase):
    def test_call_hello(self):
        self.assertIs(cindervm.hello(), None)


if __name__ == "__main__":
    unittest.main()

from .common import StaticTestBase


class StaticModuleTests(StaticTestBase):
    def test_has_common_attributes(self):
        import _static

        self.assertIsNotNone(_static.__spec__)
        self.assertIsNotNone(_static.__loader__)

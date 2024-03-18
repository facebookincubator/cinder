from .common import StaticTestBase


class StaticModuleTests(StaticTestBase):
    def test_has_common_attributes(self):
        from cinderx import static

        self.assertIsNotNone(static.__spec__)
        self.assertIsNotNone(static.__loader__)

import sys

from .common import StaticTestBase


class SysHexVersionTests(StaticTestBase):
    def test_sys_versioninfo(self):
        current_version = sys.hexversion

        for version_check_should_pass in (True, False):
            with self.subTest(msg=f"{version_check_should_pass=}"):
                if version_check_should_pass:
                    checked_version = current_version - 1
                    expected_attribute = "a"
                else:
                    checked_version = current_version + 1
                    expected_attribute = "b"
                codestr = f"""
                import sys

                if sys.hexversion >= {hex(checked_version)}:
                    class A:
                        def a(self):
                            pass
                else:
                    class A:
                        def b(self):
                            pass
                """
                with self.in_strict_module(codestr) as mod:
                    self.assertTrue(hasattr(mod.A, expected_attribute))

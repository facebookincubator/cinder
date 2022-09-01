from __future__ import annotations

import sys

from compileall import compile_dir, compile_file
from compiler.strict.loader import strict_compile

from typing import final

from .common import StrictTestBase
from .sandbox import sandbox, use_cm


STRICT_PYC_SUFFIX = (
    f"cpython-{sys.version_info.major}{sys.version_info.minor}.strict.pyc"
)


@final
class StrictCompileTest(StrictTestBase):
    def setUp(self) -> None:
        self.sbx = use_cm(sandbox, self)

    def test_compile(self) -> None:
        py_fn = self.sbx.write_file("foo.py", "import __strict__\n")
        pyc_fn = self.sbx.root / "foo.pyc"
        strict_pyc_fn = self.sbx.root / "foo.strict.pyc"

        strict_compile(str(py_fn), str(pyc_fn), doraise=True)

        self.assertTrue(strict_pyc_fn.is_file())

    def test_compile_file(self) -> None:
        codestr = """
        import __strict__

        def fn(): pass
        """
        mod_path = self.sbx.write_file("foo.py", codestr)
        compile_file(str(mod_path), strict_compile=True, quiet=1)

        self.assertTrue(
            (self.sbx.root / "__pycache__" / f"foo.{STRICT_PYC_SUFFIX}").is_file
        )

    def test_compile_dir(self) -> None:
        codestr = """
        import __strict__
        """

        package_name = "my_package"

        mod_path = self.sbx.write_file(package_name + "/foo.py", codestr)
        mod_path = self.sbx.write_file(package_name + "/__init__.py", codestr)
        mod_path = self.sbx.write_file(package_name + "/bar.py", codestr)

        compile_dir(str(self.sbx.root / package_name), strict_compile=True, quiet=1)

        files = [
            p.name for p in (self.sbx.root / package_name / "__pycache__").iterdir()
        ]

        self.assertEqual(
            sorted(files),
            [
                f"__init__.{STRICT_PYC_SUFFIX}",
                f"bar.{STRICT_PYC_SUFFIX}",
                f"foo.{STRICT_PYC_SUFFIX}",
            ],
        )

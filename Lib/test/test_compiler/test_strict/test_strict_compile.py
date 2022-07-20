from __future__ import annotations

from compiler.strict.loader import strict_compile

from typing import final

from .common import StrictTestBase
from .sandbox import sandbox, use_cm


@final
class StrictCompileTest(StrictTestBase):
    ONCALL_SHORTNAME = "strictmod"

    def setUp(self) -> None:
        self.sbx = use_cm(sandbox, self)

    def test_compile(self) -> None:
        py_fn = self.sbx.write_file("foo.py", "import __strict__\n")
        pyc_fn = self.sbx.root / "foo.pyc"
        strict_pyc_fn = self.sbx.root / "foo.strict.pyc"

        strict_compile(str(py_fn), str(pyc_fn), doraise=True)

        self.assertTrue(strict_pyc_fn.is_file())

import itertools
import os
import subprocess
import sys
import tempfile
from unittest import TestCase
from unittest.mock import patch


class PySourceLoaderTest(TestCase):
    def test_basic(self):
        tf = [False, True]
        for lazy_imports, no_pycs, strict in itertools.product(tf, tf, tf):
            with self.subTest(
                lazy_imports=lazy_imports, no_pycs=no_pycs, strict=strict
            ):
                with tempfile.TemporaryDirectory() as tmpdir:
                    env = os.environ.copy()
                    if strict:
                        env["PYTHONINSTALLSTRICTLOADER"] = "1"
                    else:
                        env["PYTHONUSEPYCOMPILER"] = "1"
                    if lazy_imports:
                        env["PYTHONLAZYIMPORTSALL"] = "1"
                    if no_pycs:
                        env["PYTHONPYCACHEPREFIX"] = tmpdir
                    proc = subprocess.run(
                        [sys.executable, "-c", "import xml; xml"],
                        capture_output=True,
                        env=env,
                    )
                    self.assertEqual(proc.returncode, 0, proc.stderr)

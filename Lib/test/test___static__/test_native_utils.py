from __future__ import annotations

import unittest
from typing import final

from __static__.native_utils import invoke_native


@final
class TestNativeInvoke(unittest.TestCase):
    def test_native_invoke(self) -> None:
        target = "libc.so.6"
        symbol = "labs"

        signature = (("__static__", "int64", "#"), ("__static__", "int64", "#"))

        self.assertEqual(invoke_native(target, symbol, signature, (-4,)), 4)

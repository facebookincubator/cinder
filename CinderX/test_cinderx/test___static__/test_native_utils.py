from __future__ import annotations

from __static__.native_utils import invoke_native

import _ctypes

import unittest
from typing import final

from cinderx.static import (
    _clear_dlopen_cache,
    _clear_dlsym_cache,
    _sizeof_dlopen_cache,
    _sizeof_dlsym_cache,
    lookup_native_symbol,
)


@final
class TestNativeInvoke(unittest.TestCase):
    def test_native_invoke(self) -> None:
        target = "libc.so.6"
        symbol = "labs"

        signature = (("__static__", "int64", "#"), ("__static__", "int64", "#"))

        self.assertEqual(invoke_native(target, symbol, signature, (-4,)), 4)

    def test_native_lookup(self) -> None:
        target = "libc.so.6"
        symbol = "labs"

        handle = _ctypes.dlopen(target)
        ctypes_result = _ctypes.dlsym(handle, symbol)
        _ctypes.dlclose(handle)

        result = lookup_native_symbol(target, symbol)

        self.assertEqual(ctypes_result, result)

    def test_native_lookup_caching(self) -> None:
        target = "libc.so.6"
        symbol = "labs"
        symbol2 = "llabs"

        _clear_dlopen_cache()
        _clear_dlsym_cache()

        # Start with a clean slate
        self.assertEqual(_sizeof_dlopen_cache(), 0)
        self.assertEqual(_sizeof_dlsym_cache(), 0)

        # 1st lookup
        lookup_native_symbol(target, symbol)
        self.assertEqual(_sizeof_dlopen_cache(), 1)
        self.assertEqual(_sizeof_dlsym_cache(), 1)

        # second lookup
        lookup_native_symbol(target, symbol)
        self.assertEqual(_sizeof_dlopen_cache(), 1)
        self.assertEqual(_sizeof_dlsym_cache(), 1)

        # third lookup - new symbol
        lookup_native_symbol(target, symbol2)
        self.assertEqual(_sizeof_dlopen_cache(), 1)
        self.assertEqual(_sizeof_dlsym_cache(), 2)

    def test_native_lookup_incorrect_argcount(self) -> None:
        with self.assertRaisesRegex(
            TypeError, "lookup_native_symbol: Expected 2 arguments"
        ):
            lookup_native_symbol("lol")

    def test_native_lookup_bad_lib_name(self) -> None:
        with self.assertRaisesRegex(
            TypeError, "classloader: 'lib_name' must be a str, got 'int'"
        ):
            lookup_native_symbol(1, "labs")

    def test_native_lookup_bad_symbol_name(self) -> None:
        with self.assertRaisesRegex(
            TypeError, "classloader: 'symbol_name' must be a str, got 'int'"
        ):
            lookup_native_symbol("libc.so.6", 42)

    def test_native_lookup_nonexistent_lib_name(self) -> None:
        non_existent_symbol = "some_non_existent_name.so"
        with self.assertRaisesRegex(
            RuntimeError,
            f"classloader: Could not load library '{non_existent_symbol}'.*",
        ):
            lookup_native_symbol("some_non_existent_name.so", "labs")

    def test_native_lookup_nonexistent_symbol_name(self) -> None:
        non_existent_symbol = "some_non_existent_name"
        with self.assertRaisesRegex(
            RuntimeError,
            f"classloader: unable to lookup '{non_existent_symbol}' in 'libc.so.6'.*",
        ):
            lookup_native_symbol("libc.so.6", non_existent_symbol)

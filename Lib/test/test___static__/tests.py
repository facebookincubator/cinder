# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

"""Tests for __static__ module.

These are tests for the plain-Python fallback implementations in __static__;
the static-Python uses are tested in test_compiler/test_static.py.

"""
import unittest
from __static__ import (
    byte,
    char,
    cbool,
    int8,
    int16,
    int32,
    int64,
    uint8,
    uint16,
    uint32,
    uint64,
    single,
    double,
    size_t,
    ssize_t,
    allow_weakrefs,
    dynamic_return,
    box,
    unbox,
    cast,
    CheckedDict,
    chkdict,
    pydict,
    PyDict,
    clen,
    rand,
    RAND_MAX,
)
from typing import Generic, Optional, TypeVar, Union, Dict

try:
    import _static
except ImportError:
    _static = None


class StaticTests(unittest.TestCase):
    def test_chkdict(self):
        tgt = dict if _static is None else _static.chkdict
        self.assertIs(CheckedDict, tgt)
        self.assertIs(chkdict, tgt)

    def test_pydict(self):
        self.assertIs(pydict, dict)
        self.assertIs(PyDict, Dict)

    def test_clen(self):
        self.assertIs(clen, len)

    def test_int_types(self):
        for typ in [
            size_t,
            ssize_t,
            int8,
            uint8,
            int16,
            uint16,
            int32,
            uint32,
            int64,
            uint64,
            byte,
            char,
            cbool,
        ]:
            with self.subTest(typ=typ):
                x = typ(1)
                self.assertEqual(x, 1)

    def test_float_types(self):
        for typ in [
            single,
            double,
        ]:
            with self.subTest(typ=typ):
                x = typ(1.0)
                self.assertEqual(x, 1.0)

    def test_box(self):
        self.assertEqual(box(1), 1)

    def test_unbox(self):
        self.assertEqual(unbox(1), 1)

    def test_allow_weakrefs(self):
        class MyClass:
            pass

        self.assertIs(MyClass, allow_weakrefs(MyClass))

    def test_dynamic_return(self):
        def foo():
            pass

        self.assertIs(foo, dynamic_return(foo))

    def test_cast(self):
        self.assertIs(cast(int, 2), 2)

    def test_cast_subtype(self):
        class Base:
            pass

        class Sub(Base):
            pass

        s = Sub()
        self.assertIs(cast(Base, s), s)

    def test_cast_fail(self):
        with self.assertRaisesRegex(TypeError, "expected int, got str"):
            cast(int, "foo")

    def test_cast_optional(self):
        self.assertIs(cast(Optional[int], None), None)
        self.assertIs(cast(int | None, None), None)
        self.assertIs(cast(None | int, None), None)

    def test_cast_generic_type(self):
        T = TypeVar("T")

        class G(Generic[T]):
            pass

        g = G()

        self.assertIs(cast(G[int], g), g)

    def test_cast_type_too_complex(self):
        with self.assertRaisesRegex(ValueError, r"cast expects type or Optional\[T\]"):
            cast(Union[int, str], int)

    def test_rand(self):
        self.assertEqual(type(RAND_MAX), int)
        self.assertLessEqual(rand(), RAND_MAX)
        self.assertGreaterEqual(rand(), 0)


if __name__ == "__main__":
    unittest.main()

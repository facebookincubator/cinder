import io
from compiler.dis_stable import Disassembler
from compiler.pycodegen import (
    BaseCodeGenerator,
    CinderCodeGenerator,
    PythonCodeGenerator,
)
from textwrap import dedent

from .common import CompilerTest


def dump_code(code):
    f = io.StringIO()
    Disassembler().dump_code(code, file=f)
    text = f.getvalue()
    return text


class Python310Tests(CompilerTest):
    maxDiff = None

    def _check(self, src, optimize=-1):
        src = dedent(src).strip()
        actual = dump_code(self.compile(src, optimize=optimize))
        expected = dump_code(compile(src, "", mode="exec", optimize=optimize))
        self.assertEqual(actual, expected)

    def _check_error(
        self, src, msg_contains, *, optimize=-1, generator=PythonCodeGenerator
    ):
        src = dedent(src).strip()
        with self.assertRaises(SyntaxError) as ctx:
            compile(src, "", mode="exec", optimize=optimize)
        cmsg = str(ctx.exception.msg)
        with self.assertRaises(SyntaxError) as ctx:
            self.compile(src, optimize=optimize, generator=generator)
        pymsg = str(ctx.exception.msg)
        self.assertEqual(pymsg, cmsg)
        self.assertIn(pymsg, msg_contains)

    def test_no_yield_in_stringified_annotation(self):
        codestr = """
            from __future__ import annotations

            def f():
                x: (yield) = 1
                return x
        """
        self._check_error(
            codestr, "'yield expression' can not be used within an annotation"
        )

    def test_yield_ok_in_non_stringified_annotation(self):
        codestr = """
            def f():
                x: (yield) = 1
                return x
        """
        self._check(codestr)

    def test_no_nested_async_comprehension(self):
        codestr = """
            async def foo(a):
                return {k: [y for y in k if await bar(y)] for k in a}
        """

        # The base code generator matches 3.10 upstream and thus has this
        # restriction, but the restriction has been lifted in 3.11
        # (see https://github.com/python/cpython/pull/6766), so we also lift
        # it in CinderCodeGenerator.
        self._check_error(
            codestr,
            "asynchronous comprehension outside of an asynchronous function",
            generator=BaseCodeGenerator,
        )
        self.compile(codestr, generator=CinderCodeGenerator)

import ast
from unittest import TestCase

from cinderx.compiler.pycodegen import compile as py_compile


class UnparseTests(TestCase):
    def test_simple_examples(self):
        examples = [
            "a",
            "+a",
            "-a",
            "not a",
            "~a",
            "x is y",
            "x is not y",
            "x < y",
            "x > y",
            "x <= y",
            "x >= y",
            "x == y",
            "x != y",
            "None",
            "True",
            "False",
            "...",
            "42.0",
            "42j",
            "'abc'",
            "b'abc'",
            "foo.bar",
            "42 .bar",
            "42.0.bar",
            "42j.bar",
            "()",
            "(1,)",
            "(1, 2)",
            "[]",
            "[1, 2]",
            "{1, 2}",
            "{}",
            "{1: 2}",
            "{1: 2, 3: 4}",
            "a + 2",
            "a - 2",
            "a * 2",
            "a @ 2",
            "a / 2",
            "a % 2",
            "a << 2",
            "a >> 2",
            "a | 2",
            "a ^ 2",
            "a // 2",
            "a ** 2",
            "a[b]",
            "a[b:c]",
            "a[:c]",
            "a[b:]",
            "a[:]",
            "a[a:b:c]",
            "a[::c]",
            "a[b,]",
            "a[b, c:d]",
            "a(b)",
            "a(b, c)",
            "a(b, *c)",
            "a(b, *c, **foo)",
            "a(b=1)",
            "a(b=1, **foo)",
            "a(a, b=1, **foo)",
            "a(a, b=1, **foo)",
            "lambda: 42",
            "lambda a: 42",
            "lambda a=2: 42",
            "lambda*foo: 42",  # oddity of how CPython unparses this...
            "lambda a, *foo: 42",
            "lambda a, *, b=2: 42",
            "lambda*, b: 42",  # oddity of how CPython unparses this...
            "lambda*, b=2: 42",  # oddity of how CPython unparses this...
            "lambda x: (x, x)",
            "1 if True else 2",
            "f'foo'",
            "f'foo{bar}'",
            "f'foo{bar!a}'",
            # joined strings get combined
            (
                "f'foo{bar:N}'f'foo{bar:N}'",
                "f'foo{bar:N}foo{bar:N}'",
            ),
            "f'foo{ {2: 3}}'",
            "f'{(lambda x: x)}'",
            "[x for x in abc]",
            "{x for x in abc}",
            "{x: y for x, y in abc}",
            "[x for x in abc if x]",
            "{x for x in abc if x}",
            "{x: y for x, y in abc if x}",
            "[x for x in abc for z in foo]",
            "{x for x in abc for z in foo}",
            "{x: y for x, y in abc for z in foo}",
            "[*abc]",
            "(x for x in y)",
            "Generator[None, None, None]",
        ]
        positions = [
            (
                "return",
                "from __future__ import annotations\ndef f() -> {example}:\n    pass",
                lambda g: g["f"].__annotations__["return"],
            ),
            (
                "argument",
                "from __future__ import annotations\ndef f(x: {example}):\n    pass",
                lambda g: g["f"].__annotations__["x"],
            ),
            (
                "assignment",
                "from __future__ import annotations\nclass C:\n    x: {example}",
                lambda g: g["C"].__annotations__["x"],
            ),
        ]

        for example in examples:
            if isinstance(example, tuple):
                example, expected = example
            else:
                expected = example
            for position, template, fetcher in positions:
                with self.subTest(example=example, position=position):

                    full = template.format(example=example)
                    code = py_compile(full, "foo.py", "exec")
                    py_globals = {}
                    exec(code, py_globals, py_globals)
                    # self.assertEqual(expected, fetcher(py_globals))

                    # Make sure we match CPython's compilation too
                    c_globals = {}
                    exec(full, c_globals, c_globals)
                    self.assertEqual(expected, fetcher(c_globals))

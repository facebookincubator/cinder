import unittest
import dis
import io
import os
import re
import contextlib

from cinderx.opcode import shadowop

def compile_and_get(code_str, funcname):
    _tmp_globals = {}
    code = compile(code_str, __file__, "exec")
    exec(code, _tmp_globals)
    return _tmp_globals[funcname]


bug1333982_str = """
def bug1333982(x=[]):
    assert 0, ([s for s in x] +
              1)
    pass
"""
bug1333982 = compile_and_get(bug1333982_str, "bug1333982")

dis_bug1333982 = """\
%3d           0 LOAD_ASSERTION_ERROR
              2 LOAD_CONST               2 (<code object <listcomp> at 0x..., file "%s", line %d>)
              4 LOAD_CONST               3 ('bug1333982.<locals>.<listcomp>')
              6 MAKE_FUNCTION            0
              8 LOAD_FAST                0 (x)
             10 GET_ITER
             12 CALL_FUNCTION            1

%3d          14 LOAD_CONST               4 (1)

%3d          16 BINARY_ADD
             18 CALL_FUNCTION            1
             20 RAISE_VARARGS            1
""" % (bug1333982.__code__.co_firstlineno + 1,
       __file__,
       bug1333982.__code__.co_firstlineno + 1,
       bug1333982.__code__.co_firstlineno + 2,
       bug1333982.__code__.co_firstlineno + 1)

dis_bug1333982_with_inline_comprehensions = """\
%3d           0 LOAD_ASSERTION_ERROR
              2 BUILD_LIST               0
              4 LOAD_FAST                0 (x)
              6 GET_ITER
        >>    8 FOR_ITER                 4 (to 18)
             10 STORE_FAST               1 (s)
             12 LOAD_FAST                1 (s)
             14 LIST_APPEND              2
             16 JUMP_ABSOLUTE            4 (to 8)
        >>   18 DELETE_FAST              1 (s)

%3d          20 LOAD_CONST               2 (1)

%3d          22 BINARY_ADD
             24 CALL_FUNCTION            1
             26 RAISE_VARARGS            1
""" % (bug1333982.__code__.co_firstlineno + 1,
       bug1333982.__code__.co_firstlineno + 2,
       bug1333982.__code__.co_firstlineno + 1)


_h_str = """
def _h(y):
    def foo(x):
        '''funcdoc'''
        return [x + z for z in y]
    return foo
"""
_h = compile_and_get(_h_str, "_h")

dis_nested_0 = """\
%3d           0 LOAD_CLOSURE             0 (y)
              2 BUILD_TUPLE              1
              4 LOAD_CONST               1 (<code object foo at 0x..., file "%s", line %d>)
              6 LOAD_CONST               2 ('_h.<locals>.foo')
              8 MAKE_FUNCTION            8 (closure)
             10 STORE_FAST               1 (foo)

%3d          12 LOAD_FAST                1 (foo)
             14 RETURN_VALUE
""" % (_h.__code__.co_firstlineno + 1,
       __file__,
       _h.__code__.co_firstlineno + 1,
       _h.__code__.co_firstlineno + 4,
)

dis_nested_1 = """%s
Disassembly of <code object foo at 0x..., file "%s", line %d>:
%3d           0 LOAD_CLOSURE             0 (x)
              2 BUILD_TUPLE              1
              4 LOAD_CONST               1 (<code object <listcomp> at 0x..., file "%s", line %d>)
              6 LOAD_CONST               2 ('_h.<locals>.foo.<locals>.<listcomp>')
              8 MAKE_FUNCTION            8 (closure)
             10 LOAD_DEREF               1 (y)
             12 GET_ITER
             14 CALL_FUNCTION            1
             16 RETURN_VALUE
""" % (dis_nested_0,
       __file__,
       _h.__code__.co_firstlineno + 1,
       _h.__code__.co_firstlineno + 3,
       __file__,
       _h.__code__.co_firstlineno + 3,
)

dis_nested_1_with_inline_comprehensions = """%s
Disassembly of <code object foo at 0x..., file "%s", line %d>:
%3d           0 BUILD_LIST               0
              2 LOAD_DEREF               0 (y)
              4 GET_ITER
        >>    6 FOR_ITER                 6 (to 20)
              8 STORE_FAST               1 (z)
             10 LOAD_FAST                0 (x)
             12 LOAD_FAST                1 (z)
             14 BINARY_ADD
             16 LIST_APPEND              2
             18 JUMP_ABSOLUTE            3 (to 6)
        >>   20 DELETE_FAST              1 (z)
             22 RETURN_VALUE
""" % (dis_nested_0,
       __file__,
       _h.__code__.co_firstlineno + 1,
       _h.__code__.co_firstlineno + 3,
)


class CinderX_DisTests(unittest.TestCase):
    _inline_comprehensions = os.getenv("PYTHONINLINECOMPREHENSIONS")

    maxDiff = None

    def get_disassembly(self, func, lasti=-1, wrapper=True, **kwargs):
        # We want to test the default printing behaviour, not the file arg
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            if wrapper:
                dis.dis(func, **kwargs)
            else:
                dis.disassemble(func, lasti, **kwargs)
        return output.getvalue()

    def strip_addresses(self, text):
        return re.sub(r'\b0x[0-9A-Fa-f]+\b', '0x...', text)

    def do_disassembly_test(self, func, expected):
        got = self.get_disassembly(func, depth=0)
        if got != expected:
            got = self.strip_addresses(got)
        self.assertEqual(got, expected)

    def test_widths(self):
        for opcode, opname in enumerate(dis.opname):
            if (
                opname
                in (
                    "BUILD_MAP_UNPACK_WITH_CALL",
                    "BUILD_TUPLE_UNPACK_WITH_CALL",
                    "JUMP_IF_NONZERO_OR_POP",
                    "JUMP_IF_NOT_EXC_MATCH",
                )
                or opcode in shadowop
            ):
                continue
            with self.subTest(opname=opname):
                width = dis._OPNAME_WIDTH
                if opcode < dis.HAVE_ARGUMENT:
                    width += 1 + dis._OPARG_WIDTH
                self.assertLessEqual(len(opname), width)

    def test_bug_1333982(self):
        # This one is checking bytecodes generated for an `assert` statement,
        # so fails if the tests are run with -O.  Skip this test then.
        if not __debug__:
            self.skipTest('need asserts, run without -O')

        # CinderX: Conditional skip for inline comprehensions
        if self._inline_comprehensions:
            self.do_disassembly_test(bug1333982, dis_bug1333982_with_inline_comprehensions)
        else:
            self.do_disassembly_test(bug1333982, dis_bug1333982)

    def test_disassemble_recursive(self):
        def check(expected, **kwargs):
            dis = self.get_disassembly(_h, **kwargs)
            dis = self.strip_addresses(dis)
            self.assertEqual(dis, expected)

        check(dis_nested_0, depth=0)
        # CinderX: Conditional skip for inline comprehensions
        if self._inline_comprehensions:
            check(dis_nested_1_with_inline_comprehensions, depth=1)
        else:
            check(dis_nested_1, depth=1)

class CinderX_DisWithFileTests(CinderX_DisTests):

    # Run the tests again, using the file arg instead of print
    def get_disassembly(self, func, lasti=-1, wrapper=True, **kwargs):
        output = io.StringIO()
        if wrapper:
            dis.dis(func, file=output, **kwargs)
        else:
            dis.disassemble(func, lasti, file=output, **kwargs)
        return output.getvalue()


if __name__ == "__main__":
    unittest.main()

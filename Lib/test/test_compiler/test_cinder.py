import dis
from compiler.pycodegen import compile as py_compile
from textwrap import dedent

from ..test_dis import DisTests


class CinderDisTests(DisTests):
    def test_super_zero_args(self):
        src = """
        class C:
            def f(self): super().f1()
        """
        expected = """\
  3           0 LOAD_GLOBAL              0 (super)
              2 LOAD_DEREF               0 (__class__)
              4 LOAD_FAST                0 (self)
              6 LOAD_METHOD_SUPER        1 ((1, True))
              8 CALL_METHOD              0
             10 POP_TOP
             12 LOAD_CONST               0 (None)
             14 RETURN_VALUE
"""

        g = {}
        exec(py_compile(dedent(src), "<string>", "exec"), g)
        self.do_disassembly_test(g["C"].f, expected)

    def test_super_zero_args_load_attr(self):
        src = """
        class C:
            def f(self): super().f1(a=1)
        """
        expected = """\
  3           0 LOAD_GLOBAL              0 (super)
              2 LOAD_DEREF               0 (__class__)
              4 LOAD_FAST                0 (self)
              6 LOAD_ATTR_SUPER          1 ((1, True))
              8 LOAD_CONST               2 (1)
             10 LOAD_CONST               3 (('a',))
             12 CALL_FUNCTION_KW         1
             14 POP_TOP
             16 LOAD_CONST               0 (None)
             18 RETURN_VALUE
"""
        g = {}
        exec(py_compile(dedent(src), "<string>", "exec"), g)
        self.do_disassembly_test(g["C"].f, expected)

    def test_super_two_args(self):
        src = """
        class C:
            def f(self): super(C, self).f1()
        """
        expected = """\
  3           0 LOAD_GLOBAL              0 (super)
              2 LOAD_GLOBAL              1 (C)
              4 LOAD_FAST                0 (self)
              6 LOAD_METHOD_SUPER        1 ((2, False))
              8 CALL_METHOD              0
             10 POP_TOP
             12 LOAD_CONST               0 (None)
             14 RETURN_VALUE
"""
        g = {}
        exec(py_compile(dedent(src), "<string>", "exec"), g)
        self.do_disassembly_test(g["C"].f, expected)

    def test_super_zero_method_args(self):
        src = """
        class C:
            def f(): super().f1()
        """
        expected = """\
  3           0 LOAD_GLOBAL              0 (super)
              2 CALL_FUNCTION            0
              4 LOAD_METHOD              1 (f1)
              6 CALL_METHOD              0
              8 POP_TOP
             10 LOAD_CONST               0 (None)
             12 RETURN_VALUE
"""
        g = {}
        exec(py_compile(dedent(src), "<string>", "exec"), g)
        self.do_disassembly_test(g["C"].f, expected)

    def test_super_two_args_attr(self):
        src = """
        class C:
            def f(self): super(C, self).f1(a=1)
        """
        expected = """\
  3           0 LOAD_GLOBAL              0 (super)
              2 LOAD_GLOBAL              1 (C)
              4 LOAD_FAST                0 (self)
              6 LOAD_ATTR_SUPER          1 ((2, False))
              8 LOAD_CONST               2 (1)
             10 LOAD_CONST               3 (('a',))
             12 CALL_FUNCTION_KW         1
             14 POP_TOP
             16 LOAD_CONST               0 (None)
             18 RETURN_VALUE
"""
        g = {}
        exec(py_compile(dedent(src), "<string>", "exec"), g)
        self.do_disassembly_test(g["C"].f, expected)

    def test_super_attr_load(self):
        src = """
        class C:
            def f(self): return super().x
        """
        expected = """\
  3           0 LOAD_GLOBAL              0 (super)
              2 LOAD_DEREF               0 (__class__)
              4 LOAD_FAST                0 (self)
              6 LOAD_ATTR_SUPER          1 ((1, True))
              8 RETURN_VALUE
"""
        g = {}
        exec(py_compile(dedent(src), "<string>", "exec"), g)
        self.do_disassembly_test(g["C"].f, expected)
        expected_dis_info = """\
Name:              f
Filename:          <string>
Argument count:    1
Positional-only arguments: 0
Kw-only arguments: 0
Number of locals:  1
Stack size:        3
Flags:             OPTIMIZED, NEWLOCALS
Constants:
   0: None
   1: (1, True)
Names:
   0: super
   1: x
Variable names:
   0: self
Free variables:
   0: __class__"""
        self.assertEqual(dedent(dis.code_info(g["C"].f)), expected_dis_info)

    def test_super_attr_store(self):
        src = """
        class C:
            def f(self): super().x = 1
        """
        expected = """\
  3           0 LOAD_CONST               1 (1)
              2 LOAD_GLOBAL              0 (super)
              4 CALL_FUNCTION            0
              6 STORE_ATTR               1 (x)
              8 LOAD_CONST               0 (None)
             10 RETURN_VALUE
"""
        g = {}
        exec(py_compile(dedent(src), "<string>", "exec"), g)
        self.do_disassembly_test(g["C"].f, expected)
        expected_dis_info = """\
Name:              f
Filename:          <string>
Argument count:    1
Positional-only arguments: 0
Kw-only arguments: 0
Number of locals:  1
Stack size:        2
Flags:             OPTIMIZED, NEWLOCALS, SUPPRESS_JIT
Constants:
   0: None
   1: 1
Names:
   0: super
   1: x
Variable names:
   0: self
Free variables:
   0: __class__"""
        self.assertEqual(dedent(dis.code_info(g["C"].f)), expected_dis_info)

    def test_super_as_global_1(self):
        src = """
        super = 1
        class C:
            def f(self): super().f1(a=1)
        """
        expected = """\
  4           0 LOAD_GLOBAL              0 (super)
              2 LOAD_DEREF               0 (__class__)
              4 LOAD_FAST                0 (self)
              6 LOAD_ATTR_SUPER          1 ((1, True))
              8 LOAD_CONST               2 (1)
             10 LOAD_CONST               3 (('a',))
             12 CALL_FUNCTION_KW         1
             14 POP_TOP
             16 LOAD_CONST               0 (None)
             18 RETURN_VALUE
"""
        g = {}
        exec(py_compile(dedent(src), "<string>", "exec"), g)
        self.do_disassembly_test(g["C"].f, expected)
        expected_dis_info = """\
Name:              f
Filename:          <string>
Argument count:    1
Positional-only arguments: 0
Kw-only arguments: 0
Number of locals:  1
Stack size:        3
Flags:             OPTIMIZED, NEWLOCALS
Constants:
   0: None
   1: (1, True)
   2: 1
   3: ('a',)
Names:
   0: super
   1: f1
Variable names:
   0: self
Free variables:
   0: __class__"""
        self.assertEqual(dedent(dis.code_info(g["C"].f)), expected_dis_info)

    def test_super_as_local_1(self):
        src = """
        class C:
            def f(self, super):
                super().f1(a=1)
        """
        expected = """\
  4           0 LOAD_FAST                1 (super)
              2 CALL_FUNCTION            0
              4 LOAD_ATTR                0 (f1)
              6 LOAD_CONST               1 (1)
              8 LOAD_CONST               2 (('a',))
             10 CALL_FUNCTION_KW         1
             12 POP_TOP
             14 LOAD_CONST               0 (None)
             16 RETURN_VALUE
"""
        g = {}
        exec(py_compile(dedent(src), "<string>", "exec"), g)
        self.do_disassembly_test(g["C"].f, expected)
        expected_dis_info = """\
Name:              f
Filename:          <string>
Argument count:    2
Positional-only arguments: 0
Kw-only arguments: 0
Number of locals:  2
Stack size:        3
Flags:             OPTIMIZED, NEWLOCALS
Constants:
   0: None
   1: 1
   2: ('a',)
Names:
   0: f1
Variable names:
   0: self
   1: super
Free variables:
   0: __class__"""
        self.assertEqual(dedent(dis.code_info(g["C"].f)), expected_dis_info)

    def test_super_as_local_2(self):
        src = """
        def f():
            super = lambda: 1
            class C:
                def f(self):
                    super().f1(a=1)
            return C
        C = f()
        """
        expected = """\
  6           0 LOAD_DEREF               1 (super)
              2 CALL_FUNCTION            0
              4 LOAD_ATTR                0 (f1)
              6 LOAD_CONST               1 (1)
              8 LOAD_CONST               2 (('a',))
             10 CALL_FUNCTION_KW         1
             12 POP_TOP
             14 LOAD_CONST               0 (None)
             16 RETURN_VALUE
"""
        g = {}
        exec(py_compile(dedent(src), "<string>", "exec"), g)
        self.do_disassembly_test(g["C"].f, expected)

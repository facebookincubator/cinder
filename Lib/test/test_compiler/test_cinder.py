import dis
from compiler.pycodegen import compile as py_compile
from textwrap import dedent

from .. import test_dis


class DualCompilerDisTests(test_dis.DisTests):
    compiler = staticmethod(compile)

    def compile(self, code_str):
        return self.compiler(dedent(code_str), "<string>", "exec")


class LoadSuperTests(DualCompilerDisTests):
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
        exec(self.compile(src), g)
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
        exec(self.compile(src), g)
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
        exec(self.compile(src), g)
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
        exec(self.compile(src), g)
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
        exec(self.compile(src), g)
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
        exec(self.compile(src), g)
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
        exec(self.compile(src), g)
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
        exec(self.compile(src), g)
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
        exec(self.compile(src), g)
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
        exec(self.compile(src), g)
        self.do_disassembly_test(g["C"].f, expected)


class LoadSuperPyCompilerTests(LoadSuperTests):
    compiler = staticmethod(py_compile)


class ComprehensionInlinerTests(DualCompilerDisTests):
    def test_sync_comp_top(self):
        # ensure module level comprehensions are not inlined
        src = """
        [x for x in lst]
        """
        expected = """\
  2           0 LOAD_CONST               0 (<code object <listcomp> at 0x..., file "<string>", line 2>)
              2 LOAD_CONST               1 ('<listcomp>')
              4 MAKE_FUNCTION            0
              6 LOAD_NAME                0 (lst)
              8 GET_ITER
             10 CALL_FUNCTION            1
             12 POP_TOP
             14 LOAD_CONST               2 (None)
             16 RETURN_VALUE
"""
        co = self.compile(src)
        self.do_disassembly_test(co, expected)

    def test_inline_sync_comp_nested_diff_scopes_1(self):
        src = """
        def f():
            [x for x in lst]
            [lambda: x for x in lst]
        """
        expected = """\
  3           0 BUILD_LIST               0
              2 LOAD_GLOBAL              0 (lst)
              4 GET_ITER
        >>    6 FOR_ITER                 4 (to 16)
              8 STORE_DEREF              0 (x)
             10 LOAD_DEREF               0 (x)
             12 LIST_APPEND              2
             14 JUMP_ABSOLUTE            3 (to 6)
        >>   16 POP_TOP

  4          18 BUILD_LIST               0
             20 LOAD_GLOBAL              0 (lst)
             22 GET_ITER
        >>   24 FOR_ITER                 8 (to 42)
             26 STORE_DEREF              0 (x)
             28 LOAD_CLOSURE             0 (x)
             30 BUILD_TUPLE              1
             32 LOAD_CONST               1 (<code object <lambda> at 0x..., file "<string>", line 4>)
             34 LOAD_CONST               2 ('f.<locals>.<lambda>')
             36 MAKE_FUNCTION            8 (closure)
             38 LIST_APPEND              2
             40 JUMP_ABSOLUTE           12 (to 24)
        >>   42 POP_TOP
             44 LOAD_CONST               0 (None)
             46 RETURN_VALUE
"""
        g = {}
        exec(self.compile(src), g)
        self.do_disassembly_test(g["f"], expected)

    def test_inline_sync_comp_nested_diff_scopes_2(self):
        src = """
        def f():
            [lambda: x for x in lst]
            [x for x in lst]
        """
        expected = """\
  3           0 BUILD_LIST               0
              2 LOAD_GLOBAL              0 (lst)
              4 GET_ITER
        >>    6 FOR_ITER                 8 (to 24)
              8 STORE_DEREF              0 (x)
             10 LOAD_CLOSURE             0 (x)
             12 BUILD_TUPLE              1
             14 LOAD_CONST               1 (<code object <lambda> at 0x..., file "<string>", line 3>)
             16 LOAD_CONST               2 ('f.<locals>.<lambda>')
             18 MAKE_FUNCTION            8 (closure)
             20 LIST_APPEND              2
             22 JUMP_ABSOLUTE            3 (to 6)
        >>   24 POP_TOP

  4          26 BUILD_LIST               0
             28 LOAD_GLOBAL              0 (lst)
             30 GET_ITER
        >>   32 FOR_ITER                 4 (to 42)
             34 STORE_DEREF              0 (x)
             36 LOAD_DEREF               0 (x)
             38 LIST_APPEND              2
             40 JUMP_ABSOLUTE           16 (to 32)
        >>   42 POP_TOP
             44 LOAD_CONST               0 (None)
             46 RETURN_VALUE
"""
        g = {}
        exec(self.compile(src), g)
        self.do_disassembly_test(g["f"], expected)

    def test_inline_sync_comp_nested_comprehensions(self):
        src = """
        def f():
            [x for x in [y for y in lst]]
        """
        expected = """\
  3           0 BUILD_LIST               0
              2 BUILD_LIST               0
              4 LOAD_GLOBAL              0 (lst)
              6 GET_ITER
        >>    8 FOR_ITER                 4 (to 18)
             10 STORE_FAST               0 (y)
             12 LOAD_FAST                0 (y)
             14 LIST_APPEND              2
             16 JUMP_ABSOLUTE            4 (to 8)
        >>   18 DELETE_FAST              0 (y)
             20 GET_ITER
        >>   22 FOR_ITER                 4 (to 32)
             24 STORE_FAST               1 (x)
             26 LOAD_FAST                1 (x)
             28 LIST_APPEND              2
             30 JUMP_ABSOLUTE           11 (to 22)
        >>   32 DELETE_FAST              1 (x)
             34 POP_TOP
             36 LOAD_CONST               0 (None)
             38 RETURN_VALUE
"""
        g = {}
        exec(self.compile(src), g)
        self.do_disassembly_test(g["f"], expected)

    def test_inline_sync_comp_named_expr_1(self):
        src = """
        def f():
            [x for x in lst if (z := 5)]
        """
        expected = """\
  3           0 BUILD_LIST               0
              2 LOAD_GLOBAL              0 (lst)
              4 GET_ITER
        >>    6 FOR_ITER                 8 (to 24)
              8 STORE_FAST               0 (x)
             10 LOAD_CONST               1 (5)
             12 DUP_TOP
             14 STORE_FAST               1 (z)
             16 POP_JUMP_IF_FALSE        3 (to 6)
             18 LOAD_FAST                0 (x)
             20 LIST_APPEND              2
             22 JUMP_ABSOLUTE            3 (to 6)
        >>   24 DELETE_FAST              0 (x)
             26 POP_TOP
             28 LOAD_CONST               0 (None)
             30 RETURN_VALUE
"""
        g = {}
        exec(self.compile(src), g)
        self.do_disassembly_test(g["f"], expected)

    def test_inline_async_comp_free_var1(self):
        src = """
async def f(lst):
    p = b'.'
    split_paths = [[c for c in s if c and c != o] async for s in lst]
        """
        expected = """\
              0 GEN_START                1

  3           2 LOAD_CONST               1 (b'.')
              4 STORE_FAST               1 (p)

  4           6 BUILD_LIST               0
              8 LOAD_FAST                0 (lst)
             10 GET_AITER
        >>   12 SETUP_FINALLY           22 (to 58)
             14 GET_ANEXT
             16 LOAD_CONST               0 (None)
             18 YIELD_FROM
             20 POP_BLOCK
             22 STORE_FAST               2 (s)
             24 BUILD_LIST               0
             26 LOAD_FAST                2 (s)
             28 GET_ITER
        >>   30 FOR_ITER                10 (to 52)
             32 STORE_FAST               3 (c)
             34 LOAD_FAST                3 (c)
             36 POP_JUMP_IF_FALSE       15 (to 30)
             38 LOAD_FAST                3 (c)
             40 LOAD_GLOBAL              0 (o)
             42 COMPARE_OP               3 (!=)
             44 POP_JUMP_IF_FALSE       15 (to 30)
             46 LOAD_FAST                3 (c)
             48 LIST_APPEND              2
             50 JUMP_ABSOLUTE           15 (to 30)
        >>   52 DELETE_FAST              3 (c)
             54 LIST_APPEND              2
             56 JUMP_ABSOLUTE            6 (to 12)
        >>   58 END_ASYNC_FOR
             60 DELETE_FAST              3 (c)
             62 DELETE_FAST              2 (s)
             64 STORE_FAST               4 (split_paths)
             66 LOAD_CONST               0 (None)
             68 RETURN_VALUE
"""
        g = {}
        exec(self.compile(src), g)
        self.do_disassembly_test(g["f"], expected)

    def test_comprehension_inlining_name_conflict_with_implicit_global(self):
        src = """
def f(lst):
    [x for x in lst]
    def g():
        return lambda: x
    return g
        """
        expected = """\
  3           0 LOAD_CONST               1 (<code object <listcomp> at 0x..., file "<string>", line 3>)
              2 LOAD_CONST               2 ('f.<locals>.<listcomp>')
              4 MAKE_FUNCTION            0
              6 LOAD_FAST                0 (lst)
              8 GET_ITER
             10 CALL_FUNCTION            1
             12 POP_TOP

  4          14 LOAD_CONST               3 (<code object g at 0x..., file "<string>", line 4>)
             16 LOAD_CONST               4 ('f.<locals>.g')
             18 MAKE_FUNCTION            0
             20 STORE_FAST               1 (g)

  6          22 LOAD_FAST                1 (g)
             24 RETURN_VALUE
"""

        g = {}
        exec(self.compile(src), g)
        self.do_disassembly_test(g["f"], expected)

    def test_use_param_1(self):
        src = """
def f(self, name, data, files=(), dirs=()):
    [os.path.join(dir, filename) for dir in files for filename in dir]
        """
        expected = """\
  3           0 BUILD_LIST               0
              2 LOAD_FAST                3 (files)
              4 GET_ITER
        >>    6 FOR_ITER                14 (to 36)
              8 STORE_FAST               5 (dir)
             10 LOAD_FAST                5 (dir)
             12 GET_ITER
        >>   14 FOR_ITER                 9 (to 34)
             16 STORE_FAST               6 (filename)
             18 LOAD_GLOBAL              0 (os)
             20 LOAD_ATTR                1 (path)
             22 LOAD_METHOD              2 (join)
             24 LOAD_FAST                5 (dir)
             26 LOAD_FAST                6 (filename)
             28 CALL_METHOD              2
             30 LIST_APPEND              3
             32 JUMP_ABSOLUTE            7 (to 14)
        >>   34 JUMP_ABSOLUTE            3 (to 6)
        >>   36 DELETE_FAST              5 (dir)
             38 DELETE_FAST              6 (filename)
             40 POP_TOP
             42 LOAD_CONST               0 (None)
             44 RETURN_VALUE
"""
        g = {}
        exec(self.compile(src), g)
        self.do_disassembly_test(g["f"], expected)

    def test_inline_comp_global1(self):
        src = """
            g = 1
            def f():
                actual = {{g: None for g in range(10)}}
                assert g == 1
        """
        expected = """\
  4           0 LOAD_CONST               1 (<code object <dictcomp> at 0x..., file "<string>", line 4>)
              2 LOAD_CONST               2 ('f.<locals>.<dictcomp>')
              4 MAKE_FUNCTION            0
              6 LOAD_GLOBAL              0 (range)
              8 LOAD_CONST               3 (10)
             10 CALL_FUNCTION            1
             12 GET_ITER
             14 CALL_FUNCTION            1
             16 BUILD_SET                1
             18 STORE_FAST               0 (actual)

  5          20 LOAD_GLOBAL              1 (g)
             22 LOAD_CONST               4 (1)
             24 COMPARE_OP               2 (==)
             26 POP_JUMP_IF_TRUE        16 (to 32)
             28 LOAD_ASSERTION_ERROR
             30 RAISE_VARARGS            1
        >>   32 LOAD_CONST               0 (None)
             34 RETURN_VALUE
"""
        g = {}
        exec(self.compile(src), g)
        self.do_disassembly_test(g["f"], expected)


class ComprehensionInlinerPyCompilerTests(ComprehensionInlinerTests):
    compiler = staticmethod(py_compile)

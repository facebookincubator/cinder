# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

import ast
import unittest
from textwrap import dedent

from _strictmodule import StrictModuleLoader, StrictAnalysisResult


class StrictModuleTest(unittest.TestCase):
    def get_loader(self):
        return StrictModuleLoader([], "", [], [])

    def test_check(self):
        source = """
        import __strict__
        x = 1
        """
        loader = self.get_loader()
        res = loader.check_source(dedent(source), "a.py", "a", [])
        self.assertTrue(res.is_valid)
        self.assertEqual(len(res.errors), 0)
        self.assertEqual(res.module_name, "a")
        self.assertEqual(res.file_name, "a.py")
        self.assertEqual(res.module_kind, 1)
        self.assertEqual(res.stub_kind, 0)

    def test_ast_get(self):
        source = """
        import __strict__
        from __strict__ import strict_slots
        @strict_slots
        class C:
            pass
        """
        loader = self.get_loader()
        res = loader.check_source(dedent(source), "a", "a.py", [])
        self.assertEqual(
            ast.dump(res.ast),
            "Module(body=[Import("
            + "names=[alias(name='__strict__')]), "
            + "ImportFrom(module='__strict__', names=[alias(name='strict_slots')], level=0), "
            + "ClassDef(name='C', bases=[], keywords=[], body=[Pass()], "
            + "decorator_list=[Name(id='strict_slots', ctx=Load())])], type_ignores=[])",
        )
        self.assertEqual(
            ast.dump(res.ast_preprocessed),
            "Module(body=[Import(names=[alias(name='__strict__')]),"
            + " ImportFrom(module='__strict__', names=[alias(name='strict_slots')], level=0),"
            + " ClassDef(name='C', bases=[], keywords=[], body=[Pass()],"
            + " decorator_list=[Name(id='strict_slots', ctx=Load()), Name(id='<enable_slots>', ctx=Load())])], type_ignores=[])",
        )


if __name__ == "__main__":
    unittest.main()

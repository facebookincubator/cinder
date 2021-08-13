from __future__ import annotations

import ast
import compiler.strict
import gc
import inspect
import sys
from compiler.strict import StrictCodeGenerator, strict_compile
from compiler.strict.common import FIXED_MODULES
from contextlib import contextmanager
from types import CodeType
from typing import Type

import cinder
from __strict__ import set_freeze_enabled
from test.test_compiler.common import CompilerTest


class StrictTestBase(CompilerTest):
    def compile(
        self,
        code,
        generator=StrictCodeGenerator,
        modname="<module>",
        optimize=0,
        peephole_enabled=True,
        ast_optimizer_enabled=True,
    ):
        if generator is not StrictCodeGenerator:
            return super().compile(
                code,
                generator,
                modname,
                optimize,
                peephole_enabled,
                ast_optimizer_enabled,
            )

        code = inspect.cleandoc("\n" + code)
        tree = ast.parse(code)
        return strict_compile(modname, f"{modname}.py", tree, optimize)

    _temp_mod_num = 0

    def _temp_mod_name(self):
        StrictTestBase._temp_mod_num += 1
        return sys._getframe().f_back.f_back.f_back.f_back.f_code.co_name + str(
            StrictTestBase._temp_mod_num
        )

    def _finalize_module(self, name, mod_dict=None):
        if name in sys.modules:
            del sys.modules[name]
        if mod_dict is not None:
            mod_dict.clear()
        gc.collect()

    @contextmanager
    def with_freeze_type_setting(self, freeze: bool):
        old_setting = set_freeze_enabled(freeze)
        try:
            yield
        finally:
            set_freeze_enabled(old_setting)

    def _exec_code(self, compiled: CodeType, name: str, additional_dicts=None):
        m = type(sys)(name)
        d = m.__dict__
        d["<fixed-modules>"] = FIXED_MODULES
        d.update(additional_dicts or {})
        sys.modules[name] = m
        exec(compiled, d)
        d["__name__"] = name
        return d, m

    def _in_module(
        self, code, name, code_gen, optimize, is_strict=False, enable_patching=False
    ):
        compiled = self.compile(code, code_gen, name, optimize)
        # if is_strict:
        #     return self._exec_strict_code(compiled, name, enable_patching)
        # else:
        return self._exec_code(compiled, name)

    @contextmanager
    def in_module(
        self,
        code,
        name=None,
        code_gen=StrictCodeGenerator,
        optimize=0,
        is_strict=False,
        enable_patching=False,
    ):
        d = None
        if name is None:
            name = self._temp_mod_name()
        try:
            d, m = self._in_module(
                code, name, code_gen, optimize, is_strict, enable_patching
            )
            yield m
        finally:
            self._finalize_module(name, d)

    def setUp(self):
        # ensure clean classloader/vtable slate for all tests
        cinder.clear_classloader_caches()

    def subTest(self, **kwargs):
        cinder.clear_classloader_caches()
        return super().subTest(**kwargs)

    @classmethod
    def setUpClass(cls):
        cls.strict_features = compiler.strict.enable_strict_features
        compiler.strict.enable_strict_features = True

    @classmethod
    def tearDownClass(cls):
        compiler.strict.enable_strict_features = cls.strict_features

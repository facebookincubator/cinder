from __future__ import annotations

import ast
import builtins

import cinder
import gc
import inspect
import sys
from cinder import cached_property
from contextlib import contextmanager
from types import CodeType
from typing import Any, Callable, Dict, Mapping, Optional, Tuple, Type
from unittest import skip

from cinderx.compiler.strict import strict_compile, StrictCodeGenerator
from cinderx.compiler.strict.common import FIXED_MODULES
from cinderx.compiler.strict.compiler import Compiler
from cinderx.compiler.strict.loader import StrictModule
from cinderx.compiler.strict.runtime import set_freeze_enabled

from test_cinderx.test_compiler.common import CompilerTest


class StrictTestBase(CompilerTest):
    def compile(
        self,
        code,
        generator=StrictCodeGenerator,
        modname="<module>",
        optimize=0,
        ast_optimizer_enabled=True,
        builtins: Dict[str, Any] = builtins.__dict__,
    ):
        if generator is not StrictCodeGenerator:
            return super().compile(
                code,
                generator,
                modname,
                optimize,
                ast_optimizer_enabled,
            )

        code = inspect.cleandoc("\n" + code)
        tree = ast.parse(code)
        return strict_compile(modname, f"{modname}.py", tree, optimize, builtins)

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


def init_cached_properties(
    cached_props: Mapping[str, str | Tuple[str, bool]]
) -> Callable[[Type[object]], Type[object]]:
    """replace the slots in a class with a cached property which uses the slot
    storage"""

    def f(cls: Type[object]) -> Type[object]:
        for name, mangled in cached_props.items():
            assert (
                isinstance(mangled, tuple)
                and len(mangled) == 2
                and isinstance(mangled[0], str)
            )
            if mangled[1] is not False:
                raise ValueError("wrong expected constant!", mangled[1])
            setattr(
                cls,
                name,
                cached_property(cls.__dict__[mangled[0]], cls.__dict__[name]),
            )
        return cls

    return f


class StrictTestWithCheckerBase(StrictTestBase):
    def check_and_compile(
        self,
        source,
        modname="<module>",
        optimize=0,
    ):
        compiler = Compiler([], "", [], [])
        source = inspect.cleandoc("\n" + source)
        code, is_valid_strict, _is_static = compiler.load_compiled_module_from_source(
            source, f"{modname}.py", modname, optimize
        )
        assert is_valid_strict
        return code

    def compile_and_run(
        self,
        code,
        generator=StrictCodeGenerator,
        modname="<module>",
        optimize=0,
        ast_optimizer_enabled=True,
        builtins: Dict[str, Any] = builtins.__dict__,
        globals: Optional[Dict[str, Any]] = None,
    ):
        c = self.compile(
            code,
            generator,
            modname,
            optimize,
            ast_optimizer_enabled,
            builtins,
        )

        additional_dicts = globals or {}
        additional_dicts.update({"<builtins>": builtins})
        d, m = self._exec_strict_code(
            c, modname, enable_patching=True, additional_dicts=additional_dicts
        )
        return m

    def _exec_strict_code(
        self,
        compiled: CodeType,
        name: str,
        enable_patching=False,
        additional_dicts=None,
    ):
        d = {"__name__": name, "<fixed-modules>": FIXED_MODULES}
        d.update(additional_dicts or {})
        m = StrictModule(d, enable_patching)
        d["<strict_module>"] = m
        sys.modules[name] = m
        exec(compiled, d)
        return d, m

    def _check_and_exec(
        self,
        source,
        modname="<module>",
        optimize=0,
    ):
        code = self.check_and_compile(source, modname, optimize)
        dict_for_rewriter = {
            "<builtins>": builtins.__dict__,
            "<init-cached-properties>": init_cached_properties,
        }
        return self._exec_strict_code(code, modname, additional_dicts=dict_for_rewriter)

    @contextmanager
    def in_checked_module(self, code, name=None, optimize=0):
        d = None
        if name is None:
            name = self._temp_mod_name()
        try:
            d, m = self._check_and_exec(code, name, optimize)
            yield m
        finally:
            self._finalize_module(name, d)

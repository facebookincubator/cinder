# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
from __future__ import annotations

import ast
from ast import NodeVisitor
from contextlib import nullcontext
from functools import cached_property
from os import path
from symtable import SymbolTable as PythonSymbolTable, SymbolTableFactory
from types import CodeType
from typing import (
    Callable,
    ContextManager,
    Iterable,
    List,
    Optional,
    Tuple,
    final,
    Dict,
    Set,
    Type,
    TYPE_CHECKING,
)

from _strictmodule import (
    StrictAnalysisResult,
    StrictModuleLoader,
    NONSTRICT_MODULE_KIND,
    STATIC_MODULE_KIND,
    STUB_KIND_MASK_TYPING,
)

from ..errors import TypedSyntaxError
from ..pycodegen import compile as python_compile
from ..static import Compiler as StaticCompiler, ModuleTable, StaticCodeGenerator
from . import strict_compile
from .class_conflict_checker import check_class_conflict
from .common import StrictModuleError
from .rewriter import StrictModuleRewriter, rewrite, remove_annotations

if TYPE_CHECKING:
    from _strictmodule import IStrictModuleLoader, StrictModuleLoaderFactory


def getSymbolTable(mod: StrictAnalysisResult) -> PythonSymbolTable:
    """
    Construct a symtable object from analysis result
    """
    return SymbolTableFactory()(mod.symtable, mod.file_name)


TIMING_LOGGER_TYPE = Callable[[str, str, str], ContextManager[None]]


@final
class Compiler(StaticCompiler):
    def __init__(
        self,
        import_path: Iterable[str],
        stub_root: str,
        allow_list_prefix: Iterable[str],
        allow_list_exact: Iterable[str],
        log_time_func: Optional[Callable[[], TIMING_LOGGER_TYPE]] = None,
        raise_on_error: bool = False,
        enable_patching: bool = False,
        loader_factory: StrictModuleLoaderFactory = StrictModuleLoader,
        use_py_compiler: bool = False,
    ) -> None:
        super().__init__(StaticCodeGenerator)
        self.import_path: List[str] = list(import_path)
        self.stub_root = stub_root
        self.allow_list_prefix = allow_list_prefix
        self.allow_list_exact = allow_list_exact
        self.loader: IStrictModuleLoader = loader_factory(
            self.import_path,
            str(stub_root),
            list(allow_list_prefix),
            list(allow_list_exact),
            True,
        )
        self.raise_on_error = raise_on_error
        self.log_time_func = log_time_func
        self.enable_patching = enable_patching
        self.ast_cache: Dict[str, ast.Module] = {}
        self.track_import_call: bool = False
        self.not_static: Set[str] = set()
        self.use_py_compiler = use_py_compiler

    def import_module(self, name: str, optimize: int) -> Optional[ModuleTable]:
        res = self.modules.get(name)
        if res is not None:
            return res

        if name in self.not_static:
            return None

        mod = self.loader.check(name)
        if mod.is_valid and name not in self.modules and len(mod.errors) == 0:
            modKind = mod.module_kind
            if modKind == STATIC_MODULE_KIND:
                root = mod.ast_preprocessed
                stubKind = mod.stub_kind
                if STUB_KIND_MASK_TYPING & stubKind:
                    root = remove_annotations(root)
                root = self._get_rewritten_ast(name, mod, root, optimize)
                log = self.log_time_func
                ctx = (
                    log()(name, mod.file_name, "declaration_visit")
                    if log
                    else nullcontext()
                )
                with ctx:
                    root = self.add_module(name, mod.file_name, root, optimize)
                self.ast_cache[name] = root
            else:
                self.not_static.add(name)

        return self.modules.get(name)

    def _get_rewritten_ast(
        self, name: str, mod: StrictAnalysisResult, root: ast.Module, optimize: int
    ) -> ast.Module:
        cached_ast = self.ast_cache.get(name)
        if cached_ast is None:
            symbols = getSymbolTable(mod)
            cached_ast = rewrite(
                root,
                symbols,
                mod.file_name,
                name,
                optimize=optimize,
                is_static=True,
                track_import_call=self.track_import_call,
            )
        return cached_ast

    def load_compiled_module_from_source(
        self,
        source: str | bytes,
        filename: str,
        name: str,
        optimize: int,
        submodule_search_locations: Optional[List[str]] = None,
        track_import_call: bool = False,
    ) -> Tuple[CodeType | None, StrictAnalysisResult]:
        mod = self.loader.check_source(
            source, filename, name, submodule_search_locations or []
        )
        errors = mod.errors
        is_valid_strict = (
            mod.is_valid
            and len(errors) == 0
            and mod.module_kind != NONSTRICT_MODULE_KIND
        )
        if errors and self.raise_on_error:
            # if raise on error, just raise the first error
            error = errors[0]
            raise StrictModuleError(error[0], error[1], error[2], error[3])
        elif is_valid_strict:
            symbols = getSymbolTable(mod)
            try:
                check_class_conflict(mod.ast, filename, symbols)
            except StrictModuleError as e:
                if self.raise_on_error:
                    raise
                mod.errors.append((e.msg, e.filename, e.lineno, e.col))

        if not is_valid_strict:
            code = self._compile_basic(mod.ast, filename, optimize)
        elif mod.module_kind == STATIC_MODULE_KIND:
            code = self._compile_static(
                mod, filename, name, optimize, track_import_call
            )
        else:
            code = self._compile_strict(
                mod, filename, name, optimize, track_import_call
            )

        return code, mod

    def _compile_basic(
        self, root: ast.Module, filename: str, optimize: int
    ) -> CodeType:
        compile_method = python_compile if self.use_py_compiler else compile
        return compile_method(
            root,
            filename,
            "exec",
            optimize=optimize,
        )

    def _compile_strict(
        self,
        mod: StrictAnalysisResult,
        filename: str,
        name: str,
        optimize: int,
        track_import_call: bool,
    ) -> CodeType:
        symbols = getSymbolTable(mod)
        tree = rewrite(
            mod.ast_preprocessed,
            symbols,
            filename,
            name,
            optimize=optimize,
            track_import_call=track_import_call,
        )
        return strict_compile(name, filename, tree, optimize)

    def _compile_static(
        self,
        mod: StrictAnalysisResult,
        filename: str,
        name: str,
        optimize: int,
        track_import_call: bool,
    ) -> CodeType | None:
        self.track_import_call = track_import_call
        root = self.ast_cache.get(name)
        if root is None:
            root = self._get_rewritten_ast(name, mod, mod.ast_preprocessed, optimize)
        code = None

        try:
            log = self.log_time_func
            ctx = log()(name, filename, "compile") if log else nullcontext()
            with ctx:
                code = self.compile(
                    name, filename, root, optimize, enable_patching=self.enable_patching
                )
        except TypedSyntaxError as e:
            err = StrictModuleError(
                e.msg or "unknown error during static compilation",
                e.filename or filename,
                e.lineno or 1,
                0,
            )
            mod.errors.append(err)
            if self.raise_on_error:
                raise err

        return code

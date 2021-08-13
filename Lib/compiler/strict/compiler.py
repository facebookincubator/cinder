# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
from __future__ import annotations

import ast
from ast import NodeVisitor
from functools import cached_property
from os import path

# pyre-fixme[21]: Could not find name `SymbolTableFactory` in `symtable` (stubbed).
from symtable import SymbolTable as PythonSymbolTable, SymbolTableFactory
from typing import (
    Callable,
    ContextManager,
    Iterable,
    List,
    Optional,
    Tuple,
    final,
    Dict,
)

# pyre-fixme[21]: Could not find module `_strictmodule`.
from _strictmodule import (
    StrictAnalysisResult,
    StrictModuleLoader,
    NONSTRICT_MODULE_KIND,
    STATIC_MODULE_KIND,
    STUB_KIND_MASK_TYPING,
)

from ..static import ModuleTable, SymbolTable, StaticCodeGenerator
from ..static.errors import TypedSyntaxError
from .rewriter import StrictModuleRewriter, rewrite, remove_annotations


# pyre-fixme[11]: Annotation `StrictAnalysisResult` is not defined as a type.
def getSymbolTable(mod: StrictAnalysisResult, filename: str) -> PythonSymbolTable:
    """
    Construct a symtable object from analysis result
    """
    # pyre-fixme[16]: symtable has no attribute SymbolTableFactory
    return SymbolTableFactory()(mod.symtable, filename)


TIMING_LOGGER_TYPE = Callable[[str, str, str], ContextManager[None]]


class StrictModuleError(Exception):
    def __init__(
        self, msg: str, filename: str, lineno: int, col: int, metadata: str = ""
    ) -> None:
        self.msg = msg
        self.filename = filename
        self.lineno = lineno
        self.col = col
        self.metadata = metadata


class Compiler:
    def __init__(
        self,
        import_path: Iterable[str],
        stub_root: str,
        allow_list_prefix: Iterable[str],
        allow_list_exact: Iterable[str],
        raise_on_error: bool = False,
    ) -> None:
        self.import_path: List[str] = list(import_path)
        self.stub_root = stub_root
        self.allow_list_prefix = allow_list_prefix
        self.allow_list_exact = allow_list_exact
        # pyre-fixme[11]: Annotation `StrictModuleLoader` is not defined as a type.
        self.loader: StrictModuleLoader = StrictModuleLoader(
            self.import_path,
            str(stub_root),
            list(allow_list_prefix),
            list(allow_list_exact),
            True,
        )
        self.raise_on_error = raise_on_error

    def load_compiled_module_from_source(
        self,
        source: str | bytes,
        filename: str,
        name: str,
        optimize: int,
        submodule_search_locations: Optional[List[str]] = None,
        track_import_call: bool = False,
    ) -> Tuple[object, StrictAnalysisResult]:
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
        return self._rewrite(
            mod, is_valid_strict, filename, name, optimize, track_import_call
        )

    def _rewrite(
        self,
        mod: StrictAnalysisResult,
        is_valid_strict: bool,
        filename: str,
        name: str,
        optimize: int,
        track_import_call: bool,
    ) -> Tuple[object, StrictAnalysisResult]:
        if is_valid_strict:
            symbols = getSymbolTable(mod, filename)
            code = rewrite(
                mod.ast_preprocessed,
                symbols,
                filename,
                name,
                "exec",
                optimize,
                track_import_call=track_import_call,
            )
        else:
            # no rewrite or preprocessing
            code = compile(
                mod.ast,
                filename,
                "exec",
                dont_inherit=True,
                optimize=optimize,
            )
        return code, mod


def rewrite_ast(
    root: ast.Module,
    symbols: PythonSymbolTable,
    filename: str,
    name: str,
    optimize: int,
    is_static: bool,
    track_import_call: bool,
) -> ast.Module:
    rewriter = StrictModuleRewriter(
        root,
        symbols,
        filename,
        name,
        "exec",
        optimize,
        is_static=is_static,
        track_import_call=track_import_call,
    )
    return rewriter.transform()


@final
class StrictSymbolTable(SymbolTable):
    def __init__(
        self,
        compiler: StaticCompiler,
        log_time_func: Optional[Callable[[], TIMING_LOGGER_TYPE]] = None,
    ) -> None:
        super().__init__(StaticCodeGenerator)
        self.compiler = compiler
        self.ast_cache: Dict[str, ast.AST] = {}
        self.optimize: Optional[int] = None
        self.track_import_call: Optional[bool] = None
        self.log_time_func = log_time_func

    # pyre-fixme[15]: inconsistent override of return type
    def import_module(self, name: str) -> Optional[ModuleTable]:
        loader = self.compiler.loader
        mod = loader.check(name)
        if mod.is_valid and name not in self.modules and len(mod.errors) == 0:
            modKind = mod.module_kind
            if modKind == STATIC_MODULE_KIND:
                root = mod.ast_preprocessed
                filename = mod.file_name
                symbols = getSymbolTable(mod, filename)
                stubKind = mod.stub_kind

                if STUB_KIND_MASK_TYPING & stubKind:
                    root = remove_annotations(root)
                root = rewrite_ast(
                    root,
                    symbols,
                    filename,
                    name,
                    self.optimize,
                    True,
                    self.track_import_call,
                )
                self.ast_cache[name] = root
                log_func = self.log_time_func
                if log_func:
                    with log_func()(name, filename, "declaration_visit"):
                        self.add_module(name, filename, root)
                else:
                    self.add_module(name, filename, root)

        return self.modules.get(name)


@final
class StaticCompiler(Compiler):
    def __init__(
        self,
        import_path: Iterable[str],
        stub_root: str,
        allow_list_prefix: Iterable[str],
        allow_list_exact: Iterable[str],
        log_time_func: Optional[Callable[[], TIMING_LOGGER_TYPE]] = None,
        raise_on_error: bool = False,
    ) -> None:
        super().__init__(
            import_path,
            stub_root,
            allow_list_prefix,
            allow_list_exact,
            raise_on_error,
        )
        self.symtable: SymbolTable = StrictSymbolTable(self)
        self.log_time_func = log_time_func

    def _rewrite(
        self,
        mod: StrictAnalysisResult,
        is_valid_strict: bool,
        filename: str,
        name: str,
        optimize: int,
        track_import_call: bool,
    ) -> Tuple[object, StrictAnalysisResult]:
        modKind = mod.module_kind
        if not is_valid_strict or modKind != STATIC_MODULE_KIND:
            return super()._rewrite(
                mod, is_valid_strict, filename, name, optimize, track_import_call
            )
        return self._rewrite_as_static(
            mod, is_valid_strict, filename, name, optimize, track_import_call
        )

    def _rewrite_as_static(
        self,
        mod: StrictAnalysisResult,
        is_valid_strict: bool,
        filename: str,
        name: str,
        optimize: int,
        track_import_call: bool,
    ) -> Tuple[object, StrictAnalysisResult]:
        # pyre-fixme [16]: no attribute optimize
        self.symtable.optimize = optimize
        # pyre-fixme [16]: no attribute track_import_call
        self.symtable.track_import_call = track_import_call
        # pyre-fixme [16]: no attribute ast_cache
        root = self.symtable.ast_cache.get(name)
        if root is None:
            symbols = getSymbolTable(mod, filename)
            if symbols is None:
                raise TypeError("expected SymbolTable, got None")
            # Perform the normal strict modules re-write, minus slotification
            root = rewrite_ast(
                mod.ast_preprocessed,
                symbols,
                filename,
                name,
                optimize,
                True,
                track_import_call,
            )
        code = None

        try:
            log_func = self.log_time_func
            if log_func:
                with log_func()(name, filename, "compile"):
                    code = self.symtable.compile(name, filename, root, optimize)
            else:
                code = self.symtable.compile(name, filename, root, optimize)
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

        return code, mod

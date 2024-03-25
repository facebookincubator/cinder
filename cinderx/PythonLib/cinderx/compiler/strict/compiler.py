# Copyright (c) Meta Platforms, Inc. and affiliates.

from __future__ import annotations

import ast
import builtins
import logging
import os
import symtable
import sys
from contextlib import nullcontext
from dataclasses import dataclass
from symtable import SymbolTable as PythonSymbolTable, SymbolTableFactory
from types import CodeType
from typing import (
    Callable,
    ContextManager,
    Dict,
    final,
    Iterable,
    List,
    Optional,
    Set,
    Tuple,
    TYPE_CHECKING,
)

from cinderx.strictmodule import (
    NONSTRICT_MODULE_KIND,
    STATIC_MODULE_KIND,
    StrictAnalysisResult,
    StrictModuleLoader,
    STUB_KIND_MASK_TYPING,
)

from ..errors import TypedSyntaxError
from ..pycodegen import compile as python_compile
from ..static import Compiler as StaticCompiler, ModuleTable, StaticCodeGenerator
from . import _static_module_ported, strict_compile
from .class_conflict_checker import check_class_conflict
from .common import StrictModuleError
from .flag_extractor import FlagExtractor, Flags
from .rewriter import remove_annotations, rewrite

if _static_module_ported:
    from cinderx.static import __build_cinder_class__
else:
    from __static__ import __build_cinder_class__

if TYPE_CHECKING:
    from cinderx.strictmodule import IStrictModuleLoader, StrictModuleLoaderFactory


def getSymbolTable(mod: StrictAnalysisResult) -> PythonSymbolTable:
    """
    Construct a symtable object from analysis result
    """
    return SymbolTableFactory()(mod.symtable, mod.file_name)


TIMING_LOGGER_TYPE = Callable[[str, str, str], ContextManager[None]]


@dataclass(frozen=True)
class SourceInfo:
    """Data about a Python module source file."""

    # module name
    name: str
    # import path prefix within which the module was found
    prefix: str
    # path to module file, relative to import path prefix
    relative_path: str
    # should always be os.path.join(prefix, relative_path)
    path: str
    # data we might need for pyc invalidation
    source: bytes
    mtime: int
    size: int


@dataclass(frozen=True)
class Dependencies:
    static: list[SourceInfo]
    nonstatic: list[str]


NOT_FOUND = object()


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
        allow_list_regex: Optional[Iterable[str]] = None,
    ) -> None:
        super().__init__(StaticCodeGenerator)
        self.import_path: List[str] = list(import_path)
        self.stub_root = stub_root
        self.allow_list_prefix = allow_list_prefix
        self.allow_list_exact = allow_list_exact
        self.allow_list_regex: Iterable[str] = allow_list_regex or []
        self.verbose = bool(
            os.getenv("PYTHONSTRICTVERBOSE")
            or sys._xoptions.get("strict-verbose") is True
        )
        self.disable_analysis = bool(
            os.getenv("PYTHONSTRICTDISABLEANALYSIS")
            or sys._xoptions.get("strict-disable-analysis") is True
        )
        self.loader: IStrictModuleLoader = loader_factory(
            self.import_path,
            str(stub_root),
            list(allow_list_prefix),
            list(allow_list_exact),
            True,  # _load_strictmod_builtin
            list(self.allow_list_regex),
            self.verbose,  # _verbose_logging
            self.disable_analysis,  # _disable_analysis
        )
        self.raise_on_error = raise_on_error
        self.log_time_func = log_time_func
        self.enable_patching = enable_patching
        self.not_static: Set[str] = set()
        self.use_py_compiler = use_py_compiler
        self.original_builtins: Dict[str, object] = dict(__builtins__)
        self.logger: logging.Logger = self._setup_logging()
        self.sources: dict[str, SourceInfo | None] = {}

    def get_dependencies(self, name: str) -> Dependencies:
        """Get static and non-static dependencies of given module."""
        mod = self.modules.get(name)
        if mod is None:
            return Dependencies(static=[], nonstatic=[])

        static = []
        nonstatic = []
        for depmod in mod.get_dependencies():
            if isinstance(depmod, ModuleTable):
                # changes to these should be reflected in our compiler magic
                # number, and don't need to be recorded as dependencies
                if depmod.name in self.intrinsic_modules:
                    continue

                source_info = self.sources.get(depmod.name)
                if source_info is None:
                    # the dependency may have been previously compiled as the
                    # main module in the same Compiler, in which case we'll have
                    # a ModuleTable already but no SourceInfo
                    source_info = self.get_source(depmod.name)
                assert source_info is not None
                static.append(source_info)
            else:  # UnknownModule, non-static
                nonstatic.append(depmod.name)

        return Dependencies(static=static, nonstatic=nonstatic)

    def import_module(self, name: str, optimize: int) -> Optional[ModuleTable]:
        res = self.modules.get(name)
        if res is not None:
            return res

        if name in self.not_static:
            return None

        source_info = self.get_source(name)
        if source_info is None:
            return None
        source = source_info.source
        filename = source_info.relative_path

        pyast = ast.parse(source)
        flags = FlagExtractor().get_flags(pyast)

        valid_if_strict = True
        if flags.is_strict:
            mod = self.loader.check(name)
            valid_if_strict = mod.is_valid and not mod.errors

        if valid_if_strict and name not in self.modules:
            if flags.is_static:
                # pyre-fixme[6]: For 1st argument expected `str` but got
                #  `Union[bytes, str]`.
                symbols = symtable.symtable(source, filename, "exec")
                root = pyast

                if filename.endswith(".pyi"):
                    root = remove_annotations(root)

                root = self._get_rewritten_ast(name, root, symbols, filename, optimize)
                log = self.log_time_func
                ctx = (
                    log()(name, filename, "declaration_visit") if log else nullcontext()
                )
                with ctx:
                    root = self.add_module(name, filename, root, optimize)
            else:
                self.not_static.add(name)

        return self.modules.get(name)

    def _get_rewritten_ast(
        self,
        name: str,
        root: ast.Module,
        symbols: PythonSymbolTable,
        filename: str,
        optimize: int,
    ) -> ast.Module:
        return rewrite(
            root,
            symbols,
            filename,
            name,
            optimize=optimize,
            is_static=True,
            builtins=self.original_builtins,
        )

    def _setup_logging(self) -> logging.Logger:
        logger = logging.Logger(__name__)
        if self.verbose:
            logger.setLevel(logging.DEBUG)
        return logger

    def load_compiled_module_from_source(
        self,
        source: str | bytes,
        filename: str,
        name: str,
        optimize: int,
        submodule_search_locations: Optional[List[str]] = None,
        override_flags: Optional[Flags] = None,
    ) -> Tuple[CodeType | None, bool, bool]:
        if override_flags and override_flags.is_strict:
            self.logger.debug(f"Forcibly treating module {name} as strict")
            self.loader.set_force_strict_by_name(name)

        pyast = ast.parse(source)
        # pyre-fixme[6]: For 1st argument expected `str` but got `Union[bytes, str]`.
        symbols = symtable.symtable(source, filename, "exec")
        flags = FlagExtractor().get_flags(pyast).merge(override_flags)

        if not flags.is_static and not flags.is_strict:
            code = self._compile_basic(name, pyast, filename, optimize)
            return (code, False, False)

        is_valid_strict = False
        if flags.is_strict:
            is_valid_strict = self._strict_analyze(
                source, flags, symbols, filename, name, submodule_search_locations
            )

        if flags.is_static:
            code = self._compile_static(pyast, symbols, filename, name, optimize)
            return (code, is_valid_strict, True)
        else:
            code = self._compile_strict(pyast, symbols, filename, name, optimize)
            return (code, is_valid_strict, False)

    def get_source(self, name: str) -> SourceInfo | None:
        if (source_info := self.sources.get(name, NOT_FOUND)) is not NOT_FOUND:
            # pyre-ignore[7]: pyre doesn't understand this can't be NOT_FOUND
            return source_info
        source_info = self._get_source(name)
        self.sources[name] = source_info
        return source_info

    def _get_source(self, name: str) -> SourceInfo | None:
        module_path = name.replace(".", os.sep)

        for path in self.import_path:
            source_info = self._get_source_info(name, path, module_path + ".py")
            if source_info is not None:
                return source_info

            source_info = self._get_source_info(
                name, path, module_path + os.sep + "__init__.py"
            )
            if source_info is not None:
                return source_info

        for path in self.import_path:
            source_info = self._get_source_info(name, path, module_path + ".pyi")
            if source_info is not None:
                return source_info

        return None

    def _get_source_info(
        self, name: str, prefix: str, relpath: str
    ) -> SourceInfo | None:
        path = os.path.join(prefix, relpath)
        if not os.path.exists(path):
            return None
        st = os.stat(path)
        with open(path, "rb") as f:
            return SourceInfo(
                name=name,
                prefix=prefix,
                relative_path=relpath,
                path=path,
                source=f.read(),
                mtime=int(st.st_mtime),
                size=st.st_size,
            )

    def _strict_analyze(
        self,
        source: str | bytes,
        flags: Flags,
        symbols: PythonSymbolTable,
        filename: str,
        name: str,
        submodule_search_locations: Optional[List[str]] = None,
    ) -> bool:
        mod = self.loader.check_source(
            source, filename, name, submodule_search_locations or []
        )

        is_valid_strict = mod.is_valid and len(mod.errors) == 0

        if mod.errors and self.raise_on_error:
            error = mod.errors[0]
            raise StrictModuleError(error[0], error[1], error[2], error[3])

        # TODO: Figure out if we need to run this analysis. This should be done only for
        # static analysis and not necessarily for strict modules. Keeping it for now since
        # it is currently running with the strict compiler.
        try:
            check_class_conflict(mod.ast, filename, symbols)
        except StrictModuleError as e:
            if self.raise_on_error:
                raise

        return is_valid_strict

    def _compile_basic(
        self, name: str, root: ast.Module, filename: str, optimize: int
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
        root: ast.Module,
        symbols: PythonSymbolTable,
        filename: str,
        name: str,
        optimize: int,
    ) -> CodeType:
        tree = rewrite(
            root,
            symbols,
            filename,
            name,
            optimize=optimize,
            builtins=self.original_builtins,
        )
        return strict_compile(name, filename, tree, optimize, self.original_builtins)

    def _compile_static(
        self,
        root: ast.Module,
        symbols: PythonSymbolTable,
        filename: str,
        name: str,
        optimize: int,
    ) -> CodeType | None:
        root = self.ast_cache.get(name) or self._get_rewritten_ast(
            name, root, symbols, filename, optimize
        )
        try:
            log = self.log_time_func
            ctx = log()(name, filename, "compile") if log else nullcontext()
            with ctx:
                return self.compile(
                    name,
                    filename,
                    root,
                    optimize,
                    enable_patching=self.enable_patching,
                    builtins=self.original_builtins,
                )
        except TypedSyntaxError as e:
            err = StrictModuleError(
                e.msg or "unknown error during static compilation",
                e.filename or filename,
                e.lineno or 1,
                0,
            )

            if self.raise_on_error:
                raise err

            return None

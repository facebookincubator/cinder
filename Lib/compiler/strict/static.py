# pyre-unsafe
from __future__ import annotations

import ast
from ast import Module

# pyre-fixme[21]: Could not find module `compiler.static`.
from compiler.static import ModuleTable, StaticCodeGenerator, SymbolTable

# pyre-fixme[21]: Could not find module `compiler.static.errors`.
from compiler.static.errors import ErrorSink as StaticErrorSink, TypedSyntaxError
from functools import lru_cache
from typing import Callable, ContextManager, Iterable, Optional, final

# pyre-fixme[21]: Could not find module `strict_modules.common`.
from strict_modules.common import ErrorSink

# pyre-fixme[21]: Could not find module `strict_modules.compiler`.
from strict_modules.compiler import Compiler

# pyre-fixme[21]: Could not find module `strict_modules.compiler.cache`.
from strict_modules.compiler.cache import NoopCacheInterface

# pyre-fixme[21]: Could not find module `strict_modules.compiler.compiler`.
from strict_modules.compiler.compiler import DEFAULT_CHECKERS, ModuleInfo, TChecker

# pyre-fixme[21]: Could not find module `strict_modules.compiler.modules`.
from strict_modules.compiler.modules import (
    AnalyzedModule,
    CompilationDependencies,
    CompiledModule,
    ModuleKind,
)

# pyre-fixme[21]: Could not find module `strict_modules.exceptions`.
from strict_modules.exceptions import StrictModuleException

# pyre-fixme[21]: Could not find module `strict_modules.rewriter`.
from strict_modules.rewriter import StrictModuleRewriter


@lru_cache(maxsize=1)
def _log_timing() -> Callable[[str, str, str], ContextManager[None]]:

    """
    Inline import to avoid importing scuba super early (it has a bunch of undesirable
    import-time side effects.
    """
    # pyre-fixme[21]: Could not find module `strict_modules.scuba_util`.
    from strict_modules.scuba_util import log_static_compiler_timing_to_scuba

    return log_static_compiler_timing_to_scuba


@final
# pyre-fixme[11]: Annotation `StrictModuleException` is not defined as a type.
class StaticError(StrictModuleException):
    pass


@final
# pyre-fixme[11]: Annotation `SymbolTable` is not defined as a type.
class StrictSymbolTable(SymbolTable):
    def __init__(
        self,
        compiler: StaticCompiler,
        # pyre-fixme[11]: Annotation `StaticErrorSink` is not defined as a type.
        error_sink: Optional[StaticErrorSink] = None,
    ) -> None:
        # pyre-fixme[19]: Expected 0 positional arguments.
        super().__init__(StaticCodeGenerator, error_sink)
        self.compiler = compiler

    # pyre-fixme[11]: Annotation `ModuleTable` is not defined as a type.
    def import_module(self, name: str) -> Optional[ModuleTable]:
        # pyre-fixme[16]: `StaticCompiler` has no attribute `load_by_name`.
        loaded = self.compiler.load_by_name(name)
        if (
            loaded is not None
            and loaded.module_kind == ModuleKind.StrictStatic
            # pyre-fixme[16]: `StrictSymbolTable` has no attribute `modules`.
            and name not in self.modules
        ):
            # We have cached analysis but we don't cache static analysis, load
            # the static analysis now.
            # pyre-fixme[16]: `StaticCompiler` has no attribute `_find_module`.
            # pyre-fixme[16]: `StaticCompiler` has no attribute `import_path`.
            mod_info = self.compiler._find_module(name, self.compiler.import_path)
            if mod_info is not None:
                ast_root = ast.parse(mod_info.source, mod_info.filename, "exec")
                with _log_timing()(name, mod_info.filename, "declaration_visit"):
                    # pyre-fixme[16]: `StrictSymbolTable` has no attribute `add_module`.
                    self.add_module(mod_info.name, mod_info.filename, ast_root)

        return self.modules.get(name)


@final
# pyre-fixme[11]: Annotation `Compiler` is not defined as a type.
class StaticCompiler(Compiler):
    def __init__(
        self,
        import_path: Iterable[str],
        # pyre-fixme[11]: Annotation `ErrorSink` is not defined as a type.
        error_factory: Callable[[], ErrorSink],
        # pyre-fixme[11]: Annotation `ModuleInfo` is not defined as a type.
        force_strict: Optional[Callable[[ModuleInfo], bool]] = None,
        # pyre-fixme[11]: Annotation `TChecker` is not defined as a type.
        checkers: Iterable[TChecker] = DEFAULT_CHECKERS,
        support_cache: bool = False,
        # pyre-fixme[11]: Annotation `NoopCacheInterface` is not defined as a type.
        cache: Optional[NoopCacheInterface] = None,
    ) -> None:
        # pyre-fixme[19]: Expected 0 positional arguments.
        super().__init__(
            import_path,
            error_factory,
            force_strict,
            checkers,
            support_cache,
            cache,
        )
        self.symtable: SymbolTable = StrictSymbolTable(self)

    def _analyze_strict(
        self,
        mod_info: ModuleInfo,
        ast_root: Module,
        # pyre-fixme[11]: Annotation `AnalyzedModule` is not defined as a type.
        module: AnalyzedModule
        # pyre-fixme[11]: Annotation `CompilationDependencies` is not defined as a type.
    ) -> CompilationDependencies:
        if module.module_kind == ModuleKind.StrictStatic:
            # Visit all of the top-level declarations and publish them in the shared
            # symbol table.  This will let other modules see our symbols, and let us
            # refer to things that show up later in the module before we see them.
            self.symtable.add_module(mod_info.name, mod_info.filename, ast_root)

        # pyre-fixme[16]: `object` has no attribute `_analyze_strict`.
        return super()._analyze_strict(mod_info, ast_root, module)

    def _rewrite(
        self,
        mod_info: ModuleInfo,
        analyzed: AnalyzedModule,
        deps: CompilationDependencies,
        optimize: int,
        track_import_call: bool,
        # pyre-fixme[11]: Annotation `CompiledModule` is not defined as a type.
    ) -> CompiledModule:
        if not analyzed.valid_strict or analyzed.module_kind != ModuleKind.StrictStatic:
            # pyre-fixme[16]: `object` has no attribute `_rewrite`.
            return super()._rewrite(
                mod_info, analyzed, deps, optimize, track_import_call
            )
        return self._rewrite_as_static(
            mod_info, analyzed, deps, optimize, track_import_call
        )

    def _rewrite_as_static(
        self,
        mod_info: ModuleInfo,
        analyzed: AnalyzedModule,
        deps: CompilationDependencies,
        optimize: int,
        track_import_call: bool,
    ) -> CompiledModule:
        symbols = deps.symbols
        if symbols is None:
            raise TypeError("expected SymbolTable, got None")
        # Perform the normal strict modules re-write, minus slotification
        rewriter = StrictModuleRewriter(
            deps.ast,
            symbols,
            mod_info.filename,
            mod_info.name,
            "exec",
            optimize,
            node_attrs=deps.node_attrs,
            is_static=True,
            track_import_call=track_import_call,
        )

        self.symtable[mod_info.name].finish_bind()

        tree = rewriter.transform()

        try:
            with _log_timing()(mod_info.name, mod_info.filename, "compile"):
                code = self.symtable.compile(
                    mod_info.name, mod_info.filename, tree, optimize
                )
        except TypedSyntaxError as e:
            errors = analyzed.errors
            if errors is not None:
                errors.error(
                    # pyre-fixme[19]: Expected 0 positional arguments.
                    StaticError(
                        e.msg or "unknown error during static compilation",
                        e.lineno or 1,
                        e.filename or mod_info.filename,
                    )
                )

        # And return the resulting code object
        return CompiledModule(
            # pyre-fixme[61]: `code` may not be initialized here.
            code=code,
            value=analyzed.value,
            module_kind=analyzed.module_kind,
            errors=analyzed.errors,
        )

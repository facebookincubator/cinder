# pyre-unsafe
from __future__ import annotations

import ast
import gc
import symtable
from ast import Module
from enum import IntFlag
from io import BytesIO
from os import path
from os.path import exists, isfile
from tokenize import detect_encoding
from typing import Callable, Dict, Iterable, List, Optional, Set, Tuple, final

# pyre-fixme[21]: Could not find module `strict_modules.abstract`.
from strict_modules.abstract import (
    AbstractList,
    AbstractModule,
    AbstractModuleLoader,
    AbstractStr,
    AbstractValue,
    BuiltinsModule,
    ListType,
)

# pyre-fixme[21]: Could not find module `strict_modules.abstract_builtins`.
from strict_modules.abstract_builtins import make_builtins

# pyre-fixme[21]: Could not find module `strict_modules.abstract_stdlib`.
from strict_modules.abstract_stdlib import AsyncioModule, DataclassesModule

# pyre-fixme[21]: Could not find module `strict_modules.abstract_strict_modules`.
from strict_modules.abstract_strict_modules import StrictModulesModule

# pyre-fixme[21]: Could not find module `strict_modules.class_conflict_checker`.
from strict_modules.class_conflict_checker import check_class_conflict

# pyre-fixme[21]: Could not find module `strict_modules.common`.
from strict_modules.common import (
    BoolErrorSink,
    ErrorSink,
    ScopeStack,
    SymbolMap,
    get_symbol_map,
)

# pyre-fixme[21]: Could not find module `strict_modules.compiler.cache`.
from strict_modules.compiler.cache import FilesystemCache, NoopCacheInterface

# pyre-fixme[21]: Could not find module `strict_modules.compiler.modules`.
from strict_modules.compiler.modules import (
    AnalyzedModule,
    CompilationDependencies,
    CompiledModule,
    ModuleKind,
)

# pyre-fixme[21]: Could not find module `strict_modules.exceptions`.
from strict_modules.exceptions import (
    StrictModuleBadStrictFlag,
    StrictModuleCacheInvalidationException,
)

# pyre-fixme[21]: Could not find module `strict_modules.module_allow_list`.
from strict_modules.module_allow_list import ALLOW_LIST

# pyre-fixme[21]: Could not find module `strict_modules.remove_annotations`.
from strict_modules.remove_annotations import remove_annotations

# pyre-fixme[21]: Could not find module `strict_modules.rewriter`.
from strict_modules.rewriter import rewrite

# pyre-fixme[21]: Could not find module `strict_modules.strictified_modules`.
from strict_modules.strictified_modules import get_implicit_source

# pyre-fixme[21]: Could not find module `strict_modules.toplevel_import_checker`.
from strict_modules.toplevel_import_checker import ToplevelImportAssignChecker


ROOT: str = path.dirname(path.dirname(path.dirname(__file__)))
STUB_ROOT: str = path.join(path.dirname(path.dirname(__file__)), "stubs")

STUB_SUFFIX: str = ".pys"
TYPING_STUB_SUFFIX: str = ".pyi"


TChecker = Callable[
    [Module, str, symtable.SymbolTable, SymbolMap, Optional[ErrorSink]], None
]


# pyre-fixme[11]: Annotation `ModuleKind` is not defined as a type.
def get_module_kind(ast_root: Module) -> ModuleKind:
    read_doc_str = False
    is_strict: Optional[ModuleKind] = None
    # Skip any doc strings and __future__ imports, look for import __strict__ as the first line
    for stmt in ast_root.body:
        if isinstance(stmt, ast.Expr):
            if isinstance(stmt.value, ast.Str) and not read_doc_str:
                read_doc_str = True
                continue
        elif isinstance(stmt, ast.ImportFrom) and stmt.module == "__future__":
            continue
        elif isinstance(stmt, ast.Import):
            strict_names = [
                a.name
                for a in stmt.names
                if a.name == "__strict__" or a.name == "__static__"
            ]
            if strict_names:
                if len(stmt.names) != 1:
                    raise StrictModuleBadStrictFlag(
                        f"{strict_names[0]} flag may not be combined with other imports",
                        stmt.lineno,
                    )
                if stmt.names[0].asname is not None:
                    raise StrictModuleBadStrictFlag(
                        f"{strict_names[0]} import cannot be aliased", stmt.lineno
                    )
                if is_strict is None:
                    is_strict = (
                        ModuleKind.Strict
                        if strict_names[0] == "__strict__"
                        else ModuleKind.StrictStatic
                    )
                    continue
                else:
                    raise StrictModuleBadStrictFlag(
                        f"{strict_names[0]} flag must be at top of module", stmt.lineno
                    )
        if is_strict is None:
            is_strict = ModuleKind.Normal
    return is_strict or ModuleKind.Normal


def get_symbols(
    source: str | bytes, filename: str, mode: str = "exec"
) -> symtable.SymbolTable:
    if isinstance(source, bytes):
        # symtable doesn't accept bytes unlike compile
        encoding, lines = detect_encoding(BytesIO(source).readline)
        symbols = symtable.symtable(source.decode(encoding), filename, mode)
    else:
        symbols = symtable.symtable(source, filename, mode)
    return symbols


# pyre-fixme[11]: Annotation `TChecker` is not defined as a type.
DEFAULT_CHECKERS: List[TChecker] = [check_class_conflict]


@final
class StubKind(IntFlag):
    """
    The last bit of the flag should be set to 1
    if the stub with that stub kind should be forced strict.
    The second bit of the flag should be set to 1
    if the stub is allowlist, meaning we have no control over source code
    Note that typing files are not forced strict by default, and
    have to be allow listed.
    """

    NONE = 0b000
    ALLOW_LIST = 0b011
    TYPING = 0b100
    STRICT = 0b101

    @staticmethod
    def get_stub_kind(mod_info: ModuleInfo) -> StubKind:
        stub_kind = StubKind.NONE
        if mod_info.filename.endswith(STUB_SUFFIX):
            return StubKind.STRICT
        elif mod_info.filename.endswith(TYPING_STUB_SUFFIX):
            stub_kind |= StubKind.TYPING
        if mod_info.name in ALLOW_LIST:
            stub_kind |= StubKind.ALLOW_LIST
        return stub_kind

    def is_forced_strict(self) -> bool:
        """
        A stub is forced strict if it is allow listed
        or a pys file
        """
        return bool(self & 1)

    def is_allowlisted(self) -> bool:
        return bool(self & 0b10)


@final
class ModuleInfo:
    def __init__(
        self,
        source: str | bytes,
        filename: str,
        name: str,
        submodule_search_locations: Optional[List[str]] = None,
    ) -> None:
        self.source = source
        self.filename = filename
        self.name = name
        self.submodule_search_locations = submodule_search_locations
        self.stub_kind: StubKind = StubKind.get_stub_kind(self)
        self._kind: Optional[ModuleKind] = None

    @property
    def kind(self) -> ModuleKind:
        module_kind = self._kind
        if module_kind is None:
            ast_root = ast.parse(self.source, self.filename, "exec")
            module_kind = self._kind = get_module_kind(ast_root)
        return module_kind

    def __str__(self) -> str:
        return f"<ModuleInfo {self.name}, {self.filename}, {self.stub_kind.name}>"


@final
# pyre-fixme[11]: Annotation `AbstractModuleLoader` is not defined as a type.
class AlwaysFailLoader(AbstractModuleLoader):
    """
    A default loader which doesn't support loading other external modules
    """

    def load_module(
        self,
        name: str,
        importer: Optional[str] = None
        # pyre-fixme[11]: Annotation `AbstractModule` is not defined as a type.
    ) -> Optional[AbstractModule]:
        pass

    def get_nested_loader(self) -> AbstractModuleLoader:
        return self


ALWAYS_FAIL_LOADER = AlwaysFailLoader()


class ModuleLoader(AbstractModuleLoader):
    def __init__(
        self,
        import_path: Iterable[str],
        # pyre-fixme[11]: Annotation `ErrorSink` is not defined as a type.
        error_factory: Callable[[], ErrorSink],
        force_strict: Optional[Callable[[ModuleInfo], bool]] = None,
        checkers: Iterable[TChecker] = DEFAULT_CHECKERS,
        support_cache: bool = False,
        # pyre-fixme[11]: Annotation `NoopCacheInterface` is not defined as a type.
        cache: Optional[NoopCacheInterface] = None,
    ) -> None:
        # pyre-fixme[11]: Annotation `AnalyzedModule` is not defined as a type.
        self._modules: Dict[str, AnalyzedModule] = {}
        for mod in [
            BuiltinsModule,
            DataclassesModule,
            AsyncioModule,
            StrictModulesModule,
        ]:
            self._modules[mod.name] = AnalyzedModule(mod)
        self.import_path = import_path
        self.force_strict = force_strict
        self.error_factory = error_factory
        self.passes: Iterable[TChecker] = checkers
        self.support_cache = support_cache
        self.graph: Dict[str, Set[str]] = {}
        self.cache: NoopCacheInterface = cache or FilesystemCache()
        # Names of modules that were not loaded from cache; any modules
        # depending on these also cannot be loaded from cache.
        self.invalidated_modules: Set[str] = set()
        # Stack of modules currently being loaded from cache (loading one
        # module from cache can cause many others to be loaded recursively, due
        # to cross-module references).
        self.cached_import_stack: List[str] = []

    def get_nested_loader(self) -> AbstractModuleLoader:
        return ALWAYS_FAIL_LOADER

    def load_module(
        self,
        name: str,
        importer: Optional[str] = None,
    ) -> Optional[AbstractModule]:
        """Get an AbstractModule by name.

        This satisfies the interface of AbstractModuleLoader, which operates
        only on AbstractValues and doesn't know about compiler internals like
        AnalyzedModule.
        """
        if self.support_cache and importer is not None:
            self._add_edge(importer, name)
        mod = self.load_by_name(name)
        if mod is not None:
            return mod.value
        return None

    def load_from_source(
        self,
        source: str | bytes,
        filename: str,
        name: str,
        submodule_search_locations: Optional[List[str]] = None,
    ) -> AnalyzedModule:
        """Get an AnalyzedModule from source/filename/name."""
        mod_info = ModuleInfo(
            source=source,
            filename=filename,
            name=name,
            submodule_search_locations=submodule_search_locations,
        )
        return self._compile(mod_info)

    def load_by_name(self, name: str) -> Optional[AnalyzedModule]:
        """Get an AnalyzedModule by module name."""
        mod = self._modules.get(name)
        if mod is not None:
            # Already loaded and analyzed...
            return mod

        path = []
        # pyre-fixme[35]: Target cannot be annotated.
        mod: Optional[AnalyzedModule] = None
        for part in name.split("."):
            path.append(part)
            mod_name = ".".join(path)
            mod = self._load_part(mod_name)
            if mod is None:
                break

        return mod

    def check_strict_from_reload(self, name: str) -> bool | None:
        mod_info = self._find_module(name, self.import_path)
        if mod_info is None:
            return None
        try:
            module_kind = mod_info.kind
        except StrictModuleBadStrictFlag:
            return False
        return module_kind != ModuleKind.Normal

    def reload_by_name(self, name: str) -> Optional[AnalyzedModule]:
        if name in self._modules:
            del self._modules[name]

        return self.load_by_name(name)

    def _compile(
        self, mod_info: ModuleInfo, strip_types: bool = False
    ) -> AnalyzedModule:
        result = self._analyze(mod_info, strip_types=strip_types)[0]
        if self._has_allowlisted_parent(mod_info):
            self._publish_on_parent(mod_info)
        return result

    def _analyze(
        self,
        mod_info: ModuleInfo,
        strip_types: bool = False
        # pyre-fixme[11]: Annotation `CompilationDependencies` is not defined as a type.
    ) -> Tuple[AnalyzedModule, Optional[CompilationDependencies]]:
        if self.support_cache:
            return self._analyze_cached(mod_info, strip_types=strip_types)
        else:
            return self._analyze_no_cache(mod_info, strip_types=strip_types)

    def _analyze_strict(
        self, mod_info: ModuleInfo, ast_root: Module, module: AnalyzedModule
    ) -> CompilationDependencies:
        symbols = get_symbols(mod_info.source, mod_info.filename, "exec")
        symbol_map = get_symbol_map(ast_root, symbols)

        # Run initial checks for strict module conformance
        # but we only need to run the interpreter on stubs
        if mod_info.stub_kind == StubKind.NONE:
            for checker in self.passes:
                checker(ast_root, mod_info.filename, symbols, symbol_map, module.errors)

        # Analyze the strict module, potentially recursing back into the
        # analysis.
        global_vars = make_builtins()
        scope = ToplevelImportAssignChecker.scope_factory(
            symbols, ast_root, global_vars
        )
        symtable = ScopeStack(
            scope,
            scope_factory=ToplevelImportAssignChecker.scope_factory,
            symbol_map=symbol_map,
        )
        abstract_module = module.value = self._make_module(
            global_vars, mod_info.name, mod_info.submodule_search_locations
        )
        visitor = ToplevelImportAssignChecker(
            root=ast_root,
            caller=abstract_module,
            symbols=symtable,
            loader=self,
            errors=module.errors,
            module_name=mod_info.name,
            filename=mod_info.filename,
            future_annotations=bool(mod_info.stub_kind & StubKind.TYPING),
        )

        visitor.analyze()

        node_attrs = visitor.node_attrs

        abstract_module.freeze()

        return CompilationDependencies(ast_root, symbols, node_attrs)

    def _should_force_strict(self, mod_info: ModuleInfo) -> bool:
        force_strict = self.force_strict
        if force_strict is None:
            return False

        return force_strict(mod_info)

    def _make_module(
        self,
        # pyre-fixme[11]: Annotation `AbstractValue` is not defined as a type.
        global_vars: Dict[str, AbstractValue],
        name: str,
        submodule_search_locations: Optional[List[str]],
    ) -> AbstractModule:
        module = AbstractModule(name, global_vars)
        module.dict["__name__"] = AbstractStr(module, name)
        if submodule_search_locations is not None:
            module.dict["__path__"] = AbstractList(
                ListType,
                module,
                [AbstractStr(module, x) for x in submodule_search_locations],
            )
        return module

    def _load_part(self, name: str) -> Optional[AnalyzedModule]:
        mod = self._modules.get(name)
        if mod is not None:
            # Already loaded and analyzed...
            return mod

        # look for strict module stubs
        stub_mod_info = self._find_module(name, [STUB_ROOT], suffix=STUB_SUFFIX)
        stub_namespace_package = False
        if stub_mod_info is not None:
            # get the source code of stubs
            stub_mod_info.source = get_implicit_source(
                stub_mod_info.source, stub_mod_info.name
            )
            stub_namespace_package = path.isdir(stub_mod_info.filename)

        # look for source code
        real_mod_info = self._find_module(name, self.import_path)
        if stub_mod_info is not None and (
            not stub_namespace_package or real_mod_info is None
        ):
            # we have a stub with source, or a stub directory with no real directory
            # in these cases use stubs
            return self._compile(stub_mod_info)
        elif real_mod_info is not None:
            # we have a stub directory with real counter parts, or no stubs
            # in these cases use real source
            return self._compile(real_mod_info)

        # look for typing stub
        typing_mod_info = self._find_module(
            name, self.import_path, suffix=TYPING_STUB_SUFFIX
        )
        if typing_mod_info is not None:
            if typing_mod_info.stub_kind.is_forced_strict():
                return self._compile(typing_mod_info)
            elif (
                typing_mod_info.stub_kind == StubKind.TYPING
                and typing_mod_info.kind == ModuleKind.StrictStatic
            ):
                # We try to compile typestubs that have `import __static__` so the static compiler can pick up
                # declarations from the stub, for more optimized codegen.
                return self._compile(typing_mod_info, strip_types=True)

        # No such module
        return None

    def _find_module(
        self, name: str, search_locations: Iterable[str], suffix: str = ".py"
    ) -> Optional[ModuleInfo]:
        # TODO: We should actually be respecting __path__ here.  When we start
        # at the top-level we should be using self.import_path, and then when
        # we import child packages we should be using their submodule_search_locations
        path_name = name.replace(".", path.sep)

        for dir in search_locations:
            fn = path.join(dir, path_name) + suffix
            if exists(fn) and isfile(fn):
                # plain module
                with open(fn) as f:
                    code = f.read()
                    return ModuleInfo(code, fn, name)

            fn = path.join(dir, path_name, "__init__") + suffix
            if exists(fn) and isfile(fn):
                # package
                with open(fn) as f:
                    code = f.read()
                    return ModuleInfo(code, fn, name, [dir])

            fn = path.join(dir, path_name)
            if path.isdir(fn):
                # namespace package
                return ModuleInfo("", fn, name, [dir])

        return None

    def _analyze_no_cache(
        self, mod_info: ModuleInfo, strip_types: bool = False
    ) -> Tuple[AnalyzedModule, CompilationDependencies]:
        ast_root = ast.parse(mod_info.source, mod_info.filename, "exec")
        if strip_types:
            ast_root = remove_annotations(ast_root)
        if mod_info.stub_kind.is_allowlisted():
            errors = BoolErrorSink()
        else:
            errors = self.error_factory()
        try:
            module_kind = get_module_kind(ast_root)
        except StrictModuleBadStrictFlag as ex:
            module_kind = ModuleKind.Normal
            errors.error(ex)

        # We match CPython's import semantics by publishing
        # the partially analyzed module into our module table now.
        module = self._modules[mod_info.name] = AnalyzedModule(
            value=None,
            module_kind=module_kind,
            errors=errors,
            dependencies=self.graph.setdefault(mod_info.name, set()),
        )
        if (
            module_kind != ModuleKind.Normal
            or mod_info.stub_kind.is_forced_strict()
            or mod_info.stub_kind == StubKind.TYPING
            or self._should_force_strict(mod_info)
        ):
            deps = self._analyze_strict(mod_info, ast_root, module)
            # The vast majority of objects we create that may need to be
            # collected are created in analysis of a module. It turns out to be
            # significantly faster to disable automatic GC and manually collect
            # here after analysis, then freeze all uncollected objects (if we
            # still have references to them here, they likely aren't going
            # anywhere) to make future GC passes faster.
            gc.collect()
            gc.freeze()
        else:
            deps = CompilationDependencies(ast_root)

        return (module, deps)

    def _analyze_cached(
        self, mod_info: ModuleInfo, strip_types: bool = False
    ) -> Tuple[AnalyzedModule, Optional[CompilationDependencies]]:
        nested_cached_import = bool(self.cached_import_stack)
        self.cached_import_stack.append(mod_info.name)
        try:
            mod = self.cache.read(mod_info, self, self.invalidated_modules)
        except StrictModuleCacheInvalidationException:
            # Some dependency of ours was invalidated while we were loading it
            # for the first time, so we have to re-analyze too. Remove our
            # half-rehydrated cached module from the modules dict. We know it
            # will be there because it's always placed there before trying to
            # load any cross-module dependencies.
            del self._modules[mod_info.name]
            mod = None
        finally:
            self.cached_import_stack.pop()
        if mod is not None:
            return (mod, None)
        self.invalidated_modules.add(mod_info.name)
        if nested_cached_import:
            # We are being loaded as part of dependency resolution for a chain
            # of one or more dependent modules that are currently
            # mid-load-from-cache; we need to signal this invalidation all the
            # way up to the top of that stack and re-start analysis from there.
            raise StrictModuleCacheInvalidationException()
        mod, deps = self._analyze_no_cache(mod_info, strip_types=strip_types)

        self.cache.write(mod_info, mod)
        return mod, deps

    def _add_edge(self, src: str, dest: str) -> None:
        self.graph.setdefault(src, set()).add(dest)

    def _publish_on_parent(self, mod_info: ModuleInfo) -> None:
        """
        Replicates python behavior by publishing child modules onto
        the parent module
        This is generally done after the parent module is already frozen and
        cached, thus this action needs to be done even when module is loaded
        from cache.
        """
        child_qual_name = mod_info.name
        child_mod = self._modules.get(child_qual_name)
        if not child_mod:
            return
        child_mod_v = child_mod.value
        if not child_mod_v:
            return
        parent_name, sep, child_name = child_qual_name.rpartition(".")
        parent_mod = self._modules.get(parent_name)
        if not parent_mod:
            return
        parent_mod_v = parent_mod.value
        if not parent_mod_v:
            return
        # following python behavior, existing names are shadowed
        # when child is imported
        parent_mod_v.dict[child_name] = child_mod_v

    def _has_allowlisted_parent(self, mod_info: ModuleInfo) -> bool:
        parent_mod_name, sep, __ = mod_info.name.rpartition(".")
        return bool(parent_mod_name and parent_mod_name in ALLOW_LIST)


class Compiler(ModuleLoader):
    def __init__(
        self,
        import_path: Iterable[str],
        error_factory: Callable[[], ErrorSink],
        force_strict: Optional[Callable[[ModuleInfo], bool]] = None,
        checkers: Iterable[TChecker] = DEFAULT_CHECKERS,
        support_cache: bool = False,
        cache: Optional[NoopCacheInterface] = None,
    ) -> None:
        super().__init__(
            import_path=import_path,
            error_factory=error_factory,
            force_strict=force_strict,
            checkers=checkers,
            support_cache=support_cache,
            cache=cache,
        )

    def load_compiled_module_from_source(
        self,
        source: str | bytes,
        filename: str,
        name: str,
        optimize: int,
        submodule_search_locations: Optional[List[str]] = None,
        track_import_call: bool = False,
        # pyre-fixme[11]: Annotation `CompiledModule` is not defined as a type.
    ) -> CompiledModule:
        mod_info = ModuleInfo(
            source=source,
            filename=filename,
            name=name,
            submodule_search_locations=submodule_search_locations,
        )
        return self._compile_to_real_module(mod_info, optimize, track_import_call)

    def _compile_to_real_module(
        self,
        mod_info: ModuleInfo,
        optimize: int,
        track_import_call: bool,
    ) -> CompiledModule:
        """no matter whether caching is enabled, in order to compile
        to a real module we need to redo the analysis, because
        no CompilationDependencies are cached
        """
        analyzed, deps = self._analyze_no_cache(mod_info)
        if self.support_cache:
            self.cache.write(mod_info, analyzed)
        return self._rewrite(mod_info, analyzed, deps, optimize, track_import_call)

    def _rewrite(
        self,
        mod_info: ModuleInfo,
        analyzed: AnalyzedModule,
        deps: CompilationDependencies,
        optimize: int,
        track_import_call: bool,
    ) -> CompiledModule:
        if analyzed.valid_strict:
            symbols = deps.symbols
            assert symbols is not None
            code = rewrite(
                deps.ast,
                symbols,
                mod_info.filename,
                mod_info.name,
                "exec",
                optimize,
                node_attrs=deps.node_attrs,
                track_import_call=track_import_call,
            )
        else:
            code = compile(
                deps.ast,
                mod_info.filename,
                "exec",
                dont_inherit=True,
                optimize=optimize,
            )
        return CompiledModule(
            code=code,
            value=analyzed.value,
            module_kind=analyzed.module_kind,
            errors=analyzed.errors,
        )

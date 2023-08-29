import ast
from _symtable import symtable
from typing import Callable, List, Protocol

NONSTRICT_MODULE_KIND: int = ...
STATIC_MODULE_KIND: int = ...
STRICT_MODULE_KIND: int = ...
STUB_KIND_MASK_ALLOWLIST: int = ...
STUB_KIND_MASK_NONE: int = ...
STUB_KIND_MASK_STRICT: int = ...
STUB_KIND_MASK_TYPING: int = ...

MUTABLE_DECORATOR: str = ...
LOOSE_SLOTS_DECORATOR: str = ...
EXTRA_SLOTS_DECORATOR: str = ...
ENABLE_SLOTS_DECORATOR: str = ...
CACHED_PROP_DECORATOR: str = ...

class StrictAnalysisResult:
    def __init__(
        self,
        _module_name: str,
        _file_name: str,
        _mod_kind: int,
        _stub_kind: int,
        _ast: ast.Module,
        _ast_preprocessed: ast.Module,
        _symtable: symtable,
        _errors: List[StrictModuleError],
        /,
    ) -> None: ...
    module_name: str
    file_name: str
    module_kind: int
    stub_kind: int
    ast: ast.Module
    ast_preprocessed: ast.Module
    symtable: symtable
    errors: List[StrictModuleError]
    is_valid: bool

class IStrictModuleLoader(Protocol):
    def check(self, _mod_name: str, /) -> StrictAnalysisResult: ...
    def check_source(
        self,
        _source: str | bytes,
        _file_name: str,
        _mod_name: str,
        _submodule_search_locations: List[str],
        /,
    ) -> StrictAnalysisResult: ...
    def set_force_strict(self, _force: bool, /) -> None: ...
    def set_force_strict_by_name(self, _name: str, /) -> None: ...

class StrictModuleLoader(IStrictModuleLoader):
    def __init__(
        self,
        _import_paths: List[str],
        _stub_import_path: str,
        _allow_list: List[str],
        _allow_list_exact: List[str],
        _load_strictmod_builtin: bool = True,
        _allow_list_regex: List[str] | None = None,
        _verbose_logs: bool = False,
        _disable_analysis: bool = False,
        /,
    ) -> None: ...

StrictModuleLoaderFactory = Callable[
    [List[str], str, List[str], List[str], bool, List[str], bool, bool],
    IStrictModuleLoader,
]

import ast
from _symtable import symtable
from typing import Any, Callable, List, Protocol

CACHED_PROP_DECORATOR: str
ENABLE_SLOTS_DECORATOR: str
EXTRA_SLOTS_DECORATOR: str
LOOSE_SLOTS_DECORATOR: str
MUTABLE_DECORATOR: str
NONSTRICT_MODULE_KIND: int
STATIC_MODULE_KIND: int
STRICT_MODULE_KIND: int
STUB_KIND_MASK_ALLOWLIST: int
STUB_KIND_MASK_NONE: int
STUB_KIND_MASK_STRICT: int
STUB_KIND_MASK_TYPING: int

class StrictAnalysisResult:
    ast: ast.Module
    errors: List[StrictModuleError]
    file_name: str
    is_valid: bool
    module_kind: int
    module_name: str
    stub_kind: int
    symtable: symtable
    def __init__(
        self,
        _module_name: str,
        _file_name: str,
        _mod_kind: int,
        _stub_kind: int,
        _ast: ast.Module,
        _symtable: symtable,
        _errors: List[StrictModuleError],
        /,
    ) -> None: ...

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
    def delete_module(self, name: str) -> bool: ...
    def get_analyzed_count(self) -> int: ...
    def set_force_strict(self, force: bool) -> bool: ...
    def set_force_strict_by_name(self, *args, **kwargs) -> Any: ...

StrictModuleLoaderFactory = Callable[
    [List[str], str, List[str], List[str], bool, List[str], bool, bool],
    IStrictModuleLoader,
]

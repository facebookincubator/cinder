import ast
from compiler.strict.common import StrictModuleError
from typing import List

NONSTRICT_MODULE_KIND: int = ...
STATIC_MODULE_KIND: int = ...
STUB_KIND_MASK_TYPING: int = ...

MUTABLE_DECORATOR: str = ...
LOOSE_SLOTS_DECORATOR: str = ...
EXTRA_SLOTS_DECORATOR: str = ...
ENABLE_SLOTS_DECORATOR: str = ...
CACHED_PROP_DECORATOR: str = ...

class StrictAnalysisResult:
    file_name: str
    is_valid: bool
    module_kind: int
    stub_kind: int
    ast: ast.Module
    ast_preprocessed: ast.Module
    errors: List[StrictModuleError]

class StrictModuleLoader:
    def __init__(
        self,
        _import_paths: List[str],
        _stub_import_path: str,
        _allow_list: List[str],
        _allow_list_exact: List[str],
        _load_strictmod_builtin: bool = True,
        /,
    ) -> None: ...
    def check(self, _mod_name: str, /) -> StrictAnalysisResult: ...
    def check_source(
        self,
        _source: str | bytes,
        _file_name: str,
        _mod_name: str,
        _submodule_search_locations: List[str],
        /,
    ) -> StrictAnalysisResult: ...

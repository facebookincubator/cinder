# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
from __future__ import annotations

import ast
from ast import (
    AST,
    AnnAssign,
    Assign,
    AsyncFunctionDef,
    Attribute,
    Call,
    ClassDef,
    Constant,
    Delete,
    DictComp,
    ExceptHandler,
    For,
    FunctionDef,
    GeneratorExp,
    Global,
    If,
    Import,
    ImportFrom,
    Lambda,
    ListComp,
    Module,
    Name,
    NodeVisitor,
    Raise,
    SetComp,
    Str,
    Try,
    alias,
    arg,
    copy_location,
    expr,
    stmt,
)
from symtable import SymbolTable
from types import CodeType, ModuleType
from typing import (
    Any,
    Dict,
    Generic,
    Iterable,
    List,
    Mapping,
    MutableMapping,
    Optional,
    Sequence,
    Set,
    TypeVar,
    Union,
    cast,
    final,
)

from ..symbols import ModuleScope, Scope, SymbolVisitor
from .common import imported_name
from .preprocessor import ALL_INDICATORS

_IMPLICIT_GLOBALS = [
    "__name__",
    "__loader__",
    "__package__",
    "__spec__",
    "__path__",
    "__file__",
    "__cached__",
]


@final
class FeatureExtractor(SymbolVisitor):
    def __init__(
        self, builtins: Dict[str, Any], ignore_names: Iterable[str] = ALL_INDICATORS
    ) -> None:
        super().__init__()
        self.builtins = builtins
        self.ignore_names: Iterable[str] = ignore_names
        self.globals: Set[str] = set()
        self.global_sets: Set[str] = set()
        self.global_dels: Set[str] = set()
        self.future_imports: Set[alias] = set()

    def is_global(self, name: str, scope: Scope) -> bool:
        if isinstance(scope, ModuleScope) or scope.global_scope:
            return True
        if name in scope.module.globals or name in scope.module.explicit_globals:
            return True
        return False

    def load_name(self, name: str, scope: Scope) -> None:
        if self.is_global(name, scope):
            self.globals.add(name)

    def store_name(self, name: str, scope: Scope) -> None:
        if self.is_global(name, scope):
            self.globals.add(name)
            self.global_sets.add(name)

    def del_name(self, name: str, scope: Scope) -> None:
        if self.is_global(name, scope):
            self.globals.add(name)
            self.global_sets.add(name)
            self.global_dels.add(name)

    def visitGlobal(self, node: Global, scope: Scope) -> None:
        super().visitGlobal(node, scope)
        for name in node.names:
            self.globals.add(name)

    def visitName(self, node: Name, scope: Scope) -> None:
        super().visitName(node, scope)
        if isinstance(node.ctx, ast.Load):
            self.load_name(node.id, scope)
        elif isinstance(node.ctx, ast.Store):
            self.store_name(node.id, scope)
        elif isinstance(node.ctx, ast.Del):
            self.del_name(node.id, scope)

    def visitImportFrom(self, node: ImportFrom, scope: Scope) -> None:
        super().visitImportFrom(node, scope)
        if node.module == "__future__":
            self.future_imports.update(node.names)

        for name in node.names:
            self.store_name(name.asname or name.name, scope)

    def visitImport(self, node: Import, scope: Scope) -> None:
        super().visitImport(node, scope)
        for name in node.names:
            self.store_name(imported_name(name), scope)

    def visitCall(self, node: Call, scope: Scope) -> None:
        func = node.func
        if isinstance(func, ast.Name):
            # We don't currently allow aliasing or shadowing exec/eval
            # so this check is currently sufficient.
            if (func.id == "exec" or func.id == "eval") and len(node.args) < 2:
                # We'll need access to our globals() helper when we transform
                # the ast
                self.globals.add("globals")
                self.globals.add("locals")
        self.generic_visit(node, scope)

    def visitTry(self, node: Try, scope: Scope) -> None:
        super().visitTry(node, scope)

        for handler in node.handlers:
            name = handler.name
            if name is None:
                continue
            self.del_name(name, scope)

    def visitClassDef(self, node: ClassDef, parent: Scope) -> None:
        self.store_name(node.name, parent)
        super().visitClassDef(node, parent)

    def visitFunctionDef(self, node: FunctionDef, parent: Scope) -> None:
        self.store_name(node.name, parent)
        super().visitFunctionDef(node, parent)

    def visitAsyncFunctionDef(self, node: AsyncFunctionDef, parent: Scope) -> None:
        self.store_name(node.name, parent)
        super().visitAsyncFunctionDef(node, parent)

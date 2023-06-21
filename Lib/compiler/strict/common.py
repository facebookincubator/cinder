# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
# pyre-unsafe

from __future__ import annotations

import ast
import os.path
import symtable
import typing
from ast import (
    alias,
    AST,
    AsyncFunctionDef,
    ClassDef,
    comprehension,
    copy_location,
    DictComp,
    FunctionDef,
    GeneratorExp,
    iter_fields,
    Lambda,
    ListComp,
    NodeVisitor,
    SetComp,
    Try,
)
from collections import deque
from symtable import Class, SymbolTable
from typing import (
    Callable,
    Dict,
    final,
    Generic,
    List,
    Mapping,
    MutableMapping,
    Optional,
    Type,
    TypeVar,
)

from .runtime import (
    _mark_cached_property,
    freeze_type,
    loose_slots,
    mutable,
    strict_slots,
)

from .track_import_call import tracker

# Increment this whenever we change the output of the strict modules
# interpreter. It must stay below 32768 (15 bits), because we use the high bit
# to encode strictness of the module.
MAGIC_NUMBER = 46


DEFAULT_STUB_PATH = os.path.dirname(__file__) + "/stubs"


def make_fixed_modules() -> Mapping[str, Mapping[str, object]]:
    typing_members = {}
    for name in typing.__all__:
        typing_members[name] = getattr(typing, name)
    strict_mod_members = {
        "freeze_type": freeze_type,
        "loose_slots": loose_slots,
        "strict_slots": strict_slots,
        "mutable": mutable,
        "_mark_cached_property": _mark_cached_property,
        "track_import_call": tracker.register,
    }

    return {
        "typing": typing_members,
        "strict_modules": dict(strict_mod_members),
        "__strict__": strict_mod_members,
    }


FIXED_MODULES: Mapping[str, Mapping[str, object]] = make_fixed_modules()


TVar = TypeVar("TScope")
TScopeData = TypeVar("TData", covariant=True)

SymbolMap = Dict[AST, SymbolTable]


class StrictModuleError(Exception):
    def __init__(
        self, msg: str, filename: str, lineno: int, col: int, metadata: str = ""
    ) -> None:
        self.msg = msg
        self.filename = filename
        self.lineno = lineno
        self.col = col
        self.metadata = metadata


@final
class SymbolMapBuilder(ast.NodeVisitor):
    def __init__(self, symbols: SymbolTable) -> None:
        self.symbol_stack: deque[SymbolTable] = deque([symbols])
        children = self.symbol_stack.popleft().get_children()
        self.symbol_stack.extendleft(reversed(children))
        self.mapping: SymbolMap = {}

    def _process_scope_node(self, node: AST) -> None:
        current_symbol = self.mapping[node] = self.symbol_stack.popleft()
        children = current_symbol.get_children()
        self.symbol_stack.extendleft(reversed(children))

    def visit_ClassDef(self, node: ClassDef) -> None:
        for child in node.bases:
            self.visit(child)
        for child in node.keywords:
            self.visit(child)
        for child in node.decorator_list:
            self.visit(child)
        self._process_scope_node(node)
        for child in node.body:
            self.visit(child)

    def visit_FunctionDef(self, node: FunctionDef) -> None:
        self._visit_function_like(node)

    def visit_AsyncFunctionDef(self, node: AsyncFunctionDef) -> None:
        self._visit_function_like(node)

    def visit_Lambda(self, node: Lambda) -> None:
        if node.args:
            self.visit(node.args)
        self._process_scope_node(node)
        self.visit(node.body)

    def _visit_function_like(self, node: FunctionDef | AsyncFunctionDef) -> None:
        # args -> returns -> decorator_list -> body
        if node.args:
            self.visit(node.args)
        returns = node.returns
        if returns:
            self.visit(returns)
        for child in node.decorator_list:
            self.visit(child)
        self._process_scope_node(node)
        for child in node.body:
            self.visit(child)

    def visit_ListComp(self, node: ListComp) -> None:
        self._visit_generator_like(node, [node.elt], node.generators)

    def visit_SetComp(self, node: SetComp) -> None:
        self._visit_generator_like(node, [node.elt], node.generators)

    def visit_DictComp(self, node: DictComp) -> None:
        self._visit_generator_like(node, [node.key, node.value], node.generators)

    def visit_GeneratorExp(self, node: GeneratorExp) -> None:
        self._visit_generator_like(node, [node.elt], node.generators)

    def _visit_generator_like(
        self, node: AST, elements: List[AST], comprehensions: List[comprehension]
    ) -> None:
        # first iter is visited in the outer scope
        self.visit(comprehensions[0].iter)
        # everything else is in the inner scope
        self._process_scope_node(node)
        # process first comprehension, without iter
        for child in comprehensions[0].ifs:
            self.visit(child)
        for comp in comprehensions[1:]:
            self.visit(comp)
        # process elements
        for element in elements:
            self.visit(element)

    def visit_Try(self, node: Try) -> None:
        # Need to match the order the symbol visitor constructs these in, which
        # walks orelse before handlers.
        for val in node.body:
            self.visit(val)
        for val in node.orelse:
            self.visit(val)
        for val in node.handlers:
            self.visit(val)
        for val in node.finalbody:
            self.visit(val)


def get_symbol_map(node: ast.AST, symtable: SymbolTable) -> SymbolMap:
    visitor = SymbolMapBuilder(symtable)
    visitor.visit(node)
    return visitor.mapping


@final
class SymbolScope(Generic[TVar, TScopeData]):
    def __init__(
        self,
        symbols: SymbolTable,
        scope_data: TScopeData,
        vars: Optional[MutableMapping[str, TVar]] = None,
        invisible: bool = False,
    ) -> None:
        self.symbols = symbols
        self.vars = vars
        self.scope_data = scope_data
        self.invisible = invisible

    def __getitem__(self, name: str) -> TVar:
        v = self.vars
        if v is None:
            raise KeyError(name)

        return v[name]

    def __setitem__(self, name: str, value: TVar) -> None:
        v = self.vars
        if v is None:
            v = self.vars = {}

        v[name] = value

    def __delitem__(self, name: str) -> None:
        v = self.vars
        if v is None:
            raise KeyError(name)

        del v[name]

    def __contains__(self, name: str) -> bool:
        v = self.vars
        if v is None:
            return False

        return name in v


def mangle_priv_name(name: str, scopes: List[SymbolScope[TVar, TScopeData]]) -> str:
    if name.startswith("__") and not name.endswith("__"):
        # symtable has name mangled private names.  Walk the scope list
        # backwards and apply the mangled class name
        for scope in reversed(scopes):
            if isinstance(scope.symbols, symtable.Class) and not scope.invisible:
                return "_" + scope.symbols.get_name().lstrip("_") + name

    return name


def imported_name(name: alias) -> str:
    return name.asname or name.name.partition(".")[0]


@final
class ScopeContextManager(Generic[TVar, TScopeData]):
    def __init__(
        self, parent: ScopeStack[TVar, TScopeData], scope: SymbolScope[TVar, TScopeData]
    ) -> None:
        self.parent = parent
        self.scope = scope

    def __enter__(self) -> SymbolScope[TVar, TScopeData]:
        self.parent.push(self.scope)
        return self.scope

    def __exit__(
        self, exc_type: Type[Exception], exc_val: Exception, exc_tb: object
    ) -> None:
        self.parent.pop()


@final
class ScopeStack(Generic[TVar, TScopeData]):
    def __init__(
        self,
        *scopes: SymbolScope[TVar, TScopeData],
        symbol_map: SymbolMap,
        scope_factory: Callable[
            [SymbolTable, AST, Optional[MutableMapping[str, TVar]]],
            SymbolScope[TVar, TScopeData],
        ] = lambda symtable, node, vars: SymbolScope(symtable, None),
    ) -> None:
        self.scopes: List[SymbolScope[TVar, TScopeData]] = list(scopes)
        self.symbol_map = symbol_map
        self.scope_factory: Callable[
            [SymbolTable, AST, Optional[MutableMapping[str, TVar]]],
            SymbolScope[TVar, TScopeData],
        ] = scope_factory

    def push(self, scope: SymbolScope[TVar, TScopeData]) -> None:
        self.scopes.append(scope)

    def pop(self) -> None:
        self.scopes.pop()

    @property
    def current_symbols(self) -> SymbolTable:
        return self.scopes[-1].symbols

    @property
    def current(self) -> SymbolScope[TVar, TScopeData]:
        return self.scopes[-1]

    @property
    def in_global_scope(self) -> bool:
        return len(self.scopes) == 1

    @property
    def in_class_scope(self) -> bool:
        return isinstance(self.scopes[-1].symbols, Class)

    def with_node_scope(
        self, node: AST, vars: Optional[MutableMapping[str, TVar]] = None
    ) -> ScopeContextManager[TVar, TScopeData]:
        next_symtable = self.symbol_map[node]
        return ScopeContextManager(self, self.scope_factory(next_symtable, node, vars))

    def is_global(self, name: str) -> bool:
        if self.in_global_scope:
            return True

        return self.current_symbols.lookup(
            mangle_priv_name(name, self.scopes)
        ).is_global()

    def is_nonlocal(self, name: str) -> bool:
        return (
            not self.is_global(name)
            and self.current_symbols.lookup(
                mangle_priv_name(name, self.scopes)
            ).is_free()
        )

    def is_local(self, name: str) -> bool:
        return self.current_symbols.lookup(
            mangle_priv_name(name, self.scopes)
        ).is_local()

    def scope_for(self, name: str) -> SymbolScope[TVar, TScopeData]:
        if self.is_global(name):
            return self.scopes[0]
        elif self.is_nonlocal(name):
            for scope in reversed(self.scopes):
                lookup = scope.symbols.lookup(name)
                if lookup.is_global() or lookup.is_free():
                    return scope

        return self.scopes[-1]

    def __getitem__(self, name: str) -> TVar:
        is_leaf = True
        for scope in reversed(self.scopes):
            if (is_leaf or not isinstance(scope.symbols, Class)) and name in scope:
                return scope[name]
            is_leaf = False
        raise KeyError(f"{name} not found in scope")

    def __contains__(self, name: str) -> bool:
        is_leaf = True
        for scope in reversed(self.scopes):
            if (is_leaf or not isinstance(scope.symbols, Class)) and name in scope:
                return True
            is_leaf = False
        return False

    def __setitem__(self, name: str, value: TVar) -> None:
        if self.is_global(name):
            self.scopes[0][name] = value
        elif self.is_nonlocal(name):
            for scope in reversed(self.scopes[:-1]):
                if name in scope:
                    scope[name] = value
                    break
        else:
            current_scope = self.scopes[-1]
            current_scope[name] = value

    def __delitem__(self, name: str) -> None:
        if self.is_global(name):
            if name in self.scopes[0]:
                del self.scopes[0][name]
        elif self.is_nonlocal(name):
            for scope in reversed(self.scopes[:-1]):
                if name in scope:
                    del scope[name]
                    break
        else:
            current_scope = self.scopes[-1]
            if name in current_scope:
                del current_scope[name]

    def local_contains(self, name: str) -> bool:
        return name in self.scopes[-1]

    def shallow_copy(self) -> ScopeStack[TVar, TScopeData]:
        # same underlying stack but a different dict
        return ScopeStack(
            *(d for d in self.scopes),
            symbol_map=self.symbol_map,
            scope_factory=self.scope_factory,
        )

    def with_scope(
        self, scope: SymbolScope[TVar, TScopeData]
    ) -> ScopeContextManager[TVar, TScopeData]:
        return ScopeContextManager(self, scope)


TAst = TypeVar("TAst", bound=AST)


class AstRewriter(NodeVisitor):
    """performs rewrites on the AST, but only produces new nodes, rather than
    modifying nodes in place like ast.NodeTransformer."""

    @staticmethod
    def update_node(node: TAst, **replacement: object) -> TAst:
        res = node
        for name, val in replacement.items():
            existing = getattr(res, name)
            if existing == val:
                continue

            if node is res:
                res = AstRewriter.clone_node(node)

            setattr(res, name, val)
        return res

    @staticmethod
    def clone_node(node: TAst) -> TAst:
        attrs = []
        for name in node._fields:
            attr = getattr(node, name, None)
            if isinstance(attr, list):
                attr = list(attr)
            attrs.append(attr)

        new = type(node)(*attrs)
        return copy_location(new, node)

    def walk_list(self, old_values: List[TAst]) -> List[TAst]:
        new_values = []
        changed = False
        for old_value in old_values:
            if isinstance(old_value, AST):
                new_value = self.visit(old_value)
                changed |= new_value is not old_value
                if new_value is None:
                    continue
                elif not isinstance(new_value, AST):
                    new_values.extend(new_value)
                    continue

                new_values.append(new_value)
            else:
                new_values.append(old_value)
        return new_values if changed else old_values

    def generic_visit(self, node: TAst) -> TAst:
        ret_node = node
        for field, old_value in iter_fields(node):
            if isinstance(old_value, list):
                new_values = self.walk_list(old_value)
                if new_values != old_value:
                    if ret_node is node:
                        ret_node = self.clone_node(node)
                    setattr(ret_node, field, new_values)
            elif isinstance(old_value, AST):
                new_node = self.visit(old_value)
                assert (
                    new_node is not None
                ), "can't remove AST nodes that aren't part of a list"
                if new_node is not old_value:
                    if ret_node is node:
                        ret_node = self.clone_node(node)

                    setattr(ret_node, field, new_node)

        return ret_node


def lineinfo(node: TAst, target: Optional[AST] = None) -> TAst:
    if not target:
        # set lineno to -1 to indicate non-user code
        node.lineno = -1
        node.col_offset = -1
        node.end_lineno = -1
        node.end_col_offset = -1
    else:
        copy_location(node, target)
    return node

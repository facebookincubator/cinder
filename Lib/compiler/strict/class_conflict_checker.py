# pyre-unsafe
from __future__ import annotations

import ast
from ast import (
    AST,
    AnnAssign,
    Assign,
    AsyncFunctionDef,
    Attribute,
    ClassDef,
    Delete,
    ExceptHandler,
    FunctionDef,
    Global,
    Import,
    ImportFrom,
    Module,
    Name,
)
from symtable import SymbolTable
from typing import MutableMapping, Optional, Set, final

# pyre-fixme[21]: Could not find module `strict_modules.common`.
from strict_modules.common import (
    ErrorSink,
    ScopeStack,
    SymbolMap,
    SymbolScope,
    imported_name,
)

# pyre-fixme[21]: Could not find module `strict_modules.exceptions`.
from strict_modules.exceptions import ClassAttributesConflictException

# pyre-fixme[21]: Could not find module `strict_modules.rewriter`.
from strict_modules.rewriter import SymbolVisitor


class TransformerScope:
    def visit_Assign(self, node: Assign) -> None:
        pass

    def visit_AnnAssign(self, node: AnnAssign) -> None:
        pass

    def loaded(self, name: str) -> None:
        pass

    def stored(self, name: str) -> None:
        pass


@final
class ClassScope(TransformerScope):
    def __init__(self) -> None:
        self.instance_fields: Set[str] = set()
        self.class_fields: Set[str] = set()

    def visit_AnnAssign(self, node: AnnAssign) -> None:
        target = node.target
        if isinstance(target, Name):
            if node.value is None:
                self.instance_fields.add(target.id)
            else:
                self.class_fields.add(target.id)

    def stored(self, name: str) -> None:
        self.class_fields.add(name)


@final
class FunctionScope(TransformerScope):
    def __init__(self, node: FunctionDef, parent: TransformerScope) -> None:
        self.node = node
        self.parent = parent

    def visit_AnnAssign(self, node: AnnAssign) -> None:
        parent = self.parent
        target = node.target
        if (
            isinstance(self.node, FunctionDef)
            and self.node.name == "__init__"
            and self.node.args.args
            and isinstance(parent, ClassScope)
            and isinstance(target, Attribute)
        ):
            self.add_attr_name(target, parent)

    def add_attr_name(self, target: Attribute, parent: ClassScope) -> None:
        """records self.name = ... when salf matches the 1st parameter"""
        value = target.value
        node = self.node
        if (
            isinstance(node, FunctionDef)
            and isinstance(value, Name)
            and value.id == node.args.args[0].arg
        ):
            parent.instance_fields.add(target.attr)

    def visit_Assign(self, node: Assign) -> None:
        parent = self.parent
        if (
            isinstance(self.node, FunctionDef)
            and self.node.name == "__init__"
            and isinstance(parent, ClassScope)
            and self.node.args.args
        ):
            for target in node.targets:
                if not isinstance(target, Attribute):
                    continue
                self.add_attr_name(target, parent)


@final
# pyre-fixme[11]: Annotation `SymbolVisitor` is not defined as a type.
class ClassConflictChecker(SymbolVisitor[object, TransformerScope]):
    def __init__(
        self,
        symbols: SymbolTable,
        # pyre-fixme[11]: Annotation `SymbolMap` is not defined as a type.
        symbol_map: SymbolMap,
        # pyre-fixme[11]: Annotation `ErrorSink` is not defined as a type.
        errors: Optional[ErrorSink],
        filename: str,
    ) -> None:
        # pyre-fixme[19]: Expected 0 positional arguments.
        super().__init__(
            ScopeStack(
                self.make_scope(symbols, None),
                symbol_map=symbol_map,
                scope_factory=self.make_scope,
            )
        )
        self.errors: ErrorSink = errors or ErrorSink()
        self.filename = filename

    def make_scope(
        self,
        symtable: SymbolTable,
        node: Optional[AST],
        vars: Optional[MutableMapping[str, object]] = None,
        # pyre-fixme[11]: Annotation `SymbolScope` is not defined as a type.
    ) -> SymbolScope[object, TransformerScope]:
        if isinstance(node, FunctionDef):
            # pyre-fixme[16]: `ClassConflictChecker` has no attribute `scopes`.
            data = FunctionScope(node, self.scopes.scopes[-1].scope_data)
        elif isinstance(node, ClassDef):
            data = ClassScope()
        else:
            data = TransformerScope()
        return SymbolScope(symtable, data)

    def visit_Name(self, node: Name) -> None:
        # pyre-fixme[16]: `ClassConflictChecker` has no attribute `scope_for`.
        scope = self.scope_for(node.id).scope_data
        if isinstance(node.ctx, ast.Load):
            scope.loaded(node.id)
        else:
            scope.stored(node.id)

    def visit_ExceptHandler(self, node: ExceptHandler) -> None:
        # pyre-fixme[16]: `ClassConflictChecker` has no attribute `generic_visit`.
        self.generic_visit(node)
        name = node.name
        if name is not None:
            # pyre-fixme[16]: `ClassConflictChecker` has no attribute `scope_for`.
            self.scope_for(name).scope_data.stored(name)

    def visit_Delete(self, node: Delete) -> None:
        for target in node.targets:
            if isinstance(target, ast.Name):
                # pyre-fixme[16]: `ClassConflictChecker` has no attribute `scope_for`.
                self.scope_for(target.id).scope_data.stored(target.id)

    def visit_Global(self, node: Global) -> None:
        # pyre-fixme[16]: `ClassConflictChecker` has no attribute `scopes`.
        if self.scopes.in_class_scope:
            for name in node.names:
                if name == "__annotations__":
                    self.errors.error(
                        ClassAttributesConflictException(
                            ["__annotations__"], node.lineno, self.filename
                        )
                    )

    def visit_ClassDef(self, node: ClassDef) -> None:
        # pyre-fixme[16]: `ClassConflictChecker` has no attribute `visit_Class_Outer`.
        self.visit_Class_Outer(node)

        # pyre-fixme[16]: `ClassConflictChecker` has no attribute `visit_Class_Inner`.
        class_scope = self.visit_Class_Inner(node).scope_data
        assert isinstance(class_scope, ClassScope)

        overlap = class_scope.instance_fields.intersection(class_scope.class_fields)
        if overlap:
            self.errors.error(
                ClassAttributesConflictException(overlap, node.lineno, self.filename)
            )

        # pyre-fixme[16]: `ClassConflictChecker` has no attribute `scope_for`.
        self.scope_for(node.name).scope_data.stored(node.name)

    def visit_FunctionDef(self, node: FunctionDef) -> None:
        # pyre-fixme[16]: `ClassConflictChecker` has no attribute `visit_Func_Outer`.
        self.visit_Func_Outer(node)

        # pyre-fixme[16]: `ClassConflictChecker` has no attribute `visit_Func_Inner`.
        func_scope = self.visit_Func_Inner(node)

        # pyre-fixme[16]: `ClassConflictChecker` has no attribute `scopes`.
        self.scopes.current[node.name] = func_scope.scope_data

        # pyre-fixme[16]: `ClassConflictChecker` has no attribute `scope_for`.
        self.scope_for(node.name).scope_data.stored(node.name)

    def visit_AsyncFunctionDef(self, node: AsyncFunctionDef) -> None:
        # pyre-fixme[16]: `ClassConflictChecker` has no attribute `visit_Func_Outer`.
        self.visit_Func_Outer(node)

        # pyre-fixme[16]: `ClassConflictChecker` has no attribute `visit_Func_Inner`.
        self.visit_Func_Inner(node)

        # pyre-fixme[16]: `ClassConflictChecker` has no attribute `scope_for`.
        self.scope_for(node.name).scope_data.stored(node.name)

    def visit_Import(self, node: Import) -> None:
        for name in node.names:
            # pyre-fixme[16]: `ClassConflictChecker` has no attribute `scope_for`.
            self.scope_for(imported_name(name)).scope_data.stored(imported_name(name))
        # pyre-fixme[16]: `ClassConflictChecker` has no attribute `generic_visit`.
        return self.generic_visit(node)

    def visit_ImportFrom(self, node: ImportFrom) -> None:
        if node.level == 0 and node.module is not None:
            for name in node.names:
                # pyre-fixme[16]: `ClassConflictChecker` has no attribute `scope_for`.
                self.scope_for(name.asname or name.name).scope_data.stored(
                    name.asname or name.name
                )

    def visit_Assign(self, node: Assign) -> None:
        # pyre-fixme[16]: `ClassConflictChecker` has no attribute `scopes`.
        self.scopes.scopes[-1].scope_data.visit_Assign(node)
        # pyre-fixme[16]: `ClassConflictChecker` has no attribute `generic_visit`.
        self.generic_visit(node)

    def visit_AnnAssign(self, node: AnnAssign) -> None:
        # pyre-fixme[16]: `ClassConflictChecker` has no attribute `scopes`.
        self.scopes.scopes[-1].scope_data.visit_AnnAssign(node)
        if node.value is not None:
            # pyre-fixme[16]: `ClassConflictChecker` has no attribute `generic_visit`.
            self.generic_visit(node)


def check_class_conflict(
    node: Module,
    filename: str,
    symbols: SymbolTable,
    symbol_map: SymbolMap,
    errors: Optional[ErrorSink] = None,
) -> None:
    visitor = ClassConflictChecker(
        symbols, symbol_map, errors=errors or ErrorSink(), filename=filename
    )
    # pyre-fixme[16]: `ClassConflictChecker` has no attribute `visit`.
    visitor.visit(node)

# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
from __future__ import annotations

import ast
from ast import (
    AST,
    AnnAssign,
    Assign,
    AsyncFor,
    AsyncFunctionDef,
    AsyncWith,
    Attribute,
    ClassDef,
    For,
    FunctionDef,
    If,
    Import,
    ImportFrom,
    Name,
    Try,
    While,
    With,
    expr,
)
from contextlib import nullcontext
from typing import Union, Sequence, Optional, List, TYPE_CHECKING

from ..visitor import ASTVisitor
from .module_table import ModuleTable
from .types import (
    AwaitableTypeRef,
    CEnumType,
    Class,
    DYNAMIC_TYPE,
    ENUM_TYPE,
    Function,
    ModuleInstance,
    NAMED_TUPLE_TYPE,
    OBJECT_TYPE,
    PROTOCOL_TYPE,
    ResolvedTypeRef,
    DecoratedMethod,
    TypeName,
    TypeRef,
)
from .visitor import GenericVisitor

if TYPE_CHECKING:
    from . import SymbolTable


class NestedScope:
    def declare_class(self, node: AST, klass: Class) -> None:
        pass

    def declare_function(self, func: Function | DecoratedMethod) -> None:
        pass

    def declare_variable(self, node: AnnAssign, module: ModuleTable) -> None:
        pass

    def declare_variables(self, node: Assign, module: ModuleTable) -> None:
        pass


TScopeTypes = Union[ModuleTable, Class, Function, NestedScope]


class DeclarationVisitor(GenericVisitor):
    def __init__(self, mod_name: str, filename: str, symbols: SymbolTable) -> None:
        module = symbols[mod_name] = ModuleTable(mod_name, filename, symbols)
        super().__init__(module)
        self.scopes: List[TScopeTypes] = [self.module]

    def finish_bind(self) -> None:
        self.module.finish_bind()

    def parent_scope(self) -> TScopeTypes:
        return self.scopes[-1]

    def enter_scope(self, scope: TScopeTypes) -> None:
        self.scopes.append(scope)

    def exit_scope(self) -> None:
        self.scopes.pop()

    def visitAnnAssign(self, node: AnnAssign) -> None:
        self.parent_scope().declare_variable(node, self.module)

    def visitAssign(self, node: Assign) -> None:
        self.parent_scope().declare_variables(node, self.module)

    def visitClassDef(self, node: ClassDef) -> None:
        bases = [self.module.resolve_type(base) or DYNAMIC_TYPE for base in node.bases]
        if not bases:
            bases.append(OBJECT_TYPE)

        with self.symtable.error_sink.error_context(self.filename, node):
            klasses = []
            for base in bases:
                klasses.append(
                    base.make_subclass(TypeName(self.module_name, node.name), bases)
                )
            for cur_type in klasses:
                if type(cur_type) != type(klasses[0]):
                    self.syntax_error("Incompatible subtypes", node)
            klass = klasses[0]

        parent_scope = self.parent_scope()
        self.enter_scope(klass)
        for item in node.body:
            with self.symtable.error_sink.error_context(self.filename, item):
                self.visit(item)

        for base in bases:
            if base is NAMED_TUPLE_TYPE:
                # In named tuples, the fields are actually elements
                # of the tuple, so we can't do any advanced binding against it.
                klass = DYNAMIC_TYPE
                break

            if base is PROTOCOL_TYPE:
                # Protocols aren't guaranteed to exist in the actual MRO, so let's treat
                # them as dynamic to force dynamic dispatch.
                klass = DYNAMIC_TYPE
                break

            if base.is_final:
                self.syntax_error(
                    f"Class `{klass.instance.name}` cannot subclass a Final class: `{base.instance.name}`",
                    node,
                )

        for d in reversed(node.decorator_list):
            if klass is DYNAMIC_TYPE:
                break
            with self.symtable.error_sink.error_context(self.filename, d):
                decorator = self.module.resolve_type(d) or DYNAMIC_TYPE
                klass = decorator.bind_decorate_class(klass)

        parent_scope.declare_class(node, klass)
        self.module.types[node] = klass
        self.exit_scope()

    def _visitFunc(self, node: Union[FunctionDef, AsyncFunctionDef]) -> None:
        function = self._make_function(node)
        self.parent_scope().declare_function(function)

    def _make_function(self, node: Union[FunctionDef, AsyncFunctionDef]) -> Function:
        func = orig_func = Function(node, self.module, self.type_ref(node))
        self.enter_scope(func)
        for item in node.body:
            self.visit(item)
        self.exit_scope()
        self.module.types[node] = func
        return func

    def visitFunctionDef(self, node: FunctionDef) -> None:
        self._visitFunc(node)

    def visitAsyncFunctionDef(self, node: AsyncFunctionDef) -> None:
        self._visitFunc(node)

    def type_ref(self, node: Union[FunctionDef, AsyncFunctionDef]) -> TypeRef:
        ann = node.returns
        if not ann:
            res = ResolvedTypeRef(DYNAMIC_TYPE)
        else:
            res = TypeRef(self.module, ann)
        if isinstance(node, AsyncFunctionDef):
            res = AwaitableTypeRef(res, self.module.symtable)
        return res

    def visitImport(self, node: Import) -> None:
        for name in node.names:
            self.symtable.import_module(name.name)
            asname = name.asname
            if asname is None:
                top_level_module = name.name.split(".")[0]
                self.module.children[top_level_module] = ModuleInstance(
                    top_level_module
                )
            else:
                self.module.children[asname] = ModuleInstance(name.name)

    def visitImportFrom(self, node: ImportFrom) -> None:
        mod_name = node.module
        if not mod_name or node.level:
            raise NotImplementedError("relative imports aren't supported")
        self.symtable.import_module(mod_name)
        mod = self.symtable.modules.get(mod_name)
        if mod is not None:
            for name in node.names:
                val = mod.children.get(name.name)
                child_name = name.asname or name.name
                if val is not None:
                    self.module.children[child_name] = val
                else:
                    # We might be facing a module imported as an attribute.
                    module_as_attribute = f"{mod_name}.{name.name}"
                    self.symtable.import_module(module_as_attribute)
                    if module_as_attribute in self.symtable.modules:
                        self.module.children[child_name] = ModuleInstance(
                            module_name=module_as_attribute
                        )

    # We don't pick up declarations in nested statements
    def visitFor(self, node: For) -> None:
        self.enter_scope(NestedScope())
        self.generic_visit(node)
        self.exit_scope()

    def visitAsyncFor(self, node: AsyncFor) -> None:
        self.enter_scope(NestedScope())
        self.generic_visit(node)
        self.exit_scope()

    def visitWhile(self, node: While) -> None:
        self.enter_scope(NestedScope())
        self.generic_visit(node)
        self.exit_scope()

    def visitIf(self, node: If) -> None:
        test = node.test
        if isinstance(test, Name) and test.id == "TYPE_CHECKING":
            self.visit(node.body)
        else:
            self.enter_scope(NestedScope())
            self.visit(node.body)
            self.exit_scope()

    def visitWith(self, node: With) -> None:
        self.enter_scope(NestedScope())
        self.generic_visit(node)
        self.exit_scope()

    def visitAsyncWith(self, node: AsyncWith) -> None:
        self.enter_scope(NestedScope())
        self.generic_visit(node)
        self.exit_scope()

    def visitTry(self, node: Try) -> None:
        self.enter_scope(NestedScope())
        self.generic_visit(node)
        self.exit_scope()

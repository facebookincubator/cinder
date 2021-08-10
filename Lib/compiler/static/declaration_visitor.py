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
    Class,
    DYNAMIC_TYPE,
    Function,
    NAMED_TUPLE_TYPE,
    OBJECT_TYPE,
    PROTOCOL_TYPE,
    ResolvedTypeRef,
    DecoratedMethod,
    TypeName,
    TypeRef,
)

if TYPE_CHECKING:
    from . import SymbolTable


class GenericVisitor(ASTVisitor):
    def __init__(self, module_name: str, filename: str, symtable: SymbolTable) -> None:
        super().__init__()
        self.module_name = module_name
        self.filename = filename
        self.symtable = symtable

    def visit(self, node: Union[AST, Sequence[AST]], *args: object) -> Optional[object]:
        # if we have a sequence of nodes, don't catch TypedSyntaxError here;
        # walk_list will call us back with each individual node in turn and we
        # can catch errors and add node info then.
        ctx = (
            self.symtable.error_sink.error_context(self.filename, node)
            if isinstance(node, AST)
            else nullcontext()
        )
        with ctx:
            return super().visit(node, *args)

    def syntax_error(self, msg: str, node: AST) -> None:
        return self.symtable.error_sink.syntax_error(msg, self.filename, node)


class InitVisitor(ASTVisitor):
    def __init__(
        self, module: ModuleTable, klass: Class, init_func: FunctionDef
    ) -> None:
        super().__init__()
        self.module = module
        self.klass = klass
        self.init_func = init_func

    def visitAnnAssign(self, node: AnnAssign) -> None:
        target = node.target
        if isinstance(target, Attribute):
            value = target.value
            if (
                isinstance(value, ast.Name)
                and value.id == self.init_func.args.args[0].arg
            ):
                attr = target.attr
                self.klass.define_slot(
                    attr,
                    TypeRef(self.module, node.annotation),
                    assignment=node,
                )

    def visitAssign(self, node: Assign) -> None:
        for target in node.targets:
            if not isinstance(target, Attribute):
                continue
            value = target.value
            if (
                isinstance(value, ast.Name)
                and value.id == self.init_func.args.args[0].arg
            ):
                attr = target.attr
                self.klass.define_slot(attr, assignment=node)


class DeclarationVisitor(GenericVisitor):
    def __init__(self, mod_name: str, filename: str, symbols: SymbolTable) -> None:
        super().__init__(mod_name, filename, symbols)
        self.module = symbols[mod_name] = ModuleTable(mod_name, filename, symbols)
        self.scopes: List[ModuleTable | Class | Function] = [self.module]

    def finish_bind(self) -> None:
        self.module.finish_bind()

    def visitAnnAssign(self, node: AnnAssign) -> None:
        self.module.decls.append((node, None))

    def parent_scope(self) -> ModuleTable | Class | Function:
        return self.scopes[-1]

    def enter_scope(self, scope: ModuleTable | Class | Function) -> None:
        self.scopes.append(scope)

    def exit_scope(self) -> None:
        self.scopes.pop()

    def visitClassDef(self, node: ClassDef) -> None:
        bases = [self.module.resolve_type(base) or DYNAMIC_TYPE for base in node.bases]
        if not bases:
            bases.append(OBJECT_TYPE)
        klass = Class(TypeName(self.module_name, node.name), bases)
        parent_scope = self.parent_scope()
        self.enter_scope(klass)
        for item in node.body:
            with self.symtable.error_sink.error_context(self.filename, item):
                if isinstance(item, (AsyncFunctionDef, FunctionDef)):
                    function = self._make_function(item)
                    if not function:
                        continue
                    klass.define_function(item.name, function, self)
                    if (
                        item.name != "__init__"
                        or not item.args.args
                        or not isinstance(item, FunctionDef)
                    ):
                        continue

                    InitVisitor(self.module, klass, item).visit(item.body)
                elif isinstance(item, AnnAssign):
                    # class C:
                    #    x: foo
                    target = item.target
                    if isinstance(target, ast.Name):
                        klass.define_slot(
                            target.id,
                            TypeRef(self.module, item.annotation),
                            # Note down whether the slot has been assigned a value.
                            assignment=item if item.value else None,
                        )
                elif isinstance(item, ClassDef):
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

        for d in node.decorator_list:
            if klass is DYNAMIC_TYPE:
                break
            with self.symtable.error_sink.error_context(self.filename, d):
                decorator = self.module.resolve_type(d) or DYNAMIC_TYPE
                klass = decorator.bind_decorate_class(klass)

        parent_scope.declare_class(node, klass)
        self.exit_scope()

    def _visitFunc(self, node: Union[FunctionDef, AsyncFunctionDef]) -> None:
        function = self._make_function(node)
        if function:
            self.module.children[function.func_name] = function

    def _make_function(
        self, node: Union[FunctionDef, AsyncFunctionDef]
    ) -> Function | DecoratedMethod | None:
        func = Function(node, self.module, self.type_ref(node.returns))
        for decorator in node.decorator_list:
            decorator_type = self.module.resolve_type(decorator) or DYNAMIC_TYPE
            func = decorator_type.bind_decorate_function(self, func)
            if not isinstance(func, (Function, DecoratedMethod)):
                return None
        return func

    def visitFunctionDef(self, node: FunctionDef) -> None:
        self._visitFunc(node)

    def visitAsyncFunctionDef(self, node: AsyncFunctionDef) -> None:
        self._visitFunc(node)

    def type_ref(self, ann: Optional[expr]) -> TypeRef:
        if not ann:
            return ResolvedTypeRef(DYNAMIC_TYPE)
        return TypeRef(self.module, ann)

    def visitImport(self, node: Import) -> None:
        for name in node.names:
            self.symtable.import_module(name.name)

    def visitImportFrom(self, node: ImportFrom) -> None:
        mod_name = node.module
        if not mod_name or node.level:
            raise NotImplementedError("relative imports aren't supported")
        self.symtable.import_module(mod_name)
        mod = self.symtable.modules.get(mod_name)
        if mod is not None:
            for name in node.names:
                val = mod.children.get(name.name)
                if val is not None:
                    self.module.children[name.asname or name.name] = val

    # We don't pick up declarations in nested statements
    def visitFor(self, node: For) -> None:
        pass

    def visitAsyncFor(self, node: AsyncFor) -> None:
        pass

    def visitWhile(self, node: While) -> None:
        pass

    def visitIf(self, node: If) -> None:
        test = node.test
        if isinstance(test, Name) and test.id == "TYPE_CHECKING":
            self.visit(node.body)

    def visitWith(self, node: With) -> None:
        pass

    def visitAsyncWith(self, node: AsyncWith) -> None:
        pass

    def visitTry(self, node: Try) -> None:
        pass

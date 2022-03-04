from __future__ import annotations

import ast
from ast import AST
from typing import Dict, Optional, final, List

from ..consts import (
    SC_CELL,
    SC_FREE,
    SC_GLOBAL_EXPLICIT,
    SC_GLOBAL_IMPLICIT,
    SC_LOCAL,
)
from ..errors import CollectingErrorSink
from ..symbols import SymbolVisitor, Scope
from ..unparse import to_expr
from ..visitor import ASTVisitor
from .types import MUTABLE, READONLY, Value
from .util import is_readonly_annotation, is_readonly_wrapped, is_readonly_func

TReadonlyTypes = Dict[AST, Value]


class ReadonlyBindingScope:
    def __init__(
        self,
        node: AST,
        binder: ReadonlyTypeBinder,
        parent: Optional[ReadonlyBindingScope] = None,
    ) -> None:
        self.node = node
        self.types: Dict[str, Value] = {}
        self.binder = binder
        self.parent = parent
        self._parent_function: Optional[
            ReadonlyBindingScope
        ] = None  # the inner most parent function, if any

    def declare(
        self,
        name: str,
        node: ast.AST,
        is_readonly: Value,
        custom_err_msg: Optional[str] = None,
    ) -> bool:
        """
        returns False if conflicting declaration is found
        If False is returned then the scope is not changed
        """
        # TODO: allow different readonliness for same name
        # in separate branches
        if self.types.get(name, is_readonly) != is_readonly:
            err_msg = (
                custom_err_msg
                or f"cannot re-declare the readonliness of '{name}' to {is_readonly}"
            )
            self.binder.readonly_type_error(err_msg, node)
            return False
        self.types[name] = is_readonly
        return True

    def get_parent_function(self) -> Optional[ReadonlyBindingScope]:
        if self._parent_function is not None:
            return self._parent_function
        parent = self.parent
        while parent is not None:
            if isinstance(parent, ReadonlyFunctionBindingScope):
                self._parent_function = parent
                return parent
            parent = parent.parent
        return None

    def ignore_scope_for_readonly(self) -> bool:
        """
        Returns whether the scope should be ignored when looking up
        readonliness of names.
        Right now, only function scopes are looked up, while class and
        module scopes are ignored
        """
        return False

    def readonly_nonlocal(self) -> bool:
        """
        Returns whether nonlocal names in the scope are always readonly
        """
        return False

    def returns_readonly(self) -> bool:
        """
        Returns whether this scope returns values as readonly (can only be True in
        function scopes)
        """
        return False

    @property
    def is_nested(self) -> bool:
        """
        Returns whether the scope is nested inside a function
        """
        return self.get_parent_function() is not None

    @property
    def is_previsit(self) -> bool:
        """
        Functions are visited twice in order to collect all names
        for inner scopes.
        In the first time, no inner scopes are visited.
        Returns whether this scope is in a pre-visit state (i.e. inner scopes are skipped)
        """
        parent_func = self.get_parent_function()
        if parent_func is not None:
            return parent_func.is_previsit
        return False

    def store_post_visit(self, node: ast.AST) -> None:
        """
        Store a node for a second visit in this scope.
        This scope should be a function, or nested inside one
        """
        parent_func = self.get_parent_function()
        if parent_func is not None:
            parent_func.store_post_visit(node)

    def finish_previsit(self) -> None:
        """
        Indicate that inner scopes in this scope can now be visited.
        No effect outside of function scopes.
        """
        pass


class IgnoredReadonlyScope(ReadonlyBindingScope):
    def declare(
        self,
        name: str,
        node: ast.AST,
        is_readonly: Value,
        custom_err_msg: Optional[str] = None,
    ) -> bool:
        if is_readonly != MUTABLE:
            self.binder.readonly_type_error(
                f"cannot declare '{name}' readonly in class/module", node
            )
        return False

    def ignore_scope_for_readonly(self) -> bool:
        return True


class ReadonlyModuleBindingScope(IgnoredReadonlyScope):
    pass


class ReadonlyClassBindingScope(IgnoredReadonlyScope):
    pass


class ReadonlyFunctionBindingScope(ReadonlyBindingScope):
    def __init__(
        self,
        node: AST,
        binder: ReadonlyTypeBinder,
        parent: Optional[ReadonlyBindingScope] = None,
        returns_readonly: bool = False,
        readonly_nonlocal: bool = False,
        pre_visit: bool = False,
    ) -> None:
        super().__init__(node, binder, parent)
        self._returns_readonly = returns_readonly
        self._readonly_nonlocal = readonly_nonlocal
        self._pre_visit = pre_visit
        self.post_visit: List[ast.AST] = []

    def readonly_nonlocal(self) -> bool:
        return self._readonly_nonlocal

    def returns_readonly(self) -> bool:
        return self._returns_readonly

    @property
    def is_previsit(self) -> bool:
        return self._pre_visit

    def store_post_visit(self, node: ast.AST) -> None:
        self.post_visit.append(node)

    def finish_previsit(self) -> None:
        self._pre_visit = False


class ImmediateReadonlyContext:
    """
    Used in type binder to indicate an immdieate context specifying
    the next AST node to have certain readonliness.
    This models readonly(expr) where expr as a whole becomes
    readonly but not any of its subexpressions
    """

    value: Optional[Value]
    expired: bool

    def __init__(self, value: Optional[Value] = None, expired: bool = False) -> None:
        self.value = value
        self.expired = expired


@final
class ReadonlyTypeBinder(ASTVisitor):
    """
    Note: as_readonly is only passed to the immediate next visit method
        if you want to pass as_readonly through generic_visit, you need to explicitly
        implement the visit_ method for that AST node
    """

    def __init__(
        self,
        node: AST,
        filename: str,
        symbols: SymbolVisitor,
        bind_types: Optional[TReadonlyTypes] = None,
    ) -> None:
        super().__init__()
        self.input = node
        self.symbols = symbols
        self.filename = filename
        self.scopes: Dict[AST, Scope] = symbols.scopes
        self.bind_types: TReadonlyTypes = bind_types or {}
        self.readonly_scope: ReadonlyBindingScope = ReadonlyModuleBindingScope(
            node, self, None
        )
        self.error_sink = CollectingErrorSink()

    # -------------------------- helpers ---------------------------
    def readonly_type_error(self, msg: str, node: AST) -> None:
        self.error_sink.syntax_error(msg, self.filename, node)

    def get_types(self) -> TReadonlyTypes:
        if not self.bind_types:
            self.visit(self.input)
        return self.bind_types

    @property
    def scope(self) -> Scope:
        return self.scopes[self.readonly_scope.node]

    def child_scope(self, scope: ReadonlyBindingScope) -> ReadonlyBindingScope:
        scope.parent = self.readonly_scope
        self.readonly_scope = scope
        return scope

    def restore_scope(self) -> ReadonlyBindingScope:
        parent = self.readonly_scope.parent
        assert parent is not None
        self.readonly_scope = parent
        return parent

    @property
    def returns_readonly(self) -> bool:
        return self.readonly_scope.returns_readonly()

    @property
    def readonly_nonlocal(self) -> bool:
        return self.readonly_scope.readonly_nonlocal()

    def declare(
        self,
        name: str,
        node: ast.AST,
        is_readonly: Value,
        custom_err_msg: Optional[str] = None,
    ) -> bool:
        return self.readonly_scope.declare(name, node, is_readonly, custom_err_msg)

    def store_node(self, node: AST, is_readonly: Value) -> None:
        self.bind_types[node] = is_readonly

    def store_mutable(self, node: AST) -> None:
        self.store_node(node, MUTABLE)

    def store_readonly(self, node: AST) -> None:
        self.store_node(node, READONLY)

    def is_readonly(self, node: AST) -> bool:
        """node is readonly"""
        return self.bind_types[node].is_readonly

    def get_name_readonly(self, name: str, is_local: bool = False) -> Optional[Value]:
        """
        This only looks through nested functions, but no modules or classes
        """
        if is_local:
            return self.readonly_scope.types.get(name)
        else:
            scope = self.readonly_scope
            while scope.parent is not None:
                if not scope.ignore_scope_for_readonly():
                    value = scope.types.get(name)
                    if value is not None:
                        return value
                scope = scope.parent

    # -------------------------- visit methods ---------------------------
    @final
    # pyre-fixme[14]: inconsistent override, no variadic *args support
    def generic_visit(self, node: AST, as_readonly: Optional[Value] = None) -> None:
        self.store_mutable(node)
        super().generic_visit(node)

    # ------------------------- Expressions -------------------------------

    def visitName(self, node: ast.Name, as_readonly: Optional[Value] = None) -> None:
        if as_readonly is not None:
            self.store_node(node, as_readonly)
            return
        name = node.id
        name_scope = self.scope.check_name(name)
        if name_scope in (SC_LOCAL, SC_CELL):
            is_readonly = self.get_name_readonly(name, is_local=True)
            if is_readonly is not None:
                self.store_node(node, is_readonly)
            else:
                # unknown local name, treat as mutable.
                # When this happens the source code should lead to a NameError or UnboundLocalError
                self.store_mutable(node)
        elif name_scope in (SC_GLOBAL_EXPLICIT, SC_GLOBAL_IMPLICIT):
            # globals are not readonly. They are handled separately
            self.store_mutable(node)
        elif name_scope == SC_FREE:
            if self.readonly_nonlocal:
                self.store_readonly(node)
            else:
                is_readonly = self.get_name_readonly(name)
                if is_readonly is not None:
                    self.store_node(node, is_readonly)
                else:
                    # TODO: T109448035 when all scenarios where new names are assigned to is covered
                    # (including for loops, comprehension, with, try...except, etc.)
                    # this should raise an exception instead
                    self.store_mutable(node)
        else:
            # TODO: T109448035 when all scenarios where new names are assigned to is covered
            # (including for loops, comprehension, with, try...except, etc.)
            # this should raise an exception instead
            self.store_mutable(node)

    def visitCall(self, node: ast.Call, as_readonly: Optional[Value] = None) -> None:
        if is_readonly_wrapped(node.func):
            if len(node.args) != 1 or len(node.keywords) > 0:
                self.readonly_type_error(
                    "readonly() only accepts 1 positional arg", node
                )
            self.visit(node.args[0], READONLY)
            self.store_readonly(node)
        else:
            self.visit(node.func)
            self.walk_list(node.args)
            self.walk_list(node.keywords)
            self.store_node(node, as_readonly or MUTABLE)
            has_star_arg = any(isinstance(a, ast.Starred) for a in node.args)
            if (node.keywords or has_star_arg) and (
                any(self.is_readonly(n) for n in node.args)
                or any(self.is_readonly(n.value) for n in node.keywords)
            ):
                # calling with keywords while any arg is readonly
                # this is unsupported for now due to difficulty in setting up guard
                # with passing keyword args
                self.readonly_type_error(
                    "Unsupported: cannot use keyword args or"
                    " star args when ANY argument is readonly",
                    node,
                )

    def visitLambda(
        self, node: ast.Lambda, as_readonly: Optional[Value] = None
    ) -> None:
        self.store_node(node, as_readonly or MUTABLE)
        if self.readonly_scope.is_previsit:
            self.readonly_scope.store_post_visit(node)
            return
        self.visit(node.args)
        self.visit(node.body)

    def visitStarred(
        self, node: ast.Starred, as_readonly: Optional[Value] = None
    ) -> None:
        self.visit(node.value)
        self.store_node(node, as_readonly or self.bind_types[node.value])

    # ------------------------- Statements --------------------------------

    def assign_to(self, lhs: AST, rhs: AST, node: AST) -> None:
        """
        Checks whether rhs can be assigned to lhs without
        breaking readonly constraints
        """
        rhs_readonly = self.bind_types[rhs]
        if isinstance(lhs, ast.Name):
            lname = lhs.id
            name_scope = self.scope.check_name(lname)
            if name_scope == SC_FREE and self.readonly_nonlocal:
                # cannot modify closure in readonly_func
                self.readonly_type_error(
                    f"cannot modify '{lname}' from a closure, inside a readonly_func annotated function",
                    node,
                )
                return
            name_readonly = self.get_name_readonly(lname)
            if name_readonly and rhs_readonly.can_convert_to(name_readonly):
                # it's okay to narrow readonliness
                pass
            else:
                err_msg = (
                    f"cannot assign {rhs_readonly} value to {name_readonly} '{lname}'. "
                    + "Remember to declare name as readonly explicitly"
                )
                self.declare(lname, lhs, rhs_readonly, custom_err_msg=err_msg)
        elif isinstance(lhs, ast.Tuple | ast.List):
            # taking elements of a readonly/mutable collection
            # results in readonly/mutable respectively, so here
            # we just assign rhs to each element on the lhs
            for lhs_child in lhs.elts:
                self.assign_to(lhs_child, rhs, node)
        elif isinstance(lhs, ast.Starred):
            self.assign_to(lhs.value, rhs, node)
        else:
            pass  # TODO check other kind of assigns

    def visitAnnAssign(self, node: ast.AnnAssign) -> None:
        is_readonly_ann = is_readonly_annotation(node.annotation)
        readonly_value = READONLY if is_readonly_ann else MUTABLE
        rhs = node.value
        lhs = node.target
        if isinstance(lhs, ast.Name):
            # var declaration
            self.declare(lhs.id, lhs, readonly_value)
        if rhs:
            self.visit(rhs, READONLY if is_readonly_ann else None)
            self.assign_to(lhs, rhs, node)

    def visitAssign(self, node: ast.Assign) -> None:
        self.visit(node.value)
        for t in node.targets:
            self.assign_to(t, node.value, node)

    def visitAugAssign(self, node: ast.AugAssign) -> None:
        self.visit(node.value)
        if self.is_readonly(node.value):
            self.readonly_type_error(
                "Unsupported: aug assign (such as '+=') when the assigned value is readonly",
                node,
            )
        self.visit(node.target)
        if self.is_readonly(node.target):
            self.readonly_type_error(
                f"Cannot modify readonly reference '{to_expr(node.target)}' via aug assign",
                node,
            )
        self.assign_to(node.target, node.value, node)

    def _visitFor(self, node: ast.For | ast.AsyncFor) -> None:
        self.visit(node.iter)
        # Although elements of the iterable will be what's actually assigned,
        # the readonly-ness of the iterable itself is what determines the
        # readonly-ness of the resulting value.
        # We also currently ignore the type_comment on the node.
        self.assign_to(node.target, node.iter, node)
        self.walk_list(node.body)
        self.walk_list(node.orelse)

    def visitFor(self, node: ast.For) -> None:
        self._visitFor(node)

    def visitAsyncFor(self, node: ast.AsyncFor) -> None:
        self._visitFor(node)

    def visitDelete(self, node: ast.Delete) -> None:
        for t in node.targets:
            self.visit(t)
            if self.is_readonly(t):
                self.readonly_type_error(
                    f"Cannot explicitly delete readonly value '{to_expr(t)}'", node
                )

    def visitRaise(self, node: ast.Raise) -> None:
        if node.exc:
            e = node.exc
            self.visit(e)
            if self.is_readonly(e):
                self.readonly_type_error(
                    f"Cannot raise readonly expression '{to_expr(e)}'", node
                )

        if node.cause:
            c = node.cause
            self.visit(c)
            if self.is_readonly(c):
                self.readonly_type_error(
                    f"Cannot raise with readonly cause '{to_expr(c)}'", node
                )

    def visitReturn(self, node: ast.Return) -> None:
        v = node.value
        if v:
            self.visit(v)
            if isinstance(self.readonly_scope, ReadonlyFunctionBindingScope):
                parent = self.readonly_scope
            else:
                parent = self.readonly_scope.get_parent_function()

            if not parent:
                if isinstance(self.readonly_scope, ReadonlyModuleBindingScope):
                    # Various tests expect a cleaner error message, and we want to reserve the
                    # readonly specific error message for actual failures to find the parent.
                    self.readonly_type_error(f"'return' outside function", node)
                else:
                    self.readonly_type_error(
                        f"Unable to determine parent function for return statement",
                        node,
                    )
                return
            if not parent.returns_readonly() and self.is_readonly(v):
                self.readonly_type_error(
                    f"Cannot return readonly expression '{to_expr(v)}' from a function returning a mutable type",
                    node,
                )

    def visitClassDef(self, node: ast.ClassDef) -> None:
        name = node.name
        self.declare(name, node, MUTABLE)
        self.store_mutable(node)
        # inheriting from readonly bases is unsafe, subclass can
        # modify base class
        self.walk_list(node.bases)
        for b in node.bases:
            if self.is_readonly(b):
                self.readonly_type_error(
                    f"cannot inherit from a readonly base class '{to_expr(b)}'", b
                )
        # using readonly value as keyword is unsafe
        # XXX: we could pass the readonliness of keyword args to
        # __new__ and do check there, in the future
        self.walk_list(node.keywords)
        for k in node.keywords:
            if self.is_readonly(k.value):
                self.readonly_type_error(
                    f"cannot pass a readonly keyword argument '{to_expr(k)}' in class definition",
                    k,
                )

        self.child_scope(ReadonlyClassBindingScope(node, self))
        self.walk_list(node.body)
        self.restore_scope()
        self.walk_list(node.decorator_list)

    def visitFunctionDef(self, node: ast.FunctionDef) -> None:
        return self._visitFunc(node)

    def visitAsyncFunctionDef(self, node: ast.AsyncFunctionDef) -> None:
        return self._visitFunc(node)

    def _visitFunc(self, node: ast.FunctionDef | ast.AsyncFunctionDef) -> None:
        name = node.name
        self.declare(name, node, MUTABLE)
        self.store_mutable(node)
        if self.readonly_scope.is_previsit:
            self.readonly_scope.store_post_visit(node)
            return

        readonly_func = any(is_readonly_func(dec) for dec in node.decorator_list)
        returns_readonly = (
            return_sig := node.returns
        ) is not None and is_readonly_annotation(return_sig)
        func_scope = ReadonlyFunctionBindingScope(
            node,
            self,
            returns_readonly=returns_readonly,
            readonly_nonlocal=readonly_func,
            pre_visit=True,
        )
        self.child_scope(func_scope)
        # put parameters in scope
        self.visit(node.args)
        self.walk_list(node.body)
        func_scope.finish_previsit()
        self.walk_list(func_scope.post_visit)
        self.restore_scope()
        self.walk_list(node.decorator_list)

    def visitarguments(self, node: ast.arguments) -> None:
        self.walk_list(node.posonlyargs)
        self.walk_list(node.args)
        self.walk_list(node.kwonlyargs)
        if (vararg := node.vararg) is not None:
            self.visitarg(vararg)
        if (kwarg := node.kwarg) is not None:
            self.visitarg(kwarg)
        # TODO: We may want to automatically assign readonliness to defaults
        # based on the annotations of the args
        self.walk_list(node.defaults)
        self.walk_list([n for n in node.kw_defaults if n is not None])

    def visitarg(self, node: ast.arg) -> None:
        if (annotation := node.annotation) is not None:
            is_readonly = is_readonly_annotation(annotation)
            readonly_value = READONLY if is_readonly else MUTABLE
        else:
            readonly_value = MUTABLE
        self.declare(node.arg, node, readonly_value)

    def visitModule(self, node: ast.Module) -> None:
        self.child_scope(ReadonlyModuleBindingScope(node, self))
        self.walk_list(node.body)
        self.restore_scope()

from __future__ import annotations

import ast
from ast import AST, Name
from typing import Dict, Optional, final, List, Tuple

from ..consts import (
    SC_CELL,
    SC_FREE,
    SC_GLOBAL_EXPLICIT,
    SC_GLOBAL_IMPLICIT,
    SC_LOCAL,
    SC_UNKNOWN,
)
from ..errors import CollectingErrorSink
from ..symbols import SymbolVisitor, Scope
from ..unparse import to_expr
from ..visitor import ASTVisitor
from .types import MUTABLE, READONLY, Value, FunctionValue
from .util import (
    is_readonly_wrapped,
    is_readonly_func,
    is_readonly_func_nonlocal,
    is_tuple_wrapped,
    READONLY_DECORATORS,
)

TReadonlyTypes = Dict[AST, Value]

READONLY_ANNOTATION: str = "Readonly"

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
            ReadonlyFunctionBindingScope
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

    def get_parent_function(self) -> Optional[ReadonlyFunctionBindingScope]:
        if self._parent_function is not None:
            return self._parent_function
        parent = self.parent
        while parent is not None:
            if isinstance(parent, ReadonlyFunctionBindingScope):
                self._parent_function = parent
                return parent
            parent = parent.parent
        return None

    def is_name_readonly(self, name: str) -> bool:
        """
        Returns if a given name is readonly
        """
        return self.types.get(name, READONLY) == READONLY

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


class NameLookupOnlyScope(ReadonlyBindingScope):
    """
    Scopes which only exist for name lookup. Names declared
    in these scopes cannot be marked readonly()
    """
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
        self.types[name] = is_readonly
        return True


class ReadonlyModuleBindingScope(NameLookupOnlyScope):
    pass


class ReadonlyClassBindingScope(NameLookupOnlyScope):
    pass


class ReadonlyComprehensionBindingScope(ReadonlyBindingScope):
    pass


class ReadonlyFunctionBindingScope(ReadonlyBindingScope):
    def __init__(
        self,
        node: AST,
        binder: ReadonlyTypeBinder,
        parent: Optional[ReadonlyBindingScope] = None,
        returns_readonly: bool = False,
        yields_readonly: bool = False,
        sends_readonly: bool = False,
        readonly_nonlocal: bool = False,
        pre_visit: bool = False,
    ) -> None:
        super().__init__(node, binder, parent)
        self._returns_readonly = returns_readonly
        self._yields_readonly = yields_readonly
        self._sends_readonly = sends_readonly
        self._readonly_nonlocal = readonly_nonlocal
        self._pre_visit = pre_visit
        self.post_visit: List[ast.AST] = []

    def readonly_nonlocal(self) -> bool:
        return self._readonly_nonlocal

    def returns_readonly(self) -> bool:
        return self._returns_readonly

    def yields_readonly(self) -> bool:
        return self._yields_readonly

    def sends_readonly(self) -> bool:
        return self._sends_readonly

    @property
    def is_previsit(self) -> bool:
        return self._pre_visit

    def store_post_visit(self, node: ast.AST) -> None:
        self.post_visit.append(node)

    def finish_previsit(self) -> None:
        self._pre_visit = False


class ImmediateReadonlyContext:
    """
    Used in type binder to indicate an immediate context specifying
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
        self.read_only_funcs: Dict[AST, FunctionValue] = {}
        self.readonly_scope: ReadonlyBindingScope = ReadonlyModuleBindingScope(
            node, self, None
        )
        self.error_sink = CollectingErrorSink()
        self.get_types()

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

    def current_readonly_function(self) -> Optional[ReadonlyFunctionBindingScope]:
        if isinstance(self.readonly_scope, ReadonlyFunctionBindingScope):
            return self.readonly_scope
        else:
            return self.readonly_scope.get_parent_function()

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

    def store_readonly_func(
        self, node: AST, readonly_func_value: FunctionValue
    ) -> None:
        self.read_only_funcs[node] = readonly_func_value

    def is_readonly(self, node: AST) -> bool:
        """node is readonly"""
        return self.bind_types[node].is_readonly

    def get_name_readonly(self, name: str, is_local: bool = False) -> Optional[Value]:
        """
        This only looks through nested functions, but no modules or classes
        """
        if is_local:
            sc = self.readonly_scope
            while isinstance(sc, ReadonlyComprehensionBindingScope):
                if name in sc.types:
                    return sc.types.get(name)
                sc = sc.parent
                if sc is None:
                    return None
            return sc.types.get(name)
        else:
            scope = self.readonly_scope
            while scope.parent is not None:
                value = scope.types.get(name)
                if value is not None:
                    return value
                scope = scope.parent

    def is_readonly_annotation(self, node: AST) -> bool:
        if isinstance(node, ast.Name) and node.id == READONLY_ANNOTATION:
            self.readonly_type_error(
                "Readonly is not a valid annotation. If a readonly value of unknown type is desired, use Readonly[object] instead.",
                node,
            )
            return False
        return (
            isinstance(node, ast.Subscript)
            and isinstance(node.value, ast.Name)
            and node.value.id == READONLY_ANNOTATION
        )

    def extract_generator_annotations_readonly(
        self,
        node: AST,
    ) -> Optional[Tuple[bool, bool, bool]]:
        """
        Returns (YieldType, SendType, ReturnType)
        """
        n = node
        if not isinstance(n, ast.Subscript):
            return None
        val = n.value
        if not isinstance(val, ast.Name):
            return None
        if val.id == "Generator" or val.id == "AsyncGenerator":
            if not isinstance(n.slice, ast.Index):
                return None
            val = n.slice.value
            if not isinstance(val, ast.Tuple):
                return None
            return (
                self.is_readonly_annotation(val.elts[0]),
                self.is_readonly_annotation(val.elts[1]),
                self.is_readonly_annotation(val.elts[2])
                if len(val.elts) == 3
                else False,
            )
        elif val.id == "Iterator" or val.id == "AsyncIterator":
            if not isinstance(n.slice, ast.Index):
                return None
            if not isinstance(n.slice.value, ast.Subscript):
                return None
            return (
                self.is_readonly_annotation(n.slice.value),
                False,
                False,
            )
        return None

    def walk_is_any_readonly(self, args: List[ast.expr]) -> bool:
        """
        Walks the list of expressions and returns true if any are readonly.
        """
        self.walk_list(args)
        for a in args:
            if self.is_readonly(a):
                return True
        return False

    def scope_check_name(self, name: str) -> int:
        """
        Determine the scope of the provided name,
        traversing nested scopes as needed.
        """
        sc = self.scope
        rd = self.readonly_scope
        name_scope = SC_UNKNOWN
        while name_scope == SC_UNKNOWN and sc is not None:
            name_scope = sc.check_name(name)
            if rd.parent is None:
                return SC_UNKNOWN
            rd = rd.parent
            sc = self.scopes[rd.node]
        return name_scope

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
        name_scope = self.scope_check_name(name)
        if name_scope in (SC_LOCAL, SC_CELL):
            is_readonly = self.get_name_readonly(name, is_local=True)
            if is_readonly is not None:
                self.store_node(node, is_readonly)
            else:
                if isinstance(
                    self.readonly_scope, ReadonlyModuleBindingScope
                ) or isinstance(self.readonly_scope, ReadonlyClassBindingScope):
                    # In class and module scope, allow fallback to builtins.
                    # Builtins are SC_GLOBAL_IMPLICIT in function scopes.
                    # Builtins are SC_FREE in nested scopes within functions.
                    if name in __builtins__:
                        self.store_mutable(node)
                        return

                # unknown local name, treat as mutable.
                # When this happens the source code should lead to a NameError or UnboundLocalError
                self.readonly_type_error(
                    f"Unknown readonlyness of local or cell var '{to_expr(node)}'",
                    node,
                )
                self.store_mutable(node)
        elif name_scope in (SC_GLOBAL_IMPLICIT, SC_GLOBAL_EXPLICIT):
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
                    # In nested scopes, builtins and other globals are treated as SC_FREE,
                    # so handle them properly if we don't know about them.
                    if name in __builtins__ or self.readonly_scope.is_nested:
                        self.store_mutable(node)
                        return
                    self.readonly_type_error(
                        f"Unknown readonlyness of free var '{to_expr(node)}'",
                        node,
                    )
                    self.store_mutable(node)
        else:
            self.readonly_type_error(
                f"Unknown scope of var '{to_expr(node)}'",
                node,
            )
            self.store_mutable(node)

    def visitCall(self, node: ast.Call, as_readonly: Optional[Value] = None) -> None:
        if is_readonly_wrapped(node):
            if len(node.args) != 1 or len(node.keywords) > 0:
                self.readonly_type_error(
                    "readonly() only accepts 1 positional arg", node
                )
            self.visit(node.args[0], READONLY)
            self.store_readonly(node)
        elif is_tuple_wrapped(node):
            self.store_node(
                node, READONLY if self.walk_is_any_readonly(node.args) else MUTABLE
            )
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

    def visitYield(self, node: ast.Yield, as_readonly: Optional[Value] = None) -> None:
        parent = self.current_readonly_function()

        if not parent:
            self.readonly_type_error(
                f"Unable to determine parent function for yield expression", node
            )
            return

        v = node.value
        if v:
            self.visit(v)
            if not parent.yields_readonly() and self.is_readonly(v):
                self.readonly_type_error(
                    f"Cannot yield readonly expression '{to_expr(v)}' from a function yielding a mutable type",
                    node,
                )

        self.store_node(node, READONLY if parent.sends_readonly() else MUTABLE)

    def visitLambda(
        self, node: ast.Lambda, as_readonly: Optional[Value] = None
    ) -> None:
        self.store_node(node, as_readonly or MUTABLE)
        if self.readonly_scope.is_previsit:
            self.readonly_scope.store_post_visit(node)
            return
        self.visit(node.args)
        self.visit(node.body)

    def visitAwait(self, node: ast.Await, as_readonly: Optional[Value] = None) -> None:
        self.visit(node.value)
        self.store_node(node, as_readonly or self.bind_types[node.value])

    def visitSubscript(
        self, node: ast.Subscript, as_readonly: Optional[Value] = None
    ) -> None:
        self.visit(node.value)
        self.visit(node.slice)
        self.store_node(node, as_readonly or self.bind_types[node.value])

    def visitList(self, node: ast.List, as_readonly: Optional[Value] = None) -> None:
        is_readonly = READONLY if self.walk_is_any_readonly(node.elts) else MUTABLE
        self.store_node(node, as_readonly or is_readonly)

    def visitSet(self, node: ast.Set, as_readonly: Optional[Value] = None) -> None:
        is_readonly = READONLY if self.walk_is_any_readonly(node.elts) else MUTABLE
        self.store_node(node, as_readonly or is_readonly)

    def visitTuple(self, node: ast.Tuple, as_readonly: Optional[Value] = None) -> None:
        is_readonly = READONLY if self.walk_is_any_readonly(node.elts) else MUTABLE
        self.store_node(node, as_readonly or is_readonly)

    def visitDict(self, node: ast.Dict, as_readonly: Optional[Value] = None) -> None:
        is_key_readonly = False
        for k in node.keys:
            if k is not None:
                self.visit(k)
                if self.is_readonly(k):
                    is_key_readonly = True
        is_value_readonly = self.walk_is_any_readonly(node.values)
        is_readonly = READONLY if (is_key_readonly or is_value_readonly) else MUTABLE
        self.store_node(node, as_readonly or is_readonly)

    def visitAttribute(
        self, node: ast.Attribute, as_readonly: Optional[Value] = None
    ) -> None:
        self.visit(node.value)
        self.store_node(node, as_readonly or self.bind_types[node.value])

    def visitStarred(
        self, node: ast.Starred, as_readonly: Optional[Value] = None
    ) -> None:
        self.visit(node.value)
        self.store_node(node, as_readonly or self.bind_types[node.value])

    def visitListComp(
        self, node: ast.ListComp, as_readonly: Optional[Value] = None
    ) -> None:
        self.child_scope(ReadonlyComprehensionBindingScope(node, self))
        self.walk_list(node.generators)
        self.visit(node.elt)
        self.store_node(node, as_readonly or self.bind_types[node.elt])
        self.restore_scope()

    def visitDictComp(
        self, node: ast.DictComp, as_readonly: Optional[Value] = None
    ) -> None:
        self.child_scope(ReadonlyComprehensionBindingScope(node, self))
        self.walk_list(node.generators)
        self.visit(node.key)
        self.visit(node.value)
        if as_readonly:
            self.store_node(node, as_readonly)
        elif (
            self.bind_types[node.key].is_readonly
            or self.bind_types[node.value].is_readonly
        ):
            self.store_readonly(node)
        else:
            self.store_mutable(node)
        self.restore_scope()

    def visitSetComp(
        self, node: ast.SetComp, as_readonly: Optional[Value] = None
    ) -> None:
        self.child_scope(ReadonlyComprehensionBindingScope(node, self))
        self.walk_list(node.generators)
        self.visit(node.elt)
        self.store_node(node, as_readonly or self.bind_types[node.elt])
        self.restore_scope()

    def visitGeneratorExp(
        self, node: ast.GeneratorExp, as_readonly: Optional[Value] = None
    ) -> None:
        self.child_scope(ReadonlyComprehensionBindingScope(node, self))
        self.walk_list(node.generators)
        self.visit(node.elt)
        self.store_node(node, as_readonly or self.bind_types[node.elt])
        self.restore_scope()

    def visitcomprehension(
        self, node: ast.comprehension, as_readonly: Optional[Value] = None
    ) -> None:
        self.visit(node.iter)
        # Although elements of the iterable will be what's actually assigned,
        # the readonly-ness of the iterable itself is what determines the
        # readonly-ness of the resulting value.
        # We also currently ignore the type_comment on the node.
        self.assign_to(node.target, node.iter, node)
        self.store_node(node, as_readonly or self.bind_types[node.target])
        self.walk_list(node.ifs)

    def visitNamedExpr(
        self, node: ast.NamedExpr, as_readonly: Optional[Value] = None
    ) -> None:
        self.visit(node.value)
        self.assign_to(node.target, node.value, node)
        self.store_node(node, as_readonly or self.bind_types[node.target])

    def visitBinOp(self, node: ast.BinOp, as_readonly: Optional[Value] = None) -> None:
        self.visit(node.left)
        self.visit(node.right)
        if self.is_readonly(node.left) or self.is_readonly(node.right):
            self.store_readonly(node)
        else:
            self.store_node(node, as_readonly or MUTABLE)

    def visitBoolOp(
        self, node: ast.BoolOp, as_readonly: Optional[Value] = None
    ) -> None:
        self.walk_list(node.values)
        self.store_node(node, as_readonly or MUTABLE)

    def visitCompare(
        self, node: ast.Compare, as_readonly: Optional[Value] = None
    ) -> None:
        self.walk_list(node.comparators)
        self.visit(node.left)
        self.store_node(node, as_readonly or MUTABLE)

    def visitUnaryOp(
        self, node: ast.UnaryOp, as_readonly: Optional[Value] = None
    ) -> None:
        self.visit(node.operand)
        self.store_node(node, as_readonly or MUTABLE)

    def visitIfExp(self, node: ast.IfExp, as_readonly: Optional[Value] = None) -> None:
        self.visit(node.test)
        self.visit(node.body)
        self.visit(node.orelse)
        if self.is_readonly(node.body) or self.is_readonly(node.orelse):
            self.store_readonly(node)
        else:
            self.store_node(node, as_readonly or MUTABLE)

    # ------------------------- Statements --------------------------------

    def assign_to(
        self,
        lhs: AST,
        rhs: AST,
        node: AST,
        override_rhs_readonly: Optional[Value] = None,
    ) -> None:
        """
        Checks whether rhs can be assigned to lhs without
        breaking readonly constraints
        """
        if override_rhs_readonly:
            rhs_readonly = override_rhs_readonly
        else:
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
            name_readonly = self.get_name_readonly(
                lname, is_local=name_scope == SC_LOCAL or name_scope == SC_CELL
            )
            if name_readonly and rhs_readonly.can_convert_to(name_readonly):
                # it's okay to narrow readonliness
                self.store_node(lhs, name_readonly)
                pass
            else:
                err_msg = (
                    f"cannot assign {rhs_readonly} value to {name_readonly} '{lname}'. "
                    + "Remember to declare name as readonly explicitly"
                )
                self.declare(lname, lhs, rhs_readonly, custom_err_msg=err_msg)
                self.store_node(lhs, rhs_readonly)
        elif isinstance(lhs, ast.Subscript):
            self.visit(lhs.value)
            if self.is_readonly(lhs.value):
                self.readonly_type_error(
                    f"Cannot modify readonly expression '{to_expr(lhs.value)}' via subscript",
                    node,
                )
            elif rhs_readonly.is_readonly:
                self.readonly_type_error(
                    f"Cannot store readonly expression '{to_expr(lhs.value)}' in a mutable container",
                    node,
                )
            self.store_node(lhs, MUTABLE)
        elif isinstance(lhs, ast.Attribute):
            self.visit(lhs.value)

            # TODO: Proactively check if property is readonly on self
            if self.is_readonly(lhs.value):
                self.readonly_type_error(
                    f"Cannot set attributes on readonly expression '{to_expr(lhs.value)}'",
                    node,
                )
            self.store_node(lhs, MUTABLE)
        elif isinstance(lhs, ast.Tuple | ast.List):
            if isinstance(rhs, ast.Tuple | ast.List) and len(lhs.elts) == len(rhs.elts):
                # Allow destructuring assignments where we can know the
                # readonly-ness of each value.
                for lhs_child, rhs_child in zip(lhs.elts, rhs.elts):
                    self.assign_to(lhs_child, rhs_child, node)
            else:
                # taking elements of a readonly/mutable collection
                # results in readonly/mutable respectively, so here
                # we just assign rhs to each element on the lhs
                for lhs_child in lhs.elts:
                    self.assign_to(lhs_child, rhs, node)
            self.store_node(lhs, self.bind_types[rhs])
        elif isinstance(lhs, ast.Starred):
            self.assign_to(lhs.value, rhs, node)
            self.store_node(lhs, self.bind_types[lhs.value])
        else:
            self.readonly_type_error(f"Unknown assignment expression {type(lhs)}", lhs)

    def visitAnnAssign(self, node: ast.AnnAssign) -> None:
        is_readonly_ann = self.is_readonly_annotation(node.annotation)
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
        self.assign_to(node.target, node.value, node)
        self.visit(node.target)
        if self.is_readonly(node.target):
            self.readonly_type_error(
                f"Cannot modify readonly reference '{to_expr(node.target)}' via aug assign",
                node,
            )

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

            parent = self.current_readonly_function()
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

    def visitYieldFrom(self, node: ast.YieldFrom) -> None:
        parent = self.current_readonly_function()

        if not parent:
            self.readonly_type_error(
                f"Unable to determine parent function for yield from expression", node
            )
            return

        v = node.value
        if not v:
            return

        self.visit(v)
        if (
            parent.yields_readonly()
            or parent.sends_readonly()
            or parent.returns_readonly()
        ):
            self.readonly_type_error(
                f"Cannot use yield from in a function that yields, sends, or returns readonly. Refactor in terms of a normal yield",
                node,
            )
        if self.is_readonly(v):
            self.readonly_type_error(
                f"Cannot use yield from on a readonly expression. Rewrite in terms of a normal yield",
                node,
            )

    def _visitWith(self, node: ast.With | ast.AsyncWith) -> None:
        for it in node.items:
            self.visit(it.context_expr)
            readonlyness = MUTABLE
            if is_readonly_wrapped(it.context_expr):
                readonlyness = READONLY

            if it.optional_vars:
                self.assign_to(
                    it.optional_vars,
                    it.context_expr,
                    node,
                    override_rhs_readonly=readonlyness,
                )
        self.walk_list(node.body)

    def visitWith(self, node: ast.With) -> None:
        self._visitWith(node)

    def visitAsyncWith(self, node: ast.AsyncWith) -> None:
        self._visitWith(node)

    def visitExceptHandler(self, node: ast.ExceptHandler) -> None:
        # readonly values can't be raised, so they also can't be caught.
        # All that's needed is to declare the name as mutable.
        if node.name:
            self.declare(node.name, node, MUTABLE)
        if node.type:
            self.visit(node.type)
        self.walk_list(node.body)

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
        readonly_nonlocal = any(
            is_readonly_func_nonlocal(dec) for dec in node.decorator_list
        )
        node.decorator_list = [
            x
            for x in node.decorator_list
            if not isinstance(x, Name) or x.id not in READONLY_DECORATORS
        ]
        returns_readonly = False
        yields_readonly = False
        sends_readonly = False
        return_sig = node.returns
        if return_sig is not None:
            returns_readonly = self.is_readonly_annotation(return_sig)
            gen = self.extract_generator_annotations_readonly(return_sig)
            if gen is not None:
                yields_readonly = gen[0]
                sends_readonly = gen[1]
                returns_readonly = gen[2]

        func_scope = ReadonlyFunctionBindingScope(
            node,
            self,
            returns_readonly=returns_readonly,
            yields_readonly=yields_readonly,
            sends_readonly=sends_readonly,
            readonly_nonlocal=readonly_func,
            pre_visit=True,
        )
        self.child_scope(func_scope)
        # Declare the function itself in the local scope.
        self.declare(name, node, MUTABLE)
        # put parameters in scope
        self.visit(node.args)
        self.walk_list(node.body)
        func_scope.finish_previsit()
        self.walk_list(func_scope.post_visit)
        self.restore_scope()
        self.walk_list(node.decorator_list)

        if not readonly_func:
            return

        # save readonly-ness of each arguments
        arg_values = []
        for arg in node.args.args:
            if func_scope.is_name_readonly(name):
                arg_values.append(READONLY)
            else:
                arg_values.append(MUTABLE)
        func_value = FunctionValue(returns_readonly, readonly_nonlocal, arg_values)
        self.store_readonly_func(node, func_value)

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
            is_readonly = self.is_readonly_annotation(annotation)
            readonly_value = READONLY if is_readonly else MUTABLE
        else:
            readonly_value = MUTABLE
        self.declare(node.arg, node, readonly_value)

    def visitalias(self, node: ast.alias) -> None:
        if node.asname:
            self.declare(node.asname, node, MUTABLE)
        else:
            self.declare(node.name, node, MUTABLE)

    def visitModule(self, node: ast.Module) -> None:
        self.child_scope(ReadonlyModuleBindingScope(node, self))
        self.walk_list(node.body)
        self.restore_scope()

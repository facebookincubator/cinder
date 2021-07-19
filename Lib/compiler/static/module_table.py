# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
from __future__ import annotations

import ast
from ast import AST, ClassDef, Subscript, Index, Name, NameConstant
from functools import partial
from typing import (
    Callable as typingCallable,
    Dict,
    List,
    Optional,
    Set,
    TYPE_CHECKING,
    Tuple,
    Union,
)

from ..pycodegen import Delegator
from ..symbols import Scope, ModuleScope
from .errors import TypedSyntaxError
from .types import (
    CType,
    Class,
    ClassVar,
    DYNAMIC_TYPE,
    FLOAT_TYPE,
    FinalClass,
    INT_TYPE,
    NONE_TYPE,
    OPTIONAL_TYPE,
    UNION_TYPE,
    UnionType,
    Value,
)

if TYPE_CHECKING:
    from . import SymbolTable


class ModuleTable:
    def __init__(
        self,
        name: str,
        filename: str,
        symtable: SymbolTable,
        members: Optional[Dict[str, Value]] = None,
    ) -> None:
        self.name = name
        self.filename = filename
        self.children: Dict[str, Value] = members or {}
        self.symtable = symtable
        self.types: Dict[Union[AST, Delegator], Value] = {}
        self.node_data: Dict[Tuple[Union[AST, Delegator], object], object] = {}
        self.nonchecked_dicts = False
        self.noframe = False
        self.decls: List[Tuple[AST, Optional[Value]]] = []
        # TODO: final constants should be typed to literals, and
        # this should be removed in the future
        self.named_finals: Dict[str, ast.Constant] = {}
        # Functions in this module that have been decorated with
        # `dynamic_return`. We actually store their `.args` node in here, not
        # the `FunctionDef` node itself, since strict modules rewriter will
        # replace the latter in between decls visit and type binding / codegen.
        self.dynamic_returns: Set[ast.AST] = set()
        # Have we completed our first pass through the module, populating
        # imports and types defined in the module? Until we have, resolving
        # type annotations is not safe.
        self.first_pass_done = False

    def declare_class(self, node: ClassDef, klass: Class) -> None:
        self.decls.append((node, klass))
        self.children[node.name] = klass

    def finish_bind(self) -> None:
        self.first_pass_done = True
        for node, value in self.decls:
            with self.symtable.error_sink.error_context(self.filename, node):
                if value is not None:
                    value.finish_bind(self)
                elif isinstance(node, ast.AnnAssign):
                    typ = self.resolve_annotation(node.annotation, is_declaration=True)
                    if typ is not None:
                        target = node.target
                        if isinstance(target, ast.Name):
                            self.children[target.id] = typ.instance
                    if isinstance(typ, FinalClass):
                        target = node.target
                        value = node.value
                        if not value:
                            raise TypedSyntaxError(
                                "Must assign a value when declaring a Final"
                            )
                        elif (
                            not isinstance(typ, CType)
                            and isinstance(target, ast.Name)
                            and isinstance(value, ast.Constant)
                        ):
                            self.named_finals[target.id] = value

        # We don't need these anymore...
        self.decls.clear()

    def resolve_type(self, node: ast.AST) -> Optional[Class]:
        # TODO handle Call
        return self._resolve(node, self.resolve_type)

    def _resolve(
        self,
        node: ast.AST,
        _resolve: typingCallable[[ast.AST], Optional[Class]],
        _resolve_subscr_target: Optional[
            typingCallable[[ast.AST], Optional[Class]]
        ] = None,
    ) -> Optional[Class]:
        if isinstance(node, ast.Name):
            res = self.resolve_name(node.id)
            if isinstance(res, Class):
                return res
        elif isinstance(node, Subscript):
            slice = node.slice
            if isinstance(slice, Index):
                val = (_resolve_subscr_target or _resolve)(node.value)
                if val is not None:
                    value = slice.value
                    if isinstance(value, ast.Tuple):
                        anns = []
                        for elt in value.elts:
                            ann = _resolve(elt) or DYNAMIC_TYPE
                            anns.append(ann)
                        values = tuple(anns)
                        gen = val.make_generic_type(values, self.symtable.generic_types)
                        return gen or val
                    else:
                        index = _resolve(value) or DYNAMIC_TYPE
                        gen = val.make_generic_type(
                            (index,), self.symtable.generic_types
                        )
                        return gen or val
        # TODO handle Attribute

    def resolve_annotation(
        self,
        node: ast.AST,
        *,
        is_declaration: bool = False,
    ) -> Optional[Class]:
        assert self.first_pass_done, (
            "Type annotations cannot be resolved until after initial pass, "
            "so that all imports and types are available."
        )

        with self.symtable.error_sink.error_context(self.filename, node):
            klass = self._resolve_annotation(node)

            if not is_declaration:
                if isinstance(klass, FinalClass):
                    raise TypedSyntaxError(
                        "Final annotation is only valid in initial declaration "
                        "of attribute or module-level constant",
                    )
                if isinstance(klass, ClassVar):
                    raise TypedSyntaxError(
                        "ClassVar is allowed only in class attribute annotations."
                    )

            # Even if we know that e.g. `builtins.str` is the exact `str` type and
            # not a subclass, and it's useful to track that knowledge, when we
            # annotate `x: str` that annotation should not exclude subclasses.
            if klass:
                klass = klass.inexact_type()
                # PEP-484 specifies that ints should be treated as a subclass of floats,
                # even though they differ in the runtime. We need to maintain the distinction
                # between the two internally, so we should view user-specified `float` annotations
                # as `float | int`. This widening of the type prevents us from applying
                # optimizations # to user-specified floats, but does not affect ints. Since we
                # don't optimize Python floats anyway, we accept this to maintain PEP-484 compatibility.

                if klass is FLOAT_TYPE:
                    klass = UNION_TYPE.make_generic_type(
                        (FLOAT_TYPE, INT_TYPE), self.symtable.generic_types
                    )

            # TODO until we support runtime checking of unions, we must for
            # safety resolve union annotations to dynamic (except for
            # optionals, which we can check at runtime)
            if (
                isinstance(klass, UnionType)
                and klass is not UNION_TYPE
                and klass is not OPTIONAL_TYPE
                and klass.opt_type is None
            ):
                return None

            return klass

    def _resolve_annotation(self, node: ast.AST) -> Optional[Class]:
        # First try to resolve non-annotation-specific forms. For resolving the
        # outer target of a subscript (e.g. `Final` in `Final[int]`) we pass
        # `is_declaration=True` to allow `Final` in that position; if in fact
        # we are not resolving a declaration, the outer `resolve_annotation`
        # (our caller) will still catch the generic Final that we end up
        # returning.
        typ = self._resolve(
            node,
            self.resolve_annotation,
            _resolve_subscr_target=partial(
                self.resolve_annotation, is_declaration=True
            ),
        )
        if typ:
            return typ
        elif isinstance(node, ast.Str):
            # pyre-ignore[16]: `AST` has no attribute `body`.
            return self.resolve_annotation(ast.parse(node.s, "", "eval").body)
        elif isinstance(node, ast.Constant):
            sval = node.value
            if sval is None:
                return NONE_TYPE
            elif isinstance(sval, str):
                return self.resolve_annotation(ast.parse(node.value, "", "eval").body)
        elif isinstance(node, NameConstant) and node.value is None:
            return NONE_TYPE
        elif isinstance(node, ast.BinOp) and isinstance(node.op, ast.BitOr):
            ltype = self.resolve_annotation(node.left)
            rtype = self.resolve_annotation(node.right)
            if ltype is None or rtype is None:
                return None
            return UNION_TYPE.make_generic_type(
                (ltype, rtype), self.symtable.generic_types
            )

    def resolve_name(self, name: str) -> Optional[Value]:
        return self.children.get(name) or self.symtable.builtins.children.get(name)

    def get_final_literal(self, node: AST, scope: Scope) -> Optional[ast.Constant]:
        if not isinstance(node, Name):
            return None

        final_val = self.named_finals.get(node.id, None)
        if (
            final_val is not None
            and isinstance(node.ctx, ast.Load)
            and (
                # Ensure the name is not shadowed in the local scope
                isinstance(scope, ModuleScope)
                or node.id not in scope.defs
            )
        ):
            return final_val

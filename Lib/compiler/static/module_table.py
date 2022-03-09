# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
from __future__ import annotations

import ast
from ast import (
    AST,
    Attribute,
    BinOp,
    Call,
    ClassDef,
    Constant,
    Expression,
    Subscript,
    Name,
)
from contextlib import nullcontext
from enum import Enum
from typing import (
    cast,
    ContextManager,
    Dict,
    List,
    Optional,
    Set,
    TYPE_CHECKING,
    Tuple,
)

from ..errors import TypedSyntaxError
from ..symbols import Scope, ModuleScope
from .types import (
    Callable,
    CType,
    Callable,
    Class,
    ClassVar,
    DynamicClass,
    ExactClass,
    Function,
    FunctionGroup,
    FinalClass,
    MethodType,
    TypeDescr,
    UnionType,
    Value,
)
from .visitor import GenericVisitor

if TYPE_CHECKING:
    from .compiler import Compiler


class ModuleFlag(Enum):
    CHECKED_DICTS = 1
    SHADOW_FRAME = 2
    CHECKED_LISTS = 3


class ReferenceVisitor(GenericVisitor[Optional[Value]]):
    def __init__(self, module: ModuleTable) -> None:
        super().__init__(module)
        self.types: Dict[AST, Value] = {}
        self.subscr_nesting = 0

    def visitName(self, node: Name) -> Optional[Value]:
        return self.module.children.get(
            node.id
        ) or self.module.compiler.builtins.children.get(node.id)

    def visitAttribute(self, node: Attribute) -> Optional[Value]:
        val = self.visit(node.value)
        if val is not None:
            return val.resolve_attr(node, self)


class AnnotationVisitor(ReferenceVisitor):
    def resolve_annotation(
        self,
        node: ast.AST,
        *,
        is_declaration: bool = False,
    ) -> Optional[Class]:
        with self.error_context(node):
            klass = self.visit(node)
            if not isinstance(klass, Class):
                return None

            if self.subscr_nesting or not is_declaration:
                if isinstance(klass, FinalClass):
                    raise TypedSyntaxError(
                        "Final annotation is only valid in initial declaration "
                        "of attribute or module-level constant",
                    )
                if isinstance(klass, ClassVar):
                    raise TypedSyntaxError(
                        "ClassVar is allowed only in class attribute annotations. "
                        "Class Finals are inferred ClassVar; do not nest with Final."
                    )

            if isinstance(klass, ExactClass):
                klass = klass.unwrap().exact_type()
            elif isinstance(klass, FinalClass):
                pass
            else:
                klass = klass.inexact_type()
            # PEP-484 specifies that ints should be treated as a subclass of floats,
            # even though they differ in the runtime. We need to maintain the distinction
            # between the two internally, so we should view user-specified `float` annotations
            # as `float | int`. This widening of the type prevents us from applying
            # optimizations to user-specified floats, but does not affect ints. Since we
            # don't optimize Python floats anyway, we accept this to maintain PEP-484 compatibility.

            if klass.unwrap() is self.type_env.float:
                klass = self.compiler.type_env.get_union(
                    (self.type_env.float, self.type_env.int)
                )

            # TODO until we support runtime checking of unions, we must for
            # safety resolve union annotations to dynamic (except for
            # optionals, which we can check at runtime)
            if (
                isinstance(klass, UnionType)
                and klass is not self.type_env.union
                and klass is not self.type_env.optional
                and klass.opt_type is None
            ):
                return None

            return klass

    def visitSubscript(self, node: Subscript) -> Optional[Value]:
        target = self.resolve_annotation(node.value, is_declaration=True)
        if target is None:
            return None

        self.subscr_nesting += 1
        slice = self.visit(node.slice) or self.type_env.DYNAMIC
        self.subscr_nesting -= 1
        return target.resolve_subscr(node, slice, self) or target

    def visitBinOp(self, node: BinOp) -> Optional[Value]:
        if isinstance(node.op, ast.BitOr):
            ltype = self.resolve_annotation(node.left)
            rtype = self.resolve_annotation(node.right)
            if ltype is None or rtype is None:
                return None
            return self.module.compiler.type_env.get_union((ltype, rtype))

    def visitConstant(self, node: Constant) -> Optional[Value]:
        sval = node.value
        if sval is None:
            return self.type_env.none
        elif isinstance(sval, str):
            n = cast(Expression, ast.parse(node.value, "", "eval")).body
            return self.visit(n)


class ModuleTable:
    def __init__(
        self,
        name: str,
        filename: str,
        compiler: Compiler,
        members: Optional[Dict[str, Value]] = None,
    ) -> None:
        self.name = name
        self.filename = filename
        self.children: Dict[str, Value] = members or {}
        self.compiler = compiler
        self.types: Dict[AST, Value] = {}
        self.node_data: Dict[Tuple[AST, object], object] = {}
        self.flags: Set[ModuleFlag] = set()
        self.decls: List[Tuple[AST, Optional[str], Optional[Value]]] = []
        self.compile_non_static: Set[AST] = set()
        # (source module, source name) for every name this module imports-from
        # another static module at top level
        self.imported_from: Dict[str, Tuple[str, str]] = {}
        # TODO: final constants should be typed to literals, and
        # this should be removed in the future
        self.named_finals: Dict[str, ast.Constant] = {}
        # Have we completed our first pass through the module, populating
        # imports and types defined in the module? Until we have, resolving
        # type annotations is not safe.
        self.first_pass_done = False
        self.ann_visitor = AnnotationVisitor(self)
        self.ref_visitor = ReferenceVisitor(self)

    def syntax_error(self, msg: str, node: AST) -> None:
        return self.compiler.error_sink.syntax_error(msg, self.filename, node)

    def error_context(self, node: Optional[AST]) -> ContextManager[None]:
        if node is None:
            return nullcontext()
        return self.compiler.error_sink.error_context(self.filename, node)

    def declare_class(self, node: ClassDef, klass: Class) -> None:
        self.decls.append((node, node.name, klass))
        self.children[node.name] = klass

    def declare_function(self, func: Function) -> None:
        existing = self.children.get(func.func_name)
        new_member = func
        if existing is not None:
            if isinstance(existing, Function):
                new_member = FunctionGroup([existing, new_member], func.klass.type_env)
            elif isinstance(existing, FunctionGroup):
                existing.functions.append(new_member)
                new_member = existing
            else:
                raise TypedSyntaxError(
                    f"function conflicts with other member {func.func_name} in {self.name}"
                )

        self.decls.append((func.node, func.func_name, new_member))
        self.children[func.func_name] = new_member

    def _get_inferred_type(self, value: ast.expr) -> Optional[Value]:
        if not isinstance(value, ast.Name):
            return None
        return self.children.get(value.id)

    def finish_bind(self) -> None:
        self.first_pass_done = True
        for node, name, value in self.decls:
            with self.error_context(node):
                if value is not None:
                    assert name is not None
                    new_value = value.finish_bind(self, None)
                    if new_value is None:
                        del self.children[name]
                    elif new_value is not value:
                        self.children[name] = new_value

                if isinstance(node, ast.AnnAssign):
                    typ = self.resolve_annotation(node.annotation, is_declaration=True)
                    if typ is not None:
                        # Special case Final[dynamic] to use inferred type.
                        target = node.target
                        instance = typ.instance
                        value = node.value
                        if (
                            value is not None
                            and isinstance(typ, FinalClass)
                            and isinstance(typ.unwrap(), DynamicClass)
                        ):
                            instance = self._get_inferred_type(value) or instance

                        if isinstance(target, ast.Name):
                            self.children[target.id] = instance

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
        typ = self.ann_visitor.visit(node)
        if isinstance(typ, Class):
            return typ

    def resolve_decorator(self, node: ast.AST) -> Optional[Value]:
        if isinstance(node, Call):
            func = self.ref_visitor.visit(node.func)
            if isinstance(func, Class):
                return func.instance
            elif isinstance(func, Callable):
                return func.return_type.resolved().instance
            elif isinstance(func, MethodType):
                return func.function.return_type.resolved().instance

        return self.ref_visitor.visit(node)

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
        return self.ann_visitor.resolve_annotation(node, is_declaration=is_declaration)

    def resolve_name_with_descr(
        self, name: str
    ) -> Tuple[Optional[Value], Optional[TypeDescr]]:
        if val := self.children.get(name):
            return val, (self.name, name)
        elif val := self.compiler.builtins.children.get(name):
            return val, None
        return None, None

    def resolve_name(self, name: str) -> Optional[Value]:
        return self.resolve_name_with_descr(name)[0]

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

    def declare_import(
        self, name: str, source: Tuple[str, str] | None, val: Value
    ) -> None:
        """Declare a name imported into this module.

        `name` is the name in this module's namespace. `source` is a (str, str)
        tuple of (source_module, source_name) for an `import from`. For a
        top-level module import, `source` should be `None`.

        """
        self.children[name] = val
        if source is not None:
            self.imported_from[name] = source

    def declare_variable(self, node: ast.AnnAssign, module: ModuleTable) -> None:
        self.decls.append((node, None, None))

    def declare_variables(self, node: ast.Assign, module: ModuleTable) -> None:
        pass

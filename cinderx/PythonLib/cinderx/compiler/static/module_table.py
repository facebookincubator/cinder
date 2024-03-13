# Copyright (c) Meta Platforms, Inc. and affiliates.

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
    Name,
    Subscript,
)
from contextlib import nullcontext
from enum import Enum
from typing import (
    cast,
    ContextManager,
    Dict,
    List,
    Optional,
    overload,
    Set,
    Tuple,
    Type,
    TYPE_CHECKING,
    Union,
)

from ..errors import TypedSyntaxError
from ..symbols import ModuleScope, Scope
from .types import (
    Callable,
    Class,
    ClassVar,
    CType,
    DataclassDecorator,
    DynamicClass,
    ExactClass,
    FinalClass,
    Function,
    FunctionGroup,
    InitVar,
    KnownBoolean,
    MethodType,
    ModuleInstance,
    NativeDecorator,
    TType,
    TypeDescr,
    UnionType,
    UnknownDecoratedMethod,
    Value,
)
from .visitor import GenericVisitor

if TYPE_CHECKING:
    from .compiler import Compiler


class ModuleFlag(Enum):
    CHECKED_DICTS = 1
    CHECKED_LISTS = 3


class ModuleTableException(Exception):
    pass


class ReferenceVisitor(GenericVisitor[Optional[Value]]):
    def __init__(self, module: ModuleTable) -> None:
        super().__init__(module)
        self.subscr_nesting = 0
        self.local_names: Dict[str, Value] = {}

    def visitName(self, node: Name) -> Optional[Value]:
        if node.id in self.local_names:
            return self.local_names[node.id]
        return self.module.get_child(
            node.id, self.context_qualname, force_decl=self.force_decl_deps
        ) or self.module.compiler.builtins.get_child_intrinsic(node.id)

    def visitAttribute(self, node: Attribute) -> Optional[Value]:
        val = self.visit(node.value)
        if val is not None:
            return val.resolve_attr(node, self)

    def add_local_name(self, name: str, value: Value) -> None:
        self.local_names[name] = value

    def clear_local_names(self) -> None:
        self.local_names = {}


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
                if isinstance(klass, InitVar):
                    raise TypedSyntaxError(
                        "InitVar is allowed only in class attribute annotations."
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
            # pyre-fixme[22]: The cast is redundant.
            n = cast(Expression, ast.parse(node.value, "", "eval")).body
            return self.visit(n)


class DepTrackingOptOut:
    """A reason for opting out of module dependency tracking.

    This helps ensure our dependency tracking is complete; we can't request type
    information from a module via ModuleTable.get_child(...) without either
    providing a `requester` (to track the dependency) or providing an opt-out
    reason.
    """

    def __init__(self, reason: str) -> None:
        self.reason = reason

INTRINSIC_OPT_OUT = DepTrackingOptOut(
    "We don't need to track dependencies to intrinsic modules; "
    "they won't change during development and require pyc updates. "
    "If they change, we should bump the static pyc magic number."
)
DEFERRED_IMPORT_OPT_OUT = DepTrackingOptOut("Tracked in ModuleTable.declare_import()")


class DeferredImport:
    def __init__(
        self,
        mod_to_import: str,
        optimize: int,
        compiler: Compiler,
        name: str | None = None,
        mod_to_return: str | None = None,
    ) -> None:
        self.mod_to_import = mod_to_import
        self.name = name
        self.mod_to_return = mod_to_return
        self.compiler = compiler
        self.optimize = optimize

    def import_mod(self, mod_name: str) -> ModuleTable | None:
        return self.compiler.import_module(mod_name, optimize=self.optimize)

    def resolve(self) -> Value:
        if self.mod_to_import not in self.compiler.modules:
            self.import_mod(self.mod_to_import)
        mod = self.compiler.modules.get(self.mod_to_import)
        if mod is not None:
            if self.name is None:
                return ModuleInstance(
                    self.mod_to_return or self.mod_to_import, self.compiler
                )
            val = mod.get_child(
                self.name, DEFERRED_IMPORT_OPT_OUT
            )
            if val is not None:
                return val
            try_mod = f"{self.mod_to_import}.{self.name}"
            self.import_mod(try_mod)
            if try_mod in self.compiler.modules:
                return ModuleInstance(try_mod, self.compiler)
            if not mod.first_pass_done:
                raise ModuleTableException(
                    f"Cannot find {self.mod_to_import}.{self.name} due to cyclic reference"
                )
        return self.compiler.type_env.DYNAMIC


class UnknownModule:
    def __init__(self, name: str) -> None:
        self.name = name


class ModuleTable:
    def __init__(
        self,
        name: str,
        filename: str,
        compiler: Compiler,
        members: Optional[Dict[str, Value]] = None,
        first_pass_done: bool = True,
    ) -> None:
        self.name = name
        self.filename = filename
        self._children: Dict[str, Value | DeferredImport] = {}
        if members is not None:
            self._children.update(members)
        self.compiler = compiler
        self.types: Dict[AST, Value] = {}
        self.node_data: Dict[Tuple[AST, object], object] = {}
        self.flags: Set[ModuleFlag] = set()
        self.decls: List[Tuple[AST, Optional[str], Optional[Value]]] = []
        self.implicit_decl_names: Set[str] = set()
        self.compile_non_static: Set[AST] = set()
        # {local-name: {(mod, qualname)}} for decl-time deps
        self.decl_deps: Dict[str, set[Tuple[str, str]]] = {}
        # {local-name: {(mod, qualname)}} for bind-time deps
        self.bind_deps: Dict[str, set[Tuple[str, str]]] = {}
        # (source module, source name) for every name this module imports-from
        # another static module at top level
        self.imported_from: Dict[str, Tuple[str, str]] = {}
        # TODO: final constants should be typed to literals, and
        # this should be removed in the future
        self.named_finals: Dict[str, ast.Constant] = {}
        # Have we completed our first pass through the module, populating
        # imports and types defined in the module? Until we have, resolving
        # type annotations is not safe.
        self.first_pass_done = first_pass_done
        self.finish_bind_done = False
        self.ann_visitor = AnnotationVisitor(self)
        self.ref_visitor = ReferenceVisitor(self)
        # the prefix children will get on their qualname; always None for
        # modules (the qualname of a class or function is within-module, it
        # doesn't include the module name)
        self.qualname: None = None

    def __repr__(self) -> str:
        return f"<ModuleTable {self.name}>"

    def get_dependencies(self) -> set[ModuleTable | UnknownModule]:
        """Return all modules this module depends on."""
        # We only include bind deps for the current module; for transitive deps
        # we follow only decl_deps
        immediate_deps = {**self.bind_deps, **self.decl_deps}
        all_modules_deps = {
            name: mod.decl_deps for name, mod in self.compiler.modules.items()
        }
        all_modules_deps[self.name] = immediate_deps
        dep_names = find_transitive_deps(self.name, all_modules_deps)
        return {
            self.compiler.modules.get(name) or UnknownModule(name) for name in dep_names
        }

    def record_dependency(
        self, name: str, target: Tuple[str, str], force_decl: bool = False
    ) -> None:
        """Record a dependency from a local name to a name in another module.

        `name` is the name in this module's namespace. `target` is a (str, str)
        tuple of (source_module, source_name).

        """
        deps = (
            self.bind_deps
            if (self.finish_bind_done and not force_decl)
            else self.decl_deps
        )
        deps.setdefault(name, set()).add(target)

    def get_child(
        self, name: str, requester: str | DepTrackingOptOut, force_decl: bool = False
    ) -> Optional[Value]:
        if not isinstance(requester, DepTrackingOptOut):

            # Using imported_from here is just an optimization that lets us skip
            # one level of transitive-dependency-following later. If modA has
            # "from modB import B", we already record `modA.B -> modB.B`, so
            # technically if `modA.f -> modA.B`, we could just record that
            # dependency, and following the transitive closure would later give
            # us `modA.f -> modB.B`. But since we have the `imported_from` data
            # available, we use it to just record `modA.f -> modB.B` initially.
            # We don't need `modA.f -> modA.B` recorded as a dependency in that
            # case, since intra-module dependencies have no direct impact on
            # recompilation, they are only needed for following the transitive
            # chain across modules.
            source = self.imported_from.get(name, (self.name, name))
            self.record_dependency(requester, source, force_decl)
        res = self._children.get(name)
        if isinstance(res, DeferredImport):
            self._children[name] = res = res.resolve()
        return res

    def syntax_error(self, msg: str, node: AST) -> None:
        return self.compiler.error_sink.syntax_error(msg, self.filename, node)

    def error_context(self, node: Optional[AST]) -> ContextManager[None]:
        if node is None:
            return nullcontext()
        return self.compiler.error_sink.error_context(self.filename, node)

    def declare_class(self, node: ClassDef, klass: Class) -> None:
        if self.first_pass_done:
            raise ModuleTableException(
                "Attempted to declare a class after the declaration visit"
            )
        self.decls.append((node, node.name, klass))
        self._children[node.name] = klass

    def declare_function(self, func: Function) -> None:
        if self.first_pass_done:
            raise ModuleTableException(
                "Attempted to declare a function after the declaration visit"
            )
        existing = self._children.get(func.func_name)
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
        self._children[func.func_name] = new_member

    def _get_inferred_type(self, value: ast.expr, requester: str) -> Optional[Value]:
        if not isinstance(value, ast.Name):
            return None
        return self.get_child(value.id, requester)

    def finish_bind(self) -> None:
        self.first_pass_done = True
        for node, name, value in self.decls:
            with self.error_context(node):
                if value is not None:
                    assert name is not None
                    new_value = value.finish_bind(self, None)
                    if new_value is None:
                        # We still need to maintain a name for unknown decorated methods since they
                        # will bind to some name, and we want to avoid throwing unknown name errors.
                        if isinstance(self.types[node], UnknownDecoratedMethod):
                            self._children[name] = self.compiler.type_env.DYNAMIC
                        else:
                            del self._children[name]
                    elif new_value is not value:
                        self._children[name] = new_value

                if isinstance(node, ast.AnnAssign) and isinstance(
                    node.target, ast.Name
                ):
                    # isinstance(target := node.target, ast.Name) above would be
                    # nicer, but pyre doesn't narrow the type of target :/
                    target = node.target
                    assert isinstance(target, ast.Name)
                    typ = self.resolve_annotation(
                        node.annotation, target.id, is_declaration=True
                    )
                    if typ is not None:
                        # Special case Final[dynamic] to use inferred type.
                        instance = typ.instance
                        value = node.value
                        if isinstance(typ, FinalClass):
                            # We keep track of annotated finals in the
                            # named_finals field - we can safely remove that
                            # information here to ensure that the rest of the
                            # type system can safely ignore it.
                            unwrapped = typ.unwrap()
                            if value is not None and isinstance(
                                unwrapped, DynamicClass
                            ):
                                instance = (
                                    self._get_inferred_type(value, target.id)
                                    or unwrapped.instance
                                )
                            else:
                                instance = unwrapped.instance

                        self._children[target.id] = instance

                    if isinstance(typ, FinalClass):
                        value = node.value
                        if not value:
                            raise TypedSyntaxError(
                                "Must assign a value when declaring a Final"
                            )
                        elif not isinstance(typ, CType) and isinstance(
                            value, ast.Constant
                        ):
                            self.named_finals[target.id] = value

        for name in self.implicit_decl_names:
            if name not in self._children:
                self._children[name] = self.compiler.type_env.DYNAMIC
        # We don't need these anymore...
        self.decls.clear()
        self.implicit_decl_names.clear()
        self.finish_bind_done = True

    def resolve_type(self, node: ast.AST, requester: str) -> Optional[Class]:
        with self.ann_visitor.temporary_context_qualname(requester):
            typ = self.ann_visitor.visit(node)
        if isinstance(typ, Class):
            return typ

    def resolve_decorator(self, node: ast.AST) -> Optional[Value]:
        if isinstance(node, Call):
            func = self.ref_visitor.visit(node.func)
            if isinstance(func, Class):
                return func.instance
            elif isinstance(func, DataclassDecorator):
                return func
            elif isinstance(func, NativeDecorator):
                return func
            elif isinstance(func, Callable):
                return func.return_type.resolved().instance
            elif isinstance(func, MethodType):
                return func.function.return_type.resolved().instance

        return self.ref_visitor.visit(node)

    def resolve_annotation(
        self,
        node: ast.AST,
        requester: str,
        *,
        # annotation on a variable or attribute declaration (could be inside a
        # function, thus not public)
        is_declaration: bool = False,
        # annotation on public API (also includes e.g. function arg/return
        # annotations, does not include function-internal declarations)
        is_decl_dep: bool = False,
    ) -> Optional[Class]:
        assert self.first_pass_done, (
            "Type annotations cannot be resolved until after initial pass, "
            "so that all imports and types are available."
        )
        with self.ann_visitor.temporary_context_qualname(requester, is_decl_dep):
            return self.ann_visitor.resolve_annotation(
                node, is_declaration=is_declaration
            )

    def resolve_name_with_descr(
        self, name: str, requester: str
    ) -> Tuple[Optional[Value], Optional[TypeDescr]]:
        if val := self.get_child(name, requester):
            return val, (self.name, name)
        elif val := self.compiler.builtins.get_child_intrinsic(name):
            return val, None
        return None, None

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
        self, name: str, target: Tuple[str, str] | None, val: Value | DeferredImport
    ) -> None:
        """Declare a name imported into this module.

        `name` is the name in this module's namespace. `target` is a (str, str)
        tuple of (source_module, source_name) for an `import from`. For a
        top-level module import, `target` should be `None`.

        """
        if self.first_pass_done:
            raise ModuleTableException(
                "Attempted to declare an import after the declaration visit"
            )
        self._children[name] = val
        if target is not None:
            self.imported_from[name] = target
            self.record_dependency(name, target)

    def declare_variable(self, node: ast.AnnAssign, module: ModuleTable) -> None:
        self.decls.append((node, None, None))

    def declare_variables(self, node: ast.Assign, module: ModuleTable) -> None:
        targets = node.targets
        for target in targets:
            if isinstance(target, ast.Name):
                self.implicit_decl_names.add(target.id)

    def get_node_data(self, key: AST, data_type: Type[TType]) -> TType:
        return cast(TType, self.node_data[key, data_type])

    def get_opt_node_data(self, key: AST, data_type: Type[TType]) -> TType | None:
        return cast(Optional[TType], self.node_data.get((key, data_type)))

    def set_node_data(self, key: AST, data_type: Type[TType], value: TType) -> None:
        self.node_data[key, data_type] = value

    def mark_known_boolean_test(self, node: ast.expr, *, value: bool) -> None:
        """
        For boolean tests that can be determined during decl-visit, we note the AST nodes
        and the boolean value. This helps us avoid visiting dead code in later passes.
        """
        self.set_node_data(
            node, KnownBoolean, KnownBoolean.TRUE if value else KnownBoolean.FALSE
        )


class IntrinsicModuleTable(ModuleTable):
    """A ModuleTable for modules that are intrinsic in the compiler."""

    def get_child_intrinsic(self, name: str) -> Optional[Value]:
        return self.get_child(name, INTRINSIC_OPT_OUT)


def find_transitive_deps(
    modname: str, all_deps: dict[str, dict[str, set[tuple[str, str]]]]
) -> set[str]:
    """Find all transitive dependency modules of `modname`.

    Given an `alldeps` dictionary of {modname: {name: {(module, name)}}}, return
    the transitive closure of module names depended on by `modname` (not
    including `modname` itself).
    """
    worklist = {dep for deps in all_deps.get(modname, {}).values() for dep in deps}
    ret = set()
    seen = set()
    while worklist:
        dep = worklist.pop()
        seen.add(dep)
        mod, name = dep
        ret.add(mod)
        worklist.update(all_deps.get(mod, {}).get(name, set()).difference(seen))
    ret.discard(modname)
    return ret

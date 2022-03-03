# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
from __future__ import annotations

import ast
from ast import (
    AST,
    And,
    AnnAssign,
    Assign,
    AsyncFunctionDef,
    Attribute,
    AugAssign,
    Await,
    BinOp,
    BoolOp,
    Call,
    ClassDef,
    Compare,
    Constant,
    DictComp,
    For,
    FormattedValue,
    FunctionDef,
    GeneratorExp,
    If,
    IfExp,
    ImportFrom,
    Index,
    Is,
    IsNot,
    JoinedStr,
    Lambda,
    ListComp,
    Module,
    Name,
    NameConstant,
    Return,
    SetComp,
    Slice,
    Starred,
    Subscript,
    Try,
    UnaryOp,
    While,
    Yield,
    YieldFrom,
    expr,
)
from contextlib import contextmanager
from enum import IntEnum
from typing import (
    Dict,
    Generator,
    List,
    Optional,
    Sequence,
    Set,
    TYPE_CHECKING,
    Type,
    Union,
    cast,
)

from ..consts import SC_GLOBAL_EXPLICIT, SC_GLOBAL_IMPLICIT, SC_LOCAL
from ..errors import CollectingErrorSink, TypedSyntaxError
from ..symbols import SymbolVisitor
from .declaration_visitor import GenericVisitor
from .effects import NarrowingEffect, NO_EFFECT
from .module_table import ModuleTable, ModuleFlag
from .types import (
    AwaitableType,
    CInstance,
    CType,
    CheckedDictInstance,
    CheckedListInstance,
    Class,
    ClassVar,
    FinalClass,
    Function,
    FunctionContainer,
    GenericClass,
    IsInstanceEffect,
    ModuleInstance,
    TypeDescr,
    Slot,
    TType,
    TypeEnvironment,
    UnionInstance,
    UnknownDecoratedMethod,
    Value,
    OptionalInstance,
    TransparentDecoratedMethod,
)

if TYPE_CHECKING:
    from .compiler import Compiler


class BindingScope:
    def __init__(
        self,
        node: AST,
        type_env: TypeEnvironment,
    ) -> None:
        self.node = node
        self.local_types: Dict[str, Value] = {}
        self.decl_types: Dict[str, TypeDeclaration] = {}
        self.type_env: TypeEnvironment = type_env

    def branch(self) -> LocalsBranch:
        return LocalsBranch(self)

    def declare(
        self, name: str, typ: Value, is_final: bool = False, is_inferred: bool = False
    ) -> TypeDeclaration:
        # For an unannotated assignment (is_inferred=True), we declare dynamic
        # type; this disallows later re-declaration, but allows any type to be
        # assigned later, so `x = None; if flag: x = "foo"` works.
        decl = TypeDeclaration(self.type_env.DYNAMIC if is_inferred else typ, is_final)
        self.decl_types[name] = decl
        self.local_types[name] = typ
        return decl


class ModuleBindingScope(BindingScope):
    def __init__(
        self,
        node: ast.Module,
        module: ModuleTable,
        type_env: TypeEnvironment,
    ) -> None:
        super().__init__(node, type_env)
        self.module = module

    def declare(
        self, name: str, typ: Value, is_final: bool = False, is_inferred: bool = False
    ) -> TypeDeclaration:
        # at module scope we will go ahead and set a declared type even without
        # an annotation, but we don't want to infer the exact type; should be
        # able to reassign to a subtype
        if is_inferred:
            typ = typ.nonliteral().inexact()
            is_inferred = False
        self.module.children[name] = typ
        return super().declare(name, typ, is_final=is_final, is_inferred=is_inferred)


class LocalsBranch:
    """Handles branching and merging local variable types"""

    def __init__(self, scope: BindingScope) -> None:
        self.scope = scope
        self.type_env: TypeEnvironment = scope.type_env
        self.entry_locals: Dict[str, Value] = dict(scope.local_types)

    def copy(self) -> Dict[str, Value]:
        """Make a copy of the current local state"""
        return dict(self.scope.local_types)

    def restore(self, state: Optional[Dict[str, Value]] = None) -> None:
        """Restore the locals to the state when we entered"""
        self.scope.local_types.clear()
        self.scope.local_types.update(state or self.entry_locals)

    def merge(self, entry_locals: Optional[Dict[str, Value]] = None) -> None:
        """Merge the entry locals, or a specific copy, into the current locals"""
        # TODO: What about del's?
        if entry_locals is None:
            entry_locals = self.entry_locals

        local_types = self.scope.local_types
        for key, value in entry_locals.items():
            if key in local_types:
                if value != local_types[key]:
                    local_types[key] = self._join(value, local_types[key])
                continue

    def changed(self) -> bool:
        return self.entry_locals != self.scope.local_types

    def _join(self, *types: Value) -> Value:
        if len(types) == 1:
            return types[0]

        return self.type_env.get_union(
            tuple(t.klass.inexact_type() for t in types)
        ).instance


class TypeDeclaration:
    def __init__(self, typ: Value, is_final: bool = False) -> None:
        self.type = typ
        self.is_final = is_final


class TerminalKind(IntEnum):
    NonTerminal = 0
    BreakOrContinue = 1
    RaiseOrReturn = 2


class TypeBinder(GenericVisitor):
    """Walks an AST and produces an optionally strongly typed AST, reporting errors when
    operations are occuring that are not sound.  Strong types are based upon places where
    annotations occur which opt-in the strong typing"""

    def __init__(
        self,
        symbols: SymbolVisitor,
        filename: str,
        compiler: Compiler,
        module_name: str,
        optimize: int,
        enable_patching: bool = False,
    ) -> None:
        module = compiler[module_name]
        super().__init__(module)
        self.symbols = symbols
        self.scopes: List[BindingScope] = []
        self.modules: Dict[str, ModuleTable] = compiler.modules
        self.optimize = optimize
        self.terminals: Dict[AST, TerminalKind] = {}
        self.type_env: TypeEnvironment = compiler.type_env
        self.inline_depth = 0
        self.inline_calls = 0
        self.enable_patching = enable_patching
        self.current_loop: AST | None = None
        self.loop_may_break: Set[AST] = set()

    @property
    def nodes_default_dynamic(self) -> bool:
        # If we have a non-throwing ErrorSink, then we may miss typing some
        # nodes on error, so default them to dynamic silently.
        return not self.error_sink.throwing

    @property
    def local_types(self) -> Dict[str, Value]:
        return self.binding_scope.local_types

    @property
    def decl_types(self) -> Dict[str, TypeDeclaration]:
        return self.binding_scope.decl_types

    @property
    def binding_scope(self) -> BindingScope:
        return self.scopes[-1]

    @property
    def scope(self) -> AST:
        return self.binding_scope.node

    def maybe_set_local_type(self, name: str, local_type: Value) -> Value:
        decl = self.get_target_decl(name)
        assert decl is not None
        decl_type = decl.type
        if local_type is self.type_env.DYNAMIC or not decl_type.klass.can_be_narrowed:
            local_type = decl_type
        self.local_types[name] = local_type
        return local_type

    def maybe_get_current_class(self) -> Optional[Class]:
        node = self.scope
        if isinstance(node, ClassDef):
            res = self.get_type(node)
            assert isinstance(res, Class)
            return res

    def visit(
        self, node: Union[AST, Sequence[AST]], *args: object
    ) -> Optional[NarrowingEffect]:
        """This override is only here to give Pyre the return type information."""
        ret = super().visit(node, *args)
        if ret is not None:
            assert isinstance(ret, NarrowingEffect)
            return ret
        return None

    def get_final_literal(self, node: AST) -> Optional[ast.Constant]:
        return self.module.get_final_literal(node, self.symbols.scopes[self.scope])

    def declare_local(
        self,
        name: str,
        typ: Value,
        is_final: bool = False,
        is_inferred: bool = False,
    ) -> None:
        if name in self.decl_types:
            raise TypedSyntaxError(f"Cannot redefine local variable {name}")
        if isinstance(typ, CInstance):
            self.check_primitive_scope(name)
        self.binding_scope.declare(
            name, typ, is_final=is_final, is_inferred=is_inferred
        )

    def check_static_import_flags(self, node: Module) -> None:
        saw_doc_str = False
        for stmt in node.body:
            if isinstance(stmt, ast.Expr):
                val = stmt.value
                if isinstance(val, ast.Constant) and isinstance(val.value, str):
                    if saw_doc_str:
                        break
                    saw_doc_str = True
                else:
                    break
            elif isinstance(stmt, ast.Import):
                continue
            elif isinstance(stmt, ast.ImportFrom):
                if stmt.module == "__static__.compiler_flags":
                    for name in stmt.names:
                        if name.name == "checked_dicts":
                            self.module.flags.add(ModuleFlag.CHECKED_DICTS)
                        elif name.name == "checked_lists":
                            self.module.flags.add(ModuleFlag.CHECKED_LISTS)
                        elif name.name in ("noframe", "shadow_frame"):
                            self.module.flags.add(ModuleFlag.SHADOW_FRAME)

    def visitModule(self, node: Module) -> None:
        self.scopes.append(
            ModuleBindingScope(
                node,
                self.module,
                type_env=self.type_env,
            )
        )

        self.check_static_import_flags(node)

        for stmt in node.body:
            self.visit(stmt)

        self.scopes.pop()

    def set_param(
        self,
        arg: ast.arg,
        arg_type: Value,
        scope: BindingScope,
    ) -> None:
        scope.declare(arg.arg, arg_type)
        self.set_type(arg, arg_type)

    def _visitParameters(self, args: ast.arguments, scope: BindingScope) -> None:
        default_index = len(args.defaults or []) - (
            len(args.posonlyargs) + len(args.args)
        )
        for arg in args.posonlyargs:
            ann = arg.annotation
            if ann:
                self.visitExpectedType(
                    ann,
                    self.type_env.DYNAMIC,
                    "argument annotation cannot be a primitive",
                )
                arg_type = self.module.resolve_annotation(ann) or self.type_env.dynamic
            elif arg.arg in scope.decl_types:
                # Already handled self
                default_index += 1
                continue
            else:
                arg_type = self.type_env.dynamic
            arg_type = arg_type.unwrap()
            if default_index >= 0:
                self.visit(args.defaults[default_index], arg_type.instance)
                self.check_can_assign_from(
                    arg_type,
                    self.get_type(args.defaults[default_index]).klass,
                    args.defaults[default_index],
                )
            default_index += 1
            self.set_param(arg, arg_type.instance, scope)

        for arg in args.args:
            ann = arg.annotation
            if ann:
                self.visitExpectedType(
                    ann,
                    self.type_env.DYNAMIC,
                    "argument annotation cannot be a primitive",
                )
                arg_type = self.module.resolve_annotation(ann) or self.type_env.dynamic
            elif arg.arg in scope.decl_types:
                # Already handled self
                default_index += 1
                continue
            else:
                arg_type = self.type_env.dynamic
            arg_type = arg_type.unwrap()
            if default_index >= 0:
                self.visit(args.defaults[default_index], arg_type.instance)
                self.check_can_assign_from(
                    arg_type,
                    self.get_type(args.defaults[default_index]).klass,
                    args.defaults[default_index],
                )
            default_index += 1
            self.set_param(arg, arg_type.instance, scope)

        vararg = args.vararg
        if vararg:
            ann = vararg.annotation
            if ann:
                self.visitExpectedType(
                    ann,
                    self.type_env.DYNAMIC,
                    "argument annotation cannot be a primitive",
                )

            self.set_param(vararg, self.type_env.tuple.exact_type().instance, scope)

        default_index = len(args.kw_defaults or []) - len(args.kwonlyargs)
        for arg in args.kwonlyargs:
            ann = arg.annotation
            if ann:
                self.visitExpectedType(
                    ann,
                    self.type_env.DYNAMIC,
                    "argument annotation cannot be a primitive",
                )
                arg_type = self.module.resolve_annotation(ann) or self.type_env.dynamic
            else:
                arg_type = self.type_env.dynamic
            arg_type = arg_type.unwrap()

            if default_index >= 0:
                default = args.kw_defaults[default_index]
                if default is not None:
                    self.visit(default, arg_type.instance)
                    self.check_can_assign_from(
                        arg_type,
                        self.get_type(default).klass,
                        default,
                    )
            default_index += 1
            self.set_param(arg, arg_type.instance, scope)

        kwarg = args.kwarg
        if kwarg:
            ann = kwarg.annotation
            if ann:
                self.visitExpectedType(
                    ann,
                    self.type_env.DYNAMIC,
                    "argument annotation cannot be a primitive",
                )
            self.set_param(kwarg, self.type_env.dict.exact_type().instance, scope)

    def new_scope(self, node: AST) -> BindingScope:
        return BindingScope(
            node,
            type_env=self.type_env,
        )

    def get_func_container(
        self, node: Union[ast.FunctionDef, ast.AsyncFunctionDef]
    ) -> FunctionContainer:
        function = self.get_type(node)
        if not isinstance(function, FunctionContainer):
            raise RuntimeError("bad value for function")

        return function

    def _visitFunc(self, node: Union[FunctionDef, AsyncFunctionDef]) -> None:
        func = self.get_func_container(node)
        func.bind_function(node, self)
        typ = self.get_type(node)
        # avoid declaring unknown-decorateds as locals in order to support
        # @overload and @property.setter
        if not isinstance(typ, UnknownDecoratedMethod):
            if isinstance(self.scope, (FunctionDef, AsyncFunctionDef)):
                # nested functions can't be invoked against; to ensure we
                # don't, declare them as dynamic type
                typ = self.type_env.DYNAMIC
            self.declare_local(node.name, typ)

    def visitFunctionDef(self, node: FunctionDef) -> None:
        self._visitFunc(node)

    def visitAsyncFunctionDef(self, node: AsyncFunctionDef) -> None:
        self._visitFunc(node)

    def visitClassDef(self, node: ClassDef) -> None:
        for decorator in node.decorator_list:
            self.visitExpectedType(
                decorator, self.type_env.DYNAMIC, "decorator cannot be a primitive"
            )

        for kwarg in node.keywords:
            self.visitExpectedType(
                kwarg.value, self.type_env.DYNAMIC, "class kwarg cannot be a primitive"
            )

        is_protocol = False
        for base in node.bases:
            self.visitExpectedType(
                base, self.type_env.DYNAMIC, "class base cannot be a primitive"
            )
            base_type = self.get_type(base)
            is_protocol |= base_type is self.type_env.protocol

        # skip type-binding protocols; they can't be instantiated and their
        # "methods" commonly won't type check anyway since they typically would
        # have no body
        if is_protocol:
            self.module.compile_non_static.add(node)
        else:
            self.scopes.append(BindingScope(node, type_env=self.type_env))

            for stmt in node.body:
                self.visit(stmt)

            self.scopes.pop()

        res = self.get_type(node)
        self.declare_local(node.name, res)

    def set_type(
        self,
        node: AST,
        type: Value,
    ) -> None:
        self.module.types[node] = type

    def get_type(self, node: AST) -> Value:
        if self.nodes_default_dynamic:
            return self.module.types.get(node, self.type_env.DYNAMIC)
        assert node in self.module.types, f"node not found: {node}, {node.lineno}"
        return self.module.types[node]

    def get_node_data(self, key: AST, data_type: Type[TType]) -> TType:
        return cast(TType, self.module.node_data[key, data_type])

    def get_opt_node_data(self, key: AST, data_type: Type[TType]) -> TType | None:
        return cast(Optional[TType], self.module.node_data.get((key, data_type)))

    def set_node_data(self, key: AST, data_type: Type[TType], value: TType) -> None:
        self.module.node_data[key, data_type] = value

    def check_primitive_scope(self, name: str) -> None:
        cur_scope = self.symbols.scopes[self.scope]
        var_scope = cur_scope.check_name(name)
        if var_scope != SC_LOCAL or isinstance(self.scope, Module):
            raise TypedSyntaxError("cannot use primitives in global or closure scope")

    def get_var_scope(self, var_id: str) -> Optional[int]:
        cur_scope = self.symbols.scopes[self.scope]
        var_scope = cur_scope.check_name(var_id)
        return var_scope

    def _check_final_attribute_reassigned(
        self,
        target: AST,
        assignment: Optional[AST],
    ) -> None:
        member = None
        klass = None
        member_name = None

        # Try to look up the Class and associated Slot
        scope = self.scope
        if isinstance(target, ast.Name) and isinstance(scope, ast.ClassDef):
            klass = self.maybe_get_current_class()
            assert isinstance(klass, Class)
            member_name = target.id
            member = klass.get_member(member_name)
        elif isinstance(target, ast.Attribute):
            val = self.get_type(target.value)
            member_name = target.attr
            # TODO this logic will be inadequate if we support metaclasses
            if isinstance(val, Class):
                klass = val
            else:
                klass = val.klass
            member = klass.get_member(member_name)

        # Ensure we don't reassign to Finals
        if (
            klass is not None
            and member is not None
            and (
                (
                    isinstance(member, Slot)
                    and member.is_final
                    and member.assignment != assignment
                )
                or (isinstance(member, Function) and member.is_final)
                or (
                    isinstance(member, TransparentDecoratedMethod)
                    and isinstance(member.function, Function)
                    and member.function.is_final
                )
            )
        ):
            self.syntax_error(
                f"Cannot assign to a Final attribute of {klass.instance.name}:{member_name}",
                target,
            )

    def visitAnnAssign(self, node: AnnAssign) -> None:
        self.visitExpectedType(
            node.annotation,
            self.type_env.DYNAMIC,
            "annotation can not be a primitive value",
        )

        target = node.target
        comp_type = (
            self.module.resolve_annotation(node.annotation, is_declaration=True)
            or self.type_env.dynamic
        )
        is_final = False
        comp_type, wrapper = comp_type.unwrap(), type(comp_type)
        if wrapper is ClassVar and not isinstance(self.scope, ClassDef):
            self.syntax_error(
                "ClassVar is allowed only in class attribute annotations.", node
            )
        if wrapper is FinalClass:
            is_final = True

        declared_type = comp_type.instance
        is_dynamic_final = is_final and declared_type is self.type_env.DYNAMIC
        if isinstance(target, Name):
            # We special case x: Final[dynamic] = value to treat `x`'s inferred type as the
            # declared type instead of the comp_type - this allows us to support aliasing of
            # functions declared as protocols.
            if is_dynamic_final:
                value = node.value
                if value:
                    self.visit(value)
                    declared_type = self.get_type(value)

            self.declare_local(target.id, declared_type, is_final)
            self.set_type(target, declared_type)

        self.visit(target)
        value = node.value
        if value and not is_dynamic_final:
            self.visitExpectedType(value, declared_type)
            if isinstance(target, Name):
                # We could be narrowing the type after the assignment, so we update it here
                # even though we assigned it above (but we never narrow primtives)
                new_type = self.get_type(value)
                local_type = self.maybe_set_local_type(target.id, new_type)
                self.set_type(target, local_type)

            self._check_final_attribute_reassigned(target, node)

    def visitAugAssign(self, node: AugAssign) -> None:
        self.visit(node.target)
        target_type = self.get_type(node.target).inexact()
        self.visit(node.value, target_type)
        self.set_type(node, target_type)

    def visitNamedExpr(self, node: ast.NamedExpr) -> None:
        target = node.target
        self.visit(target)
        target_type = self.get_type(target)
        self.visit(node.value, target_type)
        value_type = self.get_type(node.value)
        self.assign_value(target, value_type)
        self.set_type(node, target_type)

    def visitAssign(self, node: Assign) -> None:
        # Sometimes, we need to propagate types from the target to the value to allow primitives to be handled
        # correctly.  So we compute the narrowest target type. (Other checks do happen later).
        # e.g: `x: int8 = 1` means we need `1` to be of type `int8`
        narrowest_target_type = None
        for target in reversed(node.targets):
            cur_type = None
            if isinstance(target, ast.Name):
                # This is a name, it could be unassigned still
                decl_type = self.get_target_decl(target.id)
                if decl_type is not None:
                    cur_type = decl_type.type
            elif isinstance(target, (ast.Tuple, ast.List)):
                # TODO: We should walk into the tuple/list and use it to infer
                # types down on the RHS if we can
                self.visit(target)
            else:
                # This is an attribute or subscript, the assignment can't change the type
                self.visit(target)
                cur_type = self.get_type(target)

            if cur_type is not None and (
                narrowest_target_type is None
                or narrowest_target_type.klass.can_assign_from(cur_type.klass)
            ):
                narrowest_target_type = cur_type

        self.visit(node.value, narrowest_target_type)
        value_type = self.get_type(node.value)
        for target in reversed(node.targets):
            self.assign_value(target, value_type, src=node.value, assignment=node)

        self.set_type(node, value_type)

    def check_can_assign_from(
        self,
        dest: Class,
        src: Class,
        node: AST,
        reason: str = "type mismatch: {} cannot be assigned to {}",
    ) -> None:
        if not dest.can_assign_from(src) and (
            src is not self.type_env.dynamic or isinstance(dest, CType)
        ):
            if dest.inexact_type().can_assign_from(src):
                reason = reason.format(
                    src.instance.name_with_exact, dest.instance.name_with_exact
                )
            else:
                reason = reason.format(src.instance.name, dest.instance.name)

            self.syntax_error(
                reason,
                node,
            )

    def visitAssert(self, node: ast.Assert) -> None:
        effect = self.visit(node.test) or NO_EFFECT
        effect.apply(self.local_types)
        self.set_node_data(node, NarrowingEffect, effect)
        message = node.msg
        if message:
            self.visitExpectedType(
                message, self.type_env.DYNAMIC, "assert message cannot be a primitive"
            )

    def visitBoolOp(
        self, node: BoolOp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        effect = NO_EFFECT
        final_type = None
        if isinstance(node.op, And):
            for value in node.values:
                new_effect = self.visit(value) or NO_EFFECT
                effect = effect.and_(new_effect)
                final_type = self.widen(final_type, self.get_type(value))

                # apply the new effect as short circuiting would
                # eliminate it.
                new_effect.apply(self.local_types)

            # we undo the effect as we have no clue what context we're in
            # but then we return the combined effect in case we're being used
            # in a conditional context
            effect.undo(self.local_types)
        elif isinstance(node.op, ast.Or):
            for value in node.values[:-1]:
                new_effect = self.visit(value) or NO_EFFECT
                effect = effect.or_(new_effect)

                old_type = self.get_type(value)
                # The or expression will only return the `value` we're visiting if it's
                # effect holds, so we visit it assuming that the narrowing effects apply.
                new_effect.apply(self.local_types)
                self.visit(value)
                new_effect.undo(self.local_types)

                final_type = self.widen(
                    final_type,
                    # For Optional[T], we use `T` when widening, to handle `my_optional or something`
                    old_type.klass.opt_type.instance
                    if isinstance(old_type, OptionalInstance)
                    else old_type,
                )
                self.set_type(value, old_type)

                new_effect.reverse(self.local_types)
            # We know nothing about the last node of an or, so we simply widen with its type.
            new_effect = self.visit(node.values[-1]) or NO_EFFECT
            final_type = self.widen(final_type, self.get_type(node.values[-1]))

            effect.undo(self.local_types)
            effect = effect.or_(new_effect)
        else:
            for value in node.values:
                self.visit(value)
                final_type = self.widen(final_type, self.get_type(value))

        self.set_type(node, final_type or self.type_env.DYNAMIC)
        return effect

    def visitBinOp(
        self, node: BinOp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        # If we're taking pow, the output type is always double, regardless of
        # the input types, and we need to clear the type context to avoid coercing improperly.
        if isinstance(node.op, ast.Pow):
            type_ctx = None

        self.visit(node.left, type_ctx)
        self.visit(node.right, type_ctx)

        ltype = self.get_type(node.left)
        rtype = self.get_type(node.right)

        tried_right = False
        if ltype.klass.exact_type() in rtype.klass.mro[1:]:
            if rtype.bind_reverse_binop(node, self, type_ctx):
                return NO_EFFECT
            tried_right = True

        if ltype.bind_binop(node, self, type_ctx):
            return NO_EFFECT

        if not tried_right:
            rtype.bind_reverse_binop(node, self, type_ctx)

        return NO_EFFECT

    def visitUnaryOp(
        self, node: UnaryOp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        effect = self.visit(node.operand, type_ctx)
        self.get_type(node.operand).bind_unaryop(node, self, type_ctx)
        if (
            effect is not None
            and effect is not NO_EFFECT
            and isinstance(node.op, ast.Not)
        ):
            return effect.not_()
        return NO_EFFECT

    def visitLambda(
        self, node: Lambda, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        scope = BindingScope(
            node,
            type_env=self.type_env,
        )
        self._visitParameters(node.args, scope)

        self.scopes.append(scope)
        self.visitExpectedType(
            node.body, self.type_env.DYNAMIC, "lambda cannot return primitive value"
        )
        self.scopes.pop()

        self.set_type(node, self.type_env.DYNAMIC)
        return NO_EFFECT

    def visitIfExp(
        self, node: IfExp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        effect = self.visit(node.test) or NO_EFFECT
        effect.apply(self.local_types)
        self.visit(node.body, type_ctx)
        effect.reverse(self.local_types)
        self.visit(node.orelse, type_ctx)
        effect.undo(self.local_types)

        # Select the most compatible types that we can, or fallback to
        # dynamic if we can coerce to dynamic, otherwise report an error.
        body_t = self.get_type(node.body)
        else_t = self.get_type(node.orelse)
        self.set_type(
            node,
            self.type_env.get_union((body_t.klass, else_t.klass)).instance,
        )
        return NO_EFFECT

    def visitSlice(
        self, node: Slice, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        lower = node.lower
        if lower:
            self.visitExpectedType(
                lower, self.type_env.DYNAMIC, "slice indices cannot be primitives"
            )
        upper = node.upper
        if upper:
            self.visitExpectedType(
                upper, self.type_env.DYNAMIC, "slice indices cannot be primitives"
            )
        step = node.step
        if step:
            self.visitExpectedType(
                step, self.type_env.DYNAMIC, "slice indices cannot be primitives"
            )
        self.set_type(node, self.type_env.slice.instance)
        return NO_EFFECT

    def widen(self, existing: Optional[Value], new: Value) -> Value:
        if existing is None or new.klass.can_assign_from(existing.klass):
            return new
        elif existing.klass.can_assign_from(new.klass):
            return existing

        return self.type_env.get_union((existing.klass, new.klass)).instance

    def visitDict(
        self, node: ast.Dict, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        key_type: Optional[Value] = None
        value_type: Optional[Value] = None
        for k, v in zip(node.keys, node.values):
            if k:
                self.visitExpectedType(
                    k, self.type_env.DYNAMIC, "dict keys cannot be primitives"
                )
                key_type = self.widen(key_type, self.get_type(k))
                self.visitExpectedType(
                    v, self.type_env.DYNAMIC, "dict keys cannot be primitives"
                )
                value_type = self.widen(value_type, self.get_type(v))
            else:
                self.visitExpectedType(
                    v, self.type_env.DYNAMIC, "dict splat cannot be a primitive"
                )
                d_type = self.get_type(v).klass
                if d_type.generic_type_def is self.type_env.checked_dict:
                    assert isinstance(d_type, GenericClass)
                    key_type = self.widen(key_type, d_type.type_args[0].instance)
                    value_type = self.widen(value_type, d_type.type_args[1].instance)
                elif d_type in (
                    self.type_env.dict,
                    self.type_env.dict.exact_type(),
                    self.type_env.dynamic,
                ):
                    key_type = self.type_env.DYNAMIC
                    value_type = self.type_env.DYNAMIC

        self.set_dict_type(node, key_type, value_type, type_ctx)
        return NO_EFFECT

    def set_dict_type(
        self,
        node: ast.expr,
        key_type: Optional[Value],
        value_type: Optional[Value],
        type_ctx: Optional[Class],
    ) -> Value:
        if not isinstance(type_ctx, CheckedDictInstance):
            if (
                ModuleFlag.CHECKED_DICTS in self.module.flags
                and key_type is not None
                and value_type is not None
            ):
                typ = self.type_env.get_generic_type(
                    self.type_env.checked_dict,
                    (key_type.klass.inexact_type(), value_type.klass.inexact_type()),
                ).instance
            else:
                typ = self.type_env.dict.exact_type().instance
            self.set_type(node, typ)
            return typ

        # Calculate the type that is inferred by the keys and values
        assert type_ctx is not None
        type_class = type_ctx.klass
        assert type_class.generic_type_def is self.type_env.checked_dict, type_class
        assert isinstance(type_class, GenericClass)
        if key_type is None:
            key_type = type_class.type_args[0].instance

        if value_type is None:
            value_type = type_class.type_args[1].instance

        gen_type = self.type_env.get_generic_type(
            self.type_env.checked_dict,
            (key_type.klass, value_type.klass),
        )

        self.set_type(node, type_ctx)
        # We can use the type context to have a type which is wider than the
        # inferred types.  But we need to make sure that the keys/values are compatible
        # with the wider type, and if not, we'll report that the inferred type isn't
        # compatible.
        if not type_class.type_args[0].can_assign_from(
            key_type.klass
        ) or not type_class.type_args[1].can_assign_from(value_type.klass):
            self.check_can_assign_from(type_class, gen_type, node)
        return type_ctx

    def set_list_type(
        self,
        node: ast.expr,
        item_type: Optional[Value],
        type_ctx: Optional[Class],
    ) -> Value:
        if not isinstance(type_ctx, CheckedListInstance):
            if ModuleFlag.CHECKED_LISTS in self.module.flags and item_type is not None:
                typ = self.type_env.get_generic_type(
                    self.type_env.checked_list,
                    (item_type.nonliteral().klass.inexact_type(),),
                ).instance
            else:
                typ = self.type_env.list.exact_type().instance

            self.set_type(node, typ)
            return typ

        # Calculate the type that is inferred by the item.
        assert type_ctx is not None
        type_class = type_ctx.klass
        assert type_class.generic_type_def is self.type_env.checked_list, type_class
        assert isinstance(type_class, GenericClass)
        if item_type is None:
            item_type = type_class.type_args[0].instance

        gen_type = self.type_env.get_generic_type(
            self.type_env.checked_list,
            (item_type.nonliteral().klass.inexact_type(),),
        )

        self.set_type(node, type_ctx)
        # We can use the type context to have a type which is wider than the
        # inferred types.  But we need to make sure that the items are compatible
        # with the wider type, and if not, we'll report that the inferred type isn't
        # compatible.
        if not type_class.type_args[0].can_assign_from(item_type.klass):
            self.check_can_assign_from(type_class, gen_type, node)
        return type_ctx

    def visitSet(
        self, node: ast.Set, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        for elt in node.elts:
            self.visitExpectedType(
                elt, self.type_env.DYNAMIC, "set members cannot be primitives"
            )
        self.set_type(node, self.type_env.set.exact_type().instance)
        return NO_EFFECT

    def visitGeneratorExp(
        self, node: GeneratorExp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit_comprehension(node, node.generators, node.elt)
        self.set_type(node, self.type_env.DYNAMIC)
        return NO_EFFECT

    def visitListComp(
        self, node: ListComp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit_comprehension(node, node.generators, node.elt)
        item_type = self.get_type(node.elt)
        self.set_list_type(node, item_type, type_ctx)
        return NO_EFFECT

    def visitSetComp(
        self, node: SetComp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit_comprehension(node, node.generators, node.elt)
        self.set_type(node, self.type_env.set.exact_type().instance)
        return NO_EFFECT

    def get_target_decl(self, name: str) -> Optional[TypeDeclaration]:
        decl_type = self.decl_types.get(name)
        if decl_type is None:
            scope_type = self.get_var_scope(name)
            if scope_type in (SC_GLOBAL_EXPLICIT, SC_GLOBAL_IMPLICIT):
                decl_type = self.scopes[0].decl_types.get(name)
        return decl_type

    def assign_value(
        self,
        target: expr,
        value: Value,
        src: Optional[expr] = None,
        assignment: Optional[AST] = None,
    ) -> None:
        if isinstance(target, Name):
            decl_type = self.get_target_decl(target.id)
            if decl_type is None:
                self.declare_local(target.id, value, is_inferred=True)
            else:
                if decl_type.is_final:
                    self.syntax_error("Cannot assign to a Final variable", target)
                self.check_can_assign_from(decl_type.type.klass, value.klass, target)

            local_type = self.maybe_set_local_type(target.id, value)
            self.set_type(target, local_type)
        elif isinstance(target, (ast.Tuple, ast.List)):
            if isinstance(src, (ast.Tuple, ast.List)) and len(target.elts) == len(
                src.elts
            ):
                for target, inner_value in zip(target.elts, src.elts):
                    self.assign_value(
                        target, self.get_type(inner_value), src=inner_value
                    )
            elif isinstance(src, ast.Constant):
                t = src.value
                if isinstance(t, tuple) and len(t) == len(target.elts):
                    for target, inner_value in zip(target.elts, t):
                        self.assign_value(
                            target, self.type_env.constant_types[type(inner_value)]
                        )
                else:
                    for val in target.elts:
                        self.assign_value(val, self.type_env.DYNAMIC)
            else:
                for val in target.elts:
                    self.assign_value(val, self.type_env.DYNAMIC)
        else:
            self.check_can_assign_from(self.get_type(target).klass, value.klass, target)
        self._check_final_attribute_reassigned(target, assignment)

    def visitDictComp(
        self, node: DictComp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit(node.generators[0].iter)

        scope = BindingScope(
            node,
            type_env=self.type_env,
        )
        self.scopes.append(scope)

        iter_type = self.get_type(node.generators[0].iter).get_iter_type(
            node.generators[0].iter, self
        )

        self.assign_value(node.generators[0].target, iter_type)
        for if_ in node.generators[0].ifs:
            self.visit(if_)

        for gen in node.generators[1:]:
            self.visit(gen.iter)
            iter_type = self.get_type(gen.iter).get_iter_type(gen.iter, self)
            self.assign_value(gen.target, iter_type)

        self.visitExpectedType(
            node.key,
            self.type_env.DYNAMIC,
            "dictionary comprehension key cannot be a primitive",
        )
        self.visitExpectedType(
            node.value,
            self.type_env.DYNAMIC,
            "dictionary comprehension value cannot be a primitive",
        )

        self.scopes.pop()

        key_type = self.get_type(node.key)
        value_type = self.get_type(node.value)
        self.set_dict_type(node, key_type, value_type, type_ctx)

        return NO_EFFECT

    def visit_comprehension(
        self, node: ast.expr, generators: List[ast.comprehension], *elts: ast.expr
    ) -> None:
        self.visit(generators[0].iter)

        scope = BindingScope(
            node,
            type_env=self.type_env,
        )
        self.scopes.append(scope)

        iter_type = self.get_type(generators[0].iter).get_iter_type(
            generators[0].iter, self
        )

        self.assign_value(generators[0].target, iter_type)
        for if_ in generators[0].ifs:
            self.visit(if_)

        for gen in generators[1:]:
            self.visit(gen.iter)
            iter_type = self.get_type(gen.iter).get_iter_type(gen.iter, self)
            self.assign_value(gen.target, iter_type)
            for if_ in gen.ifs:
                self.visit(if_)

        for elt in elts:
            self.visitExpectedType(
                elt, self.type_env.DYNAMIC, "generator element cannot be a primitive"
            )

        self.scopes.pop()

    def visitAwait(
        self, node: Await, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visitExpectedType(
            node.value, self.type_env.DYNAMIC, "cannot await a primitive value"
        )
        self.get_type(node.value).bind_await(node, self, type_ctx)
        return NO_EFFECT

    def visitYield(
        self, node: Yield, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        value = node.value
        if value is not None:
            self.visitExpectedType(
                value, self.type_env.DYNAMIC, "cannot yield a primitive value"
            )
        self.set_type(node, self.type_env.DYNAMIC)
        return NO_EFFECT

    def visitYieldFrom(
        self, node: YieldFrom, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visitExpectedType(
            node.value, self.type_env.DYNAMIC, "cannot yield from a primitive value"
        )
        self.set_type(node, self.type_env.DYNAMIC)
        return NO_EFFECT

    def visitIndex(
        self, node: Index, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit(node.value, type_ctx)
        self.set_type(node, self.get_type(node.value))
        return NO_EFFECT

    def visitCompare(
        self, node: Compare, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        if len(node.ops) == 1 and isinstance(node.ops[0], (Is, IsNot)):
            left = node.left
            right = node.comparators[0]
            other = None

            self.set_type(node, self.type_env.bool.instance)
            self.set_type(node.ops[0], self.type_env.bool.instance)

            self.visit(left)
            self.visit(right)

            if isinstance(left, (Constant, NameConstant)) and left.value is None:
                other = right
            elif isinstance(right, (Constant, NameConstant)) and right.value is None:
                other = left

            if other is not None and isinstance(other, Name):
                var_type = self.get_type(other)

                if (
                    isinstance(var_type, UnionInstance)
                    and not var_type.klass.is_generic_type_definition
                ):
                    effect = IsInstanceEffect(
                        other, var_type, self.type_env.none.instance, self
                    )
                    if isinstance(node.ops[0], IsNot):
                        effect = effect.not_()
                    return effect

        self.visit(node.left)
        left = node.left
        ltype = self.get_type(node.left)
        node.ops = [type(op)() for op in node.ops]
        for comparator, op in zip(node.comparators, node.ops):
            self.visit(comparator)
            rtype = self.get_type(comparator)

            tried_right = False
            if ltype.klass.exact_type() in rtype.klass.mro[1:]:
                if ltype.bind_reverse_compare(
                    node, left, op, comparator, self, type_ctx
                ):
                    continue
                tried_right = True

            if ltype.bind_compare(node, left, op, comparator, self, type_ctx):
                continue

            if not tried_right:
                rtype.bind_reverse_compare(node, left, op, comparator, self, type_ctx)

            ltype = rtype
            right = comparator
        return NO_EFFECT

    def visitCall(
        self, node: Call, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit(node.func)
        return self.get_type(node.func).bind_call(node, self, type_ctx)

    def visitFormattedValue(
        self, node: FormattedValue, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visitExpectedType(
            node.value, self.type_env.DYNAMIC, "cannot use primitive in formatted value"
        )
        if fs := node.format_spec:
            self.visit(fs)
        self.set_type(node, self.type_env.DYNAMIC)
        return NO_EFFECT

    def visitJoinedStr(
        self, node: JoinedStr, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        for value in node.values:
            self.visit(value)

        self.set_type(node, self.type_env.str.exact_type().instance)
        return NO_EFFECT

    def visitConstant(
        self, node: Constant, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        if type_ctx is not None:
            type_ctx.bind_constant(node, self)
        else:
            self.type_env.DYNAMIC.bind_constant(node, self)
        return NO_EFFECT

    def visitAttribute(
        self, node: Attribute, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit(node.value)
        base = self.get_type(node.value)
        base.bind_attr(node, self, type_ctx)
        if isinstance(base, ModuleInstance):
            self.set_node_data(node, TypeDescr, (base.module_name, node.attr))
        return NO_EFFECT

    def visitSubscript(
        self, node: Subscript, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit(node.value)
        self.visit(node.slice)
        val_type = self.get_type(node.value)
        val_type.bind_subscr(node, self.get_type(node.slice), self, type_ctx)
        return NO_EFFECT

    def visitStarred(
        self, node: Starred, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visitExpectedType(
            node.value,
            self.type_env.DYNAMIC,
            "cannot use primitive in starred expression",
        )
        self.set_type(node, self.type_env.DYNAMIC)
        return NO_EFFECT

    def visitName(
        self, node: Name, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        cur_scope = self.symbols.scopes[self.scope]
        scope = cur_scope.check_name(node.id)
        if scope == SC_LOCAL and not isinstance(self.scope, Module):
            var_type = self.local_types.get(node.id, self.type_env.DYNAMIC)
            self.set_type(node, var_type)
        else:
            typ, descr = self.module.resolve_name_with_descr(node.id)
            self.set_type(node, typ or self.type_env.DYNAMIC)
            if descr is not None:
                self.set_node_data(node, TypeDescr, descr)

        type = self.get_type(node)
        if (
            isinstance(type, UnionInstance)
            and not type.klass.is_generic_type_definition
        ):
            effect = IsInstanceEffect(node, type, self.type_env.none.instance, self)
            return effect.not_()

        return NO_EFFECT

    def visitExpectedType(
        self,
        node: AST,
        expected: Value,
        reason: str = "type mismatch: {} cannot be assigned to {}",
        blame: Optional[AST] = None,
    ) -> Optional[NarrowingEffect]:
        res = self.visit(node, expected)
        self.check_can_assign_from(
            expected.klass, self.get_type(node).klass, blame or node, reason
        )
        return res

    def visitList(
        self, node: ast.List, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        item_type: Optional[Value] = None
        for elt in node.elts:
            self.visitExpectedType(elt, self.type_env.DYNAMIC)
            if isinstance(elt, ast.Starred):
                unpacked_value_type = self.get_type(elt.value)
                if isinstance(unpacked_value_type, CheckedListInstance):
                    element_type = unpacked_value_type.klass.type_args[0].instance
                else:
                    element_type = self.type_env.DYNAMIC
            else:
                element_type = self.get_type(elt)
            item_type = self.widen(item_type, element_type)
        self.set_list_type(node, item_type, type_ctx)
        return NO_EFFECT

    def visitTuple(
        self, node: ast.Tuple, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        for elt in node.elts:
            self.visitExpectedType(elt, self.type_env.DYNAMIC)
        self.set_type(node, self.type_env.tuple.exact_type().instance)
        return NO_EFFECT

    def set_terminal_kind(self, node: AST, level: TerminalKind) -> None:
        current = self.terminals.get(node, TerminalKind.NonTerminal)
        if current < level:
            self.terminals[node] = level

    def visitContinue(self, node: ast.Continue) -> None:
        self.set_terminal_kind(node, TerminalKind.BreakOrContinue)

    def visitBreak(self, node: ast.Break) -> None:
        self.set_terminal_kind(node, TerminalKind.BreakOrContinue)
        if self.current_loop is not None:
            self.loop_may_break.add(self.current_loop)

    def visitRaise(self, node: ast.Raise) -> None:
        self.set_terminal_kind(node, TerminalKind.RaiseOrReturn)
        self.generic_visit(node)

    def visitReturn(self, node: Return) -> None:
        self.set_terminal_kind(node, TerminalKind.RaiseOrReturn)
        value = node.value
        if value is not None:
            cur_scope = self.binding_scope
            func = cur_scope.node
            expected = self.type_env.DYNAMIC

            if isinstance(func, (ast.FunctionDef, ast.AsyncFunctionDef)):
                function = self.get_func_container(func)
                func_returns = function.return_type.resolved().unwrap()
                if isinstance(func_returns, AwaitableType):
                    func_returns = func_returns.type_args[0]
                expected = func_returns.instance

            self.visit(value, expected)
            returned = self.get_type(value).klass
            if (
                returned is not self.type_env.dynamic
                and not expected.klass.can_assign_from(returned)
            ):
                self.syntax_error(
                    f"return type must be {expected.name}, not "
                    + str(self.get_type(value).name),
                    node,
                )

    def visitImportFrom(self, node: ImportFrom) -> None:
        mod_name = node.module
        if node.level or not mod_name:
            raise NotImplementedError("relative imports aren't supported")

        if mod_name == "__static__":
            for alias in node.names:
                name = alias.name
                if name == "*":
                    self.syntax_error("from __static__ import * is disallowed", node)
                elif name not in self.compiler.statics.children:
                    self.syntax_error(f"unsupported static import {name}", node)
        # Unknown module, let's add a local dynamic type to ensure we don't try to infer too much.
        if mod_name not in self.compiler.modules:
            for alias in node.names:
                asname = alias.asname
                name: str = asname if asname is not None else alias.name
                self.declare_local(name, self.type_env.DYNAMIC)

    def visit_until_terminates(self, nodes: Sequence[ast.stmt]) -> TerminalKind:
        for stmt in nodes:
            self.visit(stmt)
            if stmt in self.terminals:
                return self.terminals[stmt]

        return TerminalKind.NonTerminal

    def visitIf(self, node: If) -> None:
        branch = self.binding_scope.branch()

        effect = self.visit(node.test) or NO_EFFECT
        effect.apply(self.local_types)

        terminates = self.visit_until_terminates(node.body)

        if node.orelse:
            if_end = branch.copy()
            branch.restore()

            effect.reverse(self.local_types)
            else_terminates = self.visit_until_terminates(node.orelse)
            if else_terminates:
                if terminates:
                    # We're the least severe terminal of our two children
                    self.terminals[node] = min(terminates, else_terminates)
                else:
                    branch.restore(if_end)
            elif not terminates:
                # Merge end of orelse with end of if
                branch.merge(if_end)
        elif terminates:
            effect.reverse(self.local_types)
        else:
            # Merge end of if w/ opening (with test effect reversed)
            branch.merge(effect.reverse(branch.entry_locals))

    def visitTry(self, node: Try) -> None:
        branch = self.binding_scope.branch()
        self.visit(node.body)

        branch.merge()
        post_try = branch.copy()
        merges = []

        if node.orelse:
            self.visit(node.orelse)
            merges.append(branch.copy())

        for handler in node.handlers:
            branch.restore(post_try)
            self.visit(handler)
            merges.append(branch.copy())

        branch.restore(post_try)
        for merge in merges:
            branch.merge(merge)

        if node.finalbody:
            self.visit(node.finalbody)

    def visitExceptHandler(self, node: ast.ExceptHandler) -> None:
        htype = node.type
        hname = None
        if htype:
            self.visit(htype)
            handler_type = self.get_type(htype)
            hname = node.name
            if hname:
                if handler_type is self.type_env.DYNAMIC or not isinstance(
                    handler_type, Class
                ):
                    handler_type = self.type_env.dynamic

                decl_type = self.decl_types.get(hname)
                if decl_type and decl_type.is_final:
                    self.syntax_error("Cannot assign to a Final variable", node)

                self.binding_scope.declare(hname, handler_type.instance)

        self.visit(node.body)
        if hname is not None:
            del self.decl_types[hname]
            del self.local_types[hname]

    def iterate_to_fixed_point(
        self, body: Sequence[ast.stmt], test: ast.expr | None = None
    ) -> None:
        """Iterate given loop body until local types reach a fixed point."""
        branch: LocalsBranch | None = None
        counter = 0
        entry_decls = self.decl_types.copy()
        while (not branch) or branch.changed():
            branch = self.binding_scope.branch()
            counter += 1
            if counter > 50:
                # TODO today it should not be possible to hit this case, but in
                # the future with more complex types or more accurate tracking
                # of literal types (think `x += 1` in a loop) it could become
                # possible, and we'll need a smarter approach here: union all
                # types seen? fall back to declared type?
                raise AssertionError("Too many loops in fixed-point iteration.")
            with self.temporary_error_sink(CollectingErrorSink()):
                if test is not None:
                    effect = self.visit(test) or NO_EFFECT
                    effect.apply(self.local_types)
                terminates = self.visit_until_terminates(body)
                # reset any declarations from the loop body to avoid redeclaration errors
                self.binding_scope.decl_types = entry_decls.copy()
            branch.merge()

    @contextmanager
    def in_loop(self, node: AST) -> Generator[None, None, None]:
        orig = self.current_loop
        self.current_loop = node
        try:
            yield
        finally:
            self.current_loop = orig

    def visitWhile(self, node: While) -> None:
        branch = self.scopes[-1].branch()

        with self.in_loop(node):
            self.iterate_to_fixed_point(node.body, node.test)

            effect = self.visit(node.test) or NO_EFFECT
            condition_always_true = self.get_type(node.test).is_truthy_literal()
            effect.apply(self.local_types)
            terminal_level = self.visit_until_terminates(node.body)

        does_not_break = node not in self.loop_may_break

        if terminal_level == TerminalKind.RaiseOrReturn and does_not_break:
            branch.restore()
            effect.reverse(self.local_types)
        else:
            branch.merge(effect.reverse(branch.entry_locals))

        if condition_always_true and does_not_break:
            self.set_terminal_kind(node, terminal_level)
        if node.orelse:
            # The or-else can happen after the while body, or without executing
            # it, but it can only happen after the while condition evaluates to
            # False.
            effect.reverse(self.local_types)
            self.visit(node.orelse)

            branch.merge()

    def visitFor(self, node: For) -> None:
        self.visit(node.iter)
        target_type = self.get_type(node.iter).get_iter_type(node.iter, self)
        self.visit(node.target)
        self.assign_value(node.target, target_type)
        branch = self.scopes[-1].branch()
        with self.in_loop(node):
            self.iterate_to_fixed_point(node.body)
            self.visit(node.body)
        self.visit(node.orelse)
        branch.merge()

    def visitwithitem(self, node: ast.withitem) -> None:
        self.visit(node.context_expr)
        optional_vars = node.optional_vars
        if optional_vars:
            self.visit(optional_vars)
            self.assign_value(optional_vars, self.type_env.DYNAMIC)

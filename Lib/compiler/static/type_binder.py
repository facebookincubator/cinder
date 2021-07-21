# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
from __future__ import annotations

import ast
from ast import (
    AST,
    And,
    AnnAssign,
    Assign,
    AsyncFor,
    AsyncFunctionDef,
    AsyncWith,
    Attribute,
    AugAssign,
    Await,
    BinOp,
    BoolOp,
    Bytes,
    Call,
    ClassDef,
    Compare,
    Constant,
    DictComp,
    Ellipsis,
    For,
    FormattedValue,
    FunctionDef,
    GeneratorExp,
    If,
    IfExp,
    Import,
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
    Num,
    Return,
    SetComp,
    Slice,
    Starred,
    Str,
    Subscript,
    Try,
    UnaryOp,
    While,
    With,
    Yield,
    YieldFrom,
    expr,
)
from enum import IntEnum
from typing import (
    Dict,
    List,
    Optional,
    Sequence,
    TYPE_CHECKING,
    Type,
    Union,
    cast,
)

from ..consts import SC_GLOBAL_EXPLICIT, SC_GLOBAL_IMPLICIT, SC_LOCAL
from ..pycodegen import Delegator
from ..symbols import SymbolVisitor
from .declaration_visitor import GenericVisitor
from .effects import NarrowingEffect, NO_EFFECT
from .module_table import ModuleTable
from .types import (
    BOOL_TYPE,
    CHECKED_DICT_EXACT_TYPE,
    CHECKED_DICT_TYPE,
    CInstance,
    CONSTANT_TYPES,
    CType,
    CheckedDictInstance,
    Class,
    ClassVar,
    DICT_EXACT_TYPE,
    DICT_TYPE,
    DYNAMIC,
    DYNAMIC_TYPE,
    FinalClass,
    Function,
    GenericClass,
    GenericTypesDict,
    IsInstanceEffect,
    LIST_EXACT_TYPE,
    NONE_TYPE,
    SET_EXACT_TYPE,
    SLICE_TYPE,
    STR_EXACT_TYPE,
    Slot,
    TType,
    TUPLE_EXACT_TYPE,
    UNION_TYPE,
    UnionInstance,
    Value,
)

if TYPE_CHECKING:
    from . import SymbolTable


class BindingScope:
    def __init__(self, node: AST, generic_types: GenericTypesDict) -> None:
        self.node = node
        self.local_types: Dict[str, Value] = {}
        self.decl_types: Dict[str, TypeDeclaration] = {}
        self.generic_types = generic_types

    def branch(self) -> LocalsBranch:
        return LocalsBranch(self)

    def declare(
        self, name: str, typ: Value, is_final: bool = False, is_inferred: bool = False
    ) -> TypeDeclaration:
        # For an unannotated assignment (is_inferred=True), we declare dynamic
        # type; this disallows later re-declaration, but allows any type to be
        # assigned later, so `x = None; if flag: x = "foo"` works.
        decl = TypeDeclaration(DYNAMIC if is_inferred else typ, is_final)
        self.decl_types[name] = decl
        self.local_types[name] = typ
        return decl


class ModuleBindingScope(BindingScope):
    def __init__(
        self, node: ast.Module, module: ModuleTable, generic_types: GenericTypesDict
    ) -> None:
        super().__init__(node, generic_types)
        self.module = module

    def declare(
        self, name: str, typ: Value, is_final: bool = False, is_inferred: bool = False
    ) -> TypeDeclaration:
        # at module scope we will go ahead and set a declared type even without
        # an annotation, but we don't want to infer the exact type; should be
        # able to reassign to a subtype
        if is_inferred:
            typ = typ.inexact()
            is_inferred = False
        self.module.children[name] = typ
        return super().declare(name, typ, is_final=is_final, is_inferred=is_inferred)


class LocalsBranch:
    """Handles branching and merging local variable types"""

    def __init__(self, scope: BindingScope) -> None:
        self.scope = scope
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

    def _join(self, *types: Value) -> Value:
        if len(types) == 1:
            return types[0]

        return UNION_TYPE.make_generic_type(
            tuple(t.inexact().klass for t in types), self.scope.generic_types
        ).instance


class TypeDeclaration:
    def __init__(self, typ: Value, is_final: bool = False) -> None:
        self.type = typ
        self.is_final = is_final


class TerminalKind(IntEnum):
    NonTerminal = 0
    BreakOrContinue = 1
    Return = 2


class TypeBinder(GenericVisitor):
    """Walks an AST and produces an optionally strongly typed AST, reporting errors when
    operations are occuring that are not sound.  Strong types are based upon places where
    annotations occur which opt-in the strong typing"""

    def __init__(
        self,
        symbols: SymbolVisitor,
        filename: str,
        symtable: SymbolTable,
        module_name: str,
        optimize: int = 0,
    ) -> None:
        super().__init__(module_name, filename, symtable)
        self.symbols = symbols
        self.scopes: List[BindingScope] = []
        self.symtable = symtable
        self.cur_mod: ModuleTable = symtable[module_name]
        self.optimize = optimize
        self.terminals: Dict[AST, TerminalKind] = {}
        self.inline_depth = 0
        self.inline_calls = 0

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
        if local_type is DYNAMIC or not decl_type.klass.can_be_narrowed:
            local_type = decl_type
        self.local_types[name] = local_type
        return local_type

    def maybe_get_current_class(self) -> Optional[Class]:
        current: ModuleTable | Class = self.cur_mod
        result = None
        for scope in self.scopes:
            node = scope.node
            if isinstance(node, ClassDef):
                result = current.resolve_name(node.name)
                if not isinstance(result, Class):
                    return None
                current = result
        return result

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
        return self.cur_mod.get_final_literal(node, self.symbols.scopes[self.scope])

    def declare_local(
        self,
        target: ast.Name,
        typ: Value,
        is_final: bool = False,
        is_inferred: bool = False,
    ) -> None:
        if target.id in self.decl_types:
            self.syntax_error(f"Cannot redefine local variable {target.id}", target)
        if isinstance(typ, CInstance):
            self.check_primitive_scope(target)
        self.binding_scope.declare(
            target.id, typ, is_final=is_final, is_inferred=is_inferred
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
                        if name.name == "nonchecked_dicts":
                            self.cur_mod.nonchecked_dicts = True
                        elif name.name in ("noframe", "shadow_frame"):
                            self.cur_mod.shadow_frame = True

    def visitModule(self, node: Module) -> None:
        self.scopes.append(
            ModuleBindingScope(
                node, self.cur_mod, generic_types=self.symtable.generic_types
            )
        )

        self.check_static_import_flags(node)

        for stmt in node.body:
            self.visit(stmt)

        self.scopes.pop()

    def set_param(self, arg: ast.arg, arg_type: Class, scope: BindingScope) -> None:
        scope.declare(arg.arg, arg_type.instance)
        self.set_type(arg, arg_type.instance)

    def _visitParameters(self, args: ast.arguments, scope: BindingScope) -> None:
        if args.defaults:
            for default in args.defaults:
                self.visit(default)

        if args.kw_defaults:
            for default in args.kw_defaults:
                if default is not None:
                    self.visit(default)

        default_index = len(args.defaults or []) - (
            len(args.posonlyargs) + len(args.args)
        )
        for arg in args.posonlyargs:
            ann = arg.annotation
            if ann:
                self.visitExpectedType(
                    ann, DYNAMIC, "argument annotation cannot be a primitive"
                )
                arg_type = self.cur_mod.resolve_annotation(ann) or DYNAMIC_TYPE
            elif arg.arg in scope.decl_types:
                # Already handled self
                default_index += 1
                continue
            else:
                arg_type = DYNAMIC_TYPE
            if default_index >= 0:
                self.check_can_assign_from(
                    arg_type,
                    self.get_type(args.defaults[default_index]).klass,
                    args.defaults[default_index],
                )
            default_index += 1
            self.set_param(arg, arg_type, scope)

        for arg in args.args:
            ann = arg.annotation
            if ann:
                self.visitExpectedType(
                    ann, DYNAMIC, "argument annotation cannot be a primitive"
                )
                arg_type = self.cur_mod.resolve_annotation(ann) or DYNAMIC_TYPE
            elif arg.arg in scope.decl_types:
                # Already handled self
                default_index += 1
                continue
            else:
                arg_type = DYNAMIC_TYPE

            if default_index >= 0:
                self.check_can_assign_from(
                    arg_type,
                    self.get_type(args.defaults[default_index]).klass,
                    args.defaults[default_index],
                )
            default_index += 1
            self.set_param(arg, arg_type, scope)

        vararg = args.vararg
        if vararg:
            ann = vararg.annotation
            if ann:
                self.visitExpectedType(
                    ann, DYNAMIC, "argument annotation cannot be a primitive"
                )

            self.set_param(vararg, TUPLE_EXACT_TYPE, scope)

        default_index = len(args.kw_defaults or []) - len(args.kwonlyargs)
        for arg in args.kwonlyargs:
            ann = arg.annotation
            if ann:
                self.visitExpectedType(
                    ann, DYNAMIC, "argument annotation cannot be a primitive"
                )
                arg_type = self.cur_mod.resolve_annotation(ann) or DYNAMIC_TYPE
            else:
                arg_type = DYNAMIC_TYPE

            if default_index >= 0:
                default = args.kw_defaults[default_index]
                if default is not None:
                    self.check_can_assign_from(
                        arg_type,
                        self.get_type(default).klass,
                        default,
                    )
            default_index += 1
            self.set_param(arg, arg_type, scope)

        kwarg = args.kwarg
        if kwarg:
            ann = kwarg.annotation
            if ann:
                self.visitExpectedType(
                    ann, DYNAMIC, "argument annotation cannot be a primitive"
                )
            self.set_param(kwarg, DICT_EXACT_TYPE, scope)

    def _visitFunc(self, node: Union[FunctionDef, AsyncFunctionDef]) -> None:
        scope = BindingScope(node, generic_types=self.symtable.generic_types)
        for decorator in node.decorator_list:
            self.visitExpectedType(
                decorator, DYNAMIC, "decorator cannot be a primitive"
            )
        cur_scope = self.scope

        if (
            not node.decorator_list
            and isinstance(cur_scope, ClassDef)
            and node.args.args
        ):
            # Handle type of "self"
            klass = self.cur_mod.resolve_name(cur_scope.name)
            if isinstance(klass, Class):
                self.set_param(node.args.args[0], klass, scope)
            else:
                self.set_param(node.args.args[0], DYNAMIC_TYPE, scope)

        self._visitParameters(node.args, scope)

        returns = None if node.args in self.cur_mod.dynamic_returns else node.returns
        if returns:
            # We store the return type on the node for the function as we otherwise
            # don't need to store type information for it
            expected = self.cur_mod.resolve_annotation(returns) or DYNAMIC_TYPE
            self.set_type(node, expected.instance)
            self.visitExpectedType(
                returns, DYNAMIC, "return annotation cannot be a primitive"
            )
        else:
            self.set_type(node, DYNAMIC)

        self.scopes.append(scope)

        for stmt in node.body:
            self.visit(stmt)

        self.scopes.pop()

    def visitFunctionDef(self, node: FunctionDef) -> None:
        self._visitFunc(node)

    def visitAsyncFunctionDef(self, node: AsyncFunctionDef) -> None:
        self._visitFunc(node)

    def visitClassDef(self, node: ClassDef) -> None:
        parent_scope = self.scope
        if isinstance(parent_scope, (FunctionDef, AsyncFunctionDef)):
            self.syntax_error(
                f"Cannot declare class `{node.name}` inside a function, `{parent_scope.name}`",
                node,
            )

        for decorator in node.decorator_list:
            self.visitExpectedType(
                decorator, DYNAMIC, "decorator cannot be a primitive"
            )

        for kwarg in node.keywords:
            self.visitExpectedType(
                kwarg.value, DYNAMIC, "class kwarg cannot be a primitive"
            )

        for base in node.bases:
            self.visitExpectedType(base, DYNAMIC, "class base cannot be a primitive")

        self.scopes.append(
            BindingScope(node, generic_types=self.symtable.generic_types)
        )

        for stmt in node.body:
            self.visit(stmt)

        self.scopes.pop()

    def set_type(
        self,
        node: AST,
        type: Value,
    ) -> None:
        self.cur_mod.types[node] = type

    def get_type(self, node: AST) -> Value:
        assert node in self.cur_mod.types, f"node not found: {node}, {node.lineno}"
        return self.cur_mod.types[node]

    def get_node_data(
        self, key: Union[AST, Delegator], data_type: Type[TType]
    ) -> TType:
        return cast(TType, self.cur_mod.node_data[key, data_type])

    def set_node_data(
        self, key: Union[AST, Delegator], data_type: Type[TType], value: TType
    ) -> None:
        self.cur_mod.node_data[key, data_type] = value

    def check_primitive_scope(self, node: Name) -> None:
        cur_scope = self.symbols.scopes[self.scope]
        var_scope = cur_scope.check_name(node.id)
        if var_scope != SC_LOCAL or isinstance(self.scope, Module):
            self.syntax_error("cannot use primitives in global or closure scope", node)

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
            klass = self.get_type(target.value).klass
            member_name = target.attr
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
            )
        ):
            self.syntax_error(
                f"Cannot assign to a Final attribute of {klass.instance.name}:{member_name}",
                target,
            )

    def visitAnnAssign(self, node: AnnAssign) -> None:
        self.visitExpectedType(
            node.annotation, DYNAMIC, "annotation can not be a primitive value"
        )

        target = node.target
        comp_type = (
            self.cur_mod.resolve_annotation(node.annotation, is_declaration=True)
            or DYNAMIC_TYPE
        )
        is_final = False
        if isinstance(comp_type, ClassVar):
            if not isinstance(self.scope, ClassDef):
                self.syntax_error(
                    "ClassVar is allowed only in class attribute annotations.", node
                )
            comp_type = comp_type.inner_type()
        if isinstance(comp_type, FinalClass):
            is_final = True
            comp_type = comp_type.inner_type()

        if isinstance(target, Name):
            self.declare_local(target, comp_type.instance, is_final)
            self.set_type(target, comp_type.instance)

        self.visit(target)
        value = node.value
        if value:
            self.visitExpectedType(value, comp_type.instance)
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
            src is not DYNAMIC_TYPE or isinstance(dest, CType)
        ):
            self.syntax_error(
                reason.format(src.instance.name, dest.instance.name),
                node,
            )

    def visitAssert(self, node: ast.Assert) -> None:
        effect = self.visit(node.test) or NO_EFFECT
        effect.apply(self.local_types)
        self.set_node_data(node, NarrowingEffect, effect)
        message = node.msg
        if message:
            self.visitExpectedType(
                message, DYNAMIC, "assert message cannot be a primitive"
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

                final_type = self.widen(final_type, self.get_type(value))
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

        self.set_type(node, final_type or DYNAMIC)
        return effect

    def visitBinOp(
        self, node: BinOp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit(node.left, type_ctx)
        self.visit(node.right, type_ctx)

        ltype = self.get_type(node.left)
        rtype = self.get_type(node.right)

        tried_right = False
        if ltype.klass in rtype.klass.mro[1:]:
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
        scope = BindingScope(node, generic_types=self.symtable.generic_types)
        self._visitParameters(node.args, scope)

        self.scopes.append(scope)
        self.visitExpectedType(
            node.body, DYNAMIC, "lambda cannot return primitive value"
        )
        self.scopes.pop()

        self.set_type(node, DYNAMIC)
        return NO_EFFECT

    def visitIfExp(
        self, node: IfExp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        effect = self.visit(node.test) or NO_EFFECT
        effect.apply(self.local_types)
        self.visit(node.body)
        effect.reverse(self.local_types)
        self.visit(node.orelse)
        effect.undo(self.local_types)

        # Select the most compatible types that we can, or fallback to
        # dynamic if we can coerce to dynamic, otherwise report an error.
        body_t = self.get_type(node.body)
        else_t = self.get_type(node.orelse)
        if body_t.klass.can_assign_from(else_t.klass):
            self.set_type(node, body_t)
        elif else_t.klass.can_assign_from(body_t.klass):
            self.set_type(node, else_t)
        elif DYNAMIC_TYPE.can_assign_from(
            body_t.klass
        ) and DYNAMIC_TYPE.can_assign_from(else_t.klass):
            self.set_type(node, DYNAMIC)
        else:
            self.syntax_error(
                f"if expression has incompatible types: {body_t.name} and {else_t.name}",
                node,
            )
        return NO_EFFECT

    def visitSlice(
        self, node: Slice, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        lower = node.lower
        if lower:
            self.visitExpectedType(lower, DYNAMIC, "slice indices cannot be primitives")
        upper = node.upper
        if upper:
            self.visitExpectedType(upper, DYNAMIC, "slice indices cannot be primitives")
        step = node.step
        if step:
            self.visitExpectedType(step, DYNAMIC, "slice indices cannot be primitives")
        self.set_type(node, SLICE_TYPE.instance)
        return NO_EFFECT

    def widen(self, existing: Optional[Value], new: Value) -> Value:
        if existing is None or new.klass.can_assign_from(existing.klass):
            return new
        elif existing.klass.can_assign_from(new.klass):
            return existing

        res = UNION_TYPE.make_generic_type(
            (existing.klass, new.klass), self.symtable.generic_types
        ).instance
        return res

    def visitDict(
        self, node: ast.Dict, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        key_type: Optional[Value] = None
        value_type: Optional[Value] = None
        for k, v in zip(node.keys, node.values):
            if k:
                self.visitExpectedType(k, DYNAMIC, "dict keys cannot be primitives")
                key_type = self.widen(key_type, self.get_type(k))
                self.visitExpectedType(v, DYNAMIC, "dict keys cannot be primitives")
                value_type = self.widen(value_type, self.get_type(v))
            else:
                self.visitExpectedType(v, DYNAMIC, "dict splat cannot be a primitive")
                d_type = self.get_type(v).klass
                if (
                    d_type.generic_type_def is CHECKED_DICT_TYPE
                    or d_type.generic_type_def is CHECKED_DICT_EXACT_TYPE
                ):
                    assert isinstance(d_type, GenericClass)
                    key_type = self.widen(key_type, d_type.type_args[0].instance)
                    value_type = self.widen(value_type, d_type.type_args[1].instance)
                elif d_type in (DICT_TYPE, DICT_EXACT_TYPE, DYNAMIC_TYPE):
                    key_type = DYNAMIC
                    value_type = DYNAMIC

        self.set_dict_type(node, key_type, value_type, type_ctx, is_exact=True)
        return NO_EFFECT

    def set_dict_type(
        self,
        node: ast.expr,
        key_type: Optional[Value],
        value_type: Optional[Value],
        type_ctx: Optional[Class],
        is_exact: bool = False,
    ) -> Value:
        if self.cur_mod.nonchecked_dicts or not isinstance(
            type_ctx, CheckedDictInstance
        ):
            # This is not a checked dict, or the user opted out of checked dicts
            if type_ctx in (DICT_TYPE.instance, DICT_EXACT_TYPE.instance):
                typ = type_ctx
            elif is_exact:
                typ = DICT_EXACT_TYPE.instance
            else:
                typ = DICT_TYPE.instance
            assert typ is not None
            self.set_type(node, typ)
            return typ

        # Calculate the type that is inferred by the keys and values
        assert type_ctx is not None
        type_class = type_ctx.klass
        assert type_class.generic_type_def in (
            CHECKED_DICT_EXACT_TYPE,
            CHECKED_DICT_TYPE,
        )
        assert isinstance(type_class, GenericClass)
        if key_type is None:
            key_type = type_class.type_args[0].instance

        if value_type is None:
            value_type = type_class.type_args[1].instance

        checked_dict_typ = CHECKED_DICT_EXACT_TYPE if is_exact else CHECKED_DICT_TYPE

        gen_type = checked_dict_typ.make_generic_type(
            (key_type.klass, value_type.klass), self.symtable.generic_types
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

    def visitSet(
        self, node: ast.Set, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        for elt in node.elts:
            self.visitExpectedType(elt, DYNAMIC, "set members cannot be primitives")
        self.set_type(node, SET_EXACT_TYPE.instance)
        return NO_EFFECT

    def visitGeneratorExp(
        self, node: GeneratorExp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit_comprehension(node, node.generators, node.elt)
        self.set_type(node, DYNAMIC)
        return NO_EFFECT

    def visitListComp(
        self, node: ListComp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit_comprehension(node, node.generators, node.elt)
        self.set_type(node, LIST_EXACT_TYPE.instance)
        return NO_EFFECT

    def visitSetComp(
        self, node: SetComp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit_comprehension(node, node.generators, node.elt)
        self.set_type(node, SET_EXACT_TYPE.instance)
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
                self.declare_local(target, value, is_inferred=True)
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
                        self.assign_value(target, CONSTANT_TYPES[type(inner_value)])
                else:
                    for val in target.elts:
                        self.assign_value(val, DYNAMIC)
            else:
                for val in target.elts:
                    self.assign_value(val, DYNAMIC)
        else:
            self.check_can_assign_from(self.get_type(target).klass, value.klass, target)
        self._check_final_attribute_reassigned(target, assignment)

    def visitDictComp(
        self, node: DictComp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit(node.generators[0].iter)

        scope = BindingScope(node, generic_types=self.symtable.generic_types)
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
            node.key, DYNAMIC, "dictionary comprehension key cannot be a primitive"
        )
        self.visitExpectedType(
            node.value, DYNAMIC, "dictionary comprehension value cannot be a primitive"
        )

        self.scopes.pop()

        key_type = self.get_type(node.key)
        value_type = self.get_type(node.value)
        self.set_dict_type(node, key_type, value_type, type_ctx, is_exact=True)

        return NO_EFFECT

    def visit_comprehension(
        self, node: ast.expr, generators: List[ast.comprehension], *elts: ast.expr
    ) -> None:
        self.visit(generators[0].iter)

        scope = BindingScope(node, generic_types=self.symtable.generic_types)
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

        for elt in elts:
            self.visitExpectedType(
                elt, DYNAMIC, "generator element cannot be a primitive"
            )

        self.scopes.pop()

    def visitAwait(
        self, node: Await, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visitExpectedType(node.value, DYNAMIC, "cannot await a primitive value")
        self.set_type(node, DYNAMIC)
        return NO_EFFECT

    def visitYield(
        self, node: Yield, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        value = node.value
        if value is not None:
            self.visitExpectedType(value, DYNAMIC, "cannot yield a primitive value")
        self.set_type(node, DYNAMIC)
        return NO_EFFECT

    def visitYieldFrom(
        self, node: YieldFrom, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visitExpectedType(
            node.value, DYNAMIC, "cannot yield from a primitive value"
        )
        self.set_type(node, DYNAMIC)
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

            self.set_type(node, BOOL_TYPE.instance)
            self.set_type(node.ops[0], BOOL_TYPE.instance)

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
                    effect = IsInstanceEffect(other, var_type, NONE_TYPE.instance, self)
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
            if ltype.klass in rtype.klass.mro[1:]:
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
        result = self.get_type(node.func).bind_call(node, self, type_ctx)
        return result

    def visitFormattedValue(
        self, node: FormattedValue, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visitExpectedType(
            node.value, DYNAMIC, "cannot use primitive in formatted value"
        )
        self.set_type(node, DYNAMIC)
        return NO_EFFECT

    def visitJoinedStr(
        self, node: JoinedStr, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        for value in node.values:
            self.visit(value)

        self.set_type(node, STR_EXACT_TYPE.instance)
        return NO_EFFECT

    def visitConstant(
        self, node: Constant, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        if type_ctx is not None:
            type_ctx.bind_constant(node, self)
        else:
            DYNAMIC.bind_constant(node, self)
        return NO_EFFECT

    def visitAttribute(
        self, node: Attribute, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit(node.value)
        self.get_type(node.value).bind_attr(node, self, type_ctx)
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
            node.value, DYNAMIC, "cannot use primitive in starred expression"
        )
        self.set_type(node, DYNAMIC)
        return NO_EFFECT

    def visitName(
        self, node: Name, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        cur_scope = self.symbols.scopes[self.scope]
        scope = cur_scope.check_name(node.id)
        if scope == SC_LOCAL and not isinstance(self.scope, Module):
            var_type = self.local_types.get(node.id, DYNAMIC)
            self.set_type(node, var_type)
        else:
            self.set_type(node, self.cur_mod.resolve_name(node.id) or DYNAMIC)

        type = self.get_type(node)
        if (
            isinstance(type, UnionInstance)
            and not type.klass.is_generic_type_definition
        ):
            effect = IsInstanceEffect(node, type, NONE_TYPE.instance, self)
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
        for elt in node.elts:
            self.visitExpectedType(elt, DYNAMIC)
        self.set_type(node, LIST_EXACT_TYPE.instance)
        return NO_EFFECT

    def visitTuple(
        self, node: ast.Tuple, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        for elt in node.elts:
            self.visitExpectedType(elt, DYNAMIC)
        self.set_type(node, TUPLE_EXACT_TYPE.instance)
        return NO_EFFECT

    def set_terminal_kind(self, node: AST, level: TerminalKind) -> None:
        current = self.terminals.get(node, TerminalKind.NonTerminal)
        if current < level:
            self.terminals[node] = level

    def visitContinue(self, node: ast.Continue) -> None:
        self.set_terminal_kind(node, TerminalKind.BreakOrContinue)

    def visitBreak(self, node: ast.Break) -> None:
        self.set_terminal_kind(node, TerminalKind.BreakOrContinue)

    def visitReturn(self, node: Return) -> None:
        self.set_terminal_kind(node, TerminalKind.Return)
        value = node.value
        if value is not None:
            cur_scope = self.binding_scope
            func = cur_scope.node
            expected = DYNAMIC
            if isinstance(func, (ast.FunctionDef, ast.AsyncFunctionDef)):
                func_returns = func.returns
                if func_returns:
                    expected = (
                        self.cur_mod.resolve_annotation(func_returns) or DYNAMIC_TYPE
                    ).instance

            self.visit(value, expected)
            returned = self.get_type(value).klass
            if returned is not DYNAMIC_TYPE and not expected.klass.can_assign_from(
                returned
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
                elif name not in self.symtable.statics.children:
                    self.syntax_error(f"unsupported static import {name}", node)

    def visit_until_terminates(self, nodes: List[ast.stmt]) -> TerminalKind:
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
                if handler_type is DYNAMIC or not isinstance(handler_type, Class):
                    handler_type = DYNAMIC_TYPE

                decl_type = self.decl_types.get(hname)
                if decl_type and decl_type.is_final:
                    self.syntax_error("Cannot assign to a Final variable", node)

                self.binding_scope.declare(hname, handler_type.instance)

        self.visit(node.body)
        if hname is not None:
            del self.decl_types[hname]
            del self.local_types[hname]

    def visitWhile(self, node: While) -> None:
        branch = self.scopes[-1].branch()

        effect = self.visit(node.test) or NO_EFFECT
        effect.apply(self.local_types)

        while_returns = self.visit_until_terminates(node.body) == TerminalKind.Return
        if while_returns:
            branch.restore()
            effect.reverse(self.local_types)
        else:
            branch.merge(effect.reverse(branch.entry_locals))

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
        self.visit(node.body)
        self.visit(node.orelse)

    def visitwithitem(self, node: ast.withitem) -> None:
        self.visit(node.context_expr)
        optional_vars = node.optional_vars
        if optional_vars:
            self.visit(optional_vars)
            self.assign_value(optional_vars, DYNAMIC)

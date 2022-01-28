# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
from __future__ import annotations

import ast
from ast import (
    AST,
    AnnAssign,
    Assign,
    AsyncFunctionDef,
    Attribute,
    Bytes,
    Call,
    ClassDef,
    Constant,
    FunctionDef,
    NameConstant,
    Num,
    Return,
    Starred,
    Str,
    cmpop,
    expr,
    copy_location,
)
from enum import Enum
from functools import cached_property
from types import (
    BuiltinFunctionType,
    MethodDescriptorType,
    WrapperDescriptorType,
)
from typing import (
    Callable as typingCallable,
    ClassVar as typingClassVar,
    Collection,
    Dict,
    Generic,
    Iterable,
    List,
    Mapping,
    Optional,
    Sequence,
    Set,
    TYPE_CHECKING,
    Tuple,
    Type,
    TypeVar,
    Union,
    cast,
)

from __static__ import chkdict, chklist
from _static import (  # noqa: F401
    TYPED_BOOL,
    TYPED_INT_64BIT,
    TYPED_OBJECT,
    TYPED_INT8,
    TYPED_INT16,
    TYPED_INT32,
    TYPED_INT64,
    TYPED_UINT8,
    TYPED_UINT16,
    TYPED_UINT32,
    TYPED_UINT64,
    SEQ_CHECKED_LIST,
    SEQ_LIST,
    SEQ_TUPLE,
    SEQ_LIST_INEXACT,
    SEQ_ARRAY_INT8,
    SEQ_ARRAY_INT16,
    SEQ_ARRAY_INT32,
    SEQ_ARRAY_INT64,
    SEQ_ARRAY_UINT8,
    SEQ_ARRAY_UINT16,
    SEQ_ARRAY_UINT32,
    SEQ_ARRAY_UINT64,
    SEQ_SUBSCR_UNCHECKED,
    SEQ_REPEAT_INEXACT_SEQ,
    SEQ_REPEAT_INEXACT_NUM,
    SEQ_REPEAT_REVERSED,
    SEQ_REPEAT_PRIMITIVE_NUM,
    PRIM_OP_EQ_INT,
    PRIM_OP_NE_INT,
    PRIM_OP_LT_INT,
    PRIM_OP_LE_INT,
    PRIM_OP_GT_INT,
    PRIM_OP_GE_INT,
    PRIM_OP_LT_UN_INT,
    PRIM_OP_LE_UN_INT,
    PRIM_OP_GT_UN_INT,
    PRIM_OP_GE_UN_INT,
    PRIM_OP_EQ_DBL,
    PRIM_OP_NE_DBL,
    PRIM_OP_LT_DBL,
    PRIM_OP_LE_DBL,
    PRIM_OP_GT_DBL,
    PRIM_OP_GE_DBL,
    PRIM_OP_ADD_INT,
    PRIM_OP_SUB_INT,
    PRIM_OP_MUL_INT,
    PRIM_OP_DIV_INT,
    PRIM_OP_DIV_UN_INT,
    PRIM_OP_MOD_INT,
    PRIM_OP_MOD_UN_INT,
    PRIM_OP_POW_INT,
    PRIM_OP_POW_UN_INT,
    PRIM_OP_LSHIFT_INT,
    PRIM_OP_RSHIFT_INT,
    PRIM_OP_RSHIFT_UN_INT,
    PRIM_OP_XOR_INT,
    PRIM_OP_OR_INT,
    PRIM_OP_AND_INT,
    PRIM_OP_NEG_INT,
    PRIM_OP_INV_INT,
    PRIM_OP_NEG_DBL,
    PRIM_OP_NOT_INT,
    PRIM_OP_ADD_DBL,
    PRIM_OP_SUB_DBL,
    PRIM_OP_MUL_DBL,
    PRIM_OP_DIV_DBL,
    PRIM_OP_MOD_DBL,
    PRIM_OP_POW_DBL,
    FAST_LEN_INEXACT,
    FAST_LEN_LIST,
    FAST_LEN_DICT,
    FAST_LEN_SET,
    FAST_LEN_TUPLE,
    FAST_LEN_ARRAY,
    FAST_LEN_STR,
    TYPED_DOUBLE,
)

from ..errors import TypedSyntaxError
from ..optimizer import AstOptimizer
from ..pyassem import Block
from ..pycodegen import FOR_LOOP, CodeGenerator
from ..unparse import to_expr
from ..visitor import ASTRewriter, TAst
from .effects import NarrowingEffect, NO_EFFECT
from .type_env import GenericTypeIndex, TypeEnvironment
from .visitor import GenericVisitor

if TYPE_CHECKING:
    from . import Static38CodeGenerator
    from .compiler import Compiler
    from .module_table import AnnotationVisitor, ReferenceVisitor, ModuleTable
    from .type_binder import BindingScope, TypeBinder

try:
    # pyre-ignore[21]: unknown module
    from xxclassloader import spamobj
except ImportError:
    spamobj = None


CACHED_PROPERTY_IMPL_PREFIX = "_pystatic_cprop."
ASYNC_CACHED_PROPERTY_IMPL_PREFIX = "_pystatic_async_cprop."


CBOOL_TYPE: CIntType
ENUM_TYPE: CEnumType
INT8_TYPE: CIntType
INT16_TYPE: CIntType
INT32_TYPE: CIntType
INT64_TYPE: CIntType
INT64_VALUE: CIntInstance
SIGNED_CINT_TYPES: Sequence[CIntType]
INT_TYPE: NumClass
INT_EXACT_TYPE: NumClass
FLOAT_TYPE: NumClass
COMPLEX_TYPE: NumClass
BOOL_TYPE: Class
ARRAY_TYPE: Class
DICT_TYPE: Class
LIST_TYPE: Class
TUPLE_TYPE: Class
SET_TYPE: Class

OBJECT_TYPE: Class
OBJECT: Value

DYNAMIC_TYPE: DynamicClass
DYNAMIC: DynamicInstance
FUNCTION_TYPE: Class
METHOD_TYPE: Class
MEMBER_TYPE: Class
NONE_TYPE: Class
TYPE_TYPE: Class
ARG_TYPE: Class
SLICE_TYPE: Class
CHAR_TYPE: CIntType
DOUBLE_TYPE: CDoubleType

# Prefix for temporary var names. It's illegal in normal
# Python, so there's no chance it will ever clash with a
# user defined name.
_TMP_VAR_PREFIX = "_pystatic_.0._tmp__"

CMPOP_SIGILS: Mapping[Type[cmpop], str] = {
    ast.Lt: "<",
    ast.Gt: ">",
    ast.Eq: "==",
    ast.NotEq: "!=",
    ast.LtE: "<=",
    ast.GtE: ">=",
    ast.Is: "is",
    ast.IsNot: "is",
}


class TypeRef:
    """Stores unresolved typed references, capturing the referring module
    as well as the annotation"""

    def __init__(self, module: ModuleTable, ref: ast.expr) -> None:
        self.module = module
        self.ref = ref

    def resolved(self, is_declaration: bool = False) -> Class:
        res = self.module.resolve_annotation(self.ref, is_declaration=is_declaration)
        if res is None:
            return DYNAMIC_TYPE
        return res

    def __repr__(self) -> str:
        return f"TypeRef({self.module.name}, {ast.dump(self.ref)})"


class ResolvedTypeRef(TypeRef):
    def __init__(self, type: Class) -> None:
        self._resolved = type

    def resolved(self, is_declaration: bool = False) -> Class:
        return self._resolved

    def __repr__(self) -> str:
        return f"ResolvedTypeRef({self.resolved()})"


# Pyre doesn't support recursive generics, so we can't represent the recursively
# nested tuples that make up a type_descr. Fortunately we don't need to, since
# we don't parse them in Python, we just generate them and emit them as
# constants. So just call them `Tuple[object, ...]`
TypeDescr = Tuple[object, ...]


class TypeName:
    def __init__(self, module: str, name: str) -> None:
        self.module = module
        self.name = name

    @property
    def type_descr(self) -> TypeDescr:
        """The metadata emitted into the const pool to describe a type.

        For normal types this is just the fully qualified type name as a tuple
        ('mypackage', 'mymod', 'C'). For optional types we have an extra '?'
        element appended. For generic types we append a tuple of the generic
        args' type_descrs.
        """
        return (self.module, self.name)

    @property
    def friendly_name(self) -> str:
        if self.module and self.module not in ("builtins", "__static__", "typing"):
            return f"{self.module}.{self.name}"
        return self.name


class GenericTypeName(TypeName):
    def __init__(self, module: str, name: str, args: Tuple[Class, ...]) -> None:
        super().__init__(module, name)
        self.args = args

    @property
    def type_descr(self) -> TypeDescr:
        gen_args: List[TypeDescr] = []
        for arg in self.args:
            gen_args.append(arg.type_descr)
        return (self.module, self.name, tuple(gen_args))

    @property
    def friendly_name(self) -> str:
        args = ", ".join(arg.instance.name for arg in self.args)
        return f"{super().friendly_name}[{args}]"


TType = TypeVar("TType")
TClass = TypeVar("TClass", bound="Class", covariant=True)
TClassInv = TypeVar("TClassInv", bound="Class")
CALL_ARGUMENT_CANNOT_BE_PRIMITIVE = "Call argument cannot be a primitive"


class BinOpCommonType:
    def __init__(self, value: Value) -> None:
        self.value = value


class Value:
    """base class for all values tracked at compile time."""

    def __init__(self, klass: Class) -> None:
        """name: the name of the value, for instances this is used solely for
        debug/reporting purposes.  In Class subclasses this will be the
        qualified name (e.g. module.Foo).
        klass: the Class of this object"""
        self.klass = klass

    @property
    def name(self) -> str:
        return type(self).__name__

    def exact(self) -> Value:
        return self

    def inexact(self) -> Value:
        return self

    def finish_bind(self, module: ModuleTable) -> Optional[Value]:
        return self

    def make_generic_type(
        self, index: GenericTypeIndex, type_env: TypeEnvironment
    ) -> Optional[Class]:
        pass

    def get_iter_type(self, node: ast.expr, visitor: TypeBinder) -> Value:
        """returns the type that is produced when iterating over this value"""
        visitor.syntax_error(f"cannot iterate over {self.name}", node)
        return DYNAMIC

    def as_oparg(self) -> int:
        raise TypeError(f"{self.name} not valid here")

    def resolve_attr(
        self, node: ast.Attribute, visitor: ReferenceVisitor
    ) -> Optional[Value]:
        visitor.syntax_error(f"cannot load attribute from {self.name}", node)

    def bind_attr(
        self, node: ast.Attribute, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        visitor.set_type(
            node,
            self.resolve_attr(node, visitor.module.ann_visitor) or DYNAMIC,
        )

    def bind_await(
        self, node: ast.Await, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        visitor.set_type(node, DYNAMIC)

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        visitor.syntax_error(f"cannot call {self.name}", node)
        return NO_EFFECT

    def bind_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> None:
        visitor.syntax_error(f"cannot get descriptor {self.name}", node)

    def resolve_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: ReferenceVisitor,
    ) -> Optional[Value]:
        return self

    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        return None

    def resolve_decorate_class(self, klass: Class) -> Class:
        return DYNAMIC_TYPE

    def bind_subscr(
        self,
        node: ast.Subscript,
        type: Value,
        visitor: TypeBinder,
        type_ctx: Optional[Class] = None,
    ) -> None:
        visitor.check_can_assign_from(DYNAMIC_TYPE, type.klass, node)
        visitor.set_type(
            node,
            self.resolve_subscr(node, type, visitor.module.ann_visitor) or DYNAMIC,
        )

    def resolve_subscr(
        self,
        node: ast.Subscript,
        type: Value,
        visitor: AnnotationVisitor,
    ) -> Optional[Value]:
        visitor.syntax_error(f"cannot index {self.name}", node)

    def emit_subscr(self, node: ast.Subscript, code_gen: Static38CodeGenerator) -> None:
        code_gen.update_lineno(node)
        code_gen.visit(node.value)
        code_gen.visit(node.slice)
        if isinstance(node.ctx, ast.Load):
            return self.emit_load_subscr(node, code_gen)
        elif isinstance(node.ctx, ast.Store):
            return self.emit_store_subscr(node, code_gen)
        else:
            return self.emit_delete_subscr(node, code_gen)

    def emit_load_subscr(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.emit("BINARY_SUBSCR")

    def emit_store_subscr(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.emit("STORE_SUBSCR")

    def emit_delete_subscr(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.emit("DELETE_SUBSCR")

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.defaultVisit(node)

    def emit_delete_attr(
        self, node: ast.Attribute, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.emit("DELETE_ATTR", code_gen.mangle(node.attr))

    def emit_load_attr(
        self, node: ast.Attribute, code_gen: Static38CodeGenerator
    ) -> None:
        member = self.klass.members.get(node.attr, DYNAMIC)
        member.emit_load_attr_from(node, code_gen, self.klass)

    def emit_load_attr_from(
        self, node: Attribute, code_gen: Static38CodeGenerator, klass: Class
    ) -> None:
        code_gen.emit("LOAD_ATTR", code_gen.mangle(node.attr))

    def emit_store_attr(
        self, node: ast.Attribute, code_gen: Static38CodeGenerator
    ) -> None:
        member = self.klass.members.get(node.attr, DYNAMIC)
        member.emit_store_attr_to(node, code_gen, self.klass)

    def emit_store_attr_to(
        self, node: Attribute, code_gen: Static38CodeGenerator, klass: Class
    ) -> None:
        code_gen.emit("STORE_ATTR", code_gen.mangle(node.attr))

    def emit_attr(self, node: ast.Attribute, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(node.value)
        if isinstance(node.ctx, ast.Store):
            self.emit_store_attr(node, code_gen)
        elif isinstance(node.ctx, ast.Del):
            self.emit_delete_attr(node, code_gen)
        else:
            self.emit_load_attr(node, code_gen)

    def bind_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        visitor.syntax_error(f"cannot compare with {self.name}", node)
        return False

    def bind_reverse_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        visitor.syntax_error(f"cannot reverse compare with {self.name}", node)
        return False

    def emit_compare(self, op: cmpop, code_gen: Static38CodeGenerator) -> None:
        code_gen.defaultEmitCompare(op)

    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        visitor.syntax_error(f"cannot bin op with {self.name}", node)
        return False

    def bind_reverse_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        visitor.syntax_error(f"cannot reverse bin op with {self.name}", node)
        return False

    def bind_unaryop(
        self, node: ast.UnaryOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        visitor.syntax_error(f"cannot reverse unary op with {self.name}", node)

    def emit_binop(self, node: ast.BinOp, code_gen: Static38CodeGenerator) -> None:
        code_gen.defaultVisit(node)

    def emit_forloop(self, node: ast.For, code_gen: Static38CodeGenerator) -> None:
        start = code_gen.newBlock("default_forloop_start")
        anchor = code_gen.newBlock("default_forloop_anchor")
        after = code_gen.newBlock("default_forloop_after")

        code_gen.set_lineno(node)
        code_gen.push_loop(FOR_LOOP, start, after)
        code_gen.visit(node.iter)
        code_gen.emit("GET_ITER")

        code_gen.nextBlock(start)
        code_gen.emit("FOR_ITER", anchor)
        code_gen.visit(node.target)
        code_gen.visit(node.body)
        code_gen.emit("JUMP_ABSOLUTE", start)
        code_gen.nextBlock(anchor)
        code_gen.pop_loop()

        if node.orelse:
            code_gen.visit(node.orelse)
        code_gen.nextBlock(after)

    def emit_unaryop(self, node: ast.UnaryOp, code_gen: Static38CodeGenerator) -> None:
        code_gen.defaultVisit(node)

    def emit_aug_rhs(
        self, node: ast.AugAssign, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.defaultCall(node, "emitAugRHS")

    def bind_constant(self, node: ast.Constant, visitor: TypeBinder) -> None:
        visitor.syntax_error(f"cannot constant with {self.name}", node)

    def emit_constant(
        self, node: ast.Constant, code_gen: Static38CodeGenerator
    ) -> None:
        return code_gen.defaultVisit(node)

    def emit_name(self, node: ast.Name, code_gen: Static38CodeGenerator) -> None:
        if isinstance(node.ctx, ast.Load):
            return self.emit_load_name(node, code_gen)
        elif isinstance(node.ctx, ast.Store):
            return self.emit_store_name(node, code_gen)
        else:
            return self.emit_delete_name(node, code_gen)

    def emit_load_name(self, node: ast.Name, code_gen: Static38CodeGenerator) -> None:
        code_gen.loadName(node.id)

    def emit_store_name(self, node: ast.Name, code_gen: Static38CodeGenerator) -> None:
        code_gen.storeName(node.id)

    def emit_delete_name(self, node: ast.Name, code_gen: Static38CodeGenerator) -> None:
        code_gen.delName(node.id)

    def emit_jumpif(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.visit(test)
        self.emit_jumpif_only(next, is_if_true, code_gen)

    def emit_jumpif_only(
        self, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.emit("POP_JUMP_IF_TRUE" if is_if_true else "POP_JUMP_IF_FALSE", next)

    def emit_jumpif_pop(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.visit(test)
        self.emit_jumpif_pop_only(next, is_if_true, code_gen)

    def emit_jumpif_pop_only(
        self, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.emit(
            "JUMP_IF_TRUE_OR_POP" if is_if_true else "JUMP_IF_FALSE_OR_POP", next
        )

    def emit_box(self, node: expr, code_gen: Static38CodeGenerator) -> None:
        raise RuntimeError(f"Unsupported box type: {code_gen.get_type(node)}")

    def emit_unbox(self, node: expr, code_gen: Static38CodeGenerator) -> None:
        raise RuntimeError("Unsupported unbox type")

    def get_fast_len_type(self) -> Optional[int]:
        return None

    def emit_len(
        self, node: ast.Call, code_gen: Static38CodeGenerator, boxed: bool
    ) -> None:
        if not boxed:
            raise RuntimeError("Unsupported type for clen()")
        return self.emit_call(node, code_gen)

    def make_generic(
        self, new_type: Class, name: GenericTypeName, type_env: TypeEnvironment
    ) -> Value:
        return self

    def make_literal(self, literal_value: object) -> Value:
        raise NotImplementedError(f"Type {self.name} does not support literals")

    def emit_convert(self, from_type: Value, code_gen: Static38CodeGenerator) -> None:
        pass


def resolve_attr_instance(
    node: ast.Attribute,
    inst: Object[TClassInv],
    klass: TClassInv,
    visitor: ReferenceVisitor,
) -> Optional[Value]:
    for base in klass.mro:
        member = base.members.get(node.attr)
        if member is not None:
            res = member.resolve_descr_get(node, inst, klass, visitor)
            if res is not None:
                return res

    if node.attr == "__class__":
        return klass
    else:
        return DYNAMIC


class Object(Value, Generic[TClass]):
    """Represents an instance of a type at compile time"""

    klass: TClass

    @property
    def name(self) -> str:
        return self.klass.instance_name

    def as_oparg(self) -> int:
        return TYPED_OBJECT

    @staticmethod
    def bind_dynamic_call(node: ast.Call, visitor: TypeBinder) -> NarrowingEffect:
        visitor.set_type(node, DYNAMIC)
        for arg in node.args:
            visitor.visitExpectedType(arg, DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE)

        for arg in node.keywords:
            visitor.visitExpectedType(
                arg.value, DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE
            )

        return NO_EFFECT

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        return self.bind_dynamic_call(node, visitor)

    def resolve_attr(
        self, node: ast.Attribute, visitor: ReferenceVisitor
    ) -> Optional[Value]:
        return resolve_attr_instance(node, self, self.klass, visitor)

    def emit_delete_attr(
        self, node: ast.Attribute, code_gen: Static38CodeGenerator
    ) -> None:
        if self.klass.find_slot(node):
            code_gen.emit("DELETE_ATTR", node.attr)
            return

        super().emit_delete_attr(node, code_gen)

    def emit_load_attr(
        self, node: ast.Attribute, code_gen: Static38CodeGenerator
    ) -> None:
        if member := self.klass.find_slot(node):
            member.emit_load_from_slot(code_gen)
            return

        super().emit_load_attr(node, code_gen)

    def emit_store_attr(
        self, node: ast.Attribute, code_gen: Static38CodeGenerator
    ) -> None:
        if member := self.klass.find_slot(node):
            member.emit_store_to_slot(code_gen)
            return

        super().emit_store_attr(node, code_gen)

    def bind_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> None:
        visitor.set_type(
            node,
            self.resolve_descr_get(node, inst, ctx, visitor.module.ann_visitor)
            or DYNAMIC,
        )

    def resolve_subscr(
        self,
        node: ast.Subscript,
        type: Value,
        visitor: AnnotationVisitor,
    ) -> Optional[Value]:
        return None

    def bind_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        return False

    def bind_reverse_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        visitor.set_type(op, DYNAMIC)
        if isinstance(op, (ast.Is, ast.IsNot, ast.In, ast.NotIn)):
            visitor.set_type(node, BOOL_TYPE.instance)
            return True
        visitor.set_type(node, DYNAMIC)
        return False

    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        return False

    def bind_reverse_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        # we'll set the type in case we're the only one called
        visitor.set_type(node, DYNAMIC)
        return False

    def bind_unaryop(
        self, node: ast.UnaryOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        if isinstance(node.op, ast.Not):
            visitor.set_type(node, BOOL_TYPE.instance)
        else:
            visitor.set_type(node, DYNAMIC)

    def bind_constant(self, node: ast.Constant, visitor: TypeBinder) -> None:
        if type(node.value) is int:
            node_type = visitor.compiler.get_literal_type(INT_TYPE.instance, node.value)
        else:
            node_type = CONSTANT_TYPES[type(node.value)]
        visitor.set_type(node, node_type)

    def get_iter_type(self, node: ast.expr, visitor: TypeBinder) -> Value:
        """returns the type that is produced when iterating over this value"""
        return DYNAMIC

    def __repr__(self) -> str:
        return f"<{self.name}>"


class ClassCallInfo:
    def __init__(
        self, new: Optional[ArgMapping], init: Optional[ArgMapping], dynamic_call: bool
    ) -> None:
        self.new = new
        self.init = init
        self.dynamic_call = dynamic_call


class InitVisitor(GenericVisitor[None]):
    def __init__(
        self,
        module: ModuleTable,
        klass: Class,
        init_func: FunctionDef,
    ) -> None:
        super().__init__(module)
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
                    target,
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
                self.klass.define_slot(attr, target, assignment=node)


class FunctionGroup(Value):
    """Represents a group of functions defined in a function with
    the same name.  This object is ephemeral and is removed when we
    finish the bind of a class.  Common scenarios where this occur are the
    usage of the the ".setter" syntax for properties, or the @overload
    decorator"""

    def __init__(self, functions: List[Function]) -> None:
        super().__init__(FUNCTION_TYPE)
        self.functions = functions

    def finish_bind(self, module: ModuleTable) -> Optional[Value]:
        known_funcs = []
        for func in self.functions:
            new_func = module.finish_decorator(func.node, func)
            if new_func is not None:
                known_funcs.append(new_func)

        if known_funcs and len(known_funcs) > 1:
            with module.error_context(known_funcs[1].node):
                raise TypedSyntaxError(
                    f"function '{known_funcs[1].name}' conflicts with other member"
                )
        elif not known_funcs:
            return None

        return known_funcs[0]


class Class(Object["Class"]):
    """Represents a type object at compile time"""

    suppress_exact = False

    def __init__(
        self,
        type_name: TypeName,
        bases: Optional[List[Class]] = None,
        instance: Optional[Value] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        is_exact: bool = False,
        pytype: Optional[Type[object]] = None,
        is_final: bool = False,
    ) -> None:
        super().__init__(klass or TYPE_TYPE)
        assert isinstance(bases, (type(None), list))
        self.type_name = type_name
        self.instance: Value = instance or Object(self)
        self.bases: List[Class] = self._get_bases(bases)
        self._mro: Optional[List[Class]] = None
        self._mro_inexact: Optional[Set[Class]] = None
        # members are attributes or methods
        self.members: Dict[str, Value] = members or {}
        self.is_exact = is_exact
        self.is_final = is_final
        self.allow_weakrefs = False
        self.donotcompile = False
        # This will cause all built-in method calls on the type to be done dynamically
        self.dynamic_builtinmethod_dispatch = False
        if pytype:
            self.make_type_dict(pytype)
        self.pytype = pytype
        # track AST node of each member until finish_bind, for error reporting
        self._member_nodes: Dict[str, AST] = {}

    def _get_bases(self, bases: Optional[List[Class]]) -> List[Class]:
        if bases is None:
            return [OBJECT_TYPE]
        ret = []
        for b in bases:
            ret.append(b)
            # Can't check for DYNAMIC_TYPE because that'd be a cyclic dependency
            if isinstance(b, DynamicClass):
                # If any of the defined bases is dynamic,
                # stop processing, because it doesn't matter
                # what the rest of them are.
                break
        return ret

    def make_type_dict(self, t: Type[object]) -> None:
        result: Dict[str, Value] = {}
        for k in t.__dict__.keys():
            try:
                obj = getattr(t, k)
            except AttributeError:
                continue

            if isinstance(obj, (MethodDescriptorType, WrapperDescriptorType)):
                result[k] = reflect_method_desc(obj, self)
            elif isinstance(obj, BuiltinFunctionType):
                result[k] = reflect_builtin_function(obj, self)

        self.members.update(result)

    def make_subclass(self, name: TypeName, bases: List[Class]) -> Class:
        return Class(name, bases)

    @property
    def name(self) -> str:
        return f"Type[{self.instance_name}]"

    @property
    def instance_name(self) -> str:
        name = self.qualname
        if self.is_exact and not self.suppress_exact:
            name = f"Exact[{name}]"
        return name

    def declare_class(self, node: ClassDef, klass: Class) -> None:
        self._member_nodes[node.name] = node
        self.members[node.name] = klass

    def declare_variable(self, node: AnnAssign, module: ModuleTable) -> None:
        # class C:
        #    x: foo
        target = node.target
        if isinstance(target, ast.Name):
            self.define_slot(
                target.id,
                target,
                TypeRef(module, node.annotation),
                # Note down whether the slot has been assigned a value.
                assignment=node if node.value else None,
                declared_on_class=True,
            )

    def declare_variables(self, node: Assign, module: ModuleTable) -> None:
        pass

    def resolve_name(self, name: str) -> Optional[Value]:
        return self.members.get(name)

    @property
    def qualname(self) -> str:
        return self.type_name.friendly_name

    @property
    def is_generic_parameter(self) -> bool:
        """Returns True if this Class represents a generic parameter"""
        return False

    @property
    def contains_generic_parameters(self) -> bool:
        """Returns True if this class contains any generic parameters"""
        return False

    @property
    def is_generic_type(self) -> bool:
        """Returns True if this class is a generic type"""
        return False

    @property
    def is_generic_type_definition(self) -> bool:
        """Returns True if this class is a generic type definition.
        It'll be a generic type which still has unbound generic type
        parameters"""
        return False

    @property
    def generic_type_def(self) -> Optional[Class]:
        """Gets the generic type definition that defined this class"""
        return None

    def make_generic_type(
        self,
        index: Tuple[Class, ...],
        type_env: TypeEnvironment,
    ) -> Optional[Class]:
        """Binds the generic type parameters to a generic type definition"""
        return None

    def resolve_attr(
        self, node: ast.Attribute, visitor: ReferenceVisitor
    ) -> Optional[Value]:
        for base in self.mro:
            member = base.members.get(node.attr)
            if member is not None:
                res = member.resolve_descr_get(node, None, self, visitor)
                if res is not None:
                    return res

        return super().resolve_attr(node, visitor)

    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        if isinstance(node.op, ast.BitOr):
            rtype = visitor.get_type(node.right)
            if rtype is NONE_TYPE.instance:
                rtype = NONE_TYPE
            if rtype is DYNAMIC:
                rtype = DYNAMIC_TYPE
            if not isinstance(rtype, Class):
                visitor.syntax_error(
                    f"unsupported operand type(s) for |: {self.name} and {rtype.name}",
                    node,
                )
            union = UNION_TYPE.make_generic_type(
                (self, rtype), visitor.compiler.type_env
            )
            visitor.set_type(node, union)
            return True

        return super().bind_binop(node, visitor, type_ctx)

    @property
    def can_be_narrowed(self) -> bool:
        return True

    @property
    def type_descr(self) -> TypeDescr:
        return self.type_name.type_descr

    def _resolve_dunder(self, name: str) -> Tuple[Class, Optional[Value]]:
        klass = OBJECT_TYPE
        for klass in self.mro:
            if klass is DYNAMIC_TYPE:
                return DYNAMIC_TYPE, None

            if val := klass.members.get(name):
                return klass, val

        assert klass is OBJECT_TYPE
        return OBJECT_TYPE, None

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        self_type = self.instance
        new_mapping: Optional[ArgMapping] = None
        init_mapping: Optional[ArgMapping] = None

        dynamic_call = True

        klass, new = self._resolve_dunder("__new__")
        dynamic_new = klass is DYNAMIC_TYPE
        object_new = klass is OBJECT_TYPE

        if not object_new and isinstance(new, Callable):
            new_mapping, self_type = new.map_call(
                node,
                visitor,
                None,
                [node.func] + node.args,
            )
            if new_mapping.can_call_statically():
                dynamic_call = False
            else:
                dynamic_new = True

        object_init = False

        # if __new__ returns something that isn't a subclass of
        # our type then __init__ isn't invoked
        if not dynamic_new and self_type.klass.can_assign_from(self.instance.klass):
            klass, init = self._resolve_dunder("__init__")
            dynamic_call = dynamic_call or klass is DYNAMIC_TYPE
            object_init = klass is OBJECT_TYPE
            if not object_init and isinstance(init, Callable):
                init_mapping = ArgMapping(init, node, None)
                init_mapping.bind_args(visitor, True)
                if init_mapping.can_call_statically():
                    dynamic_call = False

        if object_new and object_init:
            if node.args or node.keywords:
                visitor.syntax_error(f"{self.instance_name}() takes no arguments", node)
            else:
                dynamic_call = False

        if new_mapping is not None and init_mapping is not None:
            # If we have both a __new__ and __init__ function we can't currently
            # invoke it statically, as the arguments could have side effects.
            # In the future we could potentially do better by shuffling into
            # temporaries, but this is pretty rare.
            dynamic_call = True

        visitor.set_type(node, self_type)
        visitor.set_node_data(
            node, ClassCallInfo, ClassCallInfo(new_mapping, init_mapping, dynamic_call)
        )

        if dynamic_call:
            for arg in node.args:
                visitor.visitExpectedType(
                    arg, DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE
                )
            for arg in node.keywords:
                visitor.visitExpectedType(
                    arg.value, DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE
                )

        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        self_type = self.instance
        call_info = code_gen.get_node_data(node, ClassCallInfo)

        if call_info.dynamic_call:
            return super().emit_call(node, code_gen)

        new = call_info.new
        if new:
            new.emit(code_gen)
        else:
            code_gen.emit("TP_ALLOC", self.type_descr)

        init = call_info.init
        if init is not None:
            code_gen.emit("DUP_TOP")
            init.emit(code_gen)
            code_gen.emit("POP_TOP")  # pop None

    def can_assign_from(self, src: Class) -> bool:
        """checks to see if the src value can be assigned to this value.  Currently
        you can assign a derived type to a base type.  You cannot assign a primitive
        type to an object type.

        At some point we may also support some form of interfaces via protocols if we
        implement a more efficient form of interface dispatch than doing the dictionary
        lookup for the member."""
        return src is self or (
            not self.is_exact
            and not isinstance(src, CType)
            and src.is_subclass_of(self)
        )

    def __repr__(self) -> str:
        return f"<{self.name} class>"

    def exact_type(self) -> Class:
        return self

    def inexact_type(self) -> Class:
        return self

    def isinstance(self, src: Value) -> bool:
        return src.klass.is_subclass_of(self)

    def is_subclass_of(self, src: Class) -> bool:
        if isinstance(src, UnionType):
            # This is an important subtlety - we want the subtyping relation to satisfy
            # self < A | B if either self < A or self < B. Requiring both wouldn't be correct,
            # as we want to allow assignments of A into A | B.
            return any(self.is_subclass_of(t) for t in src.type_args)
        return src.inexact_type() in self.mro_inexact

    def check_incompatible_override(self, override: Value, inherited: Value) -> None:
        # TODO: There's more checking we should be doing to ensure
        # this is a compatible override
        if isinstance(override, TransparentDecoratedMethod):
            override = override.function

        if isinstance(inherited, TransparentDecoratedMethod):
            inherited = inherited.function

        if isinstance(override, (CachedPropertyMethod, PropertyMethod)) and isinstance(
            inherited, (CachedPropertyMethod, PropertyMethod)
        ):
            # Properties can be overridden by cached properties, and vice-versa. In that case,
            # we just look at whether the underlying functions are compatible
            override = override.function
            inherited = inherited.function

        if type(override) != type(inherited) and (
            type(override) is not Function
            or not isinstance(inherited, (BuiltinFunction, BuiltinMethodDescriptor))
        ):
            raise TypedSyntaxError(f"class cannot hide inherited member: {inherited!r}")
        if isinstance(override, Slot) and isinstance(inherited, Slot):
            # TODO we could allow covariant type overrides for Final attributes
            ot = override.type_ref
            it = inherited.type_ref
            if ot and it and ot.resolved(True) != (itr := it.resolved(True)):
                raise TypedSyntaxError(
                    f"Cannot change type of inherited attribute (inherited type '{itr.instance.name}')"
                )

    def finish_bind(self, module: ModuleTable) -> Optional[Value]:
        to_remove = set()
        for name, my_value in self.members.items():
            node = self._member_nodes.get(name, None)
            with module.error_context(node):
                new_value = my_value.finish_bind(module)
                if new_value is None:
                    to_remove.add(name)
                    continue
                elif my_value is not new_value:
                    my_value = self.members[name] = new_value

                for base in self.mro[1:]:
                    value = base.members.get(name)
                    if value is not None:
                        self.check_incompatible_override(my_value, value)
                    if isinstance(value, Slot):
                        to_remove.add(name)
                    elif isinstance(value, Function):
                        if value.func_name not in NON_VIRTUAL_METHODS:
                            if isinstance(my_value, TransparentDecoratedMethod):
                                value.validate_compat_signature(
                                    my_value.real_function, module
                                )
                            else:
                                assert isinstance(my_value, Function)
                                value.validate_compat_signature(my_value, module)
                    elif isinstance(value, TransparentDecoratedMethod):
                        if value.function.is_final:
                            raise TypedSyntaxError(
                                f"Cannot assign to a Final attribute of {self.instance.name}:{name}"
                            )
                    elif isinstance(value, StaticMethod):
                        if value.is_final:
                            raise TypedSyntaxError(
                                f"Cannot assign to a Final attribute of {self.instance.name}:{name}"
                            )
                        assert isinstance(my_value, DecoratedMethod)
                        value.real_function.validate_compat_signature(
                            my_value.real_function, module, first_arg_is_implicit=False
                        )
                    elif isinstance(value, (PropertyMethod, CachedPropertyMethod)):
                        if value.is_final:
                            raise TypedSyntaxError(
                                f"Cannot assign to a Final attribute of {self.instance.name}:{name}"
                            )
                        assert isinstance(
                            my_value, (PropertyMethod, CachedPropertyMethod)
                        )
                        value.real_function.validate_compat_signature(
                            my_value.real_function, module
                        )

        for name in to_remove:
            del self.members[name]

        # These were just for error reporting here, don't need them anymore
        self._member_nodes = {}
        return self

    def define_slot(
        self,
        name: str,
        node: AST,
        type_ref: Optional[TypeRef] = None,
        assignment: Optional[AST] = None,
        declared_on_class: bool = False,
    ) -> None:
        assigned_on_class = declared_on_class and bool(assignment)
        existing = self.members.get(name)
        if existing is None:
            self._member_nodes[name] = node
            self.members[name] = Slot(
                type_ref,
                name,
                self,
                assignment,
                assigned_on_class=assigned_on_class,
            )
        elif isinstance(existing, Slot):
            if not existing.type_ref:
                existing.type_ref = type_ref
                self._member_nodes[name] = node
            elif type_ref:
                raise TypedSyntaxError(
                    f"Cannot re-declare member '{name}' in '{self.instance.name}'"
                )
            if not existing.assignment:
                existing.assignment = assignment
            if not existing.assigned_on_class:
                existing.assigned_on_class = assigned_on_class
        else:
            raise TypedSyntaxError(
                f"slot conflicts with other member {name} in {self.name}"
            )

    def declare_function(self, func: Function) -> None:
        existing = self.members.get(func.func_name)
        new_member = func
        if existing is not None:
            if isinstance(existing, Function):
                new_member = FunctionGroup([existing, new_member])
            elif isinstance(existing, FunctionGroup):
                existing.functions.append(new_member)
                new_member = existing
            else:
                raise TypedSyntaxError(
                    f"function conflicts with other member {func.func_name} in {self.name}"
                )

        func.set_container_type(self)

        self._member_nodes[func.func_name] = func.node
        self.members[func.func_name] = new_member

        if (
            func.func_name == "__init__"
            and isinstance(func, Function)
            and func.node.args.args
        ):
            node = func.node
            if isinstance(node, FunctionDef):
                InitVisitor(func.module, self, node).visit(node.body)

    @property
    def mro(self) -> Sequence[Class]:
        mro = self._mro
        if mro is None:
            if not all(self.bases):
                # TODO: We can't compile w/ unknown bases
                mro = []
            else:
                mro = _mro(self)
            self._mro = mro

        return mro

    @property
    def mro_inexact(self) -> Collection[Class]:
        cached = self._mro_inexact
        if cached is None:
            self._mro_inexact = cached = {b.inexact_type() for b in self.mro}
        return cached

    def bind_generics(
        self,
        name: GenericTypeName,
        type_env: TypeEnvironment,
    ) -> Class:
        return self

    def find_slot(self, node: ast.Attribute) -> Optional[Slot[Class]]:
        for base in self.mro:
            member = base.members.get(node.attr)
            if (
                member is not None
                and isinstance(member, Slot)
                and not member.is_classvar
            ):
                return member
        return None

    def get_own_member(self, name: str) -> Optional[Value]:
        return self.members.get(name)

    def get_parent_member(self, name: str) -> Optional[Value]:
        # the first entry of mro is the class itself
        for b in self.mro[1:]:
            slot = b.members.get(name, None)
            if slot:
                return slot

    def get_member(self, name: str) -> Optional[Value]:
        member = self.get_own_member(name)
        if member:
            return member
        return self.get_parent_member(name)

    def get_own_final_method_names(self) -> Sequence[str]:
        final_methods = []
        for name, value in self.members.items():
            if isinstance(value, DecoratedMethod) and value.is_final:
                final_methods.append(name)
            elif isinstance(value, Function) and value.is_final:
                final_methods.append(name)
        return final_methods

    def unwrap(self) -> Class:
        return self


class BuiltinObject(Class):
    def __init__(
        self,
        type_name: TypeName,
        bases: Optional[List[Class]] = None,
        instance: Optional[Value] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        is_exact: bool = False,
        pytype: Optional[Type[object]] = None,
        is_final: bool = False,
    ) -> None:
        super().__init__(
            type_name, bases, instance, klass, members, is_exact, pytype, is_final
        )
        self.dynamic_builtinmethod_dispatch = True


class Variance(Enum):
    INVARIANT = 0
    COVARIANT = 1
    CONTRAVARIANT = 2


class GenericClass(Class):
    type_name: GenericTypeName
    is_variadic = False

    def __init__(
        self,
        name: GenericTypeName,
        bases: Optional[List[Class]] = None,
        instance: Optional[Object[Class]] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        type_def: Optional[GenericClass] = None,
        is_exact: bool = False,
        pytype: Optional[Type[object]] = None,
    ) -> None:
        super().__init__(name, bases, instance, klass, members, is_exact, pytype)
        self.gen_name = name
        self.type_def = type_def

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if self.contains_generic_parameters:
            visitor.syntax_error(
                f"cannot create instances of a generic {self.name}", node
            )
        return super().bind_call(node, visitor, type_ctx)

    def resolve_subscr(
        self,
        node: ast.Subscript,
        type: Value,
        visitor: AnnotationVisitor,
    ) -> Optional[Value]:
        slice = node.slice

        if not isinstance(slice, ast.Index):
            visitor.syntax_error("can't slice generic types", node)
            return DYNAMIC

        val = slice.value

        expected_argnum = len(self.gen_name.args)
        if isinstance(val, ast.Tuple):
            multiple: List[Class] = []
            for elt in val.elts:
                klass = visitor.resolve_annotation(elt) or DYNAMIC_TYPE
                multiple.append(klass)

            index = tuple(multiple)
            actual_argnum = len(val.elts)
        else:
            actual_argnum = 1
            single = visitor.resolve_annotation(val) or DYNAMIC_TYPE
            index = (single,)

        if (not self.is_variadic) and actual_argnum != expected_argnum:
            visitor.syntax_error(
                f"incorrect number of generic arguments for {self.instance.name}, "
                f"expected {expected_argnum}, got {actual_argnum}",
                node,
            )

        return self.make_generic_type(index, visitor.module.compiler.type_env)

    @property
    def type_args(self) -> Sequence[Class]:
        return self.type_name.args

    @property
    def contains_generic_parameters(self) -> bool:
        for arg in self.gen_name.args:
            if arg.is_generic_parameter:
                return True
        return False

    @property
    def is_generic_type(self) -> bool:
        return True

    @property
    def is_generic_type_definition(self) -> bool:
        return self.type_def is None

    @property
    def generic_type_def(self) -> Optional[Class]:
        """Gets the generic type definition that defined this class"""
        return self.type_def

    def is_subclass_of(self, src: Class) -> bool:
        type_def = self.generic_type_def
        if src.generic_type_def is not type_def:
            return super().is_subclass_of(src)

        assert isinstance(type_def, GenericClass)
        assert isinstance(src, GenericClass)
        assert len(self.type_args) == len(src.type_args)
        for def_arg, self_arg, src_arg in zip(
            type_def.type_args, self.type_args, src.type_args
        ):
            variance = def_arg.variance
            if variance is Variance.INVARIANT:
                if self_arg.is_subclass_of(src_arg) and src_arg.is_subclass_of(
                    self_arg
                ):
                    continue
            elif variance is Variance.COVARIANT:
                if self_arg.is_subclass_of(src_arg):
                    continue
            else:
                if src_arg.is_subclass_of(self_arg):
                    continue
            return False

        return True

    def make_generic_type(
        self,
        index: Tuple[Class, ...],
        type_env: TypeEnvironment,
    ) -> Class:
        instantiations = type_env._generic_types.get(self)
        if instantiations is not None:
            instance = instantiations.get(index)
            if instance is not None:
                return instance
        else:
            type_env._generic_types[self] = instantiations = {}

        type_args = index
        type_name = GenericTypeName(
            self.type_name.module, self.type_name.name, type_args
        )
        generic_bases: List[Optional[Class]] = [
            (
                base.make_generic_type(index, type_env)
                if base.contains_generic_parameters
                else base
            )
            for base in self.bases
        ]
        bases: List[Class] = [base for base in generic_bases if base is not None]
        InstanceType = type(self.instance)
        instance = InstanceType.__new__(InstanceType)
        instance.__dict__.update(self.instance.__dict__)
        concrete = type(self)(
            type_name,
            bases,
            # pyre-fixme[6]: Expected `Optional[Object[Class]]` for 3rd param but
            #  got `Value`.
            instance,
            self.klass,
            {},
            is_exact=self.is_exact,
            type_def=self,
        )

        instance.klass = concrete

        instantiations[index] = concrete
        concrete.members.update(
            {
                k: v.make_generic(concrete, type_name, type_env)
                for k, v in self.members.items()
            }
        )
        return concrete

    def bind_generics(
        self,
        name: GenericTypeName,
        type_env: TypeEnvironment,
    ) -> Class:
        if self.contains_generic_parameters:
            type_args = [
                arg for arg in self.type_name.args if isinstance(arg, GenericParameter)
            ]
            assert len(type_args) == len(self.type_name.args)
            # map the generic type parameters for the type to the parameters provided
            bind_args = tuple(name.args[arg.index] for arg in type_args)
            # We don't yet support generic methods, so all of the generic parameters are coming from the
            # type definition.

            return self.make_generic_type(bind_args, type_env)

        return self


class GenericParameter(Class):
    def __init__(
        self, name: str, index: int, variance: Variance = Variance.INVARIANT
    ) -> None:
        super().__init__(TypeName("", name), [], None, None, {})
        self.index = index
        self.variance = variance

    @property
    def name(self) -> str:
        return self.type_name.name

    @property
    def is_generic_parameter(self) -> bool:
        return True

    def bind_generics(
        self,
        name: GenericTypeName,
        type_env: TypeEnvironment,
    ) -> Class:
        return name.args[self.index]


class CType(Class):
    """base class for primitives that aren't heap allocated"""

    suppress_exact = True

    def __init__(
        self,
        type_name: TypeName,
        bases: Optional[List[Class]] = None,
        instance: Optional[CInstance[Class]] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        is_exact: bool = True,
        pytype: Optional[Type[object]] = None,
    ) -> None:
        super().__init__(
            type_name, bases or [], instance, klass, members, is_exact, pytype
        )

    @property
    def boxed(self) -> Class:
        raise NotImplementedError(type(self))

    @property
    def can_be_narrowed(self) -> bool:
        return False

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        """
        Almost the same as the base class method, but this allows args to be primitives
        so we can write something like (explicit conversions):
        x = int32(int8(5))
        """
        visitor.set_type(node, self.instance)
        for arg in node.args:
            visitor.visit(arg, self.instance)
        return NO_EFFECT

    def make_subclass(self, name: TypeName, bases: List[Class]) -> Class:
        raise TypedSyntaxError(
            f"Primitive type {self.instance_name} cannot be subclassed: {name.friendly_name}",
        )


class DynamicClass(Class):
    instance: DynamicInstance

    def __init__(self) -> None:
        super().__init__(
            # any references to dynamic at runtime are object
            TypeName("builtins", "object"),
            instance=DynamicInstance(self),
        )

    @property
    def qualname(self) -> str:
        return "dynamic"

    def can_assign_from(self, src: Class) -> bool:
        # No automatic boxing to the dynamic type
        return not isinstance(src, CType)


class DynamicInstance(Object[DynamicClass]):
    def __init__(self, klass: DynamicClass) -> None:
        super().__init__(klass)

    def emit_binop(self, node: ast.BinOp, code_gen: Static38CodeGenerator) -> None:
        if maybe_emit_sequence_repeat(node, code_gen):
            return
        code_gen.defaultVisit(node)


class NoneType(Class):
    suppress_exact = True

    def __init__(self) -> None:
        super().__init__(
            TypeName("builtins", "None"),
            [OBJECT_TYPE],
            NoneInstance(self),
            is_exact=True,
        )


UNARY_SYMBOLS: Mapping[Type[ast.unaryop], str] = {
    ast.UAdd: "+",
    ast.USub: "-",
    ast.Invert: "~",
}


class NoneInstance(Object[NoneType]):
    def bind_attr(
        self, node: ast.Attribute, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        visitor.syntax_error(f"'NoneType' object has no attribute '{node.attr}'", node)

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        visitor.syntax_error("'NoneType' object is not callable", node)
        return NO_EFFECT

    def bind_subscr(
        self,
        node: ast.Subscript,
        type: Value,
        visitor: TypeBinder,
        type_ctx: Optional[Class] = None,
    ) -> None:
        visitor.syntax_error("'NoneType' object is not subscriptable", node)

    def bind_unaryop(
        self, node: ast.UnaryOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        if not isinstance(node.op, ast.Not):
            visitor.syntax_error(
                f"bad operand type for unary {UNARY_SYMBOLS[type(node.op)]}: 'NoneType'",
                node,
            )
        visitor.set_type(node, BOOL_TYPE.instance)

    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        # support `None | int` as a union type; None is special in that it is
        # not a type but can be used synonymously with NoneType for typing.
        if isinstance(node.op, ast.BitOr):
            return self.klass.bind_binop(node, visitor, type_ctx)
        else:
            return super().bind_binop(node, visitor, type_ctx)

    def bind_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        if isinstance(op, (ast.Eq, ast.NotEq, ast.Is, ast.IsNot, ast.In, ast.NotIn)):
            return super().bind_compare(node, left, op, right, visitor, type_ctx)
        ltype = visitor.get_type(left)
        rtype = visitor.get_type(right)
        visitor.syntax_error(
            f"'{CMPOP_SIGILS[type(op)]}' not supported between '{ltype.name}' and '{rtype.name}'",
            node,
        )
        return False

    def bind_reverse_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        if isinstance(op, (ast.Eq, ast.NotEq, ast.Is, ast.IsNot)):
            return super().bind_reverse_compare(
                node, left, op, right, visitor, type_ctx
            )
        ltype = visitor.get_type(left)
        rtype = visitor.get_type(right)
        visitor.syntax_error(
            f"'{CMPOP_SIGILS[type(op)]}' not supported between '{ltype.name}' and '{rtype.name}'",
            node,
        )
        return False


# https://www.python.org/download/releases/2.3/mro/
def _merge(seqs: Iterable[List[Class]]) -> List[Class]:
    res = []
    i = 0
    while True:
        nonemptyseqs = [seq for seq in seqs if seq]
        if not nonemptyseqs:
            return res
        i += 1
        cand = None
        for seq in nonemptyseqs:  # find merge candidates among seq heads
            cand = seq[0]
            nothead = [s for s in nonemptyseqs if cand in s[1:]]
            if nothead:
                cand = None  # reject candidate
            else:
                break
        if not cand:
            types = {seq[0]: None for seq in nonemptyseqs}
            raise SyntaxError(
                "Cannot create a consistent method resolution order (MRO) for bases: "
                + ", ".join(t.name for t in types)
            )
        res.append(cand)
        for seq in nonemptyseqs:  # remove cand
            if seq[0] == cand:
                del seq[0]


def _mro(C: Class) -> List[Class]:
    "Compute the class precedence list (mro) according to C3"
    return _merge([[C]] + list(map(_mro, C.bases)) + [list(C.bases)])


class Parameter:
    def __init__(
        self,
        name: str,
        idx: int,
        type_ref: TypeRef,
        has_default: bool,
        default_val: object,
        is_kwonly: bool,
    ) -> None:
        self.name = name
        self.type_ref = type_ref
        self.index = idx
        self.has_default = has_default
        self.default_val = default_val
        self.is_kwonly = is_kwonly

    def __repr__(self) -> str:
        return (
            f"<Parameter name={self.name}, ref={self.type_ref}, "
            f"index={self.index}, has_default={self.has_default}>"
        )

    def bind_generics(
        self,
        name: GenericTypeName,
        type_env: TypeEnvironment,
    ) -> Parameter:
        klass = self.type_ref.resolved().bind_generics(name, type_env)
        if klass is not self.type_ref.resolved():
            return Parameter(
                self.name,
                self.index,
                ResolvedTypeRef(klass),
                self.has_default,
                self.default_val,
                self.is_kwonly,
            )

        return self


def is_subsequence(a: Iterable[object], b: Iterable[object]) -> bool:
    # for loops go brrrr :)
    # https://ericlippert.com/2020/03/27/new-grad-vs-senior-dev/
    itr = iter(a)
    for each in b:
        if each not in itr:
            return False
    return True


class ArgMapping:
    def __init__(
        self,
        callable: Callable[TClass],
        call: ast.Call,
        self_arg: Optional[ast.expr],
        args_override: Optional[List[ast.expr]] = None,
        descr_override: Optional[TypeDescr] = None,
    ) -> None:
        self.callable = callable
        self.call = call
        self.args: List[ast.expr] = args_override or list(call.args)
        self.kwargs: List[Tuple[Optional[str], ast.expr]] = [
            (kwarg.arg, kwarg.value) for kwarg in call.keywords
        ]
        self.self_arg = self_arg
        self.emitters: List[ArgEmitter] = []
        self.nvariadic = 0
        self.nseen = 0
        self.spills: Dict[int, SpillArg] = {}
        self.dynamic_call = False
        self.descr_override = descr_override

    def bind_args(self, visitor: TypeBinder, skip_self: bool = False) -> None:
        # TODO: handle duplicate args and other weird stuff a-la
        # https://fburl.com/diffusion/q6tpinw8
        if not self.can_call_statically():
            self.dynamic_call = True
            Object.bind_dynamic_call(self.call, visitor)
            return

        func_args = self.callable.args
        assert func_args is not None

        # Process provided position arguments to expected parameters
        expected_args = func_args
        if skip_self:
            expected_args = func_args[1:]
            self.nseen += 1

        if len(self.args) > len(expected_args):
            visitor.syntax_error(
                f"Mismatched number of args for {self.callable.name}. "
                f"Expected {len(expected_args) + skip_self}, got {len(self.args) + skip_self}",
                self.call,
            )

        for idx, (param, arg) in enumerate(zip(expected_args, self.args)):
            if param.is_kwonly:
                visitor.syntax_error(
                    f"{self.callable.qualname} takes {idx + skip_self} positional args but "
                    f"{len(self.args) + skip_self} {'was' if len(self.args) + skip_self == 1 else 'were'} given",
                    self.call,
                )
            elif isinstance(arg, Starred):
                # Skip type verification here, f(a, b, *something)
                # TODO: add support for this by implementing type constrained tuples
                self.nvariadic += 1
                star_params = expected_args[idx:]
                self.emitters.append(StarredArg(arg.value, star_params))
                self.nseen = len(func_args)
                for arg in self.args[idx:]:
                    if isinstance(arg, Starred):
                        visitor.visitExpectedType(
                            arg.value, DYNAMIC, "starred expression cannot be primitive"
                        )
                    else:
                        visitor.visitExpectedType(
                            arg, DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE
                        )
                break

            resolved_type = self.visit_arg(visitor, param, arg, "positional")
            self.emitters.append(PositionArg(arg, resolved_type))
            self.nseen += 1

        self.bind_kwargs(visitor)

        for argname, argvalue in self.kwargs:
            if argname is None:
                visitor.visit(argvalue)
                continue

            if argname not in self.callable.args_by_name:
                visitor.syntax_error(
                    f"Given argument {argname} "
                    f"does not exist in the definition of {self.callable.qualname}",
                    self.call,
                )

        # nseen must equal number of defined args if no variadic args are used
        if self.nvariadic == 0 and (self.nseen != len(func_args)):
            visitor.syntax_error(
                f"Mismatched number of args for {self.callable.name}. "
                f"Expected {len(func_args)}, got {self.nseen}",
                self.call,
            )

    def bind_kwargs(self, visitor: TypeBinder) -> None:
        func_args = self.callable.args
        assert func_args is not None

        spill_start = len(self.emitters)
        seen_variadic = False
        # Process unhandled arguments which can be populated via defaults,
        # keyword arguments, or **mapping.
        cur_kw_arg = 0
        for idx in range(self.nseen, len(func_args)):
            param = func_args[idx]
            name = param.name
            if (
                cur_kw_arg is not None
                and cur_kw_arg < len(self.kwargs)
                and self.kwargs[cur_kw_arg][0] == name
            ):
                # keyword arg hit, with the keyword arguments still in order...
                arg = self.kwargs[cur_kw_arg][1]
                resolved_type = self.visit_arg(visitor, param, arg, "keyword")
                cur_kw_arg += 1

                self.emitters.append(KeywordArg(arg, resolved_type))
                self.nseen += 1
                continue

            variadic_idx = None
            for candidate_kw in range(len(self.kwargs)):
                if name == self.kwargs[candidate_kw][0]:
                    arg = self.kwargs[candidate_kw][1]

                    tmp_name = f"{_TMP_VAR_PREFIX}{name}"
                    self.spills[candidate_kw] = SpillArg(arg, tmp_name)

                    if cur_kw_arg is not None:
                        cur_kw_arg = None
                        spill_start = len(self.emitters)

                    resolved_type = self.visit_arg(visitor, param, arg, "keyword")
                    self.emitters.append(SpilledKeywordArg(tmp_name, resolved_type))
                    break
                elif self.kwargs[candidate_kw][0] == None:
                    variadic_idx = candidate_kw
            else:
                if variadic_idx is not None:
                    # We have a f(**something), if the arg is unavailable, we
                    # load it from the mapping
                    if variadic_idx not in self.spills:
                        self.spills[variadic_idx] = SpillArg(
                            self.kwargs[variadic_idx][1], f"{_TMP_VAR_PREFIX}**"
                        )

                        if cur_kw_arg is not None:
                            cur_kw_arg = None
                            spill_start = len(self.emitters)

                    self.emitters.append(
                        KeywordMappingArg(param, f"{_TMP_VAR_PREFIX}**")
                    )
                elif param.has_default:
                    if isinstance(param.default_val, expr):
                        # We'll force these to normal calls in can_call_self, we'll add
                        # an emitter which makes sure we never try and do code gen for this
                        self.emitters.append(UnreachableArg())
                    else:
                        const = ast.Constant(param.default_val)
                        copy_location(const, self.call)
                        visitor.visit(const, param.type_ref.resolved(False).instance)
                        self.emitters.append(DefaultArg(const))
                else:
                    # It's an error if this arg did not have a default value in the definition
                    visitor.syntax_error(
                        f"Function {self.callable.qualname} expects a value for "
                        f"argument {param.name}",
                        self.call,
                    )

            self.nseen += 1

        if self.spills:
            self.emitters[spill_start:spill_start] = [
                x[1] for x in sorted(self.spills.items())
            ]

    def visit_arg(
        self, visitor: TypeBinder, param: Parameter, arg: expr, arg_style: str
    ) -> Class:
        resolved_type = param.type_ref.resolved()
        desc = (
            f"{arg_style} arg '{param.name}'"
            if param.name
            else f"{arg_style} arg {param.index}"
        )
        expected = resolved_type.instance
        visitor.visitExpectedType(
            arg,
            expected,
            f"type mismatch: {{}} received for {desc}, expected {{}}",
        )

        return resolved_type

    def needs_virtual_invoke(self, code_gen: Static38CodeGenerator) -> bool:
        if self.callable.is_final:
            return False
        self_arg = self.self_arg
        if self_arg is None:
            return False

        self_type = code_gen.get_type(self_arg)
        return not (self_type.klass.is_exact or self_type.klass.is_final)

    def can_call_statically(self) -> bool:
        func_args = self.callable.args
        if func_args is None or self.callable.has_vararg or self.callable.has_kwarg:
            return False

        has_default_args = self.callable.num_required_args < len(self.args)
        has_star_args = False
        for a in self.call.args:
            if isinstance(a, ast.Starred):
                if has_star_args:
                    # We don't support f(*a, *b)
                    return False
                has_star_args = True
            elif has_star_args:
                # We don't support f(*a, b)
                return False

        num_star_args = [isinstance(a, ast.Starred) for a in self.call.args].count(True)
        num_dstar_args = [(a.arg is None) for a in self.call.keywords].count(True)
        num_kwonly = len([arg for arg in func_args if arg.is_kwonly])

        start = 1 if self.self_arg is not None else 0
        for arg in func_args[start + len(self.call.args) :]:
            if arg.has_default and isinstance(arg.default_val, ast.expr):
                for kw_arg in self.call.keywords:
                    if kw_arg.arg == arg.name:
                        break
                else:
                    return False
        if (
            # We don't support f(**a, **b)
            (num_dstar_args + num_star_args) > 1
            # We don't support f(1, 2, *a) iff has any default arg values
            or (has_default_args and has_star_args)
            or num_kwonly
        ):
            return False

        return True

    def emit(self, code_gen: Static38CodeGenerator, extra_self: bool = False) -> None:
        if self.dynamic_call:
            code_gen.defaultVisit(self.call)
            return

        code_gen.update_lineno(self.call)

        for emitter in self.emitters:
            emitter.emit(self.call, code_gen)

        func_args = self.callable.args
        assert func_args is not None

        if self.needs_virtual_invoke(code_gen):
            code_gen.emit_invoke_method(
                self.callable.type_descr,
                len(func_args) if extra_self else len(func_args) - 1,
            )
        else:
            code_gen.emit("EXTENDED_ARG", 0)
            descr = self.descr_override or self.callable.type_descr
            code_gen.emit("INVOKE_FUNCTION", (descr, len(func_args)))


class ClassMethodArgMapping(ArgMapping):
    def __init__(
        self,
        callable: Callable[TClass],
        call: ast.Call,
        self_arg: Optional[ast.expr] = None,
        args_override: Optional[List[ast.expr]] = None,
        is_instance_call: bool = False,
    ) -> None:
        super().__init__(callable, call, self_arg, args_override)
        self.is_instance_call = is_instance_call

    def needs_virtual_invoke(self, code_gen: Static38CodeGenerator) -> bool:
        if self.callable.is_final:
            return False

        self_arg = self.self_arg
        assert self_arg is not None
        self_type = code_gen.get_type(self_arg)
        if self.is_instance_call:
            return not (self_type.klass.is_exact or self_type.klass.is_final)
        assert isinstance(self_type, Class)
        instance = self_type.instance
        return not (instance.klass.is_exact or instance.klass.is_final)

    def emit(self, code_gen: Static38CodeGenerator, extra_self: bool = False) -> None:
        if self.dynamic_call:
            code_gen.defaultVisit(self.call)
            return

        self_arg = self.self_arg
        assert self_arg is not None
        code_gen.visit(self_arg)
        if self.is_instance_call:
            code_gen.emit("LOAD_TYPE")

        code_gen.update_lineno(self.call)

        for emitter in self.emitters:
            emitter.emit(self.call, code_gen)

        func_args = self.callable.args
        assert func_args is not None

        if self.needs_virtual_invoke(code_gen):
            code_gen.emit_invoke_method(
                self.callable.type_descr,
                len(func_args) if extra_self else len(func_args) - 1,
                is_classmethod=True,
            )
        else:
            code_gen.emit("EXTENDED_ARG", 0)
            code_gen.emit("INVOKE_FUNCTION", (self.callable.type_descr, len(func_args)))


class ArgEmitter:
    def __init__(self, argument: expr, type: Class) -> None:
        self.argument = argument

        self.type = type

    def emit(self, node: Call, code_gen: Static38CodeGenerator) -> None:
        pass


class PositionArg(ArgEmitter):
    def emit(self, node: Call, code_gen: Static38CodeGenerator) -> None:
        arg_type = code_gen.get_type(self.argument)
        code_gen.visit(self.argument)

        code_gen.emit_type_check(
            self.type,
            arg_type.klass,
            node,
        )

    def __repr__(self) -> str:
        return f"PositionArg({to_expr(self.argument)}, {self.type})"


class StarredArg(ArgEmitter):
    def __init__(self, argument: expr, params: List[Parameter]) -> None:

        self.argument = argument
        self.params = params

    def emit(self, node: Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(self.argument)
        for idx, param in enumerate(self.params):
            code_gen.emit("LOAD_ITERABLE_ARG", idx)

            if (
                param.type_ref.resolved() is not None
                and param.type_ref.resolved() is not DYNAMIC
            ):
                code_gen.emit("ROT_TWO")
                code_gen.emit("CAST", param.type_ref.resolved().type_descr)
                code_gen.emit("ROT_TWO")

        # Remove the tuple from TOS
        code_gen.emit("POP_TOP")


class SpillArg(ArgEmitter):
    def __init__(self, argument: expr, temporary: str) -> None:
        self.argument = argument
        self.temporary = temporary

    def emit(self, node: Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(self.argument)
        code_gen.emit("STORE_FAST", self.temporary)

    def __repr__(self) -> str:
        return f"SpillArg(..., {self.temporary})"


class SpilledKeywordArg(ArgEmitter):
    def __init__(self, temporary: str, type: Class) -> None:
        self.temporary = temporary
        self.type = type

    def emit(self, node: Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.emit("LOAD_FAST", self.temporary)
        code_gen.emit_type_check(
            self.type,
            DYNAMIC_TYPE,
            node,
        )

    def __repr__(self) -> str:
        return f"SpilledKeywordArg({self.temporary})"


class KeywordArg(ArgEmitter):
    def __init__(self, argument: expr, type: Class) -> None:
        self.argument = argument
        self.type = type

    def emit(self, node: Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(self.argument)
        code_gen.emit_type_check(
            self.type,
            code_gen.get_type(self.argument).klass,
            node,
        )


class KeywordMappingArg(ArgEmitter):
    def __init__(self, param: Parameter, variadic: str) -> None:
        self.param = param

        self.variadic = variadic

    def emit(self, node: Call, code_gen: Static38CodeGenerator) -> None:
        if self.param.has_default:
            code_gen.emit("LOAD_CONST", self.param.default_val)
        code_gen.emit("LOAD_FAST", self.variadic)
        code_gen.emit("LOAD_CONST", self.param.name)
        if self.param.has_default:
            code_gen.emit("LOAD_MAPPING_ARG", 3)
        else:
            code_gen.emit("LOAD_MAPPING_ARG", 2)
        code_gen.emit_type_check(
            self.param.type_ref.resolved() or DYNAMIC_TYPE, DYNAMIC_TYPE, node
        )


class DefaultArg(ArgEmitter):
    def __init__(self, expr: expr) -> None:
        self.expr = expr

    def emit(self, node: Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(self.expr)


class UnreachableArg(ArgEmitter):
    def __init__(self) -> None:
        pass

    def emit(self, node: Call, code_gen: Static38CodeGenerator) -> None:
        raise ValueError("this arg should never be emitted")


class FunctionContainer(Object[Class]):
    def bind_function(
        self, node: Union[FunctionDef, AsyncFunctionDef], visitor: TypeBinder
    ) -> None:
        scope = visitor.new_scope(node)

        for decorator in reversed(node.decorator_list):
            visitor.visitExpectedType(
                decorator, DYNAMIC, "decorator cannot be a primitive"
            )

        self.bind_function_self(node, scope, visitor)
        visitor._visitParameters(node.args, scope)

        returns = node.returns
        if returns:
            visitor.visitExpectedType(
                returns, DYNAMIC, "return annotation cannot be a primitive"
            )

        self.bind_function_inner(node, visitor)

        visitor.scopes.append(scope)

        for stmt in self.get_function_body():
            visitor.visit(stmt)

        visitor.scopes.pop()

    def bind_function_inner(
        self, node: Union[FunctionDef, AsyncFunctionDef], visitor: TypeBinder
    ) -> None:
        """provides decorator specific binding pass, decorators should call
        do whatever binding is necessary and forward the call to their
        contained function"""
        pass

    def get_function_body(self) -> List[ast.stmt]:
        raise NotImplementedError(type(self))

    def replace_function(self, func: Function) -> Function | DecoratedMethod:
        """Provides the ability to replace the function through a chain of decorators.
        The outer decorator will pass the function into inner decorators, until
        we hit the original function which will return func.  The decorators
        then replace their function with the returned function.  This provides a
        feedback mechanism for when the outer decorator alters things like typing of
        the Function (e.g. classmethod which will change the type of the first
        argument)."""
        raise NotImplementedError()

    def bind_function_self(
        self,
        node: Union[FunctionDef, AsyncFunctionDef],
        scope: BindingScope,
        visitor: TypeBinder,
    ) -> None:
        cur_scope = visitor.scope
        if isinstance(cur_scope, ClassDef) and node.args.args:
            # Handle type of "self"
            self_type = DYNAMIC
            if node.name == "__new__":
                # __new__ is special and isn't a normal method, so we expect a
                # type for cls
                self_type = TYPE_TYPE.instance
            else:
                klass = visitor.maybe_get_current_class()
                if klass is not None:
                    self_type = klass.instance

            visitor.set_param(node.args.args[0], self_type, scope)

    def emit_function(
        self,
        node: ast.FunctionDef | ast.AsyncFunctionDef,
        code_gen: Static38CodeGenerator,
    ) -> str:
        raise NotImplementedError()

    def emit_function_body(
        self,
        node: ast.FunctionDef | ast.AsyncFunctionDef,
        code_gen: Static38CodeGenerator,
        first_lineno: int,
        body: List[ast.stmt],
    ) -> CodeGenerator:

        gen = code_gen.make_func_codegen(node, node.args, node.name, first_lineno)

        code_gen.processBody(node, body, gen)

        gen.finishFunction()

        code_gen.build_function(node, gen)

        return gen

    @property
    def return_type(self) -> TypeRef:
        raise NotImplementedError("No return_type")


class Callable(Object[TClass]):
    def __init__(
        self,
        klass: Class,
        func_name: str,
        module_name: str,
        args: Optional[List[Parameter]],
        args_by_name: Dict[str, Parameter],
        num_required_args: int,
        vararg: Optional[Parameter],
        kwarg: Optional[Parameter],
        return_type: TypeRef,
    ) -> None:
        super().__init__(klass)
        self.func_name = func_name
        self.module_name = module_name
        self.container_type: Optional[Class] = None
        self.args = args
        self.args_by_name = args_by_name
        self.num_required_args = num_required_args
        self.has_vararg: bool = vararg is not None
        self.has_kwarg: bool = kwarg is not None
        self._return_type = return_type
        self.is_final = False

    @property
    def return_type(self) -> TypeRef:
        return self._return_type

    @return_type.setter
    def return_type(self, value: TypeRef) -> None:
        self._return_type = value

    @property
    def qualname(self) -> str:
        cont = self.container_type
        if cont:
            return f"{cont.qualname}.{self.func_name}"
        return f"{self.module_name}.{self.func_name}"

    @property
    def type_descr(self) -> TypeDescr:
        cont = self.container_type
        if cont:
            return cont.type_descr + (self.func_name,)
        return (self.module_name, self.func_name)

    def set_container_type(self, klass: Optional[Class]) -> None:
        self.container_type = klass

    def map_call(
        self,
        node: ast.Call,
        visitor: TypeBinder,
        self_expr: Optional[ast.expr] = None,
        args_override: Optional[List[ast.expr]] = None,
        descr_override: Optional[TypeDescr] = None,
    ) -> Tuple[ArgMapping, Value]:
        arg_mapping = ArgMapping(
            self, node, self_expr, args_override, descr_override=descr_override
        )
        arg_mapping.bind_args(visitor)

        return arg_mapping, self.return_type.resolved().instance

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        # Careful adding logic here, MethodType.bind_call() will bypass it
        return self.bind_call_self(node, visitor, type_ctx)

    def bind_call_self(
        self,
        node: ast.Call,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
        self_expr: Optional[ast.expr] = None,
    ) -> NarrowingEffect:
        arg_mapping, ret_type = self.map_call(
            node,
            visitor,
            self_expr,
            node.args if self_expr is None else [self_expr] + node.args,
            descr_override=visitor.get_opt_node_data(node.func, TypeDescr),
        )

        visitor.set_type(node, ret_type)
        visitor.set_node_data(node, ArgMapping, arg_mapping)
        return NO_EFFECT

    def _emit_kwarg_temps(
        self, keywords: List[ast.keyword], code_gen: Static38CodeGenerator
    ) -> Dict[str, str]:
        temporaries = {}
        for each in keywords:
            name = each.arg
            if name is not None:
                code_gen.visit(each.value)
                temp_var_name = f"{_TMP_VAR_PREFIX}{name}"
                code_gen.emit("STORE_FAST", temp_var_name)
                temporaries[name] = temp_var_name
        return temporaries

    def _find_provided_kwargs(
        self, node: ast.Call
    ) -> Tuple[Dict[int, int], Optional[int]]:
        # This is a mapping of indices from index in the function definition --> node.keywords
        provided_kwargs: Dict[int, int] = {}
        # Index of `**something` in the call
        variadic_idx: Optional[int] = None
        for idx, argument in enumerate(node.keywords):
            name = argument.arg
            if name is not None:
                provided_kwargs[self.args_by_name[name].index] = idx
            else:
                # Because of the constraints above, we will only ever reach here once
                variadic_idx = idx
        return provided_kwargs, variadic_idx

    def emit_call_self(
        self,
        node: ast.Call,
        code_gen: Static38CodeGenerator,
        self_expr: Optional[ast.expr] = None,
    ) -> None:
        arg_mapping: ArgMapping = code_gen.get_node_data(node, ArgMapping)
        arg_mapping.emit(code_gen)


class AwaitableType(GenericClass):
    def __init__(
        self,
        type_name: Optional[GenericTypeName] = None,
        type_def: Optional[GenericClass] = None,
    ) -> None:
        super().__init__(
            type_name
            or GenericTypeName(
                "static",
                "InferredAwaitable",
                (GenericParameter("T", 0, Variance.COVARIANT),),
            ),
            instance=AwaitableInstance(self),
            type_def=type_def,
        )

    @property
    def type_descr(self) -> TypeDescr:
        # This is not a real type, so we should not emit it.
        raise NotImplementedError("Awaitables shouldn't have a type descr")

    def make_generic_type(
        self, index: Tuple[Class, ...], type_env: TypeEnvironment
    ) -> Class:
        assert len(index) == 1

        instantiations = type_env._generic_types.get(self)
        if instantiations is not None:
            instance = instantiations.get(index)
            if instance is not None:
                return instance
        else:
            type_env._generic_types[self] = instantiations = {}

        type_name = GenericTypeName(self.type_name.module, self.type_name.name, index)
        instantiations[index] = concrete = AwaitableType(
            type_name,
            type_def=self,
        )

        return concrete


class AwaitableInstance(Object[AwaitableType]):
    klass: AwaitableType

    def bind_await(
        self, node: ast.Await, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        visitor.set_type(node, self.klass.type_args[0].instance)


class AwaitableTypeRef(TypeRef):
    def __init__(self, ref: TypeRef, compiler: Compiler) -> None:
        self.ref = ref
        self.compiler = compiler

    def resolved(self, is_declaration: bool = False) -> Class:
        res = self.ref.resolved(is_declaration)
        return AWAITABLE_TYPE.make_generic_type((res,), self.compiler.type_env)

    def __repr__(self) -> str:
        return f"AwaitableTypeRef({self.ref!r})"


class ContainerTypeRef(TypeRef):
    def __init__(self, func: Function) -> None:
        self.func = func

    def __repr__(self) -> str:
        return f"ContainerTypeRef({self.func!r})"

    def resolved(self, is_declaration: bool = False) -> Class:
        res = self.func.container_type
        if res is None:
            return DYNAMIC_TYPE
        return res


class InlineRewriter(ASTRewriter):
    def __init__(self, replacements: Dict[str, ast.expr]) -> None:
        super().__init__()
        self.replacements = replacements

    def visit(
        self, node: Union[TAst, Sequence[AST]], *args: object
    ) -> Union[AST, Sequence[AST]]:
        res = super().visit(node, *args)
        if res is node:
            if isinstance(node, AST):
                return self.clone_node(node)

            return list(node)

        return res

    def visitName(self, node: ast.Name) -> AST:
        res = self.replacements.get(node.id)
        if res is None:
            return self.clone_node(node)

        return res


class InlinedCall:
    def __init__(
        self,
        expr: ast.expr,
        replacements: Dict[ast.expr, ast.expr],
        spills: Dict[str, Tuple[ast.expr, ast.Name]],
    ) -> None:
        self.expr = expr
        self.replacements = replacements
        self.spills = spills


# These are methods which are implicitly non-virtual, that is we'll never
# generate virtual invokes against them, and therefore their signatures
# also don't have any requirements to be compatible.
NON_VIRTUAL_METHODS = {"__init__", "__new__", "__init_subclass__"}


class Function(Callable[Class], FunctionContainer):
    args: List[Parameter]

    def __init__(
        self,
        node: Union[AsyncFunctionDef, FunctionDef],
        module: ModuleTable,
        return_type: TypeRef,
    ) -> None:
        super().__init__(
            FUNCTION_TYPE,
            node.name,
            module.name,
            [],
            {},
            0,
            None,
            None,
            return_type,
        )
        self.node = node
        self.module = module
        self.process_args(module)
        self.inline = False
        self.donotcompile = False
        self._inner_classes: Dict[str, Value] = {}

    def emit_function(
        self,
        node: ast.FunctionDef | ast.AsyncFunctionDef,
        code_gen: Static38CodeGenerator,
    ) -> str:
        # For decorated functions we should either have a known decorator or
        # a UnknownDecoratedFunction.  The known decorators will handle emitting
        # themselves appropriately if necessary, and UnknownDecoratedFunction
        # will handle emitting all the present decorators normally.  Therefore
        # we shouldn't have any decorators for a simple function.
        assert not node.decorator_list
        first_lineno = node.lineno

        self.emit_function_body(node, code_gen, first_lineno, node.body)

        return node.name

    def get_function_body(self) -> List[ast.stmt]:
        return self.node.body

    def replace_function(
        self, func: Function | DecoratedMethod
    ) -> Function | DecoratedMethod:
        return func

    def finish_bind(self, module: ModuleTable) -> Optional[Value]:
        func = module.finish_decorator(self.node, self)
        if func is not None:
            return func

        # With an un-analyzable decorator we want to force late binding
        # to it because we don't know what the decorator does

    @property
    def name(self) -> str:
        return f"function {self.qualname}"

    def declare_class(self, node: AST, klass: Class) -> None:
        # currently, we don't allow declaring classes within functions
        pass

    def declare_variable(self, node: AnnAssign, module: ModuleTable) -> None:
        pass

    def declare_function(self, func: Function) -> None:
        pass

    def declare_variables(self, node: Assign, module: ModuleTable) -> None:
        pass

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        res = super().bind_call(node, visitor, type_ctx)
        if self.inline and not visitor.enable_patching:
            assert isinstance(self.node.body[0], ast.Return)

            return self.bind_inline_call(node, visitor, type_ctx) or res

        return res

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if self.inline and not code_gen.enable_patching:
            return self.emit_inline_call(node, code_gen)

        return self.emit_call_self(node, code_gen)

    def bind_inline_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> Optional[NarrowingEffect]:
        args = visitor.get_node_data(node, ArgMapping)
        arg_replacements = {}
        spills = {}

        # in fixpoint iteration we may have done the inlining already
        if visitor.get_opt_node_data(node, Optional[InlinedCall]):
            return None

        if visitor.inline_depth > 20:
            visitor.set_node_data(node, Optional[InlinedCall], None)
            return None

        visitor.inline_depth += 1
        visitor.inline_calls += 1
        for idx, arg in enumerate(args.emitters):
            name = self.node.args.args[idx].arg

            if isinstance(arg, DefaultArg):
                arg_replacements[name] = constant = arg.expr
                continue
            elif not isinstance(arg, (PositionArg, KeywordArg)):
                # We don't support complicated calls to inline functions
                visitor.set_node_data(node, Optional[InlinedCall], None)
                return None

            if (
                isinstance(arg.argument, ast.Constant)
                or visitor.get_final_literal(arg.argument) is not None
            ):
                arg_replacements[name] = arg.argument
                continue

            # store to a temporary...
            tmp_name = f"{_TMP_VAR_PREFIX}{visitor.inline_calls}{name}"
            cur_scope = visitor.symbols.scopes[visitor.scope]
            cur_scope.add_def(tmp_name)

            store = ast.Name(tmp_name, ast.Store())
            copy_location(store, arg.argument)
            visitor.set_type(store, visitor.get_type(arg.argument))
            spills[tmp_name] = arg.argument, store

            replacement = ast.Name(tmp_name, ast.Load())
            copy_location(replacement, arg.argument)
            visitor.assign_value(replacement, visitor.get_type(arg.argument))

            arg_replacements[name] = replacement

        # re-write node body with replacements...
        return_stmt = self.node.body[0]
        assert isinstance(return_stmt, Return)
        ret_value = return_stmt.value
        if ret_value is not None:
            new_node = InlineRewriter(arg_replacements).visit(ret_value)
        else:
            new_node = copy_location(ast.Constant(None), return_stmt)
        new_node = AstOptimizer().visit(new_node)

        inlined_call = InlinedCall(new_node, arg_replacements, spills)
        visitor.visit(new_node)
        visitor.set_node_data(node, Optional[InlinedCall], inlined_call)

        visitor.inline_depth -= 1

    def emit_inline_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        assert isinstance(self.node.body[0], ast.Return)
        inlined_call = code_gen.get_node_data(node, Optional[InlinedCall])
        if inlined_call is None:
            return self.emit_call_self(node, code_gen)

        for name, (arg, store) in inlined_call.spills.items():
            code_gen.visit(arg)

            code_gen.get_type(store).emit_name(store, code_gen)

        code_gen.visit(inlined_call.expr)

    def resolve_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: ReferenceVisitor,
    ) -> Optional[Value]:
        if inst is None:
            return self
        else:
            return MethodType(ctx.type_name, self.node, node, self)

    def register_arg(
        self,
        name: str,
        idx: int,
        ref: TypeRef,
        has_default: bool,
        default_val: object,
        is_kwonly: bool,
    ) -> None:
        parameter = Parameter(name, idx, ref, has_default, default_val, is_kwonly)
        self.args.append(parameter)
        self.args_by_name[name] = parameter
        if not has_default:
            self.num_required_args += 1

    def process_args(self: Function, module: ModuleTable) -> None:
        """
        Register type-refs for each function argument, assume DYNAMIC if annotation is missing.
        """
        arguments = self.node.args
        nrequired = len(arguments.args) - len(arguments.defaults)
        no_defaults = cast(List[Optional[ast.expr]], [None] * nrequired)
        defaults = no_defaults + cast(List[Optional[ast.expr]], arguments.defaults)
        idx = 0
        for idx, (argument, default) in enumerate(zip(arguments.args, defaults)):
            annotation = argument.annotation
            default_val = None
            has_default = False
            if default is not None:
                has_default = True
                default_val = get_default_value(default)

            if annotation:
                ref = TypeRef(module, annotation)
            elif idx == 0:
                if self.node.name == "__new__":
                    ref = ResolvedTypeRef(TYPE_TYPE)
                else:
                    ref = ContainerTypeRef(self)
            else:
                ref = ResolvedTypeRef(DYNAMIC_TYPE)

            self.register_arg(argument.arg, idx, ref, has_default, default_val, False)

        base_idx = idx

        vararg = arguments.vararg
        if vararg:
            base_idx += 1
            self.has_vararg = True

        for argument, default in zip(arguments.kwonlyargs, arguments.kw_defaults):
            annotation = argument.annotation
            default_val = None
            has_default = default is not None
            if default is not None:
                default_val = get_default_value(default)
            if annotation:
                ref = TypeRef(module, annotation)
            else:
                ref = ResolvedTypeRef(DYNAMIC_TYPE)
            base_idx += 1
            self.register_arg(
                argument.arg, base_idx, ref, has_default, default_val, True
            )

        kwarg = arguments.kwarg
        if kwarg:
            self.has_kwarg = True

    def validate_compat_signature(
        self,
        override: Function,
        module: ModuleTable,
        first_arg_is_implicit: bool = True,
    ) -> None:
        ret_type = self.return_type.resolved()
        override_ret_type = override.return_type.resolved()

        if not ret_type.can_assign_from(override_ret_type):
            module.syntax_error(
                f"{override.qualname} overrides {self.qualname} inconsistently. "
                f"Returned type `{override_ret_type.instance_name}` is not a subtype "
                f"of the overridden return `{ret_type.instance_name}`",
                override.node,
            )

        if len(self.args) != len(override.args):
            module.syntax_error(
                f"{override.qualname} overrides {self.qualname} inconsistently. "
                "Number of arguments differ",
                override.node,
            )

        start_arg = 1 if first_arg_is_implicit else 0
        for arg, override_arg in zip(self.args[start_arg:], override.args[start_arg:]):
            if arg.name != override_arg.name:
                if arg.is_kwonly:
                    arg_desc = f"Keyword only argument `{arg.name}`"
                else:
                    arg_desc = f"Positional argument {arg.index + 1} named `{arg.name}`"

                module.syntax_error(
                    f"{override.qualname} overrides {self.qualname} inconsistently. "
                    f"{arg_desc} is overridden as `{override_arg.name}`",
                    override.node,
                )

            if arg.is_kwonly != override_arg.is_kwonly:
                module.syntax_error(
                    f"{override.qualname} overrides {self.qualname} inconsistently. "
                    f"`{arg.name}` differs by keyword only vs positional",
                    override.node,
                )

            override_type = override_arg.type_ref.resolved()
            arg_type = arg.type_ref.resolved()
            if not override_type.can_assign_from(arg_type):
                module.syntax_error(
                    f"{override.qualname} overrides {self.qualname} inconsistently. "
                    f"Parameter {arg.name} of type `{override_type.instance_name}` is not a subtype "
                    f"of the overridden parameter `{arg_type.instance_name}`",
                    override.node,
                )

        if self.has_vararg != override.has_vararg:
            module.syntax_error(
                f"{override.qualname} overrides {self.qualname} inconsistently. "
                f"Functions differ by including *args",
                override.node,
            )

        if self.has_kwarg != override.has_kwarg:
            module.syntax_error(
                f"{override.qualname} overrides {self.qualname} inconsistently. "
                f"Functions differ by including **kwargs",
                override.node,
            )

    def __repr__(self) -> str:
        return f"<{self.name} '{self.name}' instance, args={self.args}>"


class UnknownDecoratedMethod(FunctionContainer):
    """Wrapper around functions where we are unable to analyze the effect
    of the decorators"""

    def __init__(self, func: Function) -> None:
        super().__init__(DYNAMIC_TYPE)
        self.func = func

    def get_function_body(self) -> List[ast.stmt]:
        return self.func.get_function_body()

    def bind_function_self(
        self,
        node: Union[FunctionDef, AsyncFunctionDef],
        scope: BindingScope,
        visitor: TypeBinder,
    ) -> None:
        if node.args.args:
            klass = visitor.maybe_get_current_class()
            visitor.set_param(node.args.args[0], DYNAMIC, scope)

    def emit_function(
        self,
        node: ast.FunctionDef | ast.AsyncFunctionDef,
        code_gen: Static38CodeGenerator,
    ) -> str:
        name = node.name
        if node.decorator_list:
            for decorator in node.decorator_list:
                code_gen.visit(decorator)
            first_lineno = node.decorator_list[0].lineno
        else:
            first_lineno = node.lineno

        self.func.emit_function_body(
            node, code_gen, first_lineno, self.func.get_function_body()
        )

        for _ in range(len(node.decorator_list)):
            code_gen.emit("CALL_FUNCTION", 1)

        return node.name

    @property
    def return_type(self) -> TypeRef:
        if isinstance(self.func.node, AsyncFunctionDef):
            return AwaitableTypeRef(
                ResolvedTypeRef(DYNAMIC_TYPE), self.func.module.compiler
            )
        return ResolvedTypeRef(DYNAMIC_TYPE)


class MethodType(Object[Class]):
    def __init__(
        self,
        bound_type_name: TypeName,
        node: Union[AsyncFunctionDef, FunctionDef],
        target: ast.Attribute,
        function: Function,
    ) -> None:
        super().__init__(METHOD_TYPE)
        # TODO currently this type (the type the bound method was accessed
        # from) is unused, and we just end up deferring to the type where the
        # function was defined. This is fine until we want to fully support a
        # method defined in one class being also referenced as a method in
        # another class.
        self.bound_type_name = bound_type_name
        self.node = node
        self.target = target
        self.function = function

    @property
    def name(self) -> str:
        return "method " + self.function.qualname

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        result = self.function.bind_call_self(
            node, visitor, type_ctx, self.target.value
        )
        return result

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if self.function.func_name in NON_VIRTUAL_METHODS:
            return super().emit_call(node, code_gen)

        code_gen.update_lineno(node)

        self.function.emit_call_self(node, code_gen, self.target.value)


class StaticMethodInstanceBound(Object[Class]):
    """This represents a static method that has been bound to an instance
    method.  Such a thing doesn't really exist in Python as the function
    will always be returned.  But we need to defer the resolution of the
    static method to runtime if the instance that it is accessed to is not
    final or exact.  In that case we'll use an INVOKE_METHOD opcode to invoke
    it and the internal runtime machinery will understand that static methods
    should have their self parameters removed on the call."""

    def __init__(
        self,
        function: Function,
        target: ast.Attribute,
    ) -> None:
        super().__init__(STATIC_METHOD_TYPE)
        self.function = function
        self.target = target

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        arg_mapping = ArgMapping(self.function, node, self.target.value, node.args)
        arg_mapping.bind_args(visitor)

        visitor.set_type(node, self.function.return_type.resolved().instance)
        visitor.set_node_data(node, ArgMapping, arg_mapping)
        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if self.function.func_name in NON_VIRTUAL_METHODS:
            return super().emit_call(node, code_gen)

        arg_mapping: ArgMapping = code_gen.get_node_data(node, ArgMapping)
        if arg_mapping.needs_virtual_invoke(code_gen):
            # we need self for virtual invoke
            code_gen.visit(self.target.value)

        arg_mapping.emit(code_gen, extra_self=True)


class DecoratedMethod(FunctionContainer):
    def __init__(
        self, klass: Class, function: Function | DecoratedMethod, decorator: expr
    ) -> None:
        super().__init__(klass)
        self.function = function
        self.decorator = decorator

    def emit_function(
        self,
        node: ast.FunctionDef | ast.AsyncFunctionDef,
        code_gen: Static38CodeGenerator,
    ) -> str:
        self.emit_function_body(
            node, code_gen, self.decorator.lineno, self.get_function_body()
        )
        return node.name

    def emit_function_body(
        self,
        node: ast.FunctionDef | ast.AsyncFunctionDef,
        code_gen: Static38CodeGenerator,
        first_lineno: int,
        body: List[ast.stmt],
    ) -> CodeGenerator:
        code_gen.visit(self.decorator)
        self.function.emit_function_body(
            node, code_gen, first_lineno, self.get_function_body()
        )
        code_gen.emit("CALL_FUNCTION", 1)
        return code_gen

    def get_function_body(self) -> List[ast.stmt]:
        return self.function.get_function_body()

    def bind_function_inner(
        self, node: Union[FunctionDef, AsyncFunctionDef], visitor: TypeBinder
    ) -> None:
        self.function.bind_function_inner(node, visitor)

    @property
    def real_function(self) -> Function:
        function = self.function
        while not isinstance(function, Function):
            function = function.function
        return function

    @property
    def func_name(self) -> str:
        return self.function.func_name

    @property
    def is_final(self) -> bool:
        return self.function.is_final

    @property
    def return_type(self) -> TypeRef:
        return self.function.return_type

    @property
    def donotcompile(self) -> bool:
        return self.function.donotcompile

    def set_container_type(self, container_type: Optional[Class]) -> None:
        self.function.set_container_type(container_type)


class TransparentDecoratedMethod(DecoratedMethod):
    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        return self.function.bind_call(node, visitor, type_ctx)

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        return self.function.emit_call(node, code_gen)

    def emit_load_attr_from(
        self, node: Attribute, code_gen: Static38CodeGenerator, klass: Class
    ) -> None:
        self.function.emit_load_attr_from(node, code_gen, klass)

    def emit_store_attr_to(
        self, node: Attribute, code_gen: Static38CodeGenerator, klass: Class
    ) -> None:
        self.function.emit_store_attr_to(node, code_gen, klass)

    def resolve_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: ReferenceVisitor,
    ) -> Optional[Value]:
        return self.function.resolve_descr_get(node, inst, ctx, visitor)

    def resolve_attr(
        self, node: ast.Attribute, visitor: ReferenceVisitor
    ) -> Optional[Value]:
        return self.function.resolve_attr(node, visitor)

    def bind_function_self(
        self,
        node: Union[FunctionDef, AsyncFunctionDef],
        scope: BindingScope,
        visitor: TypeBinder,
    ) -> None:
        self.function.bind_function_self(node, scope, visitor)


class StaticMethod(DecoratedMethod):
    def __init__(self, function: Function | DecoratedMethod, decorator: expr) -> None:
        super().__init__(STATIC_METHOD_TYPE, function, decorator)

    @property
    def name(self) -> str:
        return "staticmethod " + self.real_function.qualname

    def replace_function(self, func: Function) -> Function | DecoratedMethod:
        return StaticMethod(self.function.replace_function(func), self.decorator)

    def bind_function_self(
        self,
        node: Union[FunctionDef, AsyncFunctionDef],
        scope: BindingScope,
        visitor: TypeBinder,
    ) -> None:
        pass

    def resolve_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: ReferenceVisitor,
    ) -> Optional[Value]:
        if inst is None:
            return self.function
        else:
            # Using .real_function here might not be adequate when we start getting more
            # complex signature changing decorators
            return StaticMethodInstanceBound(self.real_function, node)


class BoundClassMethod(Object[Class]):
    def __init__(
        self,
        function: Function,
        klass: Class,
        self_expr: ast.expr,
        is_instance_call: bool,
    ) -> None:
        super().__init__(CLASS_METHOD_TYPE)
        self.function = function
        self.klass = klass
        self.self_expr = self_expr
        self.is_instance_call = is_instance_call

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        arg_mapping = ClassMethodArgMapping(
            self.function,
            node,
            self.self_expr,
            is_instance_call=self.is_instance_call,
        )
        arg_mapping.bind_args(visitor, skip_self=True)

        visitor.set_type(node, self.function.return_type.resolved().instance)
        visitor.set_node_data(node, ArgMapping, arg_mapping)
        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if self.function.func_name in NON_VIRTUAL_METHODS:
            return super().emit_call(node, code_gen)

        arg_mapping: ArgMapping = code_gen.get_node_data(node, ArgMapping)
        arg_mapping.emit(code_gen)


class ClassMethod(DecoratedMethod):
    def __init__(self, function: Function | DecoratedMethod, decorator: expr) -> None:
        super().__init__(CLASS_METHOD_TYPE, function, decorator)

    @property
    def name(self) -> str:
        return "classmethod " + self.real_function.qualname

    def replace_function(self, func: Function) -> Function | DecoratedMethod:
        return ClassMethod(self.function.replace_function(func), self.decorator)

    def bind_function_self(
        self,
        node: Union[FunctionDef, AsyncFunctionDef],
        scope: BindingScope,
        visitor: TypeBinder,
    ) -> None:
        if node.args.args:
            klass = visitor.maybe_get_current_class()
            if klass is not None:
                visitor.set_param(node.args.args[0], klass, scope)
            else:
                visitor.set_param(node.args.args[0], DYNAMIC, scope)

    def resolve_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: ReferenceVisitor,
    ) -> Optional[Value]:
        return BoundClassMethod(
            self.real_function, ctx, node.value, is_instance_call=inst is not None
        )


class PropertyMethod(DecoratedMethod):
    def __init__(
        self,
        function: Function | DecoratedMethod,
        decorator: expr,
        property_type: Optional[Class] = None,
    ) -> None:
        super().__init__(property_type or PROPERTY_TYPE, function, decorator)

    def replace_function(self, func: Function) -> Function | DecoratedMethod:
        return PropertyMethod(self.function.replace_function(func), self.decorator)

    @property
    def name(self) -> str:
        return self.real_function.qualname

    def resolve_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: ReferenceVisitor,
    ) -> Optional[Value]:
        if inst is None:
            return DYNAMIC_TYPE
        else:
            return self.function.return_type.resolved().instance

    def emit_load_attr_from(
        self, node: Attribute, code_gen: Static38CodeGenerator, klass: Class
    ) -> None:
        if self.function.is_final or klass.is_final:
            code_gen.emit("EXTENDED_ARG", 0)
            code_gen.emit("INVOKE_FUNCTION", (self.getter_type_descr, 1))
        else:
            code_gen.emit_invoke_method(self.getter_type_descr, 0)

    def emit_store_attr_to(
        self, node: Attribute, code_gen: Static38CodeGenerator, klass: Class
    ) -> None:
        code_gen.emit("ROT_TWO")
        if self.function.is_final or klass.is_final:
            code_gen.emit("EXTENDED_ARG", 0)
            code_gen.emit("INVOKE_FUNCTION", (self.setter_type_descr, 2))
        else:
            code_gen.emit_invoke_method(self.setter_type_descr, 1)

    @property
    def container_descr(self) -> TypeDescr:
        container_type = self.real_function.container_type
        if container_type:
            return container_type.type_descr
        return (self.real_function.module_name,)

    @property
    def getter_type_descr(self) -> TypeDescr:
        return self.container_descr + ((self.function.func_name, "fget"),)

    @property
    def setter_type_descr(self) -> TypeDescr:
        return self.container_descr + ((self.function.func_name, "fset"),)


class CachedPropertyMethod(PropertyMethod):
    def __init__(self, function: Function | DecoratedMethod, decorator: expr) -> None:
        super().__init__(function, decorator, property_type=CACHED_PROPERTY_TYPE)

    def replace_function(self, func: Function) -> Function | DecoratedMethod:
        return CachedPropertyMethod(
            self.function.replace_function(func), self.decorator
        )

    def emit_function(
        self,
        node: ast.FunctionDef | ast.AsyncFunctionDef,
        code_gen: Static38CodeGenerator,
    ) -> str:
        self.function.emit_function_body(
            node, code_gen, self.decorator.lineno, self.get_function_body()
        )

        return CACHED_PROPERTY_IMPL_PREFIX + node.name


class AsyncCachedPropertyMethod(DecoratedMethod):
    def __init__(self, function: Function | DecoratedMethod, decorator: expr) -> None:
        super().__init__(ASYNC_CACHED_PROPERTY_TYPE, function, decorator)

    def replace_function(self, func: Function) -> Function | DecoratedMethod:
        return AsyncCachedPropertyMethod(
            self.function.replace_function(func), self.decorator
        )

    @property
    def name(self) -> str:
        return self.real_function.qualname

    def resolve_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: ReferenceVisitor,
    ) -> Optional[Value]:
        if inst is None:
            return DYNAMIC_TYPE
        else:
            return self.function.return_type.resolved().instance

    def emit_function(
        self,
        node: ast.FunctionDef | ast.AsyncFunctionDef,
        code_gen: Static38CodeGenerator,
    ) -> str:
        self.function.emit_function_body(
            node, code_gen, self.decorator.lineno, self.get_function_body()
        )

        return ASYNC_CACHED_PROPERTY_IMPL_PREFIX + node.name


class TypingFinalDecorator(Class):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        if isinstance(fn, DecoratedMethod):
            fn.real_function.is_final = True
        else:
            fn.is_final = True
        return TransparentDecoratedMethod(FUNCTION_TYPE, fn, decorator)

    def resolve_decorate_class(self, klass: Class) -> Class:
        klass.is_final = True
        return klass


class AllowWeakrefsDecorator(Class):
    def resolve_decorate_class(self, klass: Class) -> Class:
        klass.allow_weakrefs = True
        return klass


class ClassMethodDecorator(Class):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        if fn.klass is FUNCTION_TYPE:
            func = fn.real_function if isinstance(fn, DecoratedMethod) else fn
            args = func.args
            if args:
                klass = func.container_type
                if klass is not None:
                    args[0].type_ref = ResolvedTypeRef(TYPE_TYPE)
                else:
                    args[0].type_ref = ResolvedTypeRef(DYNAMIC_TYPE)
            return ClassMethod(fn, decorator)


class DynamicReturnDecorator(Class):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Function | DecoratedMethod:
        if isinstance(fn, DecoratedMethod):
            real_fn = fn.real_function
            self._set_dynamic_return_type(real_fn)
        if isinstance(fn, Function):
            self._set_dynamic_return_type(fn)
        return TransparentDecoratedMethod(FUNCTION_TYPE, fn, decorator)

    def _set_dynamic_return_type(self, fn: Function) -> None:
        dynamic_typeref = ResolvedTypeRef(DYNAMIC_TYPE)
        if isinstance(fn.node, AsyncFunctionDef):
            fn.return_type = AwaitableTypeRef(dynamic_typeref, fn.module.compiler)
        else:
            fn.return_type = dynamic_typeref


class StaticMethodDecorator(Class):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        if fn.klass is not FUNCTION_TYPE:
            return None

        func = fn.real_function if isinstance(fn, DecoratedMethod) else fn
        args = func.args
        if args:
            if not func.node.args.args[0].annotation:
                func.args[0].type_ref = ResolvedTypeRef(DYNAMIC_TYPE)
                fn = fn.replace_function(func)

        return StaticMethod(fn, decorator)


class InlineFunctionDecorator(Class):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Function | DecoratedMethod:
        real_fn = fn.real_function if isinstance(fn, DecoratedMethod) else fn
        if not isinstance(real_fn.node.body[0], ast.Return):
            raise TypedSyntaxError(
                "@inline only supported on functions with simple return", real_fn.node
            )

        real_fn.inline = True
        return TransparentDecoratedMethod(FUNCTION_TYPE, fn, decorator)


class DoNotCompileDecorator(Class):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        real_fn = fn.real_function if isinstance(fn, DecoratedMethod) else fn
        real_fn.donotcompile = True
        return TransparentDecoratedMethod(FUNCTION_TYPE, fn, decorator)

    def resolve_decorate_class(self, klass: Class) -> Class:
        klass.donotcompile = True
        return klass


class PropertyDecorator(Class):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        if fn.klass is not FUNCTION_TYPE:
            return None
        return PropertyMethod(fn, decorator)

    def resolve_decorate_class(self, klass: Class) -> Class:
        raise TypedSyntaxError(f"Cannot decorate a class with @property")


class CachedPropertyDecorator(Class):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        if fn.klass is not FUNCTION_TYPE:
            return None
        return CachedPropertyMethod(fn, decorator)

    def resolve_decorate_class(self, klass: Class) -> Class:
        raise TypedSyntaxError(f"Cannot decorate a class with @cached_property")


class AsyncCachedPropertyDecorator(Class):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        if fn.klass is not FUNCTION_TYPE:
            return None
        return AsyncCachedPropertyMethod(fn, decorator)

    def resolve_decorate_class(self, klass: Class) -> Class:
        raise TypedSyntaxError(f"Cannot decorate a class with @async_cached_property")


class IdentityDecorator(Class):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        return fn

    def resolve_decorate_class(self, klass: Class) -> Class:
        return klass


class BuiltinFunction(Callable[Class]):
    def __init__(
        self,
        func_name: str,
        module_name: str,
        klass: Optional[Class],
        args: Optional[Tuple[Parameter, ...]] = None,
        return_type: Optional[TypeRef] = None,
    ) -> None:
        assert isinstance(return_type, (TypeRef, type(None)))
        super().__init__(
            BUILTIN_METHOD_DESC_TYPE,
            func_name,
            module_name,
            # pyre-fixme[6]: Expected `Optional[List[Parameter]]` for 4th param but
            #  got `Optional[typing.Tuple[Parameter, ...]]`.
            args,
            {},
            0,
            None,
            None,
            return_type or ResolvedTypeRef(DYNAMIC_TYPE),
        )
        self.set_container_type(klass)

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if node.keywords:
            return super().emit_call(node, code_gen)

        code_gen.update_lineno(node)
        self.emit_call_self(node, code_gen)

    def make_generic(
        self, new_type: Class, name: GenericTypeName, type_env: TypeEnvironment
    ) -> Value:
        cur_args = self.args
        cur_ret_type = self.return_type
        if cur_args is not None and cur_ret_type is not None:
            new_args = tuple(arg.bind_generics(name, type_env) for arg in cur_args)
            new_ret_type = cur_ret_type.resolved().bind_generics(name, type_env)
            return BuiltinFunction(
                self.func_name,
                self.module_name,
                new_type,
                new_args,
                ResolvedTypeRef(new_ret_type),
            )
        else:
            return BuiltinFunction(
                self.func_name, self.module_name, new_type, None, self.return_type
            )


class BuiltinNewFunction(BuiltinFunction):
    def map_call(
        self,
        node: ast.Call,
        visitor: TypeBinder,
        self_expr: Optional[ast.expr] = None,
        args_override: Optional[List[ast.expr]] = None,
        descr_override: Optional[TypeDescr] = None,
    ) -> Tuple[ArgMapping, Value]:
        arg_mapping = ArgMapping(self, node, self_expr, args_override, descr_override)
        arg_mapping.bind_args(visitor)
        ret_type = DYNAMIC
        if args_override:
            cls_type = visitor.get_type(args_override[0])
            if isinstance(cls_type, Class):
                ret_type = cls_type.instance
                if ret_type is TYPE_TYPE:
                    # if we get a generic "type" then we don't really know
                    # what type we're producing
                    ret_type = DYNAMIC

        return arg_mapping, ret_type


class BuiltinMethodDescriptor(Callable[Class]):
    def __init__(
        self,
        func_name: str,
        container_type: Class,
        args: Optional[Tuple[Parameter, ...]] = None,
        return_type: Optional[TypeRef] = None,
        dynamic_dispatch: bool = False,
    ) -> None:
        assert isinstance(return_type, (TypeRef, type(None)))
        super().__init__(
            BUILTIN_METHOD_DESC_TYPE,
            func_name,
            container_type.type_name.module,
            # pyre-fixme[6]: Expected `Optional[List[Parameter]]` for 4th param but
            #  got `Optional[typing.Tuple[Parameter, ...]]`.
            args,
            {},
            0,
            None,
            None,
            return_type or ResolvedTypeRef(DYNAMIC_TYPE),
        )
        # When `dynamic_dispatch` is True, we will not emit INVOKE_* on this
        # method.
        self.dynamic_dispatch = dynamic_dispatch
        self.set_container_type(container_type)

    def bind_call_self(
        self,
        node: ast.Call,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
        self_expr: Optional[expr] = None,
    ) -> NarrowingEffect:
        if self.args is not None:
            return super().bind_call_self(node, visitor, type_ctx, self_expr)
        elif node.keywords:
            return super().bind_call(node, visitor, type_ctx)

        visitor.set_type(node, DYNAMIC)
        for arg in node.args:
            visitor.visitExpectedType(arg, DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE)

        return NO_EFFECT

    def resolve_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: ReferenceVisitor,
    ) -> Optional[Value]:
        if inst is None:
            return self
        else:
            return DYNAMIC if self.dynamic_dispatch else BuiltinMethod(self, node)

    def make_generic(
        self, new_type: Class, name: GenericTypeName, type_env: TypeEnvironment
    ) -> Value:
        cur_args = self.args
        cur_ret_type = self.return_type
        if cur_args is not None and cur_ret_type is not None:
            new_args = tuple(arg.bind_generics(name, type_env) for arg in cur_args)
            new_ret_type = cur_ret_type.resolved().bind_generics(name, type_env)
            return BuiltinMethodDescriptor(
                self.func_name,
                new_type,
                new_args,
                ResolvedTypeRef(new_ret_type),
            )
        else:
            return BuiltinMethodDescriptor(self.func_name, new_type)


class BuiltinMethod(Callable[Class]):
    def __init__(self, desc: BuiltinMethodDescriptor, target: ast.Attribute) -> None:
        super().__init__(
            BUILTIN_METHOD_TYPE,
            desc.func_name,
            desc.module_name,
            desc.args,
            {},
            0,
            None,
            None,
            desc.return_type,
        )
        self.desc = desc
        self.target = target
        self.set_container_type(desc.container_type)

    @property
    def name(self) -> str:
        return self.qualname

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if self.args:
            return super().bind_call_self(node, visitor, type_ctx, self.target.value)
        if node.keywords:
            return Object.bind_call(self, node, visitor, type_ctx)

        visitor.set_type(node, self.return_type.resolved().instance)
        visitor.visit(self.target.value)
        for arg in node.args:
            visitor.visitExpectedType(arg, DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE)

        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if node.keywords:
            return super().emit_call(node, code_gen)

        code_gen.update_lineno(node)

        if self.args is not None:
            self.desc.emit_call_self(node, code_gen, self.target.value)
        else:
            # Untyped method, we can still do an INVOKE_METHOD

            code_gen.visit(self.target.value)

            code_gen.update_lineno(node)
            for arg in node.args:
                code_gen.visit(arg)

            if code_gen.get_type(self.target.value).klass.is_exact:
                code_gen.emit("INVOKE_FUNCTION", (self.type_descr, len(node.args) + 1))
            else:
                code_gen.emit_invoke_method(self.type_descr, len(node.args))


def get_default_value(default: expr) -> object:
    if not isinstance(default, (Constant, Str, Num, Bytes, NameConstant, ast.Ellipsis)):

        default = AstOptimizer().visit(default)

    if isinstance(default, Str):
        return default.s
    elif isinstance(default, Num):
        return default.n
    elif isinstance(default, Bytes):
        return default.s
    elif isinstance(default, ast.Ellipsis):
        return ...
    elif isinstance(default, (ast.Constant, ast.NameConstant)):
        return default.value
    else:
        return default


# Bringing up the type system is a little special as we have dependencies
# amongst type and object
TYPE_TYPE = Class.__new__(Class)
TYPE_TYPE.type_name = TypeName("builtins", "type")
TYPE_TYPE.klass = TYPE_TYPE
TYPE_TYPE.instance = TYPE_TYPE
TYPE_TYPE.members = {}
TYPE_TYPE.is_exact = False
TYPE_TYPE.is_final = False
TYPE_TYPE.allow_weakrefs = False
TYPE_TYPE.donotcompile = False
TYPE_TYPE._mro = None
TYPE_TYPE._mro_inexact = None
TYPE_TYPE.pytype = type
TYPE_TYPE._member_nodes = {}
TYPE_TYPE.dynamic_builtinmethod_dispatch = False


class Slot(Object[TClassInv]):
    def __init__(
        self,
        type_ref: Optional[TypeRef],
        name: str,
        container_type: Class,
        assignment: Optional[AST] = None,
        assigned_on_class: bool = False,
    ) -> None:
        super().__init__(MEMBER_TYPE)
        self.container_type = container_type
        self.slot_name = name
        self.type_ref = type_ref
        self.assignment = assignment
        self.assigned_on_class = assigned_on_class

    def finish_bind(self, module: ModuleTable) -> Optional[Value]:
        if self.is_final and not self.assignment:
            raise TypedSyntaxError(
                f"Final attribute not initialized: {self.container_type.instance.name}:{self.slot_name}"
            )
        return self

    def resolve_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: ReferenceVisitor,
    ) -> Optional[Value]:
        if self.is_typed_descriptor_with_default_value():
            return self._resolved_type.instance
        if inst is None and not self.is_classvar:
            return self
        if inst and self.is_classvar and isinstance(node.ctx, ast.Store):
            raise TypedSyntaxError(
                f"Cannot assign to classvar '{self.slot_name}' on '{inst.name}' instance"
            )

        return self.decl_type.instance

    @property
    def decl_type(self) -> Class:
        return self._resolved_type.unwrap()

    @property
    def is_final(self) -> bool:
        return isinstance(self._resolved_type, FinalClass)

    @property
    def is_classvar(self) -> bool:
        # Per PEP 591, class-level Final are implicitly ClassVar
        if self.assigned_on_class and self.is_final:
            return True
        return isinstance(self._resolved_type, ClassVar)

    def is_typed_descriptor_with_default_value(self) -> bool:
        return (
            self.type_ref is not None
            and self.assigned_on_class
            and not self.is_classvar
        )

    @property
    def _resolved_type(self) -> Class:
        if tr := self.type_ref:
            return tr.resolved(is_declaration=True)
        return DYNAMIC_TYPE

    @property
    def type_descr(self) -> TypeDescr:
        return self.decl_type.type_descr

    def emit_load_from_slot(self, code_gen: Static38CodeGenerator) -> None:
        if self.is_typed_descriptor_with_default_value():
            code_gen.emit("LOAD_ATTR", code_gen.mangle(self.slot_name))
            return

        type_descr = self.container_type.type_descr
        type_descr += (self.slot_name,)
        code_gen.emit("LOAD_FIELD", type_descr)

    def emit_store_to_slot(self, code_gen: Static38CodeGenerator) -> None:
        if self.is_typed_descriptor_with_default_value():
            code_gen.emit("STORE_ATTR", code_gen.mangle(self.slot_name))
            return

        type_descr = self.container_type.type_descr
        type_descr += (self.slot_name,)
        code_gen.emit("STORE_FIELD", type_descr)


# TODO (aniketpanse): move these to a better place
OBJECT_TYPE = BuiltinObject(TypeName("builtins", "object"), bases=[])
OBJECT = OBJECT_TYPE.instance

DYNAMIC_TYPE = DynamicClass()
DYNAMIC = DYNAMIC_TYPE.instance


class BoxFunction(Object[Class]):
    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if len(node.args) != 1:
            visitor.syntax_error("box only accepts a single argument", node)

        arg = node.args[0]
        visitor.visit(arg)
        arg_type = visitor.get_type(arg)
        if isinstance(arg_type, CIntInstance):
            typ = BOOL_TYPE if arg_type.constant == TYPED_BOOL else INT_EXACT_TYPE
            visitor.set_type(node, typ.instance)
        elif isinstance(arg_type, CDoubleInstance):
            visitor.set_type(node, FLOAT_EXACT_TYPE.instance)
        elif isinstance(arg_type, CEnumInstance):
            visitor.set_type(node, arg_type.boxed)
        else:
            visitor.syntax_error(f"can't box non-primitive: {arg_type.name}", node)
        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.get_type(node.args[0]).emit_box(node.args[0], code_gen)


class UnboxFunction(Object[Class]):
    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if len(node.args) != 1:
            visitor.syntax_error("unbox only accepts a single argument", node)
        if node.keywords:
            visitor.syntax_error("unbox() takes no keyword arguments", node)

        for arg in node.args:
            visitor.visitExpectedType(arg, DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE)

        visitor.set_type(node, type_ctx or INT64_VALUE)
        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.get_type(node).emit_unbox(node.args[0], code_gen)


class LenFunction(Object[Class]):
    def __init__(self, klass: Class, boxed: bool) -> None:
        super().__init__(klass)
        self.boxed = boxed

    @property
    def name(self) -> str:
        return f"{'' if self.boxed else 'c'}len function"

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if len(node.args) != 1:
            visitor.syntax_error(
                f"len() does not accept more than one arguments ({len(node.args)} given)",
                node,
            )
        if node.keywords:
            visitor.syntax_error("len() takes no keyword arguments", node)

        arg = node.args[0]
        visitor.visitExpectedType(arg, DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE)
        arg_type = visitor.get_type(arg)
        if not self.boxed and arg_type.get_fast_len_type() is None:
            visitor.syntax_error(f"bad argument type '{arg_type.name}' for clen()", arg)

        output_type = INT_EXACT_TYPE.instance if self.boxed else INT64_TYPE.instance

        visitor.set_type(node, output_type)
        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.get_type(node.args[0]).emit_len(node, code_gen, boxed=self.boxed)


class SortedFunction(Object[Class]):
    @property
    def name(self) -> str:
        return "sorted function"

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if len(node.args) != 1:
            visitor.syntax_error(
                f"sorted() accepts one positional argument ({len(node.args)} given)",
                node,
            )
        visitor.visitExpectedType(
            node.args[0], DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE
        )
        for kw in node.keywords:
            visitor.visitExpectedType(
                kw.value, DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE
            )

        visitor.set_type(node, LIST_EXACT_TYPE.instance)
        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        super().emit_call(node, code_gen)
        code_gen.emit("REFINE_TYPE", LIST_EXACT_TYPE.type_descr)


class ExtremumFunction(Object[Class]):
    def __init__(self, klass: Class, is_min: bool) -> None:
        super().__init__(klass)
        self.is_min = is_min

    @property
    def _extremum(self) -> str:
        return "min" if self.is_min else "max"

    @property
    def name(self) -> str:
        return f"{self._extremum} function"

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if (
            # We only specialize for two args
            len(node.args) != 2
            # We don't support specialization if any kwargs are present
            or len(node.keywords) > 0
            # If we have any *args, we skip specialization
            or any(isinstance(a, ast.Starred) for a in node.args)
        ):
            return super().emit_call(node, code_gen)

        # Compile `min(a, b)` to a ternary expression, `a if a <= b else b`.
        # Similar for `max(a, b).
        endblock = code_gen.newBlock(f"{self._extremum}_end")
        elseblock = code_gen.newBlock(f"{self._extremum}_else")

        for a in node.args:
            code_gen.visit(a)

        if self.is_min:
            op = "<="
        else:
            op = ">="

        code_gen.emit("DUP_TOP_TWO")
        code_gen.emit("COMPARE_OP", op)
        code_gen.emit("POP_JUMP_IF_FALSE", elseblock)
        # Remove `b` from stack, `a` was the minimum
        code_gen.emit("POP_TOP")
        code_gen.emit("JUMP_FORWARD", endblock)
        code_gen.nextBlock(elseblock)
        # Remove `a` from the stack, `b` was the minimum
        code_gen.emit("ROT_TWO")
        code_gen.emit("POP_TOP")
        code_gen.nextBlock(endblock)


class IsInstanceEffect(NarrowingEffect):
    def __init__(
        self, name: ast.Name, prev: Value, inst: Value, visitor: TypeBinder
    ) -> None:
        self.var: str = name.id
        self.name = name
        self.prev = prev
        self.inst = inst
        reverse = prev
        if isinstance(prev, UnionInstance):
            type_args = tuple(
                ta for ta in prev.klass.type_args if not inst.klass.can_assign_from(ta)
            )
            reverse = UNION_TYPE.make_generic_type(
                type_args, visitor.compiler.type_env
            ).instance
        self.rev: Value = reverse

    def apply(
        self,
        local_types: Dict[str, Value],
        local_name_nodes: Optional[Dict[str, ast.Name]] = None,
    ) -> None:
        local_types[self.var] = self.inst
        if local_name_nodes is not None:
            local_name_nodes[self.var] = self.name

    def undo(self, local_types: Dict[str, Value]) -> None:
        local_types[self.var] = self.prev

    def reverse(
        self,
        local_types: Dict[str, Value],
        local_name_nodes: Optional[Dict[str, ast.Name]] = None,
    ) -> None:
        local_types[self.var] = self.rev
        if local_name_nodes is not None:
            local_name_nodes[self.var] = self.name


class IsInstanceFunction(Object[Class]):
    def __init__(self) -> None:
        super().__init__(FUNCTION_TYPE)

    @property
    def name(self) -> str:
        return "isinstance function"

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if node.keywords:
            visitor.syntax_error("isinstance() does not accept keyword arguments", node)
        for arg in node.args:
            visitor.visitExpectedType(arg, DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE)

        visitor.set_type(node, BOOL_TYPE.instance)
        if len(node.args) == 2:
            arg0 = node.args[0]
            if not isinstance(arg0, ast.Name):
                return NO_EFFECT

            arg1 = node.args[1]
            klass_type = None
            if isinstance(arg1, ast.Tuple):
                types = tuple(visitor.get_type(el) for el in arg1.elts)
                if all(isinstance(t, Class) for t in types):
                    klass_type = UNION_TYPE.make_generic_type(
                        types, visitor.compiler.type_env
                    )
            else:
                arg1_type = visitor.get_type(node.args[1])
                if isinstance(arg1_type, Class):
                    klass_type = arg1_type.inexact()

            if klass_type is not None:
                return IsInstanceEffect(
                    arg0,
                    visitor.get_type(arg0),
                    klass_type.instance.inexact(),
                    visitor,
                )

        return NO_EFFECT


class IsSubclassFunction(Object[Class]):
    def __init__(self) -> None:
        super().__init__(FUNCTION_TYPE)

    @property
    def name(self) -> str:
        return "issubclass function"

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if node.keywords:
            visitor.syntax_error("issubclass() does not accept keyword arguments", node)
        for arg in node.args:
            visitor.visitExpectedType(arg, DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE)
        visitor.set_type(node, BOOL_TYPE.instance)
        return NO_EFFECT


class RevealTypeFunction(Object[Class]):
    def __init__(self) -> None:
        super().__init__(FUNCTION_TYPE)

    @property
    def name(self) -> str:
        return "reveal_type function"

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if node.keywords:
            visitor.syntax_error(
                "reveal_type() does not accept keyword arguments", node
            )
        if len(node.args) != 1:
            visitor.syntax_error("reveal_type() accepts exactly one argument", node)
        arg = node.args[0]
        visitor.visit(arg)
        arg_type = visitor.get_type(arg)
        msg = f"reveal_type({to_expr(arg)}): '{arg_type.name}'"
        if isinstance(arg, ast.Name) and arg.id in visitor.decl_types:
            decl_type = visitor.decl_types[arg.id].type
            local_type = visitor.local_types[arg.id]
            msg += f", '{arg.id}' has declared type '{decl_type.name}' and local type '{local_type.name}'"
        visitor.syntax_error(msg, node)
        return NO_EFFECT


class NumClass(Class):
    def __init__(
        self,
        name: TypeName,
        pytype: Optional[Type[object]] = None,
        is_exact: bool = False,
        literal_value: Optional[int] = None,
        is_final: bool = False,
    ) -> None:
        bases: List[Class] = [OBJECT_TYPE]
        if literal_value is not None:
            is_exact = True
            bases = [INT_EXACT_TYPE]
        instance = NumExactInstance(self) if is_exact else NumInstance(self)
        super().__init__(
            name,
            bases,
            instance,
            pytype=pytype,
            is_exact=is_exact,
            is_final=is_final,
        )
        self.literal_value = literal_value

    def can_assign_from(self, src: Class) -> bool:
        if isinstance(src, NumClass):
            if self.literal_value is not None:
                return src.literal_value == self.literal_value
            if self.is_exact and src.is_exact and self.type_descr == src.type_descr:
                return True
        return super().can_assign_from(src)

    def exact_type(self) -> Class:
        if self.pytype is int:
            return INT_EXACT_TYPE
        if self.pytype is float:
            return FLOAT_EXACT_TYPE
        if self.pytype is complex:
            return COMPLEX_EXACT_TYPE
        return self

    def inexact_type(self) -> Class:
        if self.pytype is int:
            return INT_TYPE
        if self.pytype is float:
            return FLOAT_TYPE
        if self.pytype is complex:
            return COMPLEX_TYPE
        return self


class NumInstance(Object[NumClass]):
    def make_literal(self, literal_value: object) -> Value:
        assert isinstance(literal_value, int)
        return NumClass(
            self.klass.type_name, pytype=self.klass.pytype, literal_value=literal_value
        ).instance

    def bind_unaryop(
        self, node: ast.UnaryOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        if isinstance(node.op, (ast.USub, ast.Invert, ast.UAdd)):
            visitor.set_type(node, self)
        else:
            assert isinstance(node.op, ast.Not)
            visitor.set_type(node, BOOL_TYPE.instance)

    def bind_constant(self, node: ast.Constant, visitor: TypeBinder) -> None:
        self._bind_constant(node.value, node, visitor)

    def _bind_constant(
        self, value: object, node: ast.expr, visitor: TypeBinder
    ) -> None:
        value_inst = CONSTANT_TYPES.get(type(value), self)
        visitor.set_type(node, value_inst)

    def exact(self) -> Value:
        if self.klass.pytype is int:
            return INT_EXACT_TYPE.instance
        if self.klass.pytype is float:
            return FLOAT_EXACT_TYPE.instance
        if self.klass.pytype is complex:
            return COMPLEX_EXACT_TYPE.instance
        return self

    def inexact(self) -> Value:
        return self

    def emit_load_name(self, node: ast.Name, code_gen: Static38CodeGenerator) -> None:
        if self.klass.is_final and self.klass.literal_value is not None:
            return code_gen.emit("LOAD_CONST", self.klass.literal_value)
        return super().emit_load_name(node, code_gen)


class NumExactInstance(NumInstance):
    @property
    def name(self) -> str:
        if self.klass.literal_value is not None:
            return f"Literal[{self.klass.literal_value}]"
        return super().name

    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        ltype = visitor.get_type(node.left)
        rtype = visitor.get_type(node.right)
        if INT_EXACT_TYPE.can_assign_from(
            ltype.klass
        ) and INT_EXACT_TYPE.can_assign_from(rtype.klass):
            if isinstance(node.op, ast.Div):
                visitor.set_type(node, FLOAT_EXACT_TYPE.instance)
            else:
                visitor.set_type(node, INT_EXACT_TYPE.instance)
            return True
        return False

    def exact(self) -> Value:
        return self

    def inexact(self) -> Value:
        if self.klass.pytype is int:
            return INT_TYPE.instance
        if self.klass.pytype is float:
            return FLOAT_TYPE.instance
        if self.klass.pytype is complex:
            return COMPLEX_TYPE.instance
        return self


def parse_param(info: Dict[str, object], idx: int) -> Parameter:
    name = info.get("name", "")
    assert isinstance(name, str)

    return Parameter(
        name,
        idx,
        ResolvedTypeRef(parse_type(info)),
        "default" in info,
        info.get("default"),
        False,
    )


def parse_typed_signature(
    sig: Dict[str, object], klass: Optional[Class] = None
) -> Tuple[Tuple[Parameter, ...], Class]:
    args = sig["args"]
    assert isinstance(args, list)
    if klass is not None:
        signature = [Parameter("self", 0, ResolvedTypeRef(klass), False, None, False)]
    else:
        signature = []

    for idx, arg in enumerate(args):
        signature.append(parse_param(arg, idx + 1))
    return_info = sig["return"]
    assert isinstance(return_info, dict)
    return_type = parse_type(return_info)
    return tuple(signature), return_type


def parse_type(info: Dict[str, object]) -> Class:
    optional = info.get("optional", False)
    type = info.get("type")
    if type:
        klass = NAME_TO_TYPE.get(type)
        if klass is None:
            raise NotImplementedError("unsupported type: " + str(type))
    else:
        type_param = info.get("type_param")
        assert isinstance(type_param, int)
        klass = GenericParameter("T" + str(type_param), type_param)

    if optional:
        return OPTIONAL_TYPE.make_generic_type((klass,), BUILTIN_TYPE_ENV)

    return klass


def reflect_method_desc(
    obj: MethodDescriptorType | WrapperDescriptorType,
    klass: Class,
) -> BuiltinMethodDescriptor:
    sig = getattr(obj, "__typed_signature__", None)
    if sig is not None:
        signature, return_type = parse_typed_signature(sig, klass)

        method = BuiltinMethodDescriptor(
            obj.__name__,
            klass,
            signature,
            ResolvedTypeRef(return_type),
            dynamic_dispatch=klass.dynamic_builtinmethod_dispatch,
        )
    else:
        method = BuiltinMethodDescriptor(
            obj.__name__, klass, dynamic_dispatch=klass.dynamic_builtinmethod_dispatch
        )
    return method


def reflect_builtin_function(
    obj: BuiltinFunctionType, klass: Optional[Class] = None
) -> BuiltinFunction:
    sig = getattr(obj, "__typed_signature__", None)
    if sig is not None:
        signature, return_type = parse_typed_signature(sig)
        method = BuiltinFunction(
            obj.__name__,
            obj.__module__,
            klass,
            signature,
            ResolvedTypeRef(return_type),
        )
    else:
        if obj.__name__ == "__new__" and klass is not None:
            method = BuiltinNewFunction(obj.__name__, obj.__module__, klass)
        else:
            method = BuiltinFunction(obj.__name__, obj.__module__, klass)
    return method


def common_sequence_emit_len(
    node: ast.Call, code_gen: Static38CodeGenerator, oparg: int, boxed: bool
) -> None:
    if len(node.args) != 1:
        raise code_gen.syntax_error(
            f"Can only pass a single argument when checking sequence length", node
        )
    code_gen.visit(node.args[0])
    code_gen.emit("FAST_LEN", oparg)
    if boxed:
        code_gen.emit("PRIMITIVE_BOX", INT64_TYPE.type_descr)


def common_sequence_emit_jumpif(
    test: AST,
    next: Block,
    is_if_true: bool,
    code_gen: Static38CodeGenerator,
    oparg: int,
) -> None:
    code_gen.visit(test)
    code_gen.emit("FAST_LEN", oparg)
    code_gen.emit("POP_JUMP_IF_NONZERO" if is_if_true else "POP_JUMP_IF_ZERO", next)


def common_sequence_emit_forloop(
    node: ast.For, code_gen: Static38CodeGenerator, seq_type: int
) -> None:
    if seq_type == SEQ_TUPLE:
        fast_len_oparg = FAST_LEN_TUPLE
    elif seq_type in {SEQ_LIST, SEQ_CHECKED_LIST}:
        fast_len_oparg = FAST_LEN_LIST
    else:
        fast_len_oparg = FAST_LEN_ARRAY
    descr = ("__static__", "int64")
    start = code_gen.newBlock(f"seq_forloop_start")
    anchor = code_gen.newBlock(f"seq_forloop_anchor")
    after = code_gen.newBlock(f"seq_forloop_after")
    with code_gen.new_loopidx() as loop_idx:
        code_gen.set_lineno(node)
        code_gen.push_loop(FOR_LOOP, start, after)
        code_gen.visit(node.iter)

        code_gen.emit("PRIMITIVE_LOAD_CONST", (0, TYPED_INT64))
        code_gen.emit("STORE_LOCAL", (loop_idx, descr))
        code_gen.nextBlock(start)
        code_gen.emit("DUP_TOP")  # used for SEQUENCE_GET
        code_gen.emit("DUP_TOP")  # used for FAST_LEN
        code_gen.emit("FAST_LEN", fast_len_oparg)
        code_gen.emit("LOAD_LOCAL", (loop_idx, descr))
        code_gen.emit("PRIMITIVE_COMPARE_OP", PRIM_OP_GT_INT)
        code_gen.emit("POP_JUMP_IF_ZERO", anchor)
        code_gen.emit("LOAD_LOCAL", (loop_idx, descr))
        if seq_type == SEQ_TUPLE:
            # todo - we need to implement TUPLE_GET which supports primitive index
            code_gen.emit("PRIMITIVE_BOX", INT64_TYPE.type_descr)
            code_gen.emit("BINARY_SUBSCR", 2)
        else:
            code_gen.emit("SEQUENCE_GET", seq_type | SEQ_SUBSCR_UNCHECKED)
        code_gen.emit("LOAD_LOCAL", (loop_idx, descr))
        code_gen.emit("PRIMITIVE_LOAD_CONST", (1, TYPED_INT64))
        code_gen.emit("PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        code_gen.emit("STORE_LOCAL", (loop_idx, descr))
        code_gen.visit(node.target)
        code_gen.visit(node.body)
        code_gen.emit("JUMP_ABSOLUTE", start)
        code_gen.nextBlock(anchor)
        code_gen.emit("POP_TOP")  # Pop loop index
        code_gen.emit("POP_TOP")  # Pop list
        code_gen.pop_loop()

        if node.orelse:
            code_gen.visit(node.orelse)
        code_gen.nextBlock(after)


class TupleClass(Class):
    def __init__(self, is_exact: bool = False) -> None:
        instance = TupleExactInstance(self) if is_exact else TupleInstance(self)
        super().__init__(
            type_name=TypeName("builtins", "tuple"),
            instance=instance,
            is_exact=is_exact,
            pytype=tuple,
        )
        self.members["__new__"] = BuiltinNewFunction(
            "__new__",
            "builtins",
            self,
            (
                Parameter("cls", 0, ResolvedTypeRef(TYPE_TYPE), False, None, False),
                Parameter("x", 0, ResolvedTypeRef(OBJECT_TYPE), True, (), False),
            ),
            ResolvedTypeRef(self),
        )

    def exact_type(self) -> Class:
        return TUPLE_EXACT_TYPE

    def inexact_type(self) -> Class:
        return TUPLE_TYPE


class TupleInstance(Object[TupleClass]):
    def get_fast_len_type(self) -> int:
        return FAST_LEN_TUPLE | ((not self.klass.is_exact) << 4)

    def emit_len(
        self, node: ast.Call, code_gen: Static38CodeGenerator, boxed: bool
    ) -> None:
        return common_sequence_emit_len(
            node, code_gen, self.get_fast_len_type(), boxed=boxed
        )

    def emit_jumpif(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        return common_sequence_emit_jumpif(
            test, next, is_if_true, code_gen, self.get_fast_len_type()
        )

    def emit_binop(self, node: ast.BinOp, code_gen: Static38CodeGenerator) -> None:
        if maybe_emit_sequence_repeat(node, code_gen):
            return
        code_gen.defaultVisit(node)

    def exact(self) -> Value:
        return TUPLE_EXACT_TYPE.instance

    def inexact(self) -> Value:
        return TUPLE_TYPE.instance


class TupleExactInstance(TupleInstance):
    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        rtype = visitor.get_type(node.right).klass
        if isinstance(node.op, ast.Mult) and (
            INT_TYPE.can_assign_from(rtype) or rtype in SIGNED_CINT_TYPES
        ):
            visitor.set_type(node, TUPLE_EXACT_TYPE.instance)
            return True
        return super().bind_binop(node, visitor, type_ctx)

    def bind_reverse_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        ltype = visitor.get_type(node.left).klass
        if isinstance(node.op, ast.Mult) and (
            INT_TYPE.can_assign_from(ltype) or ltype in SIGNED_CINT_TYPES
        ):
            visitor.set_type(node, TUPLE_EXACT_TYPE.instance)
            return True
        return super().bind_reverse_binop(node, visitor, type_ctx)

    def emit_forloop(self, node: ast.For, code_gen: Static38CodeGenerator) -> None:
        if not isinstance(node.target, ast.Name):
            # We don't yet support `for a, b in my_tuple: ...`
            return super().emit_forloop(node, code_gen)

        return common_sequence_emit_forloop(node, code_gen, SEQ_TUPLE)


class SetClass(Class):
    def __init__(self, is_exact: bool = False) -> None:
        super().__init__(
            type_name=TypeName("builtins", "set"),
            instance=SetInstance(self),
            is_exact=is_exact,
            pytype=tuple,
        )

    def exact_type(self) -> Class:
        return SET_EXACT_TYPE

    def inexact_type(self) -> Class:
        return SET_TYPE


class SetInstance(Object[SetClass]):
    def get_fast_len_type(self) -> int:
        return FAST_LEN_SET | ((not self.klass.is_exact) << 4)

    def emit_len(
        self, node: ast.Call, code_gen: Static38CodeGenerator, boxed: bool
    ) -> None:
        if len(node.args) != 1:
            raise code_gen.syntax_error(
                "Can only pass a single argument when checking set length", node
            )
        code_gen.visit(node.args[0])
        code_gen.emit("FAST_LEN", self.get_fast_len_type())
        if boxed:
            code_gen.emit("PRIMITIVE_BOX", INT64_TYPE.type_descr)

    def emit_jumpif(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.visit(test)
        code_gen.emit("FAST_LEN", self.get_fast_len_type())
        code_gen.emit("POP_JUMP_IF_NONZERO" if is_if_true else "POP_JUMP_IF_ZERO", next)

    def exact(self) -> Value:
        return SET_EXACT_TYPE.instance

    def inexact(self) -> Value:
        return SET_TYPE.instance


def maybe_emit_sequence_repeat(
    node: ast.BinOp, code_gen: Static38CodeGenerator
) -> bool:
    if not isinstance(node.op, ast.Mult):
        return False
    for seq, num, rev in [
        (node.left, node.right, 0),
        (node.right, node.left, SEQ_REPEAT_REVERSED),
    ]:
        seq_type = code_gen.get_type(seq).klass
        num_type = code_gen.get_type(num).klass
        oparg = None
        if TUPLE_TYPE.can_assign_from(seq_type):
            oparg = SEQ_TUPLE
        elif LIST_TYPE.can_assign_from(seq_type):
            oparg = SEQ_LIST
        if oparg is None:
            continue
        if num_type in SIGNED_CINT_TYPES:
            oparg |= SEQ_REPEAT_PRIMITIVE_NUM
        elif not INT_TYPE.can_assign_from(num_type):
            continue
        if not seq_type.is_exact:
            oparg |= SEQ_REPEAT_INEXACT_SEQ
        if not num_type.is_exact:
            oparg |= SEQ_REPEAT_INEXACT_NUM
        oparg |= rev
        code_gen.visit(seq)
        code_gen.visit(num)
        code_gen.emit("REFINE_TYPE", num_type.type_descr)
        code_gen.emit("SEQUENCE_REPEAT", oparg)
        return True
    return False


class ListAppendMethod(BuiltinMethodDescriptor):
    def resolve_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: ReferenceVisitor,
    ) -> Optional[Value]:
        if inst is None:
            return self
        else:
            return ListAppendBuiltinMethod(self, node)


class ListAppendBuiltinMethod(BuiltinMethod):
    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if len(node.args) == 1 and not node.keywords:
            code_gen.visit(self.target.value)
            code_gen.visit(node.args[0])
            code_gen.emit("LIST_APPEND", 1)
            return

        return super().emit_call(node, code_gen)


class ListClass(Class):
    def __init__(self, is_exact: bool = False) -> None:
        instance = ListExactInstance(self) if is_exact else ListInstance(self)
        super().__init__(
            type_name=TypeName("builtins", "list"),
            instance=instance,
            is_exact=is_exact,
            pytype=list,
        )
        if is_exact:
            self.members["append"] = ListAppendMethod("append", self)
        # list inherits object.__new__
        del self.members["__new__"]

        self.members["__init__"] = BuiltinMethodDescriptor(
            "__init__",
            self,
            (
                Parameter("self", 0, ResolvedTypeRef(self), False, None, False),
                # Ideally we would mark this as Optional and allow calling without
                # providing the argument...
                Parameter("iterable", 0, ResolvedTypeRef(OBJECT_TYPE), True, (), False),
            ),
            ResolvedTypeRef(NONE_TYPE),
        )

    def exact_type(self) -> Class:
        return LIST_EXACT_TYPE

    def inexact_type(self) -> Class:
        return LIST_TYPE


class ListInstance(Object[ListClass]):
    def get_fast_len_type(self) -> int:
        return FAST_LEN_LIST | ((not self.klass.is_exact) << 4)

    def get_subscr_type(self) -> int:
        return SEQ_LIST_INEXACT

    def emit_len(
        self, node: ast.Call, code_gen: Static38CodeGenerator, boxed: bool
    ) -> None:
        return common_sequence_emit_len(
            node, code_gen, self.get_fast_len_type(), boxed=boxed
        )

    def emit_jumpif(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        return common_sequence_emit_jumpif(
            test, next, is_if_true, code_gen, self.get_fast_len_type()
        )

    def bind_subscr(
        self,
        node: ast.Subscript,
        type: Value,
        visitor: TypeBinder,
        type_ctx: Optional[Class] = None,
    ) -> None:
        if type.klass not in SIGNED_CINT_TYPES:
            super().bind_subscr(node, type, visitor)
        visitor.set_type(node, DYNAMIC)

    def emit_load_subscr(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        if code_gen.get_type(node.slice).klass not in SIGNED_CINT_TYPES:
            return super().emit_load_subscr(node, code_gen)
        code_gen.emit("SEQUENCE_GET", self.get_subscr_type())

    def emit_store_subscr(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        if code_gen.get_type(node.slice).klass not in SIGNED_CINT_TYPES:
            return super().emit_store_subscr(node, code_gen)
        code_gen.emit("SEQUENCE_SET", self.get_subscr_type())

    def emit_delete_subscr(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        if code_gen.get_type(node.slice).klass not in SIGNED_CINT_TYPES:
            return super().emit_delete_subscr(node, code_gen)
        code_gen.emit("LIST_DEL", self.get_subscr_type())

    def emit_binop(self, node: ast.BinOp, code_gen: Static38CodeGenerator) -> None:
        if maybe_emit_sequence_repeat(node, code_gen):
            return
        code_gen.defaultVisit(node)

    def exact(self) -> Value:
        return LIST_EXACT_TYPE.instance

    def inexact(self) -> Value:
        return LIST_TYPE.instance


class ListExactInstance(ListInstance):
    def get_subscr_type(self) -> int:
        return SEQ_LIST

    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        rtype = visitor.get_type(node.right).klass
        if isinstance(node.op, ast.Mult) and (
            INT_TYPE.can_assign_from(rtype) or rtype in SIGNED_CINT_TYPES
        ):
            visitor.set_type(node, LIST_EXACT_TYPE.instance)
            return True
        return super().bind_binop(node, visitor, type_ctx)

    def bind_reverse_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        ltype = visitor.get_type(node.left).klass
        if isinstance(node.op, ast.Mult) and (
            INT_TYPE.can_assign_from(ltype) or ltype in SIGNED_CINT_TYPES
        ):
            visitor.set_type(node, LIST_EXACT_TYPE.instance)
            return True
        return super().bind_reverse_binop(node, visitor, type_ctx)

    def emit_forloop(self, node: ast.For, code_gen: Static38CodeGenerator) -> None:
        if not isinstance(node.target, ast.Name):
            # We don't yet support `for a, b in my_list: ...`
            return super().emit_forloop(node, code_gen)

        return common_sequence_emit_forloop(node, code_gen, SEQ_LIST)


class StrClass(Class):
    def __init__(self, is_exact: bool = False) -> None:
        super().__init__(
            type_name=TypeName("builtins", "str"),
            instance=StrInstance(self),
            is_exact=is_exact,
            pytype=str,
        )

    def exact_type(self) -> Class:
        return STR_EXACT_TYPE

    def inexact_type(self) -> Class:
        return STR_TYPE


class StrInstance(Object[StrClass]):
    def get_fast_len_type(self) -> int:
        return FAST_LEN_STR | ((not self.klass.is_exact) << 4)

    def emit_len(
        self, node: ast.Call, code_gen: Static38CodeGenerator, boxed: bool
    ) -> None:
        return common_sequence_emit_len(
            node, code_gen, self.get_fast_len_type(), boxed=boxed
        )

    def emit_jumpif(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        return common_sequence_emit_jumpif(
            test, next, is_if_true, code_gen, self.get_fast_len_type()
        )

    def exact(self) -> Value:
        return STR_EXACT_TYPE.instance

    def inexact(self) -> Value:
        return STR_TYPE.instance


class DictClass(Class):
    def __init__(self, is_exact: bool = False) -> None:
        super().__init__(
            type_name=TypeName("builtins", "dict"),
            instance=DictInstance(self),
            is_exact=is_exact,
            pytype=dict,
        )

    def exact_type(self) -> Class:
        return DICT_EXACT_TYPE

    def inexact_type(self) -> Class:
        return DICT_TYPE


class DictInstance(Object[DictClass]):
    def get_fast_len_type(self) -> int:
        return FAST_LEN_DICT | ((not self.klass.is_exact) << 4)

    def emit_len(
        self, node: ast.Call, code_gen: Static38CodeGenerator, boxed: bool
    ) -> None:
        if len(node.args) != 1:
            raise code_gen.syntax_error(
                "Can only pass a single argument when checking dict length", node
            )
        code_gen.visit(node.args[0])
        code_gen.emit("FAST_LEN", self.get_fast_len_type())
        if boxed:
            code_gen.emit("PRIMITIVE_BOX", INT64_TYPE.type_descr)

    def emit_jumpif(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.visit(test)
        code_gen.emit("FAST_LEN", self.get_fast_len_type())
        code_gen.emit("POP_JUMP_IF_NONZERO" if is_if_true else "POP_JUMP_IF_ZERO", next)

    def exact(self) -> Value:
        return DICT_EXACT_TYPE.instance

    def inexact(self) -> Value:
        return DICT_TYPE.instance


class BoolClass(Class):
    def __init__(self) -> None:
        super().__init__(
            TypeName("builtins", "bool"), [INT_TYPE], pytype=bool, is_exact=True
        )
        self.members["__new__"] = BuiltinNewFunction(
            "__new__",
            "builtins",
            self,
            (
                Parameter("cls", 0, ResolvedTypeRef(TYPE_TYPE), False, None, False),
                Parameter("x", 0, ResolvedTypeRef(OBJECT_TYPE), True, False, False),
            ),
            ResolvedTypeRef(self),
        )

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if len(node.args) == 1 and not node.keywords:
            arg = node.args[0]
            visitor.visit(arg)
            arg_type = visitor.get_type(arg)
            if isinstance(arg_type, CIntInstance) and arg_type.constant == TYPED_BOOL:
                visitor.set_type(node, BOOL_TYPE.instance)
                return NO_EFFECT

        return super().bind_call(node, visitor, type_ctx)

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if len(node.args) == 1 and not node.keywords:
            arg = node.args[0]
            arg_type = code_gen.get_type(arg)
            if isinstance(arg_type, CIntInstance) and arg_type.constant == TYPED_BOOL:
                arg_type.emit_box(arg, code_gen)
                return

        super().emit_call(node, code_gen)


class AnnotatedType(Class):
    def resolve_subscr(
        self,
        node: ast.Subscript,
        type: Value,
        visitor: AnnotationVisitor,
    ) -> Optional[Value]:
        slice = node.slice

        if not isinstance(slice, ast.Index):
            visitor.syntax_error("can't slice generic types", node)
            return DYNAMIC

        val = slice.value

        if not isinstance(val, ast.Tuple) or len(val.elts) <= 1:
            visitor.syntax_error(
                "Annotated types must be parametrized by at least one annotation.", node
            )
            return None
        actual_type, *annotations = val.elts
        return visitor.resolve_annotation(actual_type)


FUNCTION_TYPE = Class(TypeName("types", "FunctionType"), is_exact=True)
METHOD_TYPE = Class(TypeName("types", "MethodType"), is_exact=True)
MEMBER_TYPE = Class(TypeName("types", "MemberDescriptorType"), is_exact=True)
BUILTIN_METHOD_DESC_TYPE = Class(
    TypeName("types", "MethodDescriptorType"), is_exact=True
)
BUILTIN_METHOD_TYPE = Class(TypeName("types", "BuiltinMethodType"), is_exact=True)
SLICE_TYPE = Class(TypeName("builtins", "slice"), is_exact=True)

# This is a special case. Normally, we populate type members by providing a `pytype` to the
# type constructor. However that relies on BUILTIN_METHOD_DESC_TYPE being defined, so we
# populate the members here, after BUILTIN_METHOD_DESC_TYPE is created.
OBJECT_TYPE.make_type_dict(object)

# builtin types
NONE_TYPE = NoneType()
STR_TYPE = StrClass()
STR_EXACT_TYPE = StrClass(is_exact=True)
INT_TYPE = NumClass(TypeName("builtins", "int"), pytype=int)
INT_EXACT_TYPE = NumClass(TypeName("builtins", "int"), pytype=int, is_exact=True)
FLOAT_TYPE = NumClass(TypeName("builtins", "float"), pytype=float)
FLOAT_EXACT_TYPE = NumClass(TypeName("builtins", "float"), pytype=float, is_exact=True)
COMPLEX_TYPE = NumClass(TypeName("builtins", "complex"), pytype=complex)
COMPLEX_EXACT_TYPE = NumClass(
    TypeName("builtins", "complex"), pytype=complex, is_exact=True
)
BYTES_TYPE = Class(TypeName("builtins", "bytes"), [OBJECT_TYPE], pytype=bytes)
BOOL_TYPE = BoolClass()
ELLIPSIS_TYPE = Class(
    TypeName("builtins", "ellipsis"), [OBJECT_TYPE], pytype=type(...), is_exact=True
)
DICT_TYPE = DictClass(is_exact=False)
DICT_EXACT_TYPE = DictClass(is_exact=True)
TUPLE_TYPE = TupleClass()
TUPLE_EXACT_TYPE = TupleClass(is_exact=True)
SET_TYPE = SetClass()
SET_EXACT_TYPE = SetClass(is_exact=True)
LIST_TYPE = ListClass()
LIST_EXACT_TYPE = ListClass(is_exact=True)

BASE_EXCEPTION_TYPE = Class(TypeName("builtins", "BaseException"), pytype=BaseException)
EXCEPTION_TYPE = Class(
    TypeName("builtins", "Exception"),
    bases=[BASE_EXCEPTION_TYPE],
    pytype=Exception,
)
STATIC_METHOD_TYPE = StaticMethodDecorator(
    TypeName("builtins", "staticmethod"),
    pytype=staticmethod,
)
CLASS_METHOD_TYPE = ClassMethodDecorator(
    TypeName("builtins", "classmethod"),
    pytype=classmethod,
)
FINAL_METHOD_TYPE = TypingFinalDecorator(TypeName("typing", "final"))
ALLOW_WEAKREFS_TYPE = AllowWeakrefsDecorator(TypeName("__static__", "allow_weakrefs"))
DYNAMIC_RETURN_TYPE = DynamicReturnDecorator(TypeName("__static__", "dynamic_return"))
INLINE_TYPE = InlineFunctionDecorator(TypeName("__static__", "inline"))
DONOTCOMPILE_TYPE = DoNotCompileDecorator(TypeName("__static__", "_donotcompile"))
PROPERTY_TYPE = PropertyDecorator(TypeName("builtins", "property"), pytype=property)
CACHED_PROPERTY_TYPE = CachedPropertyDecorator(TypeName("cinder", "cached_property"))
ASYNC_CACHED_PROPERTY_TYPE = AsyncCachedPropertyDecorator(
    TypeName("cinder", "async_cached_property")
)
IDENTITY_DECORATOR_TYPE = IdentityDecorator(
    TypeName("__strict__", "<identity-decorator>")
)

RESOLVED_INT_TYPE = ResolvedTypeRef(INT_TYPE)
RESOLVED_STR_TYPE = ResolvedTypeRef(STR_TYPE)
RESOLVED_NONE_TYPE = ResolvedTypeRef(NONE_TYPE)

TYPE_TYPE.bases = [OBJECT_TYPE]
TYPE_TYPE.make_type_dict(type)

CONSTANT_TYPES: Mapping[Type[object], Value] = {
    str: STR_EXACT_TYPE.instance,
    int: INT_EXACT_TYPE.instance,
    float: FLOAT_EXACT_TYPE.instance,
    complex: COMPLEX_EXACT_TYPE.instance,
    bytes: BYTES_TYPE.instance,
    bool: BOOL_TYPE.instance,
    type(None): NONE_TYPE.instance,
    tuple: TUPLE_EXACT_TYPE.instance,
    type(...): ELLIPSIS_TYPE.instance,
    frozenset: SET_TYPE.instance,
}

NAMED_TUPLE_TYPE = Class(TypeName("typing", "NamedTuple"))
PROTOCOL_TYPE = Class(TypeName("typing", "Protocol"))
ANNOTATED_TYPE = AnnotatedType(TypeName("typing_extensions", "Annotated"))


class TypeWrapper(GenericClass):
    def unwrap(self) -> Class:
        return self.type_args[0]


class FinalClass(TypeWrapper):
    pass


class ClassVar(TypeWrapper):
    pass


class UnionTypeName(GenericTypeName):
    @property
    def opt_type(self) -> Optional[Class]:
        """If we're an Optional (i.e. Union[T, None]), return T, otherwise None."""
        # Assumes well-formed union (no duplicate elements, >1 element)
        opt_type = None
        if len(self.args) == 2:
            if self.args[0] is NONE_TYPE:
                opt_type = self.args[1]
            elif self.args[1] is NONE_TYPE:
                opt_type = self.args[0]
        return opt_type

    @property
    def float_type(self) -> Optional[Class]:
        """Collapse `float | int` and `int | float` to `float`. Otherwise, return None."""
        if len(self.args) == 2:
            if self.args[0] is FLOAT_TYPE and self.args[1] is INT_TYPE:
                return self.args[0]
            if self.args[1] is FLOAT_TYPE and self.args[0] is INT_TYPE:
                return self.args[1]
        return None

    @property
    def type_descr(self) -> TypeDescr:
        opt_type = self.opt_type
        if opt_type is not None:
            return opt_type.type_descr + ("?",)
        # the runtime does not support unions beyond optional, so just fall back
        # to dynamic for runtime purposes
        return DYNAMIC_TYPE.type_descr

    @property
    def friendly_name(self) -> str:
        opt_type = self.opt_type
        if opt_type is not None:
            return f"Optional[{opt_type.instance.name}]"
        float_type = self.float_type
        if float_type is not None:
            return float_type.instance.name
        return super().friendly_name


class UnionType(GenericClass):
    type_name: UnionTypeName
    # Union is a variadic generic, so we don't give the unbound Union any
    # GenericParameters, and we allow it to accept any number of type args.
    is_variadic = True

    def __init__(
        self,
        type_name: Optional[UnionTypeName] = None,
        type_def: Optional[GenericClass] = None,
        instance_type: Optional[Type[Object[Class]]] = None,
        type_env: Optional[TypeEnvironment] = None,
    ) -> None:
        instance_type = instance_type or UnionInstance
        super().__init__(
            type_name or UnionTypeName("typing", "Union", ()),
            bases=[],
            instance=instance_type(self),
            type_def=type_def,
        )
        self.type_env = type_env

    @property
    def opt_type(self) -> Optional[Class]:
        return self.type_name.opt_type

    def exact_type(self) -> Class:
        type_env = self.type_env
        if type_env is not None:
            return UNION_TYPE.make_generic_type(
                tuple(a.exact_type() for a in self.type_args), type_env
            )
        return self

    def inexact_type(self) -> Class:
        type_env = self.type_env
        if type_env is not None:
            return UNION_TYPE.make_generic_type(
                tuple(a.inexact_type() for a in self.type_args), type_env
            )
        return self

    def is_subclass_of(self, src: Class) -> bool:
        # The intuitive argument for why we require each element of the union
        # to be a subclass of src is that we only want to allow assigning a union into a wider
        # union - using `any()` here would allow you to assign a wide union into one of its
        # elements.
        return all(t.is_subclass_of(src) for t in self.type_args)

    def make_generic_type(
        self,
        index: Tuple[Class, ...],
        type_env: TypeEnvironment,
    ) -> Class:
        instantiations = type_env._generic_types.get(self)
        if instantiations is not None:
            instance = instantiations.get(index)
            if instance is not None:
                return instance
        else:
            type_env._generic_types[self] = instantiations = {}

        type_args = self._simplify_args(index)
        if len(type_args) == 1 and not type_args[0].is_generic_parameter:
            return type_args[0]
        type_name = UnionTypeName(self.type_name.module, self.type_name.name, type_args)
        if any(isinstance(a, CType) for a in type_args):
            raise TypedSyntaxError(
                f"invalid union type {type_name.friendly_name}; unions cannot include primitive types"
            )
        ThisUnionType = type(self)
        if type_name.opt_type is not None:
            ThisUnionType = OptionalType
        instantiations[index] = concrete = ThisUnionType(
            type_name, type_def=self, type_env=type_env
        )
        return concrete

    def _simplify_args(self, args: Sequence[Class]) -> Tuple[Class, ...]:
        args = self._flatten_args(args)
        remove = set()
        for i, arg1 in enumerate(args):
            if i in remove:
                continue
            for j, arg2 in enumerate(args):
                # TODO this should be is_subtype_of once we split that from can_assign_from
                if i != j and arg1.can_assign_from(arg2):
                    remove.add(j)
        return tuple(arg for i, arg in enumerate(args) if i not in remove)

    def _flatten_args(self, args: Sequence[Class]) -> Sequence[Class]:
        new_args = []
        for arg in args:
            if isinstance(arg, UnionType):
                new_args.extend(self._flatten_args(arg.type_args))
            else:
                new_args.append(arg)
        return new_args


class UnionInstance(Object[UnionType]):
    def _generic_bind(
        self,
        node: ast.AST,
        callback: typingCallable[[Class, List[Class]], object],
        description: str,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> List[object]:
        if self.klass.is_generic_type_definition:
            visitor.syntax_error(f"cannot {description} unbound Union", node)
        result_types: List[Class] = []
        ret_types: List[object] = []
        try:
            for el in self.klass.type_args:
                ret_types.append(callback(el, result_types))
        except TypedSyntaxError as e:
            visitor.syntax_error(f"{self.name}: {e.msg}", node)

        union = UNION_TYPE.make_generic_type(
            tuple(result_types), visitor.compiler.type_env
        )
        visitor.set_type(node, union.instance)
        return ret_types

    def bind_attr(
        self, node: ast.Attribute, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        def cb(el: Class, result_types: List[Class]) -> None:
            el.instance.bind_attr(node, visitor, type_ctx)
            result_types.append(visitor.get_type(node).klass)

        self._generic_bind(node, cb, "access attribute from", visitor, type_ctx)

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        def cb(el: Class, result_types: List[Class]) -> NarrowingEffect:
            res = el.instance.bind_call(node, visitor, type_ctx)
            result_types.append(visitor.get_type(node).klass)
            return res

        self._generic_bind(node, cb, "call", visitor, type_ctx)
        return NO_EFFECT

    def bind_subscr(
        self,
        node: ast.Subscript,
        type: Value,
        visitor: TypeBinder,
        type_ctx: Optional[Class] = None,
    ) -> None:
        def cb(el: Class, result_types: List[Class]) -> None:
            el.instance.bind_subscr(node, type, visitor)
            result_types.append(visitor.get_type(node).klass)

        self._generic_bind(node, cb, "subscript", visitor, type_ctx)

    def bind_unaryop(
        self, node: ast.UnaryOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        def cb(el: Class, result_types: List[Class]) -> None:
            el.instance.bind_unaryop(node, visitor, type_ctx)
            result_types.append(visitor.get_type(node).klass)

        self._generic_bind(
            node,
            cb,
            "unary op",
            visitor,
            type_ctx,
        )

    def bind_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        def cb(el: Class, result_types: List[Class]) -> bool:
            if el.instance.bind_compare(node, left, op, right, visitor, type_ctx):
                result_types.append(visitor.get_type(node).klass)
                return True
            return False

        rets = self._generic_bind(node, cb, "compare", visitor, type_ctx)
        return all(rets)

    def bind_reverse_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        def cb(el: Class, result_types: List[Class]) -> bool:
            if el.instance.bind_reverse_compare(
                node, left, op, right, visitor, type_ctx
            ):
                result_types.append(visitor.get_type(node).klass)
                return True
            return False

        rets = self._generic_bind(node, cb, "compare", visitor, type_ctx)
        return all(rets)

    def exact(self) -> Value:
        return self.klass.exact_type().instance

    def inexact(self) -> Value:
        return self.klass.inexact_type().instance


class OptionalType(UnionType):
    """UnionType for instantiations with [T, None], and to support Optional[T] special form."""

    is_variadic = False

    def __init__(
        self,
        type_name: Optional[UnionTypeName] = None,
        type_def: Optional[GenericClass] = None,
        type_env: Optional[TypeEnvironment] = None,
    ) -> None:
        super().__init__(
            type_name
            or UnionTypeName("typing", "Optional", (GenericParameter("T", 0),)),
            type_def=type_def,
            instance_type=OptionalInstance,
            type_env=type_env,
        )

    @property
    def opt_type(self) -> Class:
        opt_type = self.type_name.opt_type
        if opt_type is None:
            params = ", ".join(t.name for t in self.type_args)
            raise TypeError(f"OptionalType has invalid type parameters {params}")
        return opt_type

    def make_generic_type(
        self, index: Tuple[Class, ...], type_env: TypeEnvironment
    ) -> Class:
        assert len(index) == 1
        if not index[0].is_generic_parameter:
            # Optional[T] is syntactic sugar for Union[T, None]
            index = index + (NONE_TYPE,)
        return super().make_generic_type(index, type_env)


class OptionalInstance(UnionInstance):
    """Only exists for typing purposes (so we know .klass is OptionalType)."""

    klass: OptionalType


class ArrayInstance(Object["ArrayClass"]):
    def _seq_type(self) -> int:
        idx = self.klass.index
        if not isinstance(idx, CIntType):
            # should never happen
            raise SyntaxError(f"Invalid Array type: {idx}")
        size = idx.size
        if size == 0:
            return SEQ_ARRAY_INT8 if idx.signed else SEQ_ARRAY_UINT8
        elif size == 1:
            return SEQ_ARRAY_INT16 if idx.signed else SEQ_ARRAY_UINT16
        elif size == 2:
            return SEQ_ARRAY_INT32 if idx.signed else SEQ_ARRAY_UINT32
        elif size == 3:
            return SEQ_ARRAY_INT64 if idx.signed else SEQ_ARRAY_UINT64
        else:
            raise SyntaxError(f"Invalid Array size: {size}")

    def get_iter_type(self, node: ast.expr, visitor: TypeBinder) -> Value:
        return self.klass.index.instance

    def bind_subscr(
        self,
        node: ast.Subscript,
        type: Value,
        visitor: TypeBinder,
        type_ctx: Optional[Class] = None,
    ) -> None:
        if type == SLICE_TYPE.instance:
            # Slicing preserves type
            return visitor.set_type(node, self)

        visitor.set_type(node, self.klass.index.instance)

    def _supported_index(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> bool:
        index_type = code_gen.get_type(node.slice)
        return INT_TYPE.can_assign_from(index_type.klass) or isinstance(
            index_type, CIntInstance
        )

    def _maybe_unbox_index(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        index_type = code_gen.get_type(node.slice)
        if not isinstance(index_type, CIntInstance):
            # If the index is not a primitive, unbox its value to an int64, our implementation of
            # SEQUENCE_{GET/SET} expects the index to be a primitive int.
            code_gen.emit("REFINE_TYPE", index_type.klass.type_descr)
            code_gen.emit("PRIMITIVE_UNBOX", INT64_TYPE.type_descr)

    def emit_load_subscr(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        if self._supported_index(node, code_gen):
            self._maybe_unbox_index(node, code_gen)
            code_gen.emit("SEQUENCE_GET", self._seq_type())
        else:
            super().emit_load_subscr(node, code_gen)
            if code_gen.get_type(node.slice).klass != SLICE_TYPE:
                # Falling back to BINARY_SUBSCR here, so we need to unbox the output
                code_gen.emit("REFINE_TYPE", self.klass.index.boxed.type_descr)
                code_gen.emit("PRIMITIVE_UNBOX", self.klass.index.type_descr)

    def emit_store_subscr(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        if self._supported_index(node, code_gen):
            self._maybe_unbox_index(node, code_gen)
            code_gen.emit("SEQUENCE_SET", self._seq_type())
        else:
            if code_gen.get_type(node.slice).klass != SLICE_TYPE:
                # Falling back to STORE_SUBSCR here, so need to box the value first
                code_gen.emit("ROT_THREE")
                code_gen.emit("ROT_THREE")
                code_gen.emit("PRIMITIVE_BOX", self.klass.index.type_descr)
                code_gen.emit("ROT_THREE")
            super().emit_store_subscr(node, code_gen)

    def __repr__(self) -> str:
        return f"{self.klass.type_name.name}[{self.klass.index.name!r}]"

    def get_fast_len_type(self) -> int:
        return FAST_LEN_ARRAY | ((not self.klass.is_exact) << 4)

    def emit_len(
        self, node: ast.Call, code_gen: Static38CodeGenerator, boxed: bool
    ) -> None:
        if len(node.args) != 1:
            raise code_gen.syntax_error(
                "Can only pass a single argument when checking array length", node
            )
        code_gen.visit(node.args[0])
        code_gen.emit("FAST_LEN", self.get_fast_len_type())
        if boxed:
            code_gen.emit("PRIMITIVE_BOX", INT64_TYPE.type_descr)

    def emit_forloop(self, node: ast.For, code_gen: Static38CodeGenerator) -> None:
        if not isinstance(node.target, ast.Name):
            # We don't yet support `for a, b in my_array: ...`
            return super().emit_forloop(node, code_gen)

        return common_sequence_emit_forloop(node, code_gen, self._seq_type())


class ArrayClass(GenericClass):
    def __init__(
        self,
        name: GenericTypeName,
        bases: Optional[List[Class]] = None,
        instance: Optional[Object[Class]] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        type_def: Optional[GenericClass] = None,
        is_exact: bool = False,
        pytype: Optional[Type[object]] = None,
    ) -> None:
        default_bases: List[Class] = [OBJECT_TYPE]
        default_instance: Object[Class] = ArrayInstance(self)
        super().__init__(
            name,
            bases or default_bases,
            instance or default_instance,
            klass,
            members,
            type_def,
            is_exact,
            pytype,
        )
        self.members["__new__"] = BuiltinNewFunction(
            "__new__",
            "__static__",
            self,
            (
                Parameter("cls", 0, ResolvedTypeRef(TYPE_TYPE), False, None, False),
                Parameter(
                    "initializer", 0, ResolvedTypeRef(OBJECT_TYPE), True, (), False
                ),
            ),
            ResolvedTypeRef(self),
        )

    @property
    def index(self) -> CType:
        cls = self.type_args[0]
        assert isinstance(cls, CType)
        return cls

    def make_generic_type(
        self,
        index: Tuple[Class, ...],
        type_env: TypeEnvironment,
    ) -> Class:
        for tp in index:
            if tp not in ALLOWED_ARRAY_TYPES:
                raise TypedSyntaxError(
                    f"Invalid {self.gen_name.name} element type: {tp.instance.name}"
                )
        return super().make_generic_type(index, type_env)


class VectorClass(ArrayClass):
    def __init__(
        self,
        name: GenericTypeName,
        bases: Optional[List[Class]] = None,
        instance: Optional[Object[Class]] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        type_def: Optional[GenericClass] = None,
        is_exact: bool = False,
        pytype: Optional[Type[object]] = None,
    ) -> None:
        super().__init__(
            name,
            bases,
            instance,
            klass,
            members,
            type_def,
            is_exact,
            pytype,
        )
        self.members["append"] = BuiltinMethodDescriptor(
            "append",
            self,
            (
                Parameter("self", 0, ResolvedTypeRef(self), False, None, False),
                Parameter(
                    "v",
                    0,
                    ResolvedTypeRef(VECTOR_TYPE_PARAM),
                    False,
                    None,
                    False,
                ),
            ),
        )


BUILTIN_TYPE_ENV: TypeEnvironment = TypeEnvironment()
AWAITABLE_TYPE = AwaitableType()
UNION_TYPE = UnionType()
OPTIONAL_TYPE = OptionalType()
FINAL_TYPE = FinalClass(GenericTypeName("typing", "Final", (GenericParameter("T", 0),)))
CLASSVAR_TYPE = ClassVar(
    GenericTypeName("typing", "ClassVar", (GenericParameter("T", 0),))
)
CHECKED_DICT_TYPE_NAME = GenericTypeName(
    "__static__", "chkdict", (GenericParameter("K", 0), GenericParameter("V", 1))
)
CHECKED_LIST_TYPE_NAME = GenericTypeName(
    "__static__", "chklist", (GenericParameter("T", 0),)
)


class CheckedDict(GenericClass):
    def __init__(
        self,
        name: GenericTypeName,
        bases: Optional[List[Class]] = None,
        instance: Optional[Object[Class]] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        type_def: Optional[GenericClass] = None,
        is_exact: bool = False,
        pytype: Optional[Type[object]] = None,
    ) -> None:
        if instance is None:
            instance = CheckedDictInstance(self)
        super().__init__(
            name,
            bases,
            instance,
            klass,
            members,
            type_def,
            is_exact,
            pytype,
        )
        self.members["__init__"] = self.init_func = BuiltinFunction(
            "__init__",
            "builtins",
            self,
            (
                Parameter("cls", 0, ResolvedTypeRef(TYPE_TYPE), False, None, False),
                Parameter("x", 0, ResolvedTypeRef(OBJECT_TYPE), True, (), False),
            ),
            ResolvedTypeRef(self),
        )

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if len(node.args) == 1:
            # Validate that the incoming argument is compatible with us if it's
            # anything intersting like a dict or a checked dict.
            visitor.visit(node.args[0], self.instance)
        super().bind_call(node, visitor, type_ctx)

        return NO_EFFECT


class CheckedDictInstance(Object[CheckedDict]):
    def bind_subscr(
        self,
        node: ast.Subscript,
        type: Value,
        visitor: TypeBinder,
        type_ctx: Optional[Class] = None,
    ) -> None:
        visitor.visitExpectedType(
            node.slice, self.klass.gen_name.args[0].instance, blame=node
        )
        visitor.set_type(node, self.klass.gen_name.args[1].instance)

    def emit_load_subscr(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        dict_descr = self.klass.type_descr
        getitem_descr = dict_descr + ("__getitem__",)
        code_gen.emit("EXTENDED_ARG", 0)
        code_gen.emit("INVOKE_FUNCTION", (getitem_descr, 2))

    def emit_store_subscr(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        # We have, from TOS: index, dict, value-to-store
        # We want, from TOS: value-to-store, index, dict
        code_gen.emit("ROT_THREE")
        code_gen.emit("ROT_THREE")
        dict_descr = self.klass.type_descr
        setitem_descr = dict_descr + ("__setitem__",)
        code_gen.emit("EXTENDED_ARG", 0)
        code_gen.emit("INVOKE_FUNCTION", (setitem_descr, 3))
        code_gen.emit("POP_TOP")

    def get_fast_len_type(self) -> int:
        # CheckedDict is always an exact type because we don't allow
        # subclassing it.  So we just return FAST_LEN_DICT here which works
        # because then we won't do type checks, and it has the same layout
        # as a dictionary
        return FAST_LEN_DICT

    def emit_len(
        self, node: ast.Call, code_gen: Static38CodeGenerator, boxed: bool
    ) -> None:
        if len(node.args) != 1:
            raise code_gen.syntax_error(
                "Can only pass a single argument when checking dict length", node
            )
        code_gen.visit(node.args[0])
        code_gen.emit("FAST_LEN", self.get_fast_len_type())
        if boxed:
            code_gen.emit("PRIMITIVE_BOX", INT64_TYPE.type_descr)

    def emit_jumpif(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.visit(test)
        code_gen.emit("FAST_LEN", self.get_fast_len_type())
        code_gen.emit("POP_JUMP_IF_NONZERO" if is_if_true else "POP_JUMP_IF_ZERO", next)


class CheckedList(GenericClass):
    def __init__(
        self,
        name: GenericTypeName,
        bases: Optional[List[Class]] = None,
        instance: Optional[Object[Class]] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        type_def: Optional[GenericClass] = None,
        is_exact: bool = False,
        pytype: Optional[Type[object]] = None,
    ) -> None:
        if instance is None:
            instance = CheckedListInstance(self)
        super().__init__(
            name,
            bases,
            instance,
            klass,
            members,
            type_def,
            is_exact,
            pytype,
        )
        self.members["__init__"] = self.init_func = BuiltinFunction(
            "__init__",
            "builtins",
            self,
            (
                Parameter("cls", 0, ResolvedTypeRef(TYPE_TYPE), False, None, False),
                Parameter("x", 0, ResolvedTypeRef(OBJECT_TYPE), True, (), False),
            ),
            ResolvedTypeRef(self),
        )


class CheckedListInstance(Object[CheckedList]):
    @property
    def elem_type(self) -> Value:
        return self.klass.gen_name.args[0].instance

    def bind_subscr(
        self,
        node: ast.Subscript,
        type: Value,
        visitor: TypeBinder,
        type_ctx: Optional[Class] = None,
    ) -> None:
        if type == SLICE_TYPE.instance:
            visitor.set_type(node, self)
        else:
            if type.klass not in SIGNED_CINT_TYPES:
                visitor.visitExpectedType(node.slice, INT_TYPE.instance, blame=node)
            visitor.set_type(node, self.elem_type)

    def get_iter_type(self, node: ast.expr, visitor: TypeBinder) -> Value:
        return self.elem_type

    def emit_load_subscr(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        # From slice
        if code_gen.get_type(node) == self:
            return super().emit_load_subscr(node, code_gen)

        index_is_ctype = code_gen.get_type(node.slice).klass in SIGNED_CINT_TYPES

        if index_is_ctype:
            code_gen.emit("SEQUENCE_GET", SEQ_CHECKED_LIST)
        else:
            update_descr = self.klass.type_descr + ("__getitem__",)
            code_gen.emit_invoke_method(update_descr, 1)

    def emit_store_subscr(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        # From slice
        if code_gen.get_type(node) == self:
            return super().emit_store_subscr(node, code_gen)

        index_type = code_gen.get_type(node.slice).klass

        if index_type in SIGNED_CINT_TYPES:
            # TODO add CheckedList to SEQUENCE_SET so we can emit that instead
            # of having to box the index here
            code_gen.emit("PRIMITIVE_BOX", index_type.type_descr)

        # We have, from TOS: index, list, value-to-store
        # We want, from TOS: value-to-store, index, list
        code_gen.emit("ROT_THREE")
        code_gen.emit("ROT_THREE")

        setitem_descr = self.klass.type_descr + ("__setitem__",)
        code_gen.emit_invoke_method(setitem_descr, 2)
        code_gen.emit("POP_TOP")

    def get_fast_len_type(self) -> int:
        # CheckedList is always an exact type because we don't allow
        # subclassing it.  So we just return FAST_LEN_LIST here which works
        # because then we won't do type checks, and it has the same layout
        # as a list
        return FAST_LEN_LIST

    def emit_len(
        self, node: ast.Call, code_gen: Static38CodeGenerator, boxed: bool
    ) -> None:
        if len(node.args) != 1:
            raise code_gen.syntax_error(
                "Can only pass a single argument when checking list length", node
            )
        code_gen.visit(node.args[0])
        code_gen.emit("FAST_LEN", self.get_fast_len_type())
        if boxed:
            code_gen.emit("PRIMITIVE_BOX", INT64_TYPE.type_descr)

    def emit_jumpif(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.visit(test)
        code_gen.emit("FAST_LEN", self.get_fast_len_type())
        code_gen.emit("POP_JUMP_IF_NONZERO" if is_if_true else "POP_JUMP_IF_ZERO", next)

    def emit_forloop(self, node: ast.For, code_gen: Static38CodeGenerator) -> None:
        if not isinstance(node.target, ast.Name):
            # We don't yet support `for a, b in my_list: ...`
            return super().emit_forloop(node, code_gen)

        return common_sequence_emit_forloop(node, code_gen, SEQ_CHECKED_LIST)


class CastFunction(Object[Class]):
    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if len(node.args) != 2:
            visitor.syntax_error("cast requires two parameters: type and value", node)

        for arg in node.args:
            visitor.visitExpectedType(arg, DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE)

        cast_type = visitor.module.resolve_annotation(node.args[0])
        if cast_type is None:
            visitor.syntax_error("cast to unknown type", node)
            cast_type = DYNAMIC_TYPE

        visitor.set_type(node, cast_type.instance)
        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(node.args[1])
        code_gen.emit("CAST", code_gen.get_type(node).klass.type_descr)


prim_name_to_type: Mapping[str, int] = {
    "int8": TYPED_INT8,
    "int16": TYPED_INT16,
    "int32": TYPED_INT32,
    "int64": TYPED_INT64,
    "uint8": TYPED_UINT8,
    "uint16": TYPED_UINT16,
    "uint32": TYPED_UINT32,
    "uint64": TYPED_UINT64,
}


class CInstance(Value, Generic[TClass]):
    _op_name: Dict[Type[ast.operator], str] = {
        ast.Add: "add",
        ast.Sub: "subtract",
        ast.Mult: "multiply",
        ast.FloorDiv: "divide",
        ast.Div: "divide",
        ast.Mod: "modulus",
        ast.Pow: "pow",
        ast.LShift: "left shift",
        ast.RShift: "right shift",
        ast.BitOr: "bitwise or",
        ast.BitXor: "xor",
        ast.BitAnd: "bitwise and",
    }

    @property
    def name(self) -> str:
        return self.klass.instance_name

    def binop_error(self, left: str, right: str, op: ast.operator) -> str:
        return f"cannot {self._op_name[type(op)]} {left} and {right}"

    def bind_reverse_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        visitor.visitExpectedType(
            node.left, self, self.binop_error("{}", "{}", node.op)
        )
        if isinstance(node.op, ast.Pow):
            visitor.set_type(node, DOUBLE_TYPE.instance)
        else:
            visitor.set_type(node, self)
        return True

    def get_op_id(self, op: AST) -> int:
        raise NotImplementedError("Must be implemented in the subclass")

    def emit_binop(self, node: ast.BinOp, code_gen: Static38CodeGenerator) -> None:
        code_gen.update_lineno(node)
        # In the pow case, the return type isn't the common type.
        ltype = code_gen.get_type(node.left)
        common_type = code_gen.get_opt_node_data(node, BinOpCommonType)
        common_type = (
            common_type.value if common_type is not None else code_gen.get_type(node)
        )
        code_gen.visit(node.left)
        if ltype != common_type:
            common_type.emit_convert(ltype, code_gen)
        rtype = code_gen.get_type(node.right)
        code_gen.visit(node.right)
        if rtype != common_type:
            common_type.emit_convert(rtype, code_gen)
        assert isinstance(common_type, CInstance)
        op = common_type.get_op_id(node.op)
        code_gen.emit("PRIMITIVE_BINARY_OP", op)

    def emit_load_name(self, node: ast.Name, code_gen: Static38CodeGenerator) -> None:
        code_gen.emit("LOAD_LOCAL", (node.id, self.klass.type_descr))

    def emit_store_name(self, node: ast.Name, code_gen: Static38CodeGenerator) -> None:
        code_gen.emit("STORE_LOCAL", (node.id, self.klass.type_descr))

    def emit_delete_name(self, node: ast.Name, code_gen: Static38CodeGenerator) -> None:
        raise TypedSyntaxError("deleting primitives not supported")

    def emit_aug_rhs(
        self, node: ast.AugAssign, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.visit(node.value)
        code_gen.emit("PRIMITIVE_BINARY_OP", self.get_op_id(node.op))


class CEnumInstance(CInstance["CEnumType"]):
    def __init__(
        self, klass: CEnumType, name: Optional[str] = None, value: Optional[int] = None
    ) -> None:
        super().__init__(klass)
        self.klass = klass
        self.attr_name = name
        self.value = value

    @property
    def name(self) -> str:
        class_name = super().name
        if self.attr_name is not None:
            return f"<{class_name}.{self.attr_name}: {self.value}>"
        return class_name

    def as_oparg(self) -> int:
        return TYPED_INT64

    def bind_attr(
        self, node: ast.Attribute, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        if isinstance(node.ctx, (ast.Store, ast.Del)):
            visitor.syntax_error("Enum values cannot be modified or deleted", node)

        if node.attr == "name":
            visitor.set_type(node, STR_EXACT_TYPE.instance)
            return

        if node.attr == "value":
            visitor.set_type(node, INT64_VALUE)
            return

        super().bind_attr(node, visitor, type_ctx)

    def bind_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        rtype = visitor.get_type(right)
        if not isinstance(op, (ast.Eq, ast.NotEq, ast.Is, ast.IsNot)):
            visitor.syntax_error(
                f"'{CMPOP_SIGILS[type(op)]}' not supported between '{self.name}' and '{rtype.name}'",
                node,
            )
            return False

        if rtype != self and (
            not isinstance(rtype, CEnumInstance) or self.klass != rtype.klass
        ):
            visitor.syntax_error(f"can't compare {self.name} to {rtype.name}", node)
            return False

        visitor.set_type(op, self)
        visitor.set_type(node, CBOOL_TYPE.instance)
        return True

    def bind_reverse_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        ltype = visitor.get_type(left)
        if not isinstance(op, (ast.Eq, ast.NotEq, ast.Is, ast.IsNot)):
            visitor.syntax_error(
                f"'{CMPOP_SIGILS[type(op)]}' not supported between '{ltype.name}' and '{self.name}'",
                node,
            )
            return False

        if ltype != self and (
            not isinstance(ltype, CEnumInstance) or self.klass != ltype.klass
        ):
            visitor.syntax_error(f"can't compare {ltype.name} to {self.name}", node)
            return False

        visitor.set_type(op, self)
        visitor.set_type(node, CBOOL_TYPE.instance)
        return True

    @property
    def boxed(self) -> BoxedEnumInstance:
        if name := self.attr_name:
            return self.klass.boxed.values[name]
        return self.klass.boxed.instance

    def emit_load_attr(
        self,
        node: ast.Attribute,
        code_gen: Static38CodeGenerator,
    ) -> None:
        if node.attr == "value":
            return

        code_gen.emit("PRIMITIVE_BOX", self.klass.type_descr)
        super().emit_load_attr(node, code_gen)

    def emit_box(self, node: expr, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(node)
        type = code_gen.get_type(node)
        if isinstance(type, CEnumInstance):
            code_gen.emit("PRIMITIVE_BOX", self.klass.type_descr)
        else:
            raise RuntimeError("unsupported box type: " + type.name)

    def emit_compare(self, op: cmpop, code_gen: Static38CodeGenerator) -> None:
        code_gen.emit("PRIMITIVE_COMPARE_OP", INT64_VALUE.get_op_id(op))

    def resolve_attr(
        self, node: ast.Attribute, visitor: ReferenceVisitor
    ) -> Optional[Value]:
        return resolve_attr_instance(node, self.boxed, self.klass, visitor)


class CEnumType(CType):
    instance: CEnumInstance

    def __init__(
        self,
        type_name: Optional[TypeName] = None,
        bases: Optional[List[Class]] = None,
    ) -> None:
        super().__init__(
            type_name or TypeName("__static__", "Enum"),
            bases,
            CEnumInstance(self),
        )

        self.values: Dict[str, CEnumInstance] = {}

    @cached_property
    def boxed(self) -> BoxedEnumClass:
        return BoxedEnumClass(self.type_name, self.bases)

    def make_subclass(self, name: TypeName, bases: List[Class]) -> Class:
        # TODO(wmeehan): handle enum subclassing and mix-ins
        if len(bases) > 1:
            raise TypedSyntaxError(
                f"Static Enum types cannot support multiple bases: {bases}",
            )
        if bases[0] != ENUM_TYPE:
            raise TypedSyntaxError("Static Enum types do not allow subclassing")
        return CEnumType(name, bases)

    def add_enum_value(self, name: ast.Name, const: ast.AST) -> None:
        if not isinstance(const, ast.Constant):
            raise TypedSyntaxError(f"cannot resolve enum value {const} at compile time")

        value = const.value
        if not isinstance(value, int):
            raise TypedSyntaxError(
                f"Static enum values must be int, not {type(value).__name__}"
            )
        if not INT64_VALUE.is_valid_int(value):
            raise TypedSyntaxError(
                f"Value {value} for {self.instance_name}.{name.id} is out of bounds"
            )

        self.values[name.id] = CEnumInstance(self, name.id, value)
        self.boxed.add_enum_value(name.id, value)

    def bind_attr(
        self, node: ast.Attribute, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        if isinstance(node.ctx, (ast.Store, ast.Del)):
            visitor.syntax_error("Enum values cannot be modified or deleted", node)

        if inst := self.values.get(node.attr):
            visitor.set_type(node, inst)
            return

        super().bind_attr(node, visitor, type_ctx)

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if len(node.args) != 1:
            visitor.syntax_error(
                f"{self.name} requires a single argument ({len(node.args)} given)", node
            )

        visitor.set_type(node, self.instance)
        arg = node.args[0]
        visitor.visitExpectedType(arg, DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE)

        return NO_EFFECT

    def declare_variable(self, node: AnnAssign, module: ModuleTable) -> None:
        target = node.target
        if isinstance(target, ast.Name):
            self.add_enum_value(target, node)

    def declare_variables(self, node: Assign, module: ModuleTable) -> None:
        value = node.value
        for target in node.targets:
            if isinstance(target, ast.Tuple):
                if not isinstance(value, ast.Tuple):
                    raise TypedSyntaxError(
                        f"cannot assign non-tuple enum value {value} "
                        f"to multiple variables: {target}"
                    )
                if len(target.elts) != len(value.elts):
                    raise TypedSyntaxError(
                        f"arity mismatch for enum assignment {target} = {value}"
                    )
                for name, val in zip(target.elts, value.elts):
                    assert isinstance(name, ast.Name)
                    self.add_enum_value(name, val)
            elif isinstance(target, ast.Name):
                self.add_enum_value(target, value)

    def emit_attr(
        self,
        node: ast.Attribute,
        code_gen: Static38CodeGenerator,
    ) -> None:
        if inst := self.values.get(node.attr):
            code_gen.emit("PRIMITIVE_LOAD_CONST", (inst.value, TYPED_INT64))
            return

        super().emit_attr(node, code_gen)

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if len(node.args) != 1:
            raise code_gen.syntax_error(
                f"{self.name} requires a single argument, given {len(node.args)}", node
            )

        arg = node.args[0]
        arg_type = code_gen.get_type(arg)
        if isinstance(arg_type, CEnumInstance):
            code_gen.visit(arg)
        else:
            code_gen.defaultVisit(node)
            code_gen.emit("PRIMITIVE_UNBOX", self.type_descr)


class BoxedEnumClass(Class):
    instance: BoxedEnumInstance

    def __init__(
        self,
        type_name: TypeName,
        bases: Optional[List[Class]] = None,
    ) -> None:
        boxed_bases = (
            [base.boxed if isinstance(base, CEnumType) else base for base in bases]
            if bases
            else [OBJECT_TYPE]
        )

        super().__init__(
            type_name,
            boxed_bases,
            BoxedEnumInstance(self),
        )

        self.values: Dict[str, BoxedEnumInstance] = {}

    @property
    def instance_name(self) -> str:
        return f"Boxed[{super().instance_name}]"

    def add_enum_value(self, name: str, value: int) -> None:
        self.values[name] = BoxedEnumInstance(self, name, value)


class BoxedEnumInstance(Object[BoxedEnumClass]):
    def __init__(
        self,
        klass: BoxedEnumClass,
        name: Optional[str] = None,
        value: Optional[int] = None,
    ) -> None:
        super().__init__(klass)
        self.klass = klass
        self.attr_name = name
        self.value = value

    @property
    def name(self) -> str:
        class_name = super().name
        if self.attr_name is not None:
            return f"Boxed<{class_name}.{self.attr_name}: {self.value}>"
        return class_name

    def emit_unbox(self, node: expr, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(node)
        code_gen.emit("PRIMITIVE_UNBOX", self.klass.type_descr)


class CIntInstance(CInstance["CIntType"]):
    def __init__(self, klass: CIntType, constant: int, size: int, signed: bool) -> None:
        super().__init__(klass)
        self.klass: CIntType = klass
        self.constant = constant
        self.size = size
        self.signed = signed

    def as_oparg(self) -> int:
        return self.constant

    @property
    def name(self) -> str:
        if self.klass.literal_value is not None:
            return f"Literal[{self.klass.literal_value}]"
        return super().name

    _int_binary_opcode_signed: Mapping[Type[ast.AST], int] = {
        ast.Lt: PRIM_OP_LT_INT,
        ast.Gt: PRIM_OP_GT_INT,
        ast.Eq: PRIM_OP_EQ_INT,
        ast.NotEq: PRIM_OP_NE_INT,
        ast.LtE: PRIM_OP_LE_INT,
        ast.GtE: PRIM_OP_GE_INT,
        ast.Add: PRIM_OP_ADD_INT,
        ast.Sub: PRIM_OP_SUB_INT,
        ast.Mult: PRIM_OP_MUL_INT,
        ast.FloorDiv: PRIM_OP_DIV_INT,
        ast.Div: PRIM_OP_DIV_INT,
        ast.Mod: PRIM_OP_MOD_INT,
        ast.LShift: PRIM_OP_LSHIFT_INT,
        ast.RShift: PRIM_OP_RSHIFT_INT,
        ast.BitOr: PRIM_OP_OR_INT,
        ast.BitXor: PRIM_OP_XOR_INT,
        ast.BitAnd: PRIM_OP_AND_INT,
        ast.Pow: PRIM_OP_POW_INT,
    }

    _int_binary_opcode_unsigned: Mapping[Type[ast.AST], int] = {
        ast.Lt: PRIM_OP_LT_UN_INT,
        ast.Gt: PRIM_OP_GT_UN_INT,
        ast.Eq: PRIM_OP_EQ_INT,
        ast.NotEq: PRIM_OP_NE_INT,
        ast.LtE: PRIM_OP_LE_UN_INT,
        ast.GtE: PRIM_OP_GE_UN_INT,
        ast.Add: PRIM_OP_ADD_INT,
        ast.Sub: PRIM_OP_SUB_INT,
        ast.Mult: PRIM_OP_MUL_INT,
        ast.FloorDiv: PRIM_OP_DIV_UN_INT,
        ast.Div: PRIM_OP_DIV_UN_INT,
        ast.Mod: PRIM_OP_MOD_UN_INT,
        ast.LShift: PRIM_OP_LSHIFT_INT,
        ast.RShift: PRIM_OP_RSHIFT_INT,
        ast.RShift: PRIM_OP_RSHIFT_UN_INT,
        ast.BitOr: PRIM_OP_OR_INT,
        ast.BitXor: PRIM_OP_XOR_INT,
        ast.BitAnd: PRIM_OP_AND_INT,
        ast.Pow: PRIM_OP_POW_UN_INT,
    }

    def get_op_id(self, op: AST) -> int:
        return (
            self._int_binary_opcode_signed[type(op)]
            if self.signed
            else (self._int_binary_opcode_unsigned[type(op)])
        )

    def make_literal(self, literal_value: object) -> Value:
        assert isinstance(literal_value, int)
        return CIntType(self.klass.constant, literal_value=literal_value).instance

    def validate_mixed_math(self, other: Value) -> Optional[Value]:
        if self.constant == TYPED_BOOL:
            return None
        if other is self:
            return self
        elif isinstance(other, CIntInstance):
            if other.constant == TYPED_BOOL:
                return None
            if self.signed == other.signed:
                # Signs match, we can just treat this as a comparison of the larger type.
                # Ensure we return a simple cint type even if self or other is a literal.
                size = max(self.size, other.size)
                types = SIGNED_CINT_TYPES if self.signed else UNSIGNED_CINT_TYPES
                return types[size].instance
            else:
                new_size = max(
                    self.size if self.signed else self.size + 1,
                    other.size if other.signed else other.size + 1,
                )

                if new_size <= TYPED_INT_64BIT:
                    # signs don't match, but we can promote to the next highest data type
                    return SIGNED_CINT_TYPES[new_size].instance

        return None

    def bind_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        rtype = visitor.get_type(right)
        if rtype != self and not isinstance(rtype, CIntInstance):
            visitor.visit(right, self)

        other = visitor.get_type(right)
        comparing_cbools = self.constant == TYPED_BOOL and (
            isinstance(other, CIntInstance) and other.constant == TYPED_BOOL
        )
        if comparing_cbools:
            visitor.set_type(op, CBOOL_TYPE.instance)
            visitor.set_type(node, CBOOL_TYPE.instance)
            return True

        compare_type = self.validate_mixed_math(other)
        if compare_type is None:
            visitor.syntax_error(
                f"can't compare {self.name} to {visitor.get_type(right).name}", node
            )
            compare_type = DYNAMIC

        visitor.set_type(op, compare_type)
        visitor.set_type(node, CBOOL_TYPE.instance)
        return True

    def bind_reverse_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        assert not isinstance(visitor.get_type(left), CIntInstance)
        visitor.visitExpectedType(left, self)

        visitor.set_type(op, self)
        visitor.set_type(node, CBOOL_TYPE.instance)
        return True

    def emit_compare(self, op: cmpop, code_gen: Static38CodeGenerator) -> None:
        code_gen.emit("PRIMITIVE_COMPARE_OP", self.get_op_id(op))

    def get_int_range(self) -> Tuple[int, int]:
        bits = 8 << self.size
        if self.signed:
            low = -(1 << (bits - 1))
            high = (1 << (bits - 1)) - 1
        else:
            low = 0
            high = (1 << bits) - 1
        return low, high

    def is_valid_int(self, val: int) -> bool:
        low, high = self.get_int_range()

        return low <= val <= high

    def bind_constant(self, node: ast.Constant, visitor: TypeBinder) -> None:
        if type(node.value) is int:
            node_type = visitor.compiler.get_literal_type(self, node.value)
        elif type(node.value) is bool and self is CBOOL_TYPE.instance:
            assert self is CBOOL_TYPE.instance
            node_type = self
        else:
            node_type = CONSTANT_TYPES[type(node.value)]

        visitor.set_type(node, node_type)

    def emit_constant(
        self, node: ast.Constant, code_gen: Static38CodeGenerator
    ) -> None:
        assert (literal := self.klass.literal_value) is None or self.is_valid_int(
            literal
        )
        val = node.value
        if self.constant == TYPED_BOOL:
            val = bool(val)
        code_gen.emit("PRIMITIVE_LOAD_CONST", (val, self.as_oparg()))

    def emit_jumpif_only(
        self, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.emit("POP_JUMP_IF_NONZERO" if is_if_true else "POP_JUMP_IF_ZERO", next)

    def emit_jumpif_pop_only(
        self, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.emit(
            "JUMP_IF_NONZERO_OR_POP" if is_if_true else "JUMP_IF_ZERO_OR_POP", next
        )

    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        if self.constant == TYPED_BOOL:
            raise TypedSyntaxError(
                f"cbool is not a valid operand type for {self._op_name[type(node.op)]}"
            )
        rinst = visitor.get_type(node.right)
        if rinst != self:
            if rinst.klass == LIST_EXACT_TYPE:
                visitor.set_type(node, LIST_EXACT_TYPE.instance)
                return True
            if rinst.klass == TUPLE_EXACT_TYPE:
                visitor.set_type(node, TUPLE_EXACT_TYPE.instance)
                return True

            visitor.visit(node.right, type_ctx or INT64_VALUE)

        if isinstance(node.op, ast.Pow):
            # For pow, we don't support mixed math of unsigned/signed ints.
            if isinstance(self, CIntInstance) and isinstance(rinst, CIntInstance):
                if self.signed != rinst.signed:
                    visitor.syntax_error(
                        self.binop_error(
                            self.name, visitor.get_type(node.right).name, node.op
                        ),
                        node,
                    )
        if type_ctx is None:
            type_ctx = self.validate_mixed_math(visitor.get_type(node.right))
            if type_ctx is None:
                visitor.syntax_error(
                    self.binop_error(
                        self.name, visitor.get_type(node.right).name, node.op
                    ),
                    node,
                )
                type_ctx = DYNAMIC
            else:
                visitor.set_node_data(node, BinOpCommonType, BinOpCommonType(type_ctx))
        else:
            visitor.check_can_assign_from(type_ctx.klass, self.klass, node.left)
            visitor.check_can_assign_from(
                type_ctx.klass,
                visitor.get_type(node.right).klass,
                node.right,
                self.binop_error("{1}", "{0}", node.op),
            )
        if isinstance(node.op, ast.Pow):
            visitor.set_type(node, DOUBLE_TYPE.instance)
        else:
            visitor.set_type(node, type_ctx)
        return True

    def emit_box(self, node: expr, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(node)
        type = code_gen.get_type(node)
        if isinstance(type, CIntInstance):
            code_gen.emit("PRIMITIVE_BOX", self.klass.type_descr)
        else:
            raise RuntimeError("unsupported box type: " + type.name)

    def emit_unbox(self, node: expr, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(node)
        ty = code_gen.get_type(node)
        target_ty = BOOL_TYPE if self.klass is CBOOL_TYPE else INT_TYPE
        if target_ty.can_assign_from(ty.klass):
            code_gen.emit("REFINE_TYPE", ty.klass.type_descr)
        else:
            code_gen.emit("CAST", target_ty.type_descr)
        code_gen.emit("PRIMITIVE_UNBOX", self.klass.type_descr)

    def bind_unaryop(
        self, node: ast.UnaryOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        if isinstance(node.op, (ast.USub, ast.Invert, ast.UAdd)):
            visitor.set_type(node, self)
        else:
            assert isinstance(node.op, ast.Not)
            visitor.set_type(node, CBOOL_TYPE.instance)

    def emit_unaryop(self, node: ast.UnaryOp, code_gen: Static38CodeGenerator) -> None:
        code_gen.update_lineno(node)
        code_gen.visit(node.operand)
        if isinstance(node.op, ast.USub):
            code_gen.emit("PRIMITIVE_UNARY_OP", PRIM_OP_NEG_INT)
        elif isinstance(node.op, ast.Invert):
            code_gen.emit("PRIMITIVE_UNARY_OP", PRIM_OP_INV_INT)
        elif isinstance(node.op, ast.Not):
            code_gen.emit("PRIMITIVE_UNARY_OP", PRIM_OP_NOT_INT)

    def emit_convert(self, from_type: Value, code_gen: Static38CodeGenerator) -> None:
        assert isinstance(from_type, CIntInstance)
        # Lower nibble is type-from, higher nibble is type-to.
        from_oparg = from_type.as_oparg()
        to_oparg = self.as_oparg()
        if from_oparg != to_oparg:
            code_gen.emit("CONVERT_PRIMITIVE", (to_oparg << 4) | from_oparg)


class CIntType(CType):
    instance: CIntInstance

    def __init__(
        self,
        constant: int,
        name_override: Optional[str] = None,
        literal_value: Optional[int] = None,
    ) -> None:
        self.constant = constant
        # See TYPED_SIZE macro
        self.size: int = (constant >> 1) & 3
        self.signed: bool = bool(constant & 1)
        self.literal_value = literal_value
        if name_override is None:
            name = ("" if self.signed else "u") + "int" + str(8 << self.size)
        else:
            name = name_override
        super().__init__(
            TypeName("__static__", name),
            instance=CIntInstance(self, self.constant, self.size, self.signed),
        )

    @property
    def boxed(self) -> Class:
        return INT_TYPE

    def can_assign_from(self, src: Class) -> bool:
        if isinstance(src, CIntType):
            literal = src.literal_value
            if literal is not None:
                return self.instance.is_valid_int(literal)
            if src.size <= self.size and src.signed == self.signed:
                # assignment to same or larger size, with same sign
                # is allowed
                return True
            if src.size < self.size and self.signed:
                # assignment to larger signed size from unsigned is
                # allowed
                return True

        return super().can_assign_from(src)

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if len(node.args) != 1:
            visitor.syntax_error(
                f"{self.name} requires a single argument ({len(node.args)} given)", node
            )

        # This can be used as a cast operator on primitive ints int64(uint64),
        # so we don't pass the type context.
        visitor.set_type(node, self.instance)
        arg = node.args[0]
        visitor.visit(arg, self.instance)

        arg_type = visitor.get_type(arg)
        if not self.is_valid_arg(arg_type):
            visitor.check_can_assign_from(self, arg_type.klass, arg)

        return NO_EFFECT

    def is_valid_arg(self, arg_type: Value) -> bool:
        if arg_type is DYNAMIC or arg_type is OBJECT:
            return True

        if self is CBOOL_TYPE:
            if arg_type.klass is BOOL_TYPE:
                return True
            return False

        if arg_type is INT_TYPE.instance or self.is_valid_exact_int(arg_type):
            return True

        if isinstance(arg_type, CIntInstance):
            literal = arg_type.klass.literal_value
            if literal is not None:
                return self.instance.is_valid_int(literal)
            return True

        return False

    def is_valid_exact_int(self, arg_type: Value) -> bool:
        if isinstance(arg_type, NumExactInstance):
            literal = arg_type.klass.literal_value
            if literal is not None:
                return self.instance.is_valid_int(literal)
            return True

        return False

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if len(node.args) != 1:
            raise code_gen.syntax_error(
                f"{self.name} requires a single argument ({len(node.args)} given)", node
            )

        arg = node.args[0]
        arg_type = code_gen.get_type(arg)
        if isinstance(arg_type, CIntInstance):
            code_gen.visit(arg)
            if arg_type != self.instance:
                self.instance.emit_convert(arg_type, code_gen)
        else:
            self.instance.emit_unbox(arg, code_gen)


class CDoubleInstance(CInstance["CDoubleType"]):
    _double_binary_opcode_signed: Mapping[Type[ast.AST], int] = {
        ast.Add: PRIM_OP_ADD_DBL,
        ast.Sub: PRIM_OP_SUB_DBL,
        ast.Mult: PRIM_OP_MUL_DBL,
        ast.Div: PRIM_OP_DIV_DBL,
        ast.Mod: PRIM_OP_MOD_DBL,
        ast.Pow: PRIM_OP_POW_DBL,
        ast.Lt: PRIM_OP_LT_DBL,
        ast.Gt: PRIM_OP_GT_DBL,
        ast.Eq: PRIM_OP_EQ_DBL,
        ast.NotEq: PRIM_OP_NE_DBL,
        ast.LtE: PRIM_OP_LE_DBL,
        ast.GtE: PRIM_OP_GE_DBL,
    }

    def get_op_id(self, op: AST) -> int:
        return self._double_binary_opcode_signed[type(op)]

    def as_oparg(self) -> int:
        return TYPED_DOUBLE

    def bind_unaryop(
        self, node: ast.UnaryOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        if isinstance(node.op, (ast.USub, ast.UAdd)):
            visitor.set_type(node, self)
        else:
            visitor.syntax_error("Cannot invert/not a double", node)

    def emit_unaryop(self, node: ast.UnaryOp, code_gen: Static38CodeGenerator) -> None:
        code_gen.update_lineno(node)
        assert not isinstance(
            node.op, (ast.Invert, ast.Not)
        )  # should be prevent by the type checker
        if isinstance(node.op, ast.USub):
            code_gen.visit(node.operand)
            code_gen.emit("PRIMITIVE_UNARY_OP", PRIM_OP_NEG_DBL)
        elif isinstance(node.op, ast.UAdd):
            code_gen.visit(node.operand)

    def bind_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        rtype = visitor.get_type(right)
        if rtype != self:
            if rtype == FLOAT_EXACT_TYPE.instance:
                visitor.visitExpectedType(right, self, f"can't compare {{}} to {{}}")
            else:
                visitor.syntax_error(f"can't compare {self.name} to {rtype.name}", node)

        visitor.set_type(op, self)
        visitor.set_type(node, CBOOL_TYPE.instance)
        return True

    def bind_reverse_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        ltype = visitor.get_type(left)
        if ltype != self:
            if ltype == FLOAT_EXACT_TYPE.instance:
                visitor.visitExpectedType(left, self, f"can't compare {{}} to {{}}")
            else:
                visitor.syntax_error(f"can't compare {self.name} to {ltype.name}", node)

            visitor.set_type(op, self)
            visitor.set_type(node, CBOOL_TYPE.instance)
            return True

        return False

    def emit_compare(self, op: cmpop, code_gen: Static38CodeGenerator) -> None:
        code_gen.emit("PRIMITIVE_COMPARE_OP", self.get_op_id(op))

    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        rtype = visitor.get_type(node.right)
        if type(node.op) not in self._double_binary_opcode_signed:
            visitor.syntax_error(self.binop_error(self.name, rtype.name, node.op), node)

        if rtype != self:
            visitor.visitExpectedType(
                node.right,
                type_ctx or DOUBLE_TYPE.instance,
                self.binop_error("{}", "{}", node.op),
            )

        visitor.set_type(node, self)
        return True

    def bind_constant(self, node: ast.Constant, visitor: TypeBinder) -> None:
        visitor.set_type(node, self)

    def emit_constant(
        self, node: ast.Constant, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.emit("PRIMITIVE_LOAD_CONST", (float(node.value), self.as_oparg()))

    def emit_box(self, node: expr, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(node)
        type = code_gen.get_type(node)
        if isinstance(type, CDoubleInstance):
            code_gen.emit("PRIMITIVE_BOX", self.klass.type_descr)
        else:
            raise RuntimeError("unsupported box type: " + type.name)

    def emit_unbox(self, node: expr, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(node)
        node_ty = code_gen.get_type(node)
        if FLOAT_TYPE.can_assign_from(node_ty.klass):
            code_gen.emit("REFINE_TYPE", node_ty.klass.type_descr)
        else:
            code_gen.emit("CAST", FLOAT_TYPE.type_descr)
        code_gen.emit("PRIMITIVE_UNBOX", self.klass.type_descr)


class CDoubleType(CType):
    def __init__(self) -> None:
        super().__init__(
            TypeName("__static__", "double"),
            instance=CDoubleInstance(self),
        )

    @property
    def boxed(self) -> Class:
        return FLOAT_TYPE

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if len(node.args) != 1:
            visitor.syntax_error(
                f"{self.name} requires a single argument ({len(node.args)} given)", node
            )

        visitor.set_type(node, self.instance)
        arg = node.args[0]
        visitor.visit(arg, self.instance)
        arg_type = visitor.get_type(arg)
        if arg_type not in (
            FLOAT_TYPE.instance,
            FLOAT_EXACT_TYPE.instance,
            DYNAMIC_TYPE.instance,
            self.instance,
        ):
            visitor.syntax_error(
                f"type mismatch: double cannot be created from {arg_type.name}", node
            )

        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        assert len(node.args) == 1

        arg = node.args[0]
        arg_type = code_gen.get_type(arg)
        if isinstance(arg_type, CDoubleInstance):
            code_gen.visit(arg)
        else:
            self.instance.emit_unbox(arg, code_gen)


class ModuleType(Class):
    def __init__(self) -> None:
        super().__init__(TypeName("types", "ModuleType"))


class ModuleInstance(Object["ModuleType"]):
    SPECIAL_NAMES: typingClassVar[Set[str]] = {
        "__dict__",
        "__class__",
        "__name__",
        "__patch_enabled__",
    }

    def __init__(self, module_name: str, compiler: Compiler) -> None:
        self.module_name = module_name
        self.compiler = compiler
        super().__init__(klass=MODULE_TYPE)

    def resolve_attr(
        self, node: ast.Attribute, visitor: ReferenceVisitor
    ) -> Optional[Value]:
        if node.attr in self.SPECIAL_NAMES:
            return super().resolve_attr(node, visitor)

        module_table = self.compiler.modules.get(self.module_name)
        if module_table is None:
            return DYNAMIC

        return module_table.children.get(node.attr, DYNAMIC)


MODULE_TYPE = ModuleType()

CBOOL_TYPE = CIntType(TYPED_BOOL, name_override="cbool")

ENUM_TYPE = CEnumType()

INT8_TYPE = CIntType(TYPED_INT8)
INT16_TYPE = CIntType(TYPED_INT16)
INT32_TYPE = CIntType(TYPED_INT32)
INT64_TYPE = CIntType(TYPED_INT64)

UINT8_TYPE = CIntType(TYPED_UINT8)
UINT16_TYPE = CIntType(TYPED_UINT16)
UINT32_TYPE = CIntType(TYPED_UINT32)
UINT64_TYPE = CIntType(TYPED_UINT64)

INT64_VALUE = INT64_TYPE.instance

CHAR_TYPE = CIntType(TYPED_INT8, name_override="char")
DOUBLE_TYPE = CDoubleType()
ARRAY_TYPE = ArrayClass(
    GenericTypeName("__static__", "Array", (GenericParameter("T", 0),)), is_exact=True
)

# Vectors are just currently a special type of array that support
# methods that resize them.
VECTOR_TYPE_PARAM = GenericParameter("T", 0)
VECTOR_TYPE_NAME = GenericTypeName("__static__", "Vector", (VECTOR_TYPE_PARAM,))

VECTOR_TYPE = VectorClass(VECTOR_TYPE_NAME, is_exact=True)

ALLOWED_ARRAY_TYPES: List[Class] = [
    INT8_TYPE,
    INT16_TYPE,
    INT32_TYPE,
    INT64_TYPE,
    UINT8_TYPE,
    UINT16_TYPE,
    UINT32_TYPE,
    UINT64_TYPE,
    CHAR_TYPE,
    DOUBLE_TYPE,
    FLOAT_TYPE,
]

# TODO uses of these to check if something is a CInt wrongly exclude literals
SIGNED_CINT_TYPES = [INT8_TYPE, INT16_TYPE, INT32_TYPE, INT64_TYPE]
UNSIGNED_CINT_TYPES: List[CIntType] = [
    UINT8_TYPE,
    UINT16_TYPE,
    UINT32_TYPE,
    UINT64_TYPE,
]
ALL_CINT_TYPES: Sequence[CIntType] = SIGNED_CINT_TYPES + UNSIGNED_CINT_TYPES

NAME_TO_TYPE: Mapping[object, Class] = {
    "NoneType": NONE_TYPE,
    "object": OBJECT_TYPE,
    "str": STR_TYPE,
    "__static__.int8": INT8_TYPE,
    "__static__.int16": INT16_TYPE,
    "__static__.int32": INT32_TYPE,
    "__static__.int64": INT64_TYPE,
    "__static__.uint8": UINT8_TYPE,
    "__static__.uint16": UINT16_TYPE,
    "__static__.uint32": UINT32_TYPE,
    "__static__.uint64": UINT64_TYPE,
}

CHECKED_DICT_TYPE = CheckedDict(
    CHECKED_DICT_TYPE_NAME, [OBJECT_TYPE], pytype=chkdict, is_exact=True
)

CHECKED_LIST_TYPE = CheckedList(
    CHECKED_LIST_TYPE_NAME, [OBJECT_TYPE], pytype=chklist, is_exact=True
)


class ProdAssertFunction(Object[Class]):
    def __init__(self) -> None:
        super().__init__(FUNCTION_TYPE)

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:

        if node.keywords:
            visitor.syntax_error(
                "prod_assert() does not accept keyword arguments", node
            )
            return NO_EFFECT
        num_args = len(node.args)
        if num_args != 1 and num_args != 2:
            visitor.syntax_error(
                "prod_assert() must be called with one or two arguments", node
            )
            return NO_EFFECT

        effect = visitor.visit(node.args[0]) or NO_EFFECT
        if num_args == 2:
            visitor.visitExpectedType(node.args[1], STR_TYPE.instance)
        effect.apply(visitor.local_types)
        return NO_EFFECT


if spamobj is not None:
    SPAM_OBJ = GenericClass(
        GenericTypeName("xxclassloader", "spamobj", (GenericParameter("T", 0),)),
        [OBJECT_TYPE],
        pytype=spamobj,
    )
    XXGENERIC_T = GenericParameter("T", 0)
    XXGENERIC_U = GenericParameter("U", 1)
    XXGENERIC_TYPE_NAME = GenericTypeName(
        "xxclassloader", "XXGeneric", (XXGENERIC_T, XXGENERIC_U)
    )

    class XXGeneric(GenericClass):
        def __init__(
            self,
            name: GenericTypeName,
            bases: Optional[List[Class]] = None,
            instance: Optional[Object[Class]] = None,
            klass: Optional[Class] = None,
            members: Optional[Dict[str, Value]] = None,
            type_def: Optional[GenericClass] = None,
            is_exact: bool = False,
            pytype: Optional[Type[object]] = None,
        ) -> None:
            super().__init__(
                name,
                bases,
                instance,
                klass,
                members,
                type_def,
                is_exact,
                pytype,
            )
            self.members["foo"] = BuiltinMethodDescriptor(
                "foo",
                self,
                (
                    Parameter("self", 0, ResolvedTypeRef(self), False, None, False),
                    Parameter(
                        "t",
                        0,
                        ResolvedTypeRef(XXGENERIC_T),
                        False,
                        None,
                        False,
                    ),
                    Parameter(
                        "u",
                        0,
                        ResolvedTypeRef(XXGENERIC_U),
                        False,
                        None,
                        False,
                    ),
                ),
            )

    XX_GENERIC_TYPE = XXGeneric(XXGENERIC_TYPE_NAME, [OBJECT_TYPE])
else:
    SPAM_OBJ: Optional[GenericClass] = None


class ContextDecoratorClass(Class):
    def __init__(
        self,
        name: Optional[TypeName] = None,
        bases: Optional[List[Class]] = None,
        subclass: bool = False,
    ) -> None:
        super().__init__(
            name or TypeName("__static__", "ContextDecorator"),
            bases or [OBJECT_TYPE],
            ContextDecoratorInstance(self),
        )
        if not subclass:
            self.members["_recreate_cm"] = BuiltinMethodDescriptor(
                "_recreate_cm",
                self,
                (Parameter("self", 0, ResolvedTypeRef(self), False, None, False),),
                ResolvedTypeRef(self),
            )

    def make_subclass(self, name: TypeName, bases: List[Class]) -> Class:
        if len(bases) == 1:
            return ContextDecoratorClass(name, bases, True)
        return super().make_subclass(name, bases)


class ContextDecoratorInstance(Object[ContextDecoratorClass]):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        if fn.klass is FUNCTION_TYPE:
            return ContextDecoratedMethod(FUNCTION_TYPE, fn, decorator, self)
        return None


class ContextDecoratedMethod(DecoratedMethod):
    def __init__(
        self,
        klass: Class,
        function: Function | DecoratedMethod,
        decorator: expr,
        ctx_dec: ContextDecoratorInstance,
    ) -> None:
        super().__init__(klass, function, decorator)
        self.ctx_dec = ctx_dec
        self.body: List[ast.stmt] = self.make_function_body(
            function.get_function_body(), function, decorator
        )

    @staticmethod
    def get_temp_name(function: Function, decorator: expr) -> str:
        klass = function.container_type
        dec_index = function.node.decorator_list.index(decorator)

        if klass is not None:
            klass_name = klass.type_name.name
            return f"<{klass_name}.{function.func_name}_decorator_{dec_index}>"

        return f"<{function.func_name}_decorator_{dec_index}>"

    def get_function_body(self) -> List[ast.stmt]:
        return self.body

    @staticmethod
    def make_function_body(
        body: List[ast.stmt], fn: Function | DecoratedMethod, decorator: expr
    ) -> List[ast.stmt]:
        if isinstance(fn, DecoratedMethod):
            real_func = fn.real_function
        else:
            real_func = fn

        node = real_func.node
        klass = real_func.container_type
        dec_name = ContextDecoratedMethod.get_temp_name(real_func, decorator)

        if klass is not None:
            if ContextDecoratedMethod.can_load_from_class(klass, real_func):
                load_name = ast.Name(node.args.args[0].arg, ast.Load())
            else:
                load_name = ast.Name(klass.type_name.name, ast.Load())
            decorator_var = ast.Attribute(load_name, dec_name, ast.Load())
        else:
            decorator_var = ast.Name(dec_name, ast.Load())

        load_recreate = ast.Attribute(
            decorator_var,
            "_recreate_cm",
            ast.Load(),
        )
        call_recreate = ast.Call(load_recreate, [], [])

        with_item = ast.copy_location(ast.withitem(call_recreate, []), body[0])

        ast.fix_missing_locations(with_item)

        return [cast(ast.stmt, ast.With([with_item], body))]

    def replace_function(self, func: Function) -> Function | DecoratedMethod:
        return ContextDecoratedMethod(
            self.klass,
            self.function.replace_function(func),
            self.decorator,
            self.ctx_dec,
        )

    @staticmethod
    def can_load_from_class(klass: Class, func: Function) -> bool:
        if not func.args:
            return False

        arg_type = func.args[0].type_ref.resolved(False)
        return klass.can_assign_from(arg_type)

    def bind_function_inner(
        self, node: Union[FunctionDef, AsyncFunctionDef], visitor: TypeBinder
    ) -> None:
        klass = self.real_function.container_type
        dec_name = self.get_temp_name(self.real_function, self.decorator)
        if klass is not None:
            klass.define_slot(
                dec_name,
                self.decorator,
                ResolvedTypeRef(
                    CLASSVAR_TYPE.make_generic_type(
                        (self.ctx_dec.klass,),
                        self.real_function.module.compiler.type_env,
                    )
                ),
                assignment=node,
            )
        else:
            visitor.binding_scope.declare(dec_name, self.ctx_dec, is_final=True)
        self.function.bind_function_inner(node, visitor)

    def emit_function_body(
        self,
        node: ast.FunctionDef | ast.AsyncFunctionDef,
        code_gen: Static38CodeGenerator,
        first_lineno: int,
        body: List[ast.stmt],
    ) -> CodeGenerator:
        dec_name = self.get_temp_name(self.real_function, self.decorator)
        code_gen.visit(self.decorator)
        code_gen.storeName(dec_name)

        return self.function.emit_function_body(node, code_gen, first_lineno, body)

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        return self.function.bind_call(node, visitor, type_ctx)

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        return self.function.emit_call(node, code_gen)

    def resolve_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: ReferenceVisitor,
    ) -> Optional[Value]:
        return self.function.resolve_descr_get(node, inst, ctx, visitor)


CONTEXT_DECORATOR_TYPE = ContextDecoratorClass()

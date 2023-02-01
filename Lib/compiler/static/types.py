# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

from __future__ import annotations

from __static__ import chkdict, chklist

import ast
import dataclasses

import sys
from ast import (
    AnnAssign,
    Assign,
    AST,
    AsyncFunctionDef,
    Attribute,
    Bytes,
    Call,
    ClassDef,
    cmpop,
    Constant,
    copy_location,
    expr,
    FunctionDef,
    NameConstant,
    Num,
    Return,
    Starred,
    Str,
)
from copy import copy
from enum import Enum, IntEnum
from functools import cached_property
from types import (
    BuiltinFunctionType,
    GetSetDescriptorType,
    MethodDescriptorType,
    WrapperDescriptorType,
)
from typing import (
    Callable as typingCallable,
    cast,
    ClassVar as typingClassVar,
    Dict,
    Generic,
    Iterable,
    List,
    Mapping,
    Optional,
    Sequence,
    Set,
    Tuple,
    Type,
    TYPE_CHECKING,
    TypeVar,
    Union,
)

from _static import (  # noqa: F401
    FAST_LEN_ARRAY,
    FAST_LEN_DICT,
    FAST_LEN_INEXACT,
    FAST_LEN_LIST,
    FAST_LEN_SET,
    FAST_LEN_STR,
    FAST_LEN_TUPLE,
    PRIM_OP_ADD_DBL,
    PRIM_OP_ADD_INT,
    PRIM_OP_AND_INT,
    PRIM_OP_DIV_DBL,
    PRIM_OP_DIV_INT,
    PRIM_OP_DIV_UN_INT,
    PRIM_OP_EQ_DBL,
    PRIM_OP_EQ_INT,
    PRIM_OP_GE_DBL,
    PRIM_OP_GE_INT,
    PRIM_OP_GE_UN_INT,
    PRIM_OP_GT_DBL,
    PRIM_OP_GT_INT,
    PRIM_OP_GT_UN_INT,
    PRIM_OP_INV_INT,
    PRIM_OP_LE_DBL,
    PRIM_OP_LE_INT,
    PRIM_OP_LE_UN_INT,
    PRIM_OP_LSHIFT_INT,
    PRIM_OP_LT_DBL,
    PRIM_OP_LT_INT,
    PRIM_OP_LT_UN_INT,
    PRIM_OP_MOD_DBL,
    PRIM_OP_MOD_INT,
    PRIM_OP_MOD_UN_INT,
    PRIM_OP_MUL_DBL,
    PRIM_OP_MUL_INT,
    PRIM_OP_NE_DBL,
    PRIM_OP_NE_INT,
    PRIM_OP_NEG_DBL,
    PRIM_OP_NEG_INT,
    PRIM_OP_NOT_INT,
    PRIM_OP_OR_INT,
    PRIM_OP_POW_DBL,
    PRIM_OP_POW_INT,
    PRIM_OP_POW_UN_INT,
    PRIM_OP_RSHIFT_INT,
    PRIM_OP_RSHIFT_UN_INT,
    PRIM_OP_SUB_DBL,
    PRIM_OP_SUB_INT,
    PRIM_OP_XOR_INT,
    SEQ_ARRAY_INT64,
    SEQ_CHECKED_LIST,
    SEQ_LIST,
    SEQ_LIST_INEXACT,
    SEQ_REPEAT_INEXACT_NUM,
    SEQ_REPEAT_INEXACT_SEQ,
    SEQ_REPEAT_PRIMITIVE_NUM,
    SEQ_REPEAT_REVERSED,
    SEQ_SUBSCR_UNCHECKED,
    SEQ_TUPLE,
    TYPED_BOOL,
    TYPED_DOUBLE,
    TYPED_INT16,
    TYPED_INT32,
    TYPED_INT64,
    TYPED_INT8,
    TYPED_INT_64BIT,
    TYPED_OBJECT,
    TYPED_UINT16,
    TYPED_UINT32,
    TYPED_UINT64,
    TYPED_UINT8,
)

from ..consts import CO_STATICALLY_COMPILED
from ..errors import TypedSyntaxError
from ..optimizer import AstOptimizer
from ..pyassem import Block, FVC_REPR
from ..pycodegen import CodeGenerator, FOR_LOOP
from ..symbols import FunctionScope
from ..unparse import to_expr
from ..visitor import ASTRewriter, TAst
from .effects import NarrowingEffect, NO_EFFECT, TypeState
from .visitor import GenericVisitor

if TYPE_CHECKING:
    from . import PyFlowGraph38Static, Static38CodeGenerator
    from .compiler import Compiler
    from .declaration_visitor import DeclarationVisitor
    from .module_table import AnnotationVisitor, ModuleTable
    from .type_binder import BindingScope, TypeBinder

try:
    # pyre-ignore[21]: unknown module
    from xxclassloader import spamobj
except ImportError:
    spamobj = None


CACHED_PROPERTY_IMPL_PREFIX = "_pystatic_cprop."
ASYNC_CACHED_PROPERTY_IMPL_PREFIX = "_pystatic_async_cprop."


GenericTypeIndex = Tuple["Class", ...]
GenericTypesDict = Dict["Class", Dict[GenericTypeIndex, "Class"]]


class TypeEnvironment:
    def __init__(self) -> None:
        self._generic_types: GenericTypesDict = {}
        self._literal_types: Dict[Tuple[Value, object], Value] = {}
        self._nonliteral_types: Dict[Value, Value] = {}
        self._exact_types: Dict[Class, Class] = {}
        self._inexact_types: Dict[Class, Class] = {}
        # Bringing up the type system is a little special as we have dependencies
        # amongst type and object
        self.type: Class = Class.__new__(Class)
        self.type.type_name = TypeName("builtins", "type")
        self.type.type_env = self
        self.type.klass = self.type
        self.type.instance = self.type
        self.type.members = {}
        self.type.is_exact = False
        self.type.is_final = False
        self.type.allow_weakrefs = False
        self.type.donotcompile = False
        self.type._mro = None
        self.type.pytype = type
        self.type._member_nodes = {}
        self.type.dynamic_builtinmethod_dispatch = False
        self.object: Class = BuiltinObject(
            TypeName("builtins", "object"),
            self,
            bases=[],
        )
        self.type.bases = [self.object]
        self.dynamic = DynamicClass(self)

        self.builtin_method_desc = Class(
            TypeName("types", "MethodDescriptorType"),
            self,
            is_exact=True,
        )
        self.builtin_method = Class(
            TypeName("types", "BuiltinMethodType"), self, is_exact=True
        )
        self.getset_descriptor: Class = Class(
            TypeName("builtins", "getset_descriptor"), self, [self.object]
        )
        # We special case make_type_dict() on object for bootstrapping purposes.
        self.object.pytype = object
        self.object.members["__class__"] = ClassGetSetDescriptor(self.getset_descriptor)
        self.object.make_type_dict()

        self.type.make_type_dict()
        self.type.members["__name__"] = TypeDunderNameGetSetDescriptor(
            self.getset_descriptor, self
        )

        self.getset_descriptor.pytype = GetSetDescriptorType
        self.getset_descriptor.make_type_dict()
        self.str = StrClass(self)
        self.int = NumClass(TypeName("builtins", "int"), self, pytype=int)
        self.float = NumClass(TypeName("builtins", "float"), self, pytype=float)
        self.complex = NumClass(TypeName("builtins", "complex"), self, pytype=complex)
        self.bytes = Class(
            TypeName("builtins", "bytes"), self, [self.object], pytype=bytes
        )
        self.bool: Class = BoolClass(self)
        self.cbool: CIntType = CIntType(TYPED_BOOL, self, name_override="cbool")
        self.range: Class = Class(
            TypeName("builtins", "range"), self, [self.object], pytype=range
        )

        self.int8: CIntType = CIntType(TYPED_INT8, self)
        self.int16: CIntType = CIntType(TYPED_INT16, self)
        self.int32: CIntType = CIntType(TYPED_INT32, self)
        self.int64: CIntType = CIntType(TYPED_INT64, self)
        self.uint8: CIntType = CIntType(TYPED_UINT8, self)
        self.uint16: CIntType = CIntType(TYPED_UINT16, self)
        self.uint32: CIntType = CIntType(TYPED_UINT32, self)
        self.uint64: CIntType = CIntType(TYPED_UINT64, self)
        # TODO uses of these to check if something is a CInt wrongly exclude literals
        self.signed_cint_types: Sequence[CIntType] = [
            self.int8,
            self.int16,
            self.int32,
            self.int64,
        ]
        self.unsigned_cint_types: Sequence[CIntType] = [
            self.uint8,
            self.uint16,
            self.uint32,
            self.uint64,
        ]
        self.all_cint_types: Sequence[CIntType] = (
            self.signed_cint_types + self.unsigned_cint_types
        )
        self.none = NoneType(self)
        self.optional = OptionalType(self)
        self.name_to_type: Mapping[str, Class] = {
            "NoneType": self.none,
            "object": self.object,
            "str": self.str,
            "__static__.int8": self.int8,
            "__static__.int16": self.int16,
            "__static__.int32": self.int32,
            "__static__.int64": self.int64,
            "__static__.uint8": self.uint8,
            "__static__.uint16": self.uint16,
            "__static__.uint32": self.uint32,
            "__static__.uint64": self.uint64,
        }
        if spamobj is not None:
            self.spam_obj: Optional[GenericClass] = GenericClass(
                GenericTypeName(
                    "xxclassloader", "spamobj", (GenericParameter("T", 0, self),)
                ),
                self,
                [self.object],
                pytype=spamobj,
            )
        else:
            self.spam_obj = None
        checked_dict_type_name = GenericTypeName(
            "__static__",
            "chkdict",
            (GenericParameter("K", 0, self), GenericParameter("V", 1, self)),
        )
        checked_list_type_name = GenericTypeName(
            "__static__", "chklist", (GenericParameter("T", 0, self),)
        )
        self.checked_dict = CheckedDict(
            checked_dict_type_name,
            self,
            [self.object],
            pytype=chkdict,
            is_exact=True,
        )
        self.checked_list = CheckedList(
            checked_list_type_name,
            self,
            [self.object],
            pytype=chklist,
            is_exact=True,
        )
        self.ellipsis = Class(
            TypeName("builtins", "ellipsis"),
            self,
            [self.object],
            pytype=type(...),
            is_exact=True,
        )
        self.dict = DictClass(self, is_exact=False)
        self.list = ListClass(self)
        self.set = SetClass(self, is_exact=False)
        self.frozenset = FrozenSetClass(self, is_exact=False)
        self.tuple = TupleClass(self)
        self.function = Class(TypeName("types", "FunctionType"), self, is_exact=True)
        self.method = Class(TypeName("types", "MethodType"), self, is_exact=True)
        self.member = Class(
            TypeName("types", "MemberDescriptorType"), self, is_exact=True
        )
        self.slice = Class(TypeName("builtins", "slice"), self, is_exact=True)
        self.super = SuperClass(self, is_exact=True)
        self.char = CIntType(TYPED_INT8, self, name_override="char")
        self.module = ModuleType(self)
        self.double = CDoubleType(self)

        self.array = ArrayClass(
            GenericTypeName("__static__", "Array", (GenericParameter("T", 0, self),)),
            self,
            is_exact=True,
        )

        # We have found no strong reason (yet) to support arrays of other types of
        # primitives
        self.allowed_array_types: List[Class] = [
            self.int64,
        ]

        self.static_method = StaticMethodDecorator(
            TypeName("builtins", "staticmethod"),
            self,
            pytype=staticmethod,
        )
        self.class_method = ClassMethodDecorator(
            TypeName("builtins", "classmethod"),
            self,
            pytype=classmethod,
        )
        self.final_method = TypingFinalDecorator(TypeName("typing", "final"), self)
        self.awaitable = AwaitableType(self)
        self.union = UnionType(self)
        self.final = FinalClass(
            GenericTypeName("typing", "Final", (GenericParameter("T", 0, self),)), self
        )
        self.classvar = ClassVar(
            GenericTypeName("typing", "ClassVar", (GenericParameter("T", 0, self),)),
            self,
        )
        self.initvar = InitVar(
            GenericTypeName(
                "dataclasses", "InitVar", (GenericParameter("T", 0, self),)
            ),
            self,
        )
        self.exact = ExactClass(
            GenericTypeName("typing", "Exact", (GenericParameter("T", 0, self),)), self
        )
        self.named_tuple = Class(TypeName("typing", "NamedTuple"), self)
        self.protocol = Class(TypeName("typing", "Protocol"), self)
        self.typed_dict = Class(TypeName("typing", "TypedDict"), self)
        self.literal = LiteralType(TypeName("typing", "Literal"), self)
        self.annotated = AnnotatedType(TypeName("typing", "Annotated"), self)
        self.not_implemented = Class(
            TypeName("builtins", "NotImplementedType"),
            self,
            bases=[self.object],
            pytype=type(NotImplemented),
        )
        self.base_exception = Class(
            TypeName("builtins", "BaseException"), self, pytype=BaseException
        )
        self.exception = Class(
            TypeName("builtins", "Exception"),
            self,
            bases=[self.base_exception],
            pytype=Exception,
        )
        self.value_error: Class = self._builtin_exception_class(ValueError)
        self.os_error: Class = self._builtin_exception_class(OSError)
        self.runtime_error: Class = self._builtin_exception_class(RuntimeError)
        self.syntax_error: Class = self._builtin_exception_class(SyntaxError)
        self.arithmetic_error: Class = self._builtin_exception_class(ArithmeticError)
        self.assertion_error: Class = self._builtin_exception_class(AssertionError)
        self.attribute_error: Class = self._builtin_exception_class(AttributeError)
        self.blocking_io_error: Class = self._builtin_exception_class(
            BlockingIOError, base=self.os_error
        )
        self.buffer_error: Class = self._builtin_exception_class(BufferError)
        self.child_process_error: Class = self._builtin_exception_class(
            ChildProcessError, base=self.os_error
        )
        self.connection_error: Class = self._builtin_exception_class(
            ConnectionError, base=self.os_error
        )
        self.broken_pipe_error: Class = self._builtin_exception_class(
            BrokenPipeError, self.connection_error
        )
        self.connection_aborted_error: Class = self._builtin_exception_class(
            ConnectionAbortedError, base=self.connection_error
        )
        self.connection_refused_error: Class = self._builtin_exception_class(
            ConnectionRefusedError, base=self.connection_error
        )
        self.connection_reset_error: Class = self._builtin_exception_class(
            ConnectionResetError, base=self.connection_error
        )
        self.environment_error: Class = self._builtin_exception_class(EnvironmentError)
        self.eof_error: Class = self._builtin_exception_class(EOFError)
        self.file_exists_error: Class = self._builtin_exception_class(
            FileExistsError, base=self.os_error
        )
        self.file_not_found_error: Class = self._builtin_exception_class(
            FileNotFoundError, base=self.os_error
        )
        self.floating_point_error: Class = self._builtin_exception_class(
            FloatingPointError, base=self.arithmetic_error
        )
        self.generator_exit: Class = self._builtin_exception_class(
            GeneratorExit, base=self.base_exception
        )
        self.import_error: Class = self._builtin_exception_class(ImportError)
        self.indentation_error: Class = self._builtin_exception_class(
            IndentationError, base=self.syntax_error
        )
        self.index_error: Class = self._builtin_exception_class(IndexError)
        self.interrupted_error: Class = self._builtin_exception_class(
            InterruptedError, base=self.os_error
        )
        self.io_error: Class = self._builtin_exception_class(IOError)
        self.is_a_directory_error: Class = self._builtin_exception_class(
            IsADirectoryError, base=self.os_error
        )
        self.key_error: Class = self._builtin_exception_class(KeyError)
        self.keyboard_interrupt: Class = self._builtin_exception_class(
            KeyboardInterrupt, base=self.base_exception
        )
        self.lookup_error: Class = self._builtin_exception_class(LookupError)
        self.memory_error: Class = self._builtin_exception_class(MemoryError)
        self.module_not_found_error: Class = self._builtin_exception_class(
            ModuleNotFoundError, base=self.import_error
        )
        self.name_error: Class = self._builtin_exception_class(NameError)
        self.not_a_directory_error: Class = self._builtin_exception_class(
            NotADirectoryError, base=self.os_error
        )
        self.not_implemented_error: Class = self._builtin_exception_class(
            NotImplementedError, base=self.runtime_error
        )
        self.overflow_error: Class = self._builtin_exception_class(
            OverflowError, base=self.arithmetic_error
        )
        self.permission_error: Class = self._builtin_exception_class(
            PermissionError, base=self.os_error
        )
        self.process_lookup_error: Class = self._builtin_exception_class(
            ProcessLookupError, base=self.os_error
        )
        self.recursion_error: Class = self._builtin_exception_class(
            RecursionError, base=self.runtime_error
        )
        self.reference_error: Class = self._builtin_exception_class(ReferenceError)
        self.stop_async_iteration: Class = self._builtin_exception_class(
            StopAsyncIteration
        )
        self.stop_iteration: Class = self._builtin_exception_class(StopIteration)
        self.system_error: Class = self._builtin_exception_class(SystemError)
        self.system_exit: Class = self._builtin_exception_class(
            SystemExit, base=self.base_exception
        )
        self.tab_error: Class = self._builtin_exception_class(
            TabError, base=self.indentation_error
        )
        self.timeout_error: Class = self._builtin_exception_class(
            TimeoutError, base=self.os_error
        )
        self.type_error: Class = self._builtin_exception_class(TypeError)
        self.unicode_error: Class = self._builtin_exception_class(
            UnicodeError, base=self.value_error
        )
        self.unbound_local_error: Class = self._builtin_exception_class(
            UnboundLocalError, base=self.name_error
        )
        self.unicode_decode_error: Class = self._builtin_exception_class(
            UnicodeDecodeError, base=self.unicode_error
        )
        self.unicode_encode_error: Class = self._builtin_exception_class(
            UnicodeEncodeError, base=self.unicode_error
        )
        self.unicode_translate_error: Class = self._builtin_exception_class(
            UnicodeTranslateError, base=self.unicode_error
        )
        self.zero_division_error: Class = self._builtin_exception_class(
            ZeroDivisionError, base=self.arithmetic_error
        )

        self.warning: Class = self._builtin_exception_class(Warning)
        self.bytes_warning: Class = self._builtin_exception_class(
            BytesWarning, base=self.warning
        )
        self.deprecation_warning: Class = self._builtin_exception_class(
            DeprecationWarning, base=self.warning
        )
        self.future_warning: Class = self._builtin_exception_class(
            FutureWarning, base=self.warning
        )
        self.import_warning: Class = self._builtin_exception_class(
            ImportWarning, base=self.warning
        )
        self.pending_deprecation_warning: Class = self._builtin_exception_class(
            PendingDeprecationWarning, base=self.warning
        )
        self.resource_warning: Class = self._builtin_exception_class(
            ResourceWarning, base=self.warning
        )
        self.runtime_warning: Class = self._builtin_exception_class(
            RuntimeWarning, base=self.warning
        )
        self.syntax_warning: Class = self._builtin_exception_class(
            SyntaxWarning, base=self.warning
        )
        self.unicode_warning: Class = self._builtin_exception_class(
            UnicodeWarning, base=self.warning
        )
        self.user_warning: Class = self._builtin_exception_class(
            UserWarning, base=self.warning
        )

        self.allow_weakrefs = AllowWeakrefsDecorator(
            TypeName("__static__", "allow_weakrefs"), self
        )
        self.dynamic_return = DynamicReturnDecorator(
            TypeName("__static__", "dynamic_return"), self
        )
        self.inline = InlineFunctionDecorator(TypeName("__static__", "inline"), self)
        self.donotcompile = DoNotCompileDecorator(
            TypeName("__static__", "_donotcompile"), self
        )
        self.property = PropertyDecorator(
            TypeName("builtins", "property"),
            self,
            pytype=property,
        )
        self.overload = OverloadDecorator(
            TypeName("typing", "overload"),
            self,
        )
        self.cached_property = CachedPropertyDecorator(
            TypeName("cinder", "cached_property"), self
        )
        self.async_cached_property = AsyncCachedPropertyDecorator(
            TypeName("cinder", "async_cached_property"), self
        )
        self.dataclass = DataclassDecorator(self)
        self.dataclass_field = DataclassFieldType(self)
        self.dataclass_field_function = DataclassFieldFunction(self)
        self.constant_types: Mapping[Type[object], Value] = {
            str: self.str.exact_type().instance,
            int: self.int.exact_type().instance,
            float: self.float.exact_type().instance,
            complex: self.complex.exact_type().instance,
            bytes: self.bytes.instance,
            bool: self.bool.instance,
            type(None): self.none.instance,
            tuple: self.tuple.exact_type().instance,
            type(...): self.ellipsis.instance,
            frozenset: self.set.instance,
        }
        self.enum: EnumType = EnumType(self)
        self.int_enum: IntEnumType = IntEnumType(self)
        self.string_enum: StringEnumType = StringEnumType(self)
        self.exc_context_decorator = ContextDecoratorClass(
            self, TypeName("__static__", "ExcContextDecorator")
        )
        self.context_decorator = ContextDecoratorClass(
            self,
            TypeName("__static__", "ContextDecorator"),
            bases=[self.exc_context_decorator],
        )
        self.crange_iterator = CRangeIterator(self.type)

        self.str.exact_type().patch_reflected_method_types(self)

        self.native_decorator = NativeDecorator(self)

        if spamobj is not None:
            T = GenericParameter("T", 0, self)
            U = GenericParameter("U", 1, self)
            XXGENERIC_TYPE_NAME = GenericTypeName("xxclassloader", "XXGeneric", (T, U))
            self.xx_generic: XXGeneric = XXGeneric(
                XXGENERIC_TYPE_NAME, self, [self.object]
            )

    def _builtin_exception_class(
        self, exception_type: Type[object], base: Optional[Class] = None
    ) -> Class:
        if base is None:
            base = self.exception
        return Class(
            TypeName("builtins", exception_type.__name__),
            self,
            bases=[base],
            pytype=exception_type,
        )

    def get_generic_type(
        self, generic_type: GenericClass, index: GenericTypeIndex
    ) -> Class:
        instantiations = self._generic_types.setdefault(generic_type, {})
        instance = instantiations.get(index)
        if instance is not None:
            return instance
        concrete = generic_type.make_generic_type(index)
        instantiations[index] = concrete
        concrete.members.update(
            {
                # pyre-ignore[6]: We trust that the type name is generic here.
                k: v.make_generic(concrete, concrete.type_name, self)
                for k, v in generic_type.members.items()
            }
        )
        return concrete

    def get_literal_type(self, base_type: Value, literal_value: object) -> Value:
        # Literals are always exact
        base_type = base_type.exact()
        key = (base_type, literal_value)
        if key not in self._literal_types:
            self._literal_types[key] = literal_type = base_type.make_literal(
                literal_value, self
            )
            self._nonliteral_types[literal_type] = base_type
        return self._literal_types[key]

    def get_nonliteral_type(self, literal_type: Value) -> Value:
        return self._nonliteral_types.get(literal_type, literal_type)

    def get_exact_type(self, klass: Class) -> Class:
        if klass.is_exact:
            return klass
        if klass in self._exact_types:
            return self._exact_types[klass]
        exact_klass = klass._create_exact_type()
        self._exact_types[klass] = exact_klass
        self._inexact_types[exact_klass] = klass
        return exact_klass

    def get_inexact_type(self, klass: Class) -> Class:
        if not klass.is_exact:
            return klass
        # Some types are always exact by default and have no inexact version. In that case,
        # the exact type is the correct value to return.
        if klass not in self._inexact_types:
            return klass
        return self._inexact_types[klass]

    @property
    def DYNAMIC(self) -> Value:
        return self.dynamic.instance

    @property
    def OBJECT(self) -> Value:
        return self.object.instance

    def get_union(self, index: GenericTypeIndex) -> Class:
        return self.get_generic_type(self.union, index)


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
    ast.In: "in",
}


class KnownBoolean(IntEnum):
    FALSE = 0
    TRUE = 1


class TypeRef:
    """Stores unresolved typed references, capturing the referring module
    as well as the annotation"""

    def __init__(self, module: ModuleTable, ref: ast.expr) -> None:
        self._module = module
        self._ref = ref

    def resolved(self, is_declaration: bool = False) -> Class:
        res = self._module.resolve_annotation(self._ref, is_declaration=is_declaration)
        if res is None:
            return self._module.compiler.type_env.dynamic
        return res

    def __repr__(self) -> str:
        return f"TypeRef({self._module.name}, {ast.dump(self._ref)})"


class ResolvedTypeRef(TypeRef):
    def __init__(self, type: Class) -> None:
        self._resolved = type

    def resolved(self, is_declaration: bool = False) -> Class:
        return self._resolved

    def __repr__(self) -> str:
        return f"ResolvedTypeRef({self.resolved()})"


class SelfTypeRef(TypeRef):
    """Marker for return type of methods that return SelfType."""

    def __init__(self, bound: Class) -> None:
        self._bound = bound

    def resolved(self, is_declaration: bool = False) -> Class:
        return self._bound

    def __repr__(self) -> str:
        return f"SelfTypeRef({self.resolved()})"


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

    @property
    def name_with_exact(self) -> str:
        return self.name

    def exact(self) -> Value:
        return self

    def inexact(self) -> Value:
        return self

    def nonliteral(self) -> Value:
        return self.klass.type_env.get_nonliteral_type(self)

    def finish_bind(self, module: ModuleTable, klass: Class | None) -> Optional[Value]:
        return self

    def make_generic_type(self, index: GenericTypeIndex) -> Optional[Class]:
        pass

    def get_iter_type(self, node: ast.expr, visitor: TypeBinder) -> Value:
        """returns the type that is produced when iterating over this value"""
        visitor.syntax_error(f"cannot iterate over {self.name}", node)
        return visitor.type_env.DYNAMIC

    def as_oparg(self) -> int:
        raise TypeError(f"{self.name} not valid here")

    def can_override(self, override: Value, klass: Class, module: ModuleTable) -> bool:
        return type(self) == type(override)

    def resolve_attr(
        self, node: ast.Attribute, visitor: GenericVisitor[object]
    ) -> Optional[Value]:
        visitor.syntax_error(f"cannot load attribute from {self.name}", node)

    def bind_attr(
        self, node: ast.Attribute, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        visitor.set_type(
            node,
            self.resolve_attr(node, visitor.module.ann_visitor)
            or visitor.type_env.DYNAMIC,
        )

    def bind_await(
        self, node: ast.Await, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        visitor.set_type(node, visitor.type_env.DYNAMIC)

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
        visitor: GenericVisitor[object],
    ) -> Optional[Value]:
        return self

    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        return None

    def resolve_decorate_class(
        self,
        klass: Class,
        decorator: expr,
        visitor: DeclarationVisitor,
    ) -> Class:
        return self.klass.type_env.dynamic

    def bind_subscr(
        self,
        node: ast.Subscript,
        type: Value,
        visitor: TypeBinder,
        type_ctx: Optional[Class] = None,
    ) -> None:
        visitor.check_can_assign_from(visitor.type_env.dynamic, type.klass, node)
        visitor.set_type(
            node,
            self.resolve_subscr(node, type, visitor.module.ann_visitor)
            or visitor.type_env.DYNAMIC,
        )

    def resolve_subscr(
        self,
        node: ast.Subscript,
        type: Value,
        visitor: AnnotationVisitor,
    ) -> Optional[Value]:
        visitor.syntax_error(f"cannot index {self.name}", node)

    def emit_subscr(self, node: ast.Subscript, code_gen: Static38CodeGenerator) -> None:
        code_gen.set_lineno(node)
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

    def emit_decorator_call(
        self, class_def: ClassDef, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.emit("CALL_FUNCTION", 1)

    def emit_delete_attr(
        self, node: ast.Attribute, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.emit("DELETE_ATTR", code_gen.mangle(node.attr))

    def emit_load_attr(
        self, node: ast.Attribute, code_gen: Static38CodeGenerator
    ) -> None:
        member = self.klass.members.get(node.attr, self.klass.type_env.DYNAMIC)
        member.emit_load_attr_from(node, code_gen, self.klass)

    def emit_load_attr_from(
        self, node: Attribute, code_gen: Static38CodeGenerator, klass: Class
    ) -> None:
        if klass is klass.type_env.dynamic:
            code_gen.perf_warning(
                "Define the object's class in a Static Python "
                "module for more efficient attribute load",
                node,
            )
        elif klass.type_env.dynamic in klass.bases:
            code_gen.perf_warning(
                f"Make the base class of {klass.instance_name} that defines "
                f"attribute {node.attr} static for more efficient attribute load",
                node,
            )
        code_gen.emit("LOAD_ATTR", code_gen.mangle(node.attr))

    def emit_store_attr(
        self, node: ast.Attribute, code_gen: Static38CodeGenerator
    ) -> None:
        member = self.klass.members.get(node.attr, self.klass.type_env.DYNAMIC)
        member.emit_store_attr_to(node, code_gen, self.klass)

    def emit_store_attr_to(
        self, node: Attribute, code_gen: Static38CodeGenerator, klass: Class
    ) -> None:
        if klass is klass.type_env.dynamic:
            code_gen.perf_warning(
                f"Define the object's class in a Static Python "
                "module for more efficient attribute store",
                node,
            )
        elif klass.type_env.dynamic in klass.bases:
            code_gen.perf_warning(
                f"Make the base class of {klass.instance_name} that defines "
                f"attribute {node.attr} static for more efficient attribute store",
                node,
            )
        code_gen.emit("STORE_ATTR", code_gen.mangle(node.attr))

    def emit_attr(self, node: ast.Attribute, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(node.value)
        if isinstance(node.ctx, ast.Store):
            self.emit_store_attr(node, code_gen)
        elif isinstance(node.ctx, ast.Del):
            self.emit_delete_attr(node, code_gen)
        else:
            self.emit_load_attr(node, code_gen)

    def bind_forloop_target(self, target: ast.expr, visitor: TypeBinder) -> None:
        visitor.visit(target)

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

    def make_literal(self, literal_value: object, type_env: TypeEnvironment) -> Value:
        raise NotImplementedError(f"Type {self.name} does not support literals")

    def emit_convert(self, from_type: Value, code_gen: Static38CodeGenerator) -> None:
        pass

    def is_truthy_literal(self) -> bool:
        return False


def resolve_instance_attr(
    node: ast.Attribute,
    inst: Object[Class],
    visitor: GenericVisitor[object],
) -> Optional[Value]:
    for base in inst.klass.mro:
        member = base.members.get(node.attr)
        if member is not None:
            res = member.resolve_descr_get(node, inst, inst.klass, visitor)
            if res is not None:
                return res

    return inst.klass.type_env.DYNAMIC


def resolve_instance_attr_by_name(
    base: ast.expr,
    attr: str,
    inst: Object[Class],
    visitor: GenericVisitor[object],
) -> Optional[Value]:
    node = ast.Attribute(base, attr, ast.Load())
    return resolve_instance_attr(node, inst, visitor)


class Object(Value, Generic[TClass]):
    """Represents an instance of a type at compile time"""

    klass: TClass

    @property
    def name(self) -> str:
        return self.klass.qualname

    @property
    def name_with_exact(self) -> str:
        return self.klass.instance_name_with_exact

    def as_oparg(self) -> int:
        return TYPED_OBJECT

    @staticmethod
    def bind_dynamic_call(node: ast.Call, visitor: TypeBinder) -> NarrowingEffect:
        visitor.set_type(node, visitor.type_env.DYNAMIC)
        for arg in node.args:
            visitor.visitExpectedType(
                arg, visitor.type_env.DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE
            )

        for arg in node.keywords:
            visitor.visitExpectedType(
                arg.value, visitor.type_env.DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE
            )

        return NO_EFFECT

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        return self.bind_dynamic_call(node, visitor)

    def resolve_attr(
        self, node: ast.Attribute, visitor: GenericVisitor[object]
    ) -> Optional[Value]:
        return resolve_instance_attr(node, self, visitor)

    def emit_delete_attr(
        self, node: ast.Attribute, code_gen: Static38CodeGenerator
    ) -> None:
        if self.klass.find_slot(node) and node.attr != "__dict__":
            code_gen.emit("DELETE_ATTR", node.attr)
            return

        super().emit_delete_attr(node, code_gen)

    def emit_load_attr(
        self, node: ast.Attribute, code_gen: Static38CodeGenerator
    ) -> None:
        if (member := self.klass.find_slot(node)) and node.attr != "__dict__":
            member.emit_load_from_slot(code_gen)
            return

        super().emit_load_attr(node, code_gen)

    def emit_store_attr(
        self, node: ast.Attribute, code_gen: Static38CodeGenerator
    ) -> None:
        if (member := self.klass.find_slot(node)) and node.attr != "__dict__":
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
            or visitor.type_env.DYNAMIC,
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
        visitor.set_type(op, visitor.type_env.DYNAMIC)
        if isinstance(op, (ast.Is, ast.IsNot, ast.In, ast.NotIn)):
            visitor.set_type(node, visitor.type_env.bool.instance)
            return True
        visitor.set_type(node, visitor.type_env.DYNAMIC)
        return False

    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        return False

    def bind_reverse_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        # we'll set the type in case we're the only one called
        visitor.set_type(node, visitor.type_env.DYNAMIC)
        return False

    def bind_unaryop(
        self, node: ast.UnaryOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        if isinstance(node.op, ast.Not):
            visitor.set_type(node, visitor.type_env.bool.instance)
        else:
            visitor.set_type(node, visitor.type_env.DYNAMIC)

    def bind_constant(self, node: ast.Constant, visitor: TypeBinder) -> None:
        if type(node.value) is int:
            node_type = visitor.type_env.get_literal_type(
                visitor.type_env.int.instance, node.value
            )
        elif type(node.value) is bool:
            node_type = visitor.type_env.get_literal_type(
                visitor.type_env.bool.instance, node.value
            )
        else:
            node_type = visitor.type_env.constant_types[type(node.value)]
        visitor.set_type(node, node_type)

    def get_iter_type(self, node: ast.expr, visitor: TypeBinder) -> Value:
        """returns the type that is produced when iterating over this value"""
        return visitor.type_env.DYNAMIC

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
        function: Function,
        klass: Class,
        init_func: FunctionDef,
    ) -> None:
        super().__init__(function.module)
        self.args_by_name: Dict[str, Parameter] = function.args_by_name
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
                node_value = node.value

                # Infer attribute's type based off of the type of the RHS of
                # the assignment
                if (
                    isinstance(node_value, ast.Name)
                    and (param := self.args_by_name.get(node_value.id))
                    and not self.klass.get_member(attr)
                ):
                    self.klass.define_slot(
                        attr,
                        target,
                        param.type_ref,
                        assignment=node,
                    )
                else:
                    self.klass.define_slot(attr, target, assignment=node)


class FunctionGroup(Value):
    """Represents a group of functions defined in a function with
    the same name.  This object is ephemeral and is removed when we
    finish the bind of a class.  Common scenarios where this occur are the
    usage of the the ".setter" syntax for properties, or the @overload
    decorator"""

    def __init__(self, functions: List[Function], type_env: TypeEnvironment) -> None:
        super().__init__(type_env.function)
        self.functions = functions

    def finish_bind(self, module: ModuleTable, klass: Class | None) -> Optional[Value]:
        known_funcs = []
        for func in self.functions:
            new_func = func.finish_bind(module, klass)

            underlying_func = new_func
            while isinstance(underlying_func, TransparentDecoratedMethod):
                underlying_func = underlying_func.function

            if new_func is not None and not isinstance(
                underlying_func, TransientDecoratedMethod
            ):
                known_funcs.append(new_func)
                module.ref_visitor.add_local_name(new_func.node.name, new_func)

        module.ref_visitor.clear_local_names()

        if known_funcs and len(known_funcs) > 1:
            with module.error_context(known_funcs[1].node):
                raise TypedSyntaxError(
                    f"function '{known_funcs[1].name}' conflicts with other member"
                )
        elif not known_funcs:
            return None

        return known_funcs[0]

    def resolve_attr(
        self, node: ast.Attribute, visitor: GenericVisitor[object]
    ) -> Optional[Value]:
        visitor.syntax_error(f"cannot load attribute from {self.name}", node)


class Class(Object["Class"]):
    """Represents a type object at compile time"""

    def __init__(
        self,
        type_name: TypeName,
        type_env: TypeEnvironment,
        bases: Optional[List[Class]] = None,
        instance: Optional[Value] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        is_exact: bool = False,
        pytype: Optional[Type[object]] = None,
        is_final: bool = False,
    ) -> None:
        super().__init__(klass or type_env.type)
        assert isinstance(bases, (type(None), list))
        self.type_name = type_name
        self.type_env = type_env
        self.instance: Value = instance or Object(self)
        self.bases: List[Class] = self._get_bases(bases)
        self._mro: Optional[List[Class]] = None
        # members are attributes or methods
        self.members: Dict[str, Value] = members or {}
        self.is_exact = is_exact
        self.is_final = is_final
        self.allow_weakrefs = False
        self.donotcompile = False
        # This will cause all built-in method calls on the type to be done dynamically
        self.dynamic_builtinmethod_dispatch = False
        self.pytype = pytype
        if self.pytype is not None:
            self.make_type_dict()
        # track AST node of each member until finish_bind, for error reporting
        self._member_nodes: Dict[str, AST] = {}

    def _get_bases(self, bases: Optional[List[Class]]) -> List[Class]:
        if bases is None:
            return [self.klass.type_env.object]
        ret = []
        for b in bases:
            ret.append(b)
            # Can't check for dynamic because that'd be a cyclic dependency
            if isinstance(b, DynamicClass):
                # If any of the defined bases is dynamic,
                # stop processing, because it doesn't matter
                # what the rest of them are.
                break
        return ret

    def make_type_dict(self) -> None:
        pytype = self.pytype
        if pytype is None:
            return
        result: Dict[str, Value] = {}
        for k in pytype.__dict__.keys():
            # Constructors might set custom members, make sure to respect those.
            if k in self.members:
                continue
            try:
                obj = pytype.__dict__[k]
            except AttributeError:
                continue

            if isinstance(obj, (MethodDescriptorType, WrapperDescriptorType)):
                result[k] = reflect_method_desc(obj, self, self.type_env)
            elif isinstance(obj, BuiltinFunctionType):
                result[k] = reflect_builtin_function(obj, self, self.type_env)
            elif isinstance(obj, GetSetDescriptorType):
                result[k] = GetSetDescriptor(self.type_env.getset_descriptor)

        self.members.update(result)

    def make_subclass(self, name: TypeName, bases: List[Class]) -> Class:
        return Class(name, self.type_env, bases)

    @property
    def name(self) -> str:
        return f"Type[{self.instance_name}]"

    @property
    def name_with_exact(self) -> str:
        return f"Type[{self.instance_name_with_exact}]"

    @property
    def instance_name(self) -> str:
        # We need to break the loop for `builtins.type`, as `builtins.type`'s instance is a Class.
        if type(self.instance) == Class:
            return "type"
        return self.instance.name

    @property
    def instance_name_with_exact(self) -> str:
        name = self.instance.name
        if self.is_exact:
            return f"Exact[{name}]"
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

    def reflected_method_types(self, type_env: TypeEnvironment) -> Dict[str, Class]:
        return {}

    def patch_reflected_method_types(self, type_env: TypeEnvironment) -> None:
        for name, return_type in self.reflected_method_types(type_env).items():
            member = self.members[name]
            assert isinstance(member, BuiltinMethodDescriptor)
            member.return_type = ResolvedTypeRef(return_type)

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
    ) -> Optional[Class]:
        """Binds the generic type parameters to a generic type definition"""
        return None

    def resolve_attr(
        self, node: ast.Attribute, visitor: GenericVisitor[object]
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
            if rtype is visitor.type_env.none.instance:
                rtype = visitor.type_env.none
            if rtype is visitor.type_env.DYNAMIC:
                rtype = visitor.type_env.dynamic
            if not isinstance(rtype, Class):
                visitor.syntax_error(
                    f"unsupported operand type(s) for |: {self.name} and {rtype.name}",
                    node,
                )
                return False
            union = visitor.type_env.get_union((self, rtype))
            visitor.set_type(node, union)
            return True

        return super().bind_binop(node, visitor, type_ctx)

    @property
    def can_be_narrowed(self) -> bool:
        return True

    @property
    def type_descr(self) -> TypeDescr:
        if self.is_exact:
            return self.type_name.type_descr + ("!",)
        return self.type_name.type_descr

    def _resolve_dunder(self, name: str) -> Tuple[Class, Optional[Value]]:
        klass = self.type_env.object
        for klass in self.mro:
            if klass is self.type_env.dynamic:
                return self.type_env.dynamic, None

            if val := klass.members.get(name):
                return klass, val

        assert klass.inexact_type() is self.type_env.object
        return self.type_env.object, None

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        self_type = self.instance
        new_mapping: Optional[ArgMapping] = None
        init_mapping: Optional[ArgMapping] = None

        dynamic_call = True

        klass, new = self._resolve_dunder("__new__")
        dynamic_new = klass is self.type_env.dynamic
        object_new = klass.inexact_type() is self.type_env.object

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
            dynamic_call = dynamic_call or klass is self.type_env.dynamic
            object_init = klass.inexact_type() is self.type_env.object
            if not object_init and isinstance(init, Callable):
                init_mapping = ArgMapping(init, node, visitor, None)
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

        if not self.is_exact and not self.is_final:
            dynamic_call = True

        visitor.set_type(node, self_type)
        visitor.set_node_data(
            node, ClassCallInfo, ClassCallInfo(new_mapping, init_mapping, dynamic_call)
        )

        if dynamic_call:
            for arg in node.args:
                visitor.visitExpectedType(
                    arg, visitor.type_env.DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE
                )
            for arg in node.keywords:
                visitor.visitExpectedType(
                    arg.value,
                    visitor.type_env.DYNAMIC,
                    CALL_ARGUMENT_CANNOT_BE_PRIMITIVE,
                )

        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
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
            (not self.is_exact or src.instance.nonliteral() is self.instance)
            and not isinstance(src, CType)
            and src.instance.nonliteral().klass.is_subclass_of(self)
        )

    def __repr__(self) -> str:
        return f"<{self.name} class>"

    def exact(self) -> Class:
        return self

    def inexact(self) -> Class:
        return self

    def exact_type(self) -> Class:
        return self.type_env.get_exact_type(self)

    def inexact_type(self) -> Class:
        return self.type_env.get_inexact_type(self)

    def _create_exact_type(self) -> Class:
        instance = copy(self.instance)
        klass = type(self)(
            type_name=self.type_name,
            type_env=self.type_env,
            bases=self.bases,
            klass=self.klass,
            members=self.members,
            instance=instance,
            is_exact=True,
            pytype=self.pytype,
            is_final=self.is_final,
        )
        # We need to point the instance's klass to the new class we just created.
        instance.klass = klass
        # `donotcompile` and `allow_weakrefs` are set via decorators after construction, and we
        # need to persist these for consistency.
        klass.donotcompile = self.donotcompile
        klass.allow_weakrefs = self.allow_weakrefs
        return klass

    def isinstance(self, src: Value) -> bool:
        return src.klass.is_subclass_of(self)

    def is_subclass_of(self, src: Class) -> bool:
        if isinstance(src, UnionType):
            # This is an important subtlety - we want the subtyping relation to satisfy
            # self < A | B if either self < A or self < B. Requiring both wouldn't be correct,
            # as we want to allow assignments of A into A | B.
            return any(self.is_subclass_of(t) for t in src.type_args)
        return src.exact_type() in self.mro

    def _check_compatible_property_override(
        self, override: Value, inherited: Value
    ) -> bool:
        # Properties can be overridden by cached properties, and vice-versa.
        valid_sync_override = isinstance(
            override, (CachedPropertyMethod, PropertyMethod)
        ) and isinstance(inherited, (CachedPropertyMethod, PropertyMethod))

        valid_async_override = isinstance(
            override, (AsyncCachedPropertyMethod, PropertyMethod)
        ) and isinstance(inherited, (AsyncCachedPropertyMethod, PropertyMethod))

        return valid_sync_override or valid_async_override

    def check_incompatible_override(
        self, override: Value, inherited: Value, module: ModuleTable
    ) -> None:
        # TODO: There's more checking we should be doing to ensure
        # this is a compatible override
        if isinstance(override, TransparentDecoratedMethod):
            override = override.function

        if not inherited.can_override(override, self, module):
            raise TypedSyntaxError(f"class cannot hide inherited member: {inherited!r}")

    def finish_bind(self, module: ModuleTable, klass: Class | None) -> Optional[Value]:
        todo = set(self.members.keys())
        finished = set()

        while todo:
            name = todo.pop()
            my_value = self.members[name]
            new_value = self._finish_bind_one(name, my_value, module)
            if new_value is None:
                del self.members[name]
            else:
                self.members[name] = new_value
            finished.add(name)
            # account for the possibility that finish_bind of one member added new members
            todo.update(self.members.keys())
            todo.difference_update(finished)

        # These were just for error reporting here, don't need them anymore
        self._member_nodes = {}
        return self

    def _finish_bind_one(
        self, name: str, my_value: Value, module: ModuleTable
    ) -> Value | None:
        node = self.inexact_type()._member_nodes.get(name, None)
        with module.error_context(node):
            new_value = my_value.finish_bind(module, self)
            if new_value is None:
                return None
            my_value = new_value

            for base in self.mro[1:]:
                value = base.members.get(name)
                if value is not None:
                    self.check_incompatible_override(my_value, value, module)

                if isinstance(value, Slot):
                    # use the base class slot
                    return None

        return my_value

    def define_slot(
        self,
        name: str,
        node: AST,
        type_ref: Optional[TypeRef] = None,
        assignment: Optional[AST] = None,
        declared_on_class: bool = False,
    ) -> None:
        existing = self.members.get(name)
        if existing is None:
            self._member_nodes[name] = node
            self.members[name] = Slot(
                type_ref,
                name,
                self,
                assignment,
                declared_on_class=declared_on_class,
            )
        elif isinstance(existing, Slot):
            if not existing.type_ref:
                existing.type_ref = type_ref
                self._member_nodes[name] = node
            elif type_ref:
                raise TypedSyntaxError(
                    f"Cannot re-declare member '{name}' in '{self.instance.name}'"
                )
            existing.update(assignment, declared_on_class)
        else:
            raise TypedSyntaxError(
                f"slot conflicts with other member {name} in {self.name}"
            )

    def declare_function(self, func: Function) -> None:
        existing = self.members.get(func.func_name)
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
                InitVisitor(func, self, node).visit(node.body)

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

    def emit_type_check(self, src: Class, code_gen: Static38CodeGenerator) -> None:
        if src is self.type_env.dynamic:
            code_gen.emit("CAST", self.type_descr)
        else:
            assert self.can_assign_from(src)

    def emit_extra_members(
        self, node: ClassDef, code_gen: Static38CodeGenerator
    ) -> None:
        pass


class BuiltinObject(Class):
    def __init__(
        self,
        type_name: TypeName,
        type_env: TypeEnvironment,
        bases: Optional[List[Class]] = None,
        instance: Optional[Value] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        is_exact: bool = False,
        is_final: bool = False,
        pytype: Optional[Type[object]] = None,
    ) -> None:
        super().__init__(
            type_name,
            type_env,
            bases,
            instance,
            klass,
            members,
            is_exact,
            is_final=is_final,
        )
        self.dynamic_builtinmethod_dispatch = True

    def emit_type_check(self, src: Class, code_gen: Static38CodeGenerator) -> None:
        assert self.can_assign_from(src)


class Variance(Enum):
    INVARIANT = 0
    COVARIANT = 1
    CONTRAVARIANT = 2


class GenericClass(Class):
    type_name: GenericTypeName
    is_variadic = False

    def __init__(
        self,
        type_name: GenericTypeName,
        type_env: TypeEnvironment,
        bases: Optional[List[Class]] = None,
        instance: Optional[Object[Class]] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        type_def: Optional[GenericClass] = None,
        is_exact: bool = False,
        pytype: Optional[Type[object]] = None,
        is_final: bool = False,
    ) -> None:
        super().__init__(
            type_name,
            type_env,
            bases,
            instance,
            klass,
            members,
            is_exact,
            pytype,
            is_final,
        )
        self.gen_name = type_name
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

        if isinstance(slice, ast.Slice):
            visitor.syntax_error("can't slice generic types", node)
            return visitor.type_env.DYNAMIC

        expected_argnum = len(self.gen_name.args)
        if isinstance(slice, ast.Tuple):
            multiple: List[Class] = []
            for elt in slice.elts:
                klass = visitor.resolve_annotation(elt) or self.type_env.dynamic
                multiple.append(klass)

            index = tuple(multiple)
            actual_argnum = len(slice.elts)
        else:
            actual_argnum = 1
            single = visitor.resolve_annotation(slice) or self.type_env.dynamic
            index = (single,)

        if (not self.is_variadic) and actual_argnum != expected_argnum:
            visitor.syntax_error(
                f"incorrect number of generic arguments for {self.instance.name}, "
                f"expected {expected_argnum}, got {actual_argnum}",
                node,
            )

        return visitor.type_env.get_generic_type(self, index)

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
        src_type_def = src.generic_type_def

        if (
            src_type_def is None
            or type_def is None
            or not type_def.is_subclass_of(src_type_def)
        ):
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
    ) -> Class:
        type_name = GenericTypeName(self.type_name.module, self.type_name.name, index)
        generic_bases: List[Optional[Class]] = [
            (
                self.type_env.get_generic_type(base, index)
                if isinstance(base, GenericClass) and base.contains_generic_parameters
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
            self.type_env,
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

            return type_env.get_generic_type(self, bind_args)

        return self


class GenericParameter(Class):
    def __init__(
        self,
        name: str,
        index: int,
        type_env: TypeEnvironment,
        variance: Variance = Variance.INVARIANT,
    ) -> None:
        super().__init__(
            TypeName("", name), type_env, [], None, None, {}, is_exact=True
        )
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

    def __init__(
        self,
        type_name: TypeName,
        type_env: TypeEnvironment,
        bases: Optional[List[Class]] = None,
        instance: Optional[CInstance[Class]] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        is_exact: bool = True,
        pytype: Optional[Type[object]] = None,
    ) -> None:
        super().__init__(
            type_name,
            type_env,
            bases or [],
            instance,
            klass,
            members,
            is_exact,
            pytype,
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

    def emit_type_check(self, src: Class, code_gen: Static38CodeGenerator) -> None:
        assert self.can_assign_from(src)


class DynamicClass(Class):
    instance: DynamicInstance

    def __init__(self, type_env: TypeEnvironment) -> None:
        super().__init__(
            # any references to dynamic at runtime are object
            TypeName("builtins", "object"),
            type_env,
            instance=DynamicInstance(self),
            is_exact=True,
        )

    @property
    def qualname(self) -> str:
        return "dynamic"

    @property
    def instance_name_with_exact(self) -> str:
        return "dynamic"

    def can_assign_from(self, src: Class) -> bool:
        # No automatic boxing to the dynamic type
        # disallow assigning non read only to read only
        return not isinstance(src, CType)

    def emit_type_check(self, src: Class, code_gen: Static38CodeGenerator) -> None:
        assert self.can_assign_from(src)

    @property
    def type_descr(self) -> TypeDescr:
        # `dynamic` is an exact type - it appears in MROs, so we want to avoid an exact/inexact
        # version of dynamic from co-existing. However, dynamic is compatible with every type.
        # We special case the type descr to avoid the exactness tag ("!") to ensure that thunks
        # type check against the `(builtins, object)` descr instead of the exact one.
        return ("builtins", "object")


class DynamicInstance(Object[DynamicClass]):
    def __init__(self, klass: DynamicClass) -> None:
        super().__init__(klass)

    def emit_binop(self, node: ast.BinOp, code_gen: Static38CodeGenerator) -> None:
        if maybe_emit_sequence_repeat(node, code_gen):
            return
        code_gen.defaultVisit(node)


class NoneType(Class):
    def __init__(self, type_env: TypeEnvironment) -> None:
        super().__init__(
            TypeName("builtins", "None"),
            type_env,
            [type_env.object],
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
        if node.attr in visitor.type_env.object.members:
            return super().bind_attr(node, visitor, type_ctx)
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
        visitor.set_type(node, visitor.type_env.bool.instance)

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
    bases = list(map(lambda base: base.exact_type(), C.bases))
    return _merge([[C.exact_type()]] + list(map(_mro, bases)) + [bases])


class ParamStyle(Enum):
    NORMAL = 0
    POSONLY = 1
    KWONLY = 2


class Parameter:
    def __init__(
        self,
        name: str,
        idx: int,
        type_ref: TypeRef,
        has_default: bool,
        default_val: object,
        style: ParamStyle,
    ) -> None:
        self.name = name
        self.type_ref = type_ref
        self.index = idx
        self.has_default = has_default
        self.default_val = default_val
        self.style = style

    @property
    def is_kwonly(self) -> bool:
        return self.style == ParamStyle.KWONLY

    @property
    def is_posonly(self) -> bool:
        return self.style == ParamStyle.POSONLY

    def __repr__(self) -> str:
        return (
            f"<Parameter name={self.name}, ref={self.type_ref}, "
            f"index={self.index}, has_default={self.has_default}, "
            f"style={self.style}>"
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
                self.style,
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
        visitor: TypeBinder,
        self_arg: Optional[ast.expr],
        args_override: Optional[List[ast.expr]] = None,
        descr_override: Optional[TypeDescr] = None,
    ) -> None:
        self.callable = callable
        self.call = call
        self.visitor = visitor
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
                            arg.value,
                            visitor.type_env.DYNAMIC,
                            "starred expression cannot be primitive",
                        )
                    else:
                        visitor.visitExpectedType(
                            arg,
                            visitor.type_env.DYNAMIC,
                            CALL_ARGUMENT_CANNOT_BE_PRIMITIVE,
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

    def _bind_default_value(self, param: Parameter, visitor: TypeBinder) -> None:
        if isinstance(param.default_val, expr):
            # We'll force these to normal calls in can_call_self, we'll add
            # an emitter which makes sure we never try and do code gen for this
            self.emitters.append(UnreachableArg())
        else:
            const = ast.Constant(param.default_val)
            copy_location(const, self.call)
            visitor.visit(const, param.type_ref.resolved(False).instance)
            self.emitters.append(DefaultArg(const))

    def bind_kwargs(self, visitor: TypeBinder) -> None:
        func_args = self.callable.args
        assert func_args is not None

        spill_start = len(self.emitters)
        # Process unhandled arguments which can be populated via defaults,
        # keyword arguments, or **mapping.
        cur_kw_arg = 0
        for idx in range(self.nseen, len(func_args)):
            param = func_args[idx]
            if param.is_posonly:
                if param.has_default:
                    self._bind_default_value(param, visitor)
                    self.nseen += 1
                    continue
                visitor.syntax_error(
                    f"Missing value for positional-only arg {idx}", self.call
                )
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
                    self._bind_default_value(param, visitor)
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
                    self.visitor.perf_warning(
                        "Multiple *args prevents more efficient static call", self.call
                    )
                    return False
                has_star_args = True
            elif has_star_args:
                # We don't support f(*a, b)
                self.visitor.perf_warning(
                    "Positional arg after *args prevents more efficient static call",
                    self.call,
                )
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
        if (num_dstar_args + num_star_args) > 1:
            # We don't support f(**a, **b)
            self.visitor.perf_warning(
                "Multiple **kwargs prevents more efficient static call", self.call
            )
            return False
        elif has_default_args and has_star_args:
            # We don't support f(1, 2, *a) iff has any default arg values
            self.visitor.perf_warning(
                "Passing *args to function with default values prevents more efficient static call",
                self.call,
            )
            return False
        elif num_kwonly:
            self.visitor.perf_warning(
                "Keyword-only args in called function prevents more efficient static call",
                self.call,
            )
            return False

        return True

    def emit(self, code_gen: Static38CodeGenerator, extra_self: bool = False) -> None:
        if self.dynamic_call:
            code_gen.defaultVisit(self.call)
            return

        code_gen.set_lineno(self.call)

        for emitter in self.emitters:
            emitter.emit(self.call, code_gen)

        func_args = self.callable.args
        assert func_args is not None

        if self.needs_virtual_invoke(code_gen):
            self.visitor.perf_warning(
                f"Method {self.callable.func_name} can be overridden. "
                "Make method or class final for more efficient call",
                self.call,
            )
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
        visitor: TypeBinder,
        self_arg: Optional[ast.expr] = None,
        args_override: Optional[List[ast.expr]] = None,
        is_instance_call: bool = False,
    ) -> None:
        super().__init__(callable, call, visitor, self_arg, args_override)
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

        code_gen.set_lineno(self.call)

        for emitter in self.emitters:
            emitter.emit(self.call, code_gen)

        func_args = self.callable.args
        assert func_args is not None

        if self.needs_virtual_invoke(code_gen):
            self.visitor.perf_warning(
                f"Method {self.callable.func_name} can be overridden. "
                "Make method or class final for more efficient call",
                self.call,
            )
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

        self.type.emit_type_check(arg_type.klass, code_gen)

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
                and param.type_ref.resolved() is not code_gen.compiler.type_env.DYNAMIC
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
        self.type.emit_type_check(code_gen.compiler.type_env.dynamic, code_gen)

    def __repr__(self) -> str:
        return f"SpilledKeywordArg({self.temporary})"


class KeywordArg(ArgEmitter):
    def __init__(self, argument: expr, type: Class) -> None:
        self.argument = argument
        self.type = type

    def emit(self, node: Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(self.argument)
        self.type.emit_type_check(code_gen.get_type(self.argument).klass, code_gen)


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
        param_type = (
            self.param.type_ref.resolved() or code_gen.compiler.type_env.dynamic
        )
        param_type.emit_type_check(code_gen.compiler.type_env.dynamic, code_gen)


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
                decorator, visitor.type_env.DYNAMIC, "decorator cannot be a primitive"
            )

        self.bind_function_self(node, scope, visitor)
        visitor._visitParameters(node.args, scope)

        returns = node.returns
        if returns:
            visitor.visitExpectedType(
                returns,
                visitor.type_env.DYNAMIC,
                "return annotation cannot be a primitive",
            )

        self.bind_function_inner(node, visitor)

        visitor.scopes.append(scope)

        terminates = visitor.visit_check_terminal(self.get_function_body())

        typ = visitor.get_type(node)
        if not terminates and not isinstance(
            typ, (TransientDecoratedMethod, NativeDecoratedFunction)
        ):
            expected = self.get_expected_return()
            if not expected.klass.can_assign_from(visitor.type_env.none):
                raise TypedSyntaxError(
                    f"Function has declared return type '{expected.name}' "
                    "but can implicitly return None."
                )

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
            self_type = visitor.type_env.DYNAMIC
            if node.name in ("__new__", "__init_subclass__"):
                # __new__ is special and isn't a normal method, so we expect a
                # type for cls
                self_type = visitor.type_env.type.instance
            else:
                klass = visitor.maybe_get_current_class()
                if klass is not None:
                    # Since methods can be called by subclasses, take some care to ensure self
                    # is always inexact.
                    self_type = klass.inexact_type().instance

            visitor.set_param(node.args.args[0], self_type, scope)

    def emit_function(
        self,
        node: ast.FunctionDef | ast.AsyncFunctionDef,
        code_gen: Static38CodeGenerator,
    ) -> str:
        raise NotImplementedError()

    def emit_function_with_decorators(
        self,
        func: Function,
        node: ast.FunctionDef | ast.AsyncFunctionDef,
        code_gen: Static38CodeGenerator,
    ) -> str:
        if node.decorator_list:
            for decorator in node.decorator_list:
                code_gen.visit(decorator)
            first_lineno = node.decorator_list[0].lineno
        else:
            first_lineno = node.lineno

        func.emit_function_body(node, code_gen, first_lineno, func.get_function_body())

        for _ in range(len(node.decorator_list)):
            code_gen.emit("CALL_FUNCTION", 1)

        return node.name

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

    def get_expected_return(self) -> Value:
        func_returns = self.return_type.resolved()
        if isinstance(func_returns, AwaitableType):
            func_returns = func_returns.type_args[0]
        return func_returns.instance

    @property
    def return_type(self) -> TypeRef:
        raise NotImplementedError("No return_type")


def resolve_assign_error_msg(
    dest: Class, src: Class, reason: str = "type mismatch: {} cannot be assigned to {}"
) -> str:
    if dest.inexact_type().can_assign_from(src):
        reason = reason.format(
            src.instance.name_with_exact, dest.instance.name_with_exact
        )
    else:
        reason = reason.format(src.instance.name, dest.instance.name)
    return reason


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

    def can_override(self, override: Value, klass: Class, module: ModuleTable) -> bool:
        if self.is_final:
            raise TypedSyntaxError(
                f"Cannot assign to a Final attribute of {klass.instance.name}:{self.name}"
            )

        if self.func_name not in NON_VIRTUAL_METHODS:
            if isinstance(override, TransparentDecoratedMethod):
                func = override.real_function
            else:
                assert isinstance(override, Function)
                func = override
            self.validate_compat_signature(func, module)

            return True

        if isinstance(override, Function):
            return True

        return super().can_override(override, klass, module)

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
        self.container_type = klass.inexact_type() if klass is not None else klass

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
                f"Returned type `{override_ret_type.instance_name}` "
                "is not a subtype "
                f"of the overridden return `{ret_type.instance_name}`",
                override.node,
            )

        args = self.args
        if args is None:
            # Untyped builtin method; cannot validate signature compatibility.
            return

        override_args = override.args
        if self.has_kwarg:
            # if we accept **kwargs, the overridden method can define any kwarg it wants
            # and it remains a valid override
            arg_names = set(arg.name for arg in args)
            override_args = [
                p for p in override_args if not p.has_default or p.name in arg_names
            ]

        if len(args) != len(override_args):
            module.syntax_error(
                f"{override.qualname} overrides {self.qualname} inconsistently. "
                "Number of arguments differ",
                override.node,
            )

        start_arg = 1 if first_arg_is_implicit else 0
        for arg, override_arg in zip(args[start_arg:], override.args[start_arg:]):
            if not arg.is_posonly and arg.name != override_arg.name:
                if arg.is_kwonly:
                    arg_desc = f"Keyword only argument `{arg.name}`"
                else:
                    arg_desc = f"Positional argument {arg.index + 1} named `{arg.name}`"

                module.syntax_error(
                    f"{override.qualname} overrides {self.qualname} inconsistently. "
                    f"{arg_desc} is overridden as `{override_arg.name}`",
                    override.node,
                )

            if override_arg.is_posonly and not arg.is_posonly:
                module.syntax_error(
                    f"{override.qualname} overrides {self.qualname} inconsistently. "
                    f"`{override_arg.name}` is positional-only in override, not in base",
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
                reason = (
                    f"{override.qualname} overrides {self.qualname} inconsistently. "
                    + f"Parameter {arg.name}"
                    + " of type `{1}` is not a supertype of the overridden parameter `{0}`"
                )
                reason = resolve_assign_error_msg(override_type, arg_type, reason)
                module.syntax_error(reason, override.node)

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

    def map_call(
        self,
        node: ast.Call,
        visitor: TypeBinder,
        self_expr: Optional[ast.expr] = None,
        args_override: Optional[List[ast.expr]] = None,
        descr_override: Optional[TypeDescr] = None,
    ) -> Tuple[ArgMapping, Value]:
        arg_mapping = ArgMapping(
            self,
            node,
            visitor,
            self_expr,
            args_override,
            descr_override=descr_override,
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
        type_env: TypeEnvironment,
        type_name: Optional[GenericTypeName] = None,
        type_def: Optional[GenericClass] = None,
        is_exact: bool = False,
    ) -> None:
        super().__init__(
            type_name
            or GenericTypeName(
                "static",
                "InferredAwaitable",
                (GenericParameter("T", 0, type_env, Variance.COVARIANT),),
            ),
            type_env,
            instance=AwaitableInstance(self),
            type_def=type_def,
            is_exact=is_exact,
        )

    def _create_exact_type(self) -> Class:
        return type(self)(
            self.type_env,
            self.type_name,
            self.type_def,
            is_exact=True,
        )

    @property
    def type_descr(self) -> TypeDescr:
        # This is not a real type, so we should not emit it.
        raise NotImplementedError("Awaitables shouldn't have a type descr")

    def make_generic_type(self, index: Tuple[Class, ...]) -> Class:
        assert len(index) == 1
        type_name = GenericTypeName(self.type_name.module, self.type_name.name, index)
        return AwaitableType(self.type_env, type_name, type_def=self)


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
        return self.compiler.type_env.get_generic_type(
            self.compiler.type_env.awaitable, (res,)
        )

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
            return self.func.klass.type_env.dynamic
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
            module.compiler.type_env.function,
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

    def finish_bind(
        self, module: ModuleTable, klass: Class | None
    ) -> Function | DecoratedMethod | None:
        res: Function | DecoratedMethod = self
        for decorator in reversed(self.node.decorator_list):
            decorator_type = (
                module.resolve_decorator(decorator) or self.klass.type_env.dynamic
            )
            new = decorator_type.resolve_decorate_function(res, decorator)
            if new and new is not res:
                new = new.finish_bind(module, klass)
            if new is None:
                # With an un-analyzable decorator we want to force late binding
                # to it because we don't know what the decorator does
                module.types[self.node] = UnknownDecoratedMethod(self)
                return None
            res = new

        module.types[self.node] = res
        return res

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
                arg_replacements[name] = arg.expr
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
        visitor: GenericVisitor[object],
    ) -> Optional[Value]:
        if inst is None:
            return self
        else:
            return MethodType(ctx.type_name, self.node, node.value, self)

    def register_arg(
        self,
        name: str,
        idx: int,
        ref: TypeRef,
        has_default: bool,
        default_val: object,
        style: ParamStyle,
    ) -> None:
        parameter = Parameter(name, idx, ref, has_default, default_val, style)
        self.args.append(parameter)
        self.args_by_name[name] = parameter
        if not has_default:
            self.num_required_args += 1

    def process_arg(
        self: Function,
        module: ModuleTable,
        idx: int,
        argument: ast.arg,
        default: ast.expr,
        style: ParamStyle,
    ) -> None:
        annotation = argument.annotation
        default_val = None
        has_default = False
        if default is not None:
            has_default = True
            default_val = get_default_value(default)

        if annotation:
            ref = TypeRef(module, annotation)
        elif idx == 0:
            if self.node.name in ("__new__", "__init_subclass__"):
                ref = ResolvedTypeRef(self.klass.type_env.type)
            else:
                ref = ContainerTypeRef(self)
        else:
            ref = ResolvedTypeRef(self.klass.type_env.dynamic)

        self.register_arg(argument.arg, idx, ref, has_default, default_val, style)

    def process_args(self: Function, module: ModuleTable) -> None:
        """
        Register type-refs for each function argument, assume type_env.DYNAMIC if annotation is missing.
        """
        arguments = self.node.args
        nposonly = len(arguments.posonlyargs)
        nrequired = nposonly + len(arguments.args) - len(arguments.defaults)
        posargs = arguments.posonlyargs + arguments.args
        no_defaults = cast(List[Optional[ast.expr]], [None] * nrequired)
        defaults = no_defaults + cast(List[Optional[ast.expr]], arguments.defaults)

        idx = 0
        for idx, (argument, default) in enumerate(zip(posargs, defaults)):
            style = ParamStyle.POSONLY if idx < nposonly else ParamStyle.NORMAL
            self.process_arg(module, idx, argument, default, style)

        base_idx = idx

        vararg = arguments.vararg
        if vararg:
            base_idx += 1
            self.has_vararg = True

        for argument, default in zip(arguments.kwonlyargs, arguments.kw_defaults):
            base_idx += 1
            self.process_arg(module, base_idx, argument, default, ParamStyle.KWONLY)

        kwarg = arguments.kwarg
        if kwarg:
            self.has_kwarg = True

    def __repr__(self) -> str:
        return f"<{self.name} '{self.name}' instance, args={self.args}>"


class UnknownDecoratedMethod(FunctionContainer):
    """Wrapper around functions where we are unable to analyze the effect
    of the decorators"""

    def __init__(self, func: Function) -> None:
        super().__init__(func.klass.type_env.dynamic)
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
            visitor.set_param(node.args.args[0], visitor.type_env.DYNAMIC, scope)

    def emit_function(
        self,
        node: ast.FunctionDef | ast.AsyncFunctionDef,
        code_gen: Static38CodeGenerator,
    ) -> str:
        return self.emit_function_with_decorators(self.func, node, code_gen)

    @property
    def return_type(self) -> TypeRef:
        if isinstance(self.func.node, AsyncFunctionDef):
            return AwaitableTypeRef(
                ResolvedTypeRef(self.klass.type_env.dynamic),
                self.func.module.compiler,
            )
        return ResolvedTypeRef(self.klass.type_env.dynamic)


class MethodType(Object[Class]):
    def __init__(
        self,
        bound_type_name: TypeName,
        node: Union[AsyncFunctionDef, FunctionDef],
        target: ast.expr,
        function: Function,
    ) -> None:
        super().__init__(function.klass.type_env.method)
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
        result = self.function.bind_call_self(node, visitor, type_ctx, self.target)
        return result

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if self.function.func_name in NON_VIRTUAL_METHODS:
            return super().emit_call(node, code_gen)

        code_gen.set_lineno(node)

        self.function.emit_call_self(node, code_gen, self.target)


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
        target: ast.expr,
    ) -> None:
        super().__init__(function.klass.type_env.static_method)
        self.function = function
        self.target = target

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        arg_mapping = ArgMapping(self.function, node, visitor, self.target, node.args)
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
            code_gen.visit(self.target)

        arg_mapping.emit(code_gen, extra_self=True)


class DecoratedMethod(FunctionContainer):
    def __init__(
        self, klass: Class, function: Function | DecoratedMethod, decorator: expr
    ) -> None:
        super().__init__(klass)
        self.function = function
        self.decorator = decorator

    def finish_bind(self, module: ModuleTable, klass: Class | None) -> DecoratedMethod:
        # This override exists only for typing purposes.
        return self

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

    @property
    def node(self) -> Union[FunctionDef, AsyncFunctionDef]:
        return self.real_function.node

    @property
    def container_type(self) -> Optional[Class]:
        return self.real_function.container_type

    def set_container_type(self, container_type: Optional[Class]) -> None:
        self.function.set_container_type(container_type)


class TransparentDecoratedMethod(DecoratedMethod):
    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        return self.function.bind_call(node, visitor, type_ctx)

    def can_override(self, override: Value, klass: Class, module: ModuleTable) -> bool:
        return self.function.can_override(override, klass, module)

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
        visitor: GenericVisitor[object],
    ) -> Optional[Value]:
        return self.function.resolve_descr_get(node, inst, ctx, visitor)

    def resolve_attr(
        self, node: ast.Attribute, visitor: GenericVisitor[object]
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
        super().__init__(function.klass.type_env.static_method, function, decorator)

    def can_override(self, override: Value, klass: Class, module: ModuleTable) -> bool:
        if self.is_final:
            raise TypedSyntaxError(
                f"Cannot assign to a Final attribute of {klass.instance.name}:{self.real_function.qualname}"
            )
        assert isinstance(override, DecoratedMethod)
        self.real_function.validate_compat_signature(
            override.real_function, module, first_arg_is_implicit=False
        )
        return True

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
        visitor: GenericVisitor[object],
    ) -> Optional[Value]:
        if inst is None:
            return self.function
        else:
            # Using .real_function here might not be adequate when we start getting more
            # complex signature changing decorators
            return StaticMethodInstanceBound(self.real_function, node.value)


class BoundClassMethod(Object[Class]):
    def __init__(
        self,
        function: Function,
        klass: Class,
        self_expr: ast.expr,
        is_instance_call: bool,
    ) -> None:
        super().__init__(klass.type_env.class_method)
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
            visitor,
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
        super().__init__(function.klass.type_env.class_method, function, decorator)

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
                visitor.set_param(node.args.args[0], klass.inexact_type(), scope)
            else:
                visitor.set_param(node.args.args[0], visitor.type_env.DYNAMIC, scope)

    def resolve_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: GenericVisitor[object],
    ) -> Optional[Value]:
        return BoundClassMethod(
            self.real_function, ctx, node.value, is_instance_call=inst is not None
        )


class GetSetDescriptor(Object[Class]):
    pass


class ClassGetSetDescriptor(GetSetDescriptor):
    def resolve_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: GenericVisitor[object],
    ) -> Optional[Value]:
        if inst is not None:
            return inst.klass
        else:
            return self


class TypeDunderNameGetSetDescriptor(GetSetDescriptor):
    def __init__(self, klass: Class, type_env: TypeEnvironment) -> None:
        super().__init__(klass)
        self.type_env = type_env

    def resolve_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: GenericVisitor[object],
    ) -> Optional[Value]:
        return self.type_env.str.instance


class PropertyMethod(DecoratedMethod):
    def __init__(
        self,
        function: Function | DecoratedMethod,
        decorator: expr,
        property_type: Optional[Class] = None,
    ) -> None:
        super().__init__(
            property_type or function.klass.type_env.property, function, decorator
        )

    def can_override(self, override: Value, klass: Class, module: ModuleTable) -> bool:
        if self.is_final:
            raise TypedSyntaxError(
                f"Cannot assign to a Final attribute of {klass.instance.name}:{self.name}"
            )

        if isinstance(override, PropertyMethod):
            self.real_function.validate_compat_signature(override.real_function, module)
            return True
        elif isinstance(override, Slot):
            it = self.return_type
            ot = override.type_ref
            if ot and it and ot.resolved(True) != (itr := it.resolved(True)):
                raise TypedSyntaxError(
                    f"Cannot change type of inherited attribute (inherited type '{itr.instance.name}')"
                )

            return True

        return super().can_override(override, klass, module)

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
        visitor: GenericVisitor[object],
    ) -> Optional[Value]:
        if inst is None:
            return self.klass.type_env.dynamic
        else:
            return self.function.return_type.resolved().instance

    def emit_load_attr_from(
        self, node: Attribute, code_gen: Static38CodeGenerator, klass: Class
    ) -> None:
        if self.function.is_final or klass.is_final or klass.is_exact:
            code_gen.emit("EXTENDED_ARG", 0)
            code_gen.emit("INVOKE_FUNCTION", (self.getter_type_descr, 1))
        else:
            code_gen.perf_warning(
                f"Getter for property {node.attr} can be overridden. Make "
                "method or class final for more efficient property load",
                node,
            )
            code_gen.emit_invoke_method(self.getter_type_descr, 0)

    def emit_store_attr_to(
        self, node: Attribute, code_gen: Static38CodeGenerator, klass: Class
    ) -> None:
        code_gen.emit("ROT_TWO")
        if self.function.is_final or klass.is_final:
            code_gen.emit("EXTENDED_ARG", 0)
            code_gen.emit("INVOKE_FUNCTION", (self.setter_type_descr, 2))
        else:
            code_gen.perf_warning(
                f"Setter for property {node.attr} can be overridden. Make "
                "method or class final for more efficient property store",
                node,
            )
            code_gen.emit_invoke_method(self.setter_type_descr, 1)
        code_gen.emit("POP_TOP")

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

    def resolve_attr(
        self, node: ast.Attribute, visitor: GenericVisitor[object]
    ) -> Optional[Value]:
        if node.attr == "setter":
            return PropertySetterDecorator(
                TypeName("builtins", "property"), self.real_function.klass.type_env
            )
        return super().resolve_attr(node, visitor)


class CachedPropertyMethod(PropertyMethod):
    def __init__(self, function: Function | DecoratedMethod, decorator: expr) -> None:
        super().__init__(
            function,
            decorator,
            property_type=function.klass.type_env.cached_property,
        )

    def replace_function(self, func: Function) -> Function | DecoratedMethod:
        return CachedPropertyMethod(
            self.function.replace_function(func), self.decorator
        )

    def emit_load_attr_from(
        self, node: Attribute, code_gen: Static38CodeGenerator, klass: Class
    ) -> None:
        code_gen.emit_invoke_method(self.getter_type_descr, 0)

    def emit_store_attr_to(
        self, node: Attribute, code_gen: Static38CodeGenerator, klass: Class
    ) -> None:
        code_gen.emit("ROT_TWO")
        code_gen.emit_invoke_method(self.setter_type_descr, 1)
        code_gen.emit("POP_TOP")

    def emit_function(
        self,
        node: ast.FunctionDef | ast.AsyncFunctionDef,
        code_gen: Static38CodeGenerator,
    ) -> str:
        self.function.emit_function_body(
            node, code_gen, self.decorator.lineno, self.get_function_body()
        )

        return CACHED_PROPERTY_IMPL_PREFIX + node.name

    def resolve_attr(
        self, node: ast.Attribute, visitor: GenericVisitor[object]
    ) -> Optional[Value]:
        if node.attr == "setter":
            visitor.syntax_error(
                f"cached_property {self.name} does not support setters",
                node,
            )
        return super().resolve_attr(node, visitor)


class AsyncCachedPropertyMethod(PropertyMethod):
    def __init__(self, function: Function | DecoratedMethod, decorator: expr) -> None:
        super().__init__(
            function,
            decorator,
            property_type=function.klass.type_env.async_cached_property,
        )

    def replace_function(self, func: Function) -> Function | DecoratedMethod:
        return AsyncCachedPropertyMethod(
            self.function.replace_function(func), self.decorator
        )

    def emit_load_attr_from(
        self, node: Attribute, code_gen: Static38CodeGenerator, klass: Class
    ) -> None:
        code_gen.emit_invoke_method(self.getter_type_descr, 0)

    def emit_store_attr_to(
        self, node: Attribute, code_gen: Static38CodeGenerator, klass: Class
    ) -> None:
        code_gen.emit("ROT_TWO")
        code_gen.emit_invoke_method(self.setter_type_descr, 1)
        code_gen.emit("POP_TOP")

    def emit_function(
        self,
        node: ast.FunctionDef | ast.AsyncFunctionDef,
        code_gen: Static38CodeGenerator,
    ) -> str:
        self.function.emit_function_body(
            node, code_gen, self.decorator.lineno, self.get_function_body()
        )

        return ASYNC_CACHED_PROPERTY_IMPL_PREFIX + node.name

    def resolve_attr(
        self, node: ast.Attribute, visitor: GenericVisitor[object]
    ) -> Optional[Value]:
        if node.attr == "setter":
            visitor.syntax_error(
                f"async_cached_property {self.name} does not support setters",
                node,
            )
        return super().resolve_attr(node, visitor)


class TypingFinalDecorator(Class):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        if isinstance(fn, DecoratedMethod):
            fn.real_function.is_final = True
        else:
            fn.is_final = True
        return TransparentDecoratedMethod(self.type_env.function, fn, decorator)

    def resolve_decorate_class(
        self,
        klass: Class,
        decorator: expr,
        visitor: DeclarationVisitor,
    ) -> Class:
        klass.is_final = True
        return klass


class AllowWeakrefsDecorator(Class):
    def resolve_decorate_class(
        self,
        klass: Class,
        decorator: expr,
        visitor: DeclarationVisitor,
    ) -> Class:
        klass.allow_weakrefs = True
        return klass


class ClassMethodDecorator(Class):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        if fn.klass is self.type_env.function:
            func = fn.real_function if isinstance(fn, DecoratedMethod) else fn
            args = func.args
            if args:
                klass = func.container_type
                if klass is not None:
                    args[0].type_ref = ResolvedTypeRef(self.type_env.type)
                else:
                    args[0].type_ref = ResolvedTypeRef(self.type_env.dynamic)
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
        return TransparentDecoratedMethod(self.type_env.function, fn, decorator)

    def _set_dynamic_return_type(self, fn: Function) -> None:
        dynamic_typeref = ResolvedTypeRef(self.type_env.dynamic)
        if isinstance(fn.node, AsyncFunctionDef):
            fn.return_type = AwaitableTypeRef(dynamic_typeref, fn.module.compiler)
        else:
            fn.return_type = dynamic_typeref


class StaticMethodDecorator(Class):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        if fn.klass is not self.type_env.function:
            return None

        func = fn.real_function if isinstance(fn, DecoratedMethod) else fn
        args = func.args
        if args:
            if not func.node.args.args[0].annotation:
                func.args[0].type_ref = ResolvedTypeRef(self.type_env.dynamic)
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
        return TransparentDecoratedMethod(self.type_env.function, fn, decorator)


class DoNotCompileDecorator(Class):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        real_fn = fn.real_function if isinstance(fn, DecoratedMethod) else fn
        real_fn.donotcompile = True
        return TransparentDecoratedMethod(self.type_env.function, fn, decorator)

    def resolve_decorate_class(
        self,
        klass: Class,
        decorator: expr,
        visitor: DeclarationVisitor,
    ) -> Class:
        klass.donotcompile = True
        return klass


class NativeDecoratedFunction(Function):
    def finish_bind(
        self, module: ModuleTable, klass: Class | None
    ) -> Function | DecoratedMethod | None:
        module.types[self.node] = self
        return self

    def emit_function(
        self,
        node: ast.FunctionDef | ast.AsyncFunctionDef,
        code_gen: Static38CodeGenerator,
    ) -> str:
        # This is only different from the parent method in that it allows
        # decorators to exist on the `node`.
        first_lineno = node.lineno
        self.emit_function_body(node, code_gen, first_lineno, node.body)
        return node.name

    def emit_function_body(
        self,
        node: ast.FunctionDef | ast.AsyncFunctionDef,
        code_gen: Static38CodeGenerator,
        first_lineno: int,
        body: List[ast.stmt],
    ) -> CodeGenerator:

        gen = code_gen.make_func_codegen(node, node.args, node.name, first_lineno)
        callable_name = node.name

        # This is equivalent to:
        #
        #   raise RuntimeError("native callable ...")
        gen.emit("LOAD_GLOBAL", "RuntimeError")
        gen.emit(
            "LOAD_CONST",
            f"native callable '{callable_name}' can only be called from static modules",
        )
        gen.emit("CALL_FUNCTION", 1)
        gen.emit("RAISE_VARARGS", 1)
        gen.emit("LOAD_CONST", None)
        gen.emit("RETURN_VALUE")

        gen.finishFunction()

        code_gen.build_function(node, gen)
        return gen

    def bind_function_inner(
        self, node: Union[FunctionDef, AsyncFunctionDef], visitor: TypeBinder
    ) -> None:
        if isinstance(self.container_type, Class):
            visitor.syntax_error("Cannot decorate a method with @native", node)

        # Ensure the function has no statements in the body
        bad_statement = None
        for statement in node.body:
            if not isinstance(statement, ast.Pass):
                bad_statement = statement
                break
        if bad_statement is not None:
            visitor.syntax_error(
                "@native callables cannot contain a function body, only 'pass' is allowed",
                bad_statement,
            )

        # Ensure the function has only "normal" (not posonly) positional
        # args, and that all are primitives.
        args = node.args
        if args.posonlyargs:
            visitor.syntax_error(
                "@native callables cannot contain pos-only args",
                args.posonlyargs[0],
            )

        if args.kwonlyargs:
            visitor.syntax_error(
                "@native callables cannot contain kw-only args",
                args.kwonlyargs[0],
            )

        if args.defaults:
            visitor.syntax_error(
                "@native callables cannot contain kwargs",
                args.defaults[0],
            )

        positional_args = args.args

        for each in positional_args:
            arg_type = visitor.get_type(each)
            if not isinstance(arg_type, CIntInstance):
                visitor.syntax_error(
                    f"@native: expected a primitive arg for {each.arg}, not {arg_type.name}",
                    each,
                )

        return super().bind_function_inner(node, visitor)

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        decorator = code_gen.get_type(self.node.decorator_list[0])
        assert isinstance(decorator, NativeDecorator)

        for arg in node.args:
            code_gen.visit(arg)

        call_signature = []
        for arg in self.args:
            call_signature.append(arg.type_ref.resolved().type_descr)
        call_signature.append(self.return_type.resolved().type_descr)
        code_gen.emit(
            "INVOKE_NATIVE",
            ((decorator.lib_name, self.node.name), tuple(call_signature)),
        )


class NativeDecorator(Callable[Class]):
    def __init__(self, type_env: TypeEnvironment) -> None:
        params = [
            Parameter(
                "lib", 0, ResolvedTypeRef(type_env.str), True, None, ParamStyle.POSONLY
            ),
        ]
        super().__init__(
            type_env.function,
            "native",
            "__static__",
            params,
            {param.name: param for param in params},
            0,
            None,
            None,
            ResolvedTypeRef(type_env.dynamic),
        )
        self.type_env = type_env
        self.lib_name: Optional[str] = None

    def bind_call_self(
        self,
        node: ast.Call,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
        self_expr: Optional[ast.expr] = None,
    ) -> NarrowingEffect:
        result = super().bind_call_self(node, visitor, type_ctx, self_expr)

        lib_arg = self.args_by_name.get("lib")
        assert lib_arg is not None  # ensured by declaration visit

        lib_arg_node = node.args[0]

        if not isinstance(lib_arg_node, ast.Constant):
            lib_arg_final_node = visitor.get_final_literal(lib_arg_node)
            if lib_arg_final_node is None:
                visitor.syntax_error(
                    f"@native decorator 'lib' argument must be a `str`",
                    lib_arg_node,
                )
            lib_arg_node = lib_arg_final_node

        assert isinstance(lib_arg_node, ast.Constant)
        value = lib_arg_node.value
        if not isinstance(value, str):
            visitor.syntax_error(
                f"@native decorator 'lib' argument must be a `str` not {type(value)}",
                lib_arg_node,
            )

        self.lib_name = value
        visitor.set_type(node, self)
        return NO_EFFECT

    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> NativeDecoratedFunction:
        if not isinstance(decorator, ast.Call):
            raise TypedSyntaxError(
                "@native decorator must specify the library to be loaded"
            )

        if decorator.keywords:
            raise TypedSyntaxError("@native decorator takes no keyword arguments")

        args = decorator.args
        if len(args) != 1:
            raise TypedSyntaxError(
                "@native decorator accepts a single parameter, the path to .so file"
            )

        if isinstance(args[0], ast.Starred):
            raise TypedSyntaxError("@native decorator takes no starred arguments")

        if len(fn.node.decorator_list) != 1:
            raise TypedSyntaxError(
                "@native decorator cannot be used with other decorators"
            )

        if isinstance(fn.node, ast.AsyncFunctionDef):
            raise TypedSyntaxError(
                "@native decorator cannot be used on async functions"
            )

        assert not isinstance(fn, DecoratedMethod)

        native_fn = NativeDecoratedFunction(fn.node, fn.module, fn.return_type)
        native_fn.set_container_type(fn.container_type)
        return native_fn

    def resolve_decorate_class(
        self,
        klass: Class,
        decorator: expr,
        visitor: DeclarationVisitor,
    ) -> Class:
        raise TypedSyntaxError(f"Cannot decorate a class with @native")


class PropertyDecorator(Class):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        if fn.klass is not self.type_env.function:
            return None
        return PropertyMethod(fn, decorator)

    def resolve_decorate_class(
        self,
        klass: Class,
        decorator: expr,
        visitor: DeclarationVisitor,
    ) -> Class:
        raise TypedSyntaxError(f"Cannot decorate a class with @property")


class CachedPropertyDecorator(Class):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        if fn.klass is not self.type_env.function:
            return None
        return CachedPropertyMethod(fn, decorator)

    def resolve_decorate_class(
        self,
        klass: Class,
        decorator: expr,
        visitor: DeclarationVisitor,
    ) -> Class:
        raise TypedSyntaxError(f"Cannot decorate a class with @cached_property")


class AsyncCachedPropertyDecorator(Class):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        if fn.klass is not self.type_env.function:
            return None
        return AsyncCachedPropertyMethod(fn, decorator)

    def resolve_decorate_class(
        self,
        klass: Class,
        decorator: expr,
        visitor: DeclarationVisitor,
    ) -> Class:
        raise TypedSyntaxError(f"Cannot decorate a class with @async_cached_property")


class IdentityDecorator(Class):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        return fn

    def resolve_decorate_class(
        self,
        klass: Class,
        decorator: expr,
        visitor: DeclarationVisitor,
    ) -> Class:
        return klass


class OverloadDecorator(Class):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        if isinstance(fn, DecoratedMethod):
            fn = fn.real_function
        if fn.klass is not self.type_env.function:
            return None
        return TransientDecoratedMethod(fn, decorator)

    def resolve_decorate_class(
        self,
        klass: Class,
        decorator: expr,
        visitor: DeclarationVisitor,
    ) -> Class:
        raise TypedSyntaxError(f"Cannot decorate a class with @overload")


# The transient name here is meant to imply that even though we understand the decorated method
# statically, the runtime will have no record of it. The common situations where this concept
# is useful is for `typing.overload`, where the overloads are shadowed in the runtime, and
# `property.setter`, where the application of the decorator returns the same property object
# that decorates the function. In the rest of the type checker, we refrain from declaring these
# functions, and they should never be visible externally.
class TransientDecoratedMethod(DecoratedMethod):
    def __init__(self, fn: Function | DecoratedMethod, decorator: expr) -> None:
        super().__init__(fn.klass, fn, decorator)

    def emit_function(
        self,
        node: ast.FunctionDef | ast.AsyncFunctionDef,
        code_gen: Static38CodeGenerator,
    ) -> str:
        return self.emit_function_with_decorators(self.real_function, node, code_gen)


class PropertySetterDecorator(Class):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        if fn.klass is not self.type_env.function:
            return None
        return TransientDecoratedMethod(fn, decorator)

    def resolve_decorate_class(
        self,
        klass: Class,
        decorator: expr,
        visitor: DeclarationVisitor,
    ) -> Class:
        raise TypedSyntaxError(f"Cannot decorate a class with @property.setter")


class DataclassDecorator(Callable[Class]):
    def __init__(self, type_env: TypeEnvironment) -> None:
        params = [
            Parameter(
                "cls", 0, ResolvedTypeRef(type_env.type), True, None, ParamStyle.POSONLY
            ),
            Parameter(
                "init", 1, ResolvedTypeRef(type_env.bool), True, True, ParamStyle.KWONLY
            ),
            Parameter(
                "repr", 2, ResolvedTypeRef(type_env.bool), True, True, ParamStyle.KWONLY
            ),
            Parameter(
                "eq", 3, ResolvedTypeRef(type_env.bool), True, True, ParamStyle.KWONLY
            ),
            Parameter(
                "order",
                4,
                ResolvedTypeRef(type_env.bool),
                True,
                False,
                ParamStyle.KWONLY,
            ),
            Parameter(
                "unsafe_hash",
                5,
                ResolvedTypeRef(type_env.bool),
                True,
                False,
                ParamStyle.KWONLY,
            ),
            Parameter(
                "frozen",
                6,
                ResolvedTypeRef(type_env.bool),
                True,
                False,
                ParamStyle.KWONLY,
            ),
        ]
        super().__init__(
            type_env.function,
            "dataclass",
            "__static__",
            params,
            {param.name: param for param in params},
            0,
            None,
            None,
            ResolvedTypeRef(type_env.dynamic),
        )
        self.type_env = type_env

    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        raise TypedSyntaxError(f"Cannot decorate a function or method with @dataclass")

    def resolve_decorate_class(
        self,
        klass: Class,
        decorator: expr,
        visitor: DeclarationVisitor,
    ) -> Class:
        for base in klass.mro:
            # any dynamic superclass might be a dataclass
            # fall back to dynamic behavior to pick up __dataclass_fields__ at runtime
            if base is self.type_env.dynamic:
                visitor.perf_warning(
                    f"Dataclass {klass.qualname} has a dynamic base. Convert all of "
                    "its bases to Static Python to resolve dataclass at compile time.",
                    decorator,
                )
                return self.type_env.dynamic

        if not isinstance(decorator, ast.Call):
            return Dataclass(self.type_env, klass)

        if decorator.args:
            raise TypedSyntaxError("dataclass() takes no positional arguments")

        kwargs = {
            "init": True,
            "repr": True,
            "eq": True,
            "order": False,
            "unsafe_hash": False,
            "frozen": False,
        }

        for kw in decorator.keywords:
            name, val = kw.arg, kw.value
            if name not in kwargs:
                raise TypedSyntaxError(
                    f"dataclass() got an unexpected keyword argument '{name}'"
                )
            if not isinstance(val, ast.Constant) or not isinstance(val.value, bool):
                raise TypedSyntaxError(
                    "dataclass() arguments must be boolean constants"
                )
            kwargs[name] = val.value

        return Dataclass(self.type_env, klass, **kwargs)

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        res = super().bind_call(node, visitor, type_ctx)
        visitor.set_type(node, self)
        return res

    def emit_decorator_call(
        self, class_def: ClassDef, code_gen: Static38CodeGenerator
    ) -> None:
        # If we were able to resolve the class def,
        # then there's no need to emit any code for this decorator,
        # since we already handled its effects statically.
        if code_gen.get_type(class_def) is self.type_env.dynamic:
            super().emit_decorator_call(class_def, code_gen)


class DataclassFieldFunction(Callable[Class]):
    def __init__(self, type_env: TypeEnvironment) -> None:
        params = [
            Parameter(
                "default",
                0,
                ResolvedTypeRef(type_env.dynamic),
                True,
                dataclasses.MISSING,
                ParamStyle.KWONLY,
            ),
            Parameter(
                "default_factory",
                1,
                ResolvedTypeRef(type_env.dynamic),
                True,
                dataclasses.MISSING,
                ParamStyle.KWONLY,
            ),
            Parameter(
                "init", 2, ResolvedTypeRef(type_env.bool), True, True, ParamStyle.KWONLY
            ),
            Parameter(
                "repr", 3, ResolvedTypeRef(type_env.bool), True, True, ParamStyle.KWONLY
            ),
            Parameter(
                "hash",
                4,
                ResolvedTypeRef(type_env.bool),
                True,
                None,
                ParamStyle.KWONLY,
            ),
            Parameter(
                "compare",
                5,
                ResolvedTypeRef(type_env.bool),
                True,
                True,
                ParamStyle.KWONLY,
            ),
            Parameter(
                "metadata",
                6,
                ResolvedTypeRef(type_env.bool),
                True,
                None,
                ParamStyle.KWONLY,
            ),
        ]
        super().__init__(
            type_env.function,
            "dataclass",
            "field",
            params,
            {param.name: param for param in params},
            0,
            None,
            None,
            ResolvedTypeRef(type_env.dataclass_field),
        )
        self.type_env = type_env


class DataclassFieldType(Class):
    def __init__(self, type_env: TypeEnvironment) -> None:
        super().__init__(
            TypeName("dataclasses", "Field"),
            type_env,
            instance=DataclassField(self),
            is_exact=True,
            pytype=dataclasses.Field,
            is_final=True,
        )


class DataclassFieldKind(Enum):
    FIELD = 0
    INITVAR = 1
    CLASSVAR = 2


class DataclassField(Object[DataclassFieldType]):
    def __init__(
        self,
        klass: DataclassFieldType,
        name: Optional[str] = None,
        type_ref: Optional[TypeRef] = None,
        default: Optional[AST] = None,
        default_factory: Optional[AST] = None,
        init: bool = True,
        repr: bool = True,
        hash: Optional[bool] = None,
        compare: bool = True,
        metadata: Optional[AST] = None,
        kw_only: bool = False,
    ) -> None:
        super().__init__(klass)
        self._field_name = name
        self.type_ref = type_ref
        self.default = default
        self.default_factory = default_factory
        self.init = init
        self.repr = repr
        self.hash = hash
        self.compare = compare
        self.metadata = metadata
        self.kind: DataclassFieldKind = DataclassFieldKind.FIELD
        self.base_class: Optional[Dataclass] = None
        self.default_class: Optional[Dataclass] = None
        self.kw_only = kw_only

        if type_ref is not None:
            wrapper = type(type_ref.resolved(True))
            if wrapper is ClassVar:
                self.kind = DataclassFieldKind.CLASSVAR
            elif wrapper is InitVar:
                self.kind = DataclassFieldKind.INITVAR

    @property
    def has_default(self) -> bool:
        return self.default is not None or self.default_factory is not None

    @property
    def field_name(self) -> str:
        assert self._field_name is not None
        return self._field_name

    @property
    def type_annotation(self) -> str:
        assert self.type_ref is not None
        return to_expr(self.type_ref._ref)

    @property
    def type_descr(self) -> TypeDescr:
        assert self.base_class is not None
        return (*self.base_class.type_descr, self.field_name)

    @property
    def unwrapped_type(self) -> Tuple[Class, Type[Class]]:
        assert self.type_ref is not None
        klass = self.type_ref.resolved(True)
        return klass.unwrap(), type(klass)

    @property
    def type_wrapper(self) -> Type[Class]:
        return self.unwrapped_type[1]

    @property
    def unwrapped_descr(self) -> TypeDescr:
        return self.unwrapped_type[0].type_descr

    @property
    def unwrapped_ref(self) -> TypeRef:
        return ResolvedTypeRef(self.unwrapped_type[0])

    def bind_field(self, visitor: TypeBinder) -> None:
        if (
            self.kind in (DataclassFieldKind.CLASSVAR, DataclassFieldKind.INITVAR)
            and self.default_factory is not None
        ):
            raise TypedSyntaxError(
                f"field {self.field_name} cannot have a default factory"
            )
        if self.kind is DataclassFieldKind.FIELD and self.default is not None:
            default_type = visitor.get_type(self.default).klass.inexact_type()
            if default_type in (
                visitor.type_env.list,
                visitor.type_env.dict,
                visitor.type_env.set,
            ):
                raise TypedSyntaxError(
                    f"mutable default {default_type.qualname} for field "
                    f"{self.field_name} is not allowed: use default_factory"
                )
        if self.kind is DataclassFieldKind.INITVAR and not self.init:
            raise TypedSyntaxError("InitVar fields must have init=True")

    def emit_field(self, target: AST, code_gen: Static38CodeGenerator) -> None:
        default = self.default
        if default is not None:
            code_gen.visit(default)
            assert code_gen.get_type(target).klass.can_assign_from(
                code_gen.get_type(default).klass
            )
            code_gen.visit(target)
        elif self.default_factory is not None:
            code_gen.visit(self.default_factory)
            code_gen.visit(target)
        # no default value, nothing to emit

    def load_default(self, klass: Dataclass, code_gen: Static38CodeGenerator) -> None:
        default = self.default_class
        assert default is not None
        if default.inexact_type() is klass.inexact_type():
            code_gen.emit("LOAD_NAME", self.field_name)
        else:
            code_gen.emit("LOAD_CLASS", default.type_descr)
            code_gen.emit("LOAD_ATTR", self.field_name)

    def __repr__(self) -> str:
        return (
            "Field("
            f"name={self.field_name!r},"
            f"type={self.type_ref!r},"
            f"default={self.default!r},"
            f"default_factory={self.default_factory!r},"
            f"init={self.init!r},"
            f"repr={self.repr!r},"
            f"hash={self.hash!r},"
            f"compare={self.compare!r},"
            f"metadata={self.metadata!r},"
            f"_field_type={self.kind}"
            ")"
        )


class Dataclass(Class):
    def __init__(
        self,
        type_env: TypeEnvironment,
        klass: Class,
        init: bool = True,
        repr: bool = True,
        eq: bool = True,
        order: bool = False,
        unsafe_hash: bool = False,
        frozen: bool = False,
    ) -> None:
        super().__init__(
            type_name=klass.type_name,
            type_env=type_env,
            bases=klass.bases,
            instance=DataclassInstance(self),
            klass=klass.klass,
            members=copy(klass.members),
            is_exact=klass.is_exact,
            pytype=klass.pytype,
            is_final=klass.is_final,
        )
        self.wrapped_class = klass

        self.init = init
        self.repr = repr
        self.eq = eq
        self.order = order
        self.unsafe_hash = unsafe_hash
        self.frozen = frozen

        self.fields: Dict[str, DataclassField] = {}

        # Fields that are passed as arguments to __init__,
        # where field.kind is FIELD or INITVAR and field.init is True
        self.init_fields: Dict[str, DataclassField] = {}

        # Fields where field.kind is FIELD
        self.true_fields: Dict[str, DataclassField] = {}

        if order:
            if not eq:
                raise TypedSyntaxError("eq must be true if order is true")
            for name in ("__lt__", "__le__", "__gt__", "__ge__"):
                if name in self.wrapped_class.members:
                    raise TypedSyntaxError(
                        f"Cannot overwrite attribute {name} in class {self.type_name.name}. "
                        "Consider using functools.total_ordering"
                    )

        if frozen:
            for name in ("__setattr__", "__delattr__"):
                if name in self.wrapped_class.members:
                    raise TypedSyntaxError(
                        f"Cannot overwrite attribute {name} in class {self.type_name.name}"
                    )

        if unsafe_hash and self.has_explicit_hash:
            raise TypedSyntaxError(
                f"Cannot overwrite attribute __hash__ in class {self.type_name.name}"
            )

    @property
    def generate_eq(self) -> bool:
        return self.eq and "__eq__" not in self.wrapped_class.members

    @property
    def generate_init(self) -> bool:
        return self.init and "__init__" not in self.wrapped_class.members

    @property
    def generate_repr(self) -> bool:
        return self.repr and "__repr__" not in self.wrapped_class.members

    @property
    def has_explicit_hash(self) -> bool:
        return "__hash__" in self.wrapped_class.members and not (
            self.wrapped_class.members["__hash__"] is None
            and "__eq__" in self.wrapped_class.members
        )

    def check_consistent_override(
        self,
        name: str,
        existing: DataclassField,
        field: DataclassField,
    ) -> None:
        existing_type, existing_wrapper = existing.unwrapped_type
        new_type, new_wrapper = field.unwrapped_type
        if existing_wrapper in (ClassVar, InitVar):
            if new_wrapper is not existing_wrapper:
                raise TypedSyntaxError(
                    f"Override of field '{name}' must be a {existing_wrapper.__name__}"
                )
        elif new_wrapper in (ClassVar, InitVar):
            raise TypedSyntaxError(
                f"Override of field '{name}' cannot be a {new_wrapper.__name__}"
            )
        if new_type is not existing_type:
            raise TypedSyntaxError(
                f"Type of field '{name}' on class "
                f"'{self.qualname}' conflicts with base type. "
                f"Base field has annotation {existing.type_annotation}, "
                f"but overridden field has annotation {field.type_annotation}"
            )

    def finish_bind(self, module: ModuleTable, klass: Class | None) -> Optional[Value]:
        # Class declarations do not allow bases to be forward-referenced,
        # so by now all base dataclasses have their fields fully resolved
        any_frozen_base = False
        has_dataclass_bases = False
        for base in self.mro[-1:0:-1]:
            if isinstance(base, Dataclass):
                has_dataclass_bases = True
                for name, field in base.fields.items():
                    if (existing := self.fields.get(name)) is not None:
                        self.check_consistent_override(name, existing, field)
                    self.fields[name] = field

                self.fields.update(base.fields)
                if base.frozen:
                    any_frozen_base = True

        if has_dataclass_bases:
            if any_frozen_base and not self.frozen:
                raise TypedSyntaxError(
                    "cannot inherit non-frozen dataclass from a frozen one"
                )
            if not any_frozen_base and self.frozen:
                raise TypedSyntaxError(
                    "cannot inherit frozen dataclass from a non-frozen one"
                )

        for name, slot in self.members.items():
            if not isinstance(slot, Slot) or not slot.declared_on_class:
                continue

            base_field = self.fields.get(name)
            self.fields[name] = field = DataclassField(
                self.type_env.dataclass_field,
                name,
                slot.type_ref,
            )

            if base_field is None:
                field.base_class = self
            else:
                self.check_consistent_override(name, base_field, field)
                field.base_class = base_field.base_class

            if not slot.assigned_on_class:
                if base_field is not None:
                    field.default = base_field.default
                    field.default_class = base_field.default_class
                continue

            assignment = slot.assignment
            assert isinstance(assignment, AnnAssign)
            default = assignment.value
            assert default is not None

            if not isinstance(default, ast.Call):
                field.default = default
                field.default_class = self
                continue

            func = module.ref_visitor.visit(default.func)
            if func is not self.type_env.dataclass_field_function:
                field.default = default
                field.default_class = self
                continue

            if default.args:
                raise TypedSyntaxError(
                    "dataclasses.field() takes no positional arguments"
                )

            for kw in default.keywords:
                name, node = kw.arg, kw.value
                if name in ("default", "default_factory", "metadata"):
                    setattr(field, name, node)
                elif name in ("init", "repr", "compare"):
                    if not isinstance(node, ast.Constant) or not isinstance(
                        node.value, bool
                    ):
                        raise TypedSyntaxError(
                            f"dataclasses.field() argument '{name}' must be a boolean constant"
                        )
                    setattr(field, name, node.value)
                elif name == "hash":
                    # hash is allowed to be bool or None
                    if not isinstance(node, ast.Constant) or not (
                        node.value is None or isinstance(node.value, bool)
                    ):
                        raise TypedSyntaxError(
                            "dataclasses.field() argument 'hash' must be None or a boolean constant"
                        )
                    field.hash = node.value
                else:
                    raise TypedSyntaxError(
                        f"dataclasses.field() got an unexpected keyword argument '{name}'"
                    )

            if field.default is not None and field.default_factory is not None:
                raise TypedSyntaxError(
                    "cannot specify both default and default_factory"
                )
            if field.default is None and field.default_factory is None:
                # no default - clear the assignment from the slot
                slot.assignment = None
                slot.assigned_on_class = False
            else:
                field.default_class = self

        self.init_fields = {
            name: field
            for name, field in self.fields.items()
            if field.kind is not DataclassFieldKind.CLASSVAR and field.init
        }
        self.true_fields = {
            name: field
            for name, field in self.fields.items()
            if field.kind is DataclassFieldKind.FIELD
        }

        if self.init:
            init_params = [
                Parameter(
                    "self",
                    0,
                    ResolvedTypeRef(self.inexact_type()),
                    False,
                    None,
                    ParamStyle.POSONLY,
                )
            ]

            seen_default = False
            for name, field in self.init_fields.items():
                if has_default := field.has_default:
                    seen_default = True
                elif seen_default:
                    raise TypedSyntaxError(
                        f"non-default argument '{name}' follows default argument"
                    )

                if field.default is not None:
                    default = ast.Name(name, ast.Load())
                elif field.default_factory is not None:
                    default = ast.Name("_HAS_DEFAULT_FACTORY", ast.Load())
                else:
                    default = None
                init_params.append(
                    Parameter(
                        name,
                        len(init_params),
                        field.unwrapped_ref,
                        has_default,
                        default,
                        ParamStyle.NORMAL,
                    )
                )

            if "__init__" not in self.members:
                self.members["__init__"] = BuiltinMethodDescriptor(
                    "__init__",
                    self,
                    init_params,
                    ResolvedTypeRef(self.type_env.none),
                )

        # Replace the wrapped class with self in all methods
        for member in self.members.values():
            if not isinstance(member, Callable):
                continue
            if member.container_type is self.wrapped_class.inexact_type():
                member.set_container_type(self.inexact_type())

        return super().finish_bind(module, klass)

    def bind_field(
        self,
        name: str,
        assignment: Optional[AST],
        visitor: TypeBinder,
    ) -> Optional[AST]:
        if assignment is None:
            # annotation without default value or field()
            return None

        visitor.visitExpectedType(assignment, self.type_env.DYNAMIC)

        field = self.fields[name]
        field.bind_field(visitor)
        if isinstance(visitor.get_type(assignment), DataclassField):
            visitor.set_type(assignment, field)
            return field.default

        return assignment

    def flow_graph(
        self,
        node: ClassDef,
        code_gen: Static38CodeGenerator,
        func: str,
        args: Tuple[str, ...],
        return_type_descr: TypeDescr,
    ) -> PyFlowGraph38Static:
        scope = FunctionScope(func, code_gen.cur_mod, code_gen.scope.klass)
        scope.parent = code_gen.scope

        graph = code_gen.flow_graph(
            func,
            code_gen.graph.filename,
            scope,
            args=args,
            optimized=1,
            firstline=node.lineno,
        )
        graph.setFlag(CO_STATICALLY_COMPILED)
        graph.extra_consts.append(return_type_descr)
        return graph

    def emit_method(
        self,
        code_gen: Static38CodeGenerator,
        graph: PyFlowGraph38Static,
        oparg: int,
    ) -> None:
        code_gen.emit("LOAD_CONST", graph)
        code_gen.emit("LOAD_CONST", f"{self.type_name.name}.{graph.name}")
        code_gen.emit("MAKE_FUNCTION", oparg)
        code_gen.emit("STORE_NAME", graph.name)

    def emit_dunder_comparison(
        self,
        node: ClassDef,
        code_gen: Static38CodeGenerator,
        fields: List[DataclassField],
        method_name: str,
        op: str,
    ) -> None:
        graph = self.flow_graph(
            node,
            code_gen,
            method_name,
            ("self", "other"),
            self.type_env.object.type_descr,
        )
        false = graph.newBlock()

        graph.emit("CHECK_ARGS", (0, self.inexact_type().type_descr))
        graph.emit("LOAD_FAST", "other")
        graph.emit("LOAD_TYPE")
        graph.emit("LOAD_FAST", "self")
        graph.emit("LOAD_TYPE")
        graph.emit("IS_OP", 0)
        graph.emit("POP_JUMP_IF_FALSE", false)

        for field in fields:
            graph.emit("LOAD_FAST", "self")
            graph.emit("LOAD_FIELD", field.type_descr)
        graph.emit("BUILD_TUPLE", len(fields))

        # Since Py_TYPE(self) is Py_TYPE(other), we can depend on slots
        for field in fields:
            graph.emit("LOAD_FAST", "other")
            graph.emit("LOAD_FIELD", field.type_descr)
        graph.emit("BUILD_TUPLE", len(fields))

        graph.emit("COMPARE_OP", op)
        graph.emit("RETURN_VALUE")

        graph.nextBlock(false)
        graph.emit("LOAD_GLOBAL", "NotImplemented")
        graph.emit("RETURN_VALUE")

        self.emit_method(code_gen, graph, 0)

    def emit_dunder_delattr_or_setattr(
        self, node: ClassDef, code_gen: Static38CodeGenerator, delete: bool
    ) -> None:
        if delete:
            method_name = "__delattr__"
            args = ("self", "name")
            msg = "cannot delete field "
        else:
            method_name = "__setattr__"
            args = ("self", "name", "value")
            msg = "cannot assign to field "

        graph = self.flow_graph(
            node, code_gen, method_name, args, self.type_env.none.type_descr
        )
        error = graph.newBlock()
        super_call = graph.newBlock()

        graph.emit(
            "CHECK_ARGS",
            (0, self.inexact_type().type_descr, 1, self.type_env.str.type_descr),
        )
        graph.emit("LOAD_FAST", "self")
        # TODO(T92470300): graph.emit("CAST", (self.exact_type().type_descr, False))
        graph.emit("LOAD_TYPE")
        graph.emit("LOAD_CLASS", self.type_descr)
        graph.emit("IS_OP", 0)
        graph.emit("POP_JUMP_IF_TRUE", error)

        graph.nextBlock()
        graph.emit("LOAD_FAST", "name")
        graph.emit("LOAD_CONST", tuple(self.true_fields))
        graph.emit("CONTAINS_OP", 0)
        graph.emit("POP_JUMP_IF_FALSE", super_call)

        graph.nextBlock(error)
        graph.emit("LOAD_CLASS", self.type_descr)
        graph.emit("LOAD_METHOD", "_FrozenInstanceError")
        graph.emit("LOAD_CONST", msg)
        graph.emit("LOAD_FAST", "name")
        graph.emit("FORMAT_VALUE", FVC_REPR)
        graph.emit("BUILD_STRING", 2)
        graph.emit("CALL_METHOD", 1)
        graph.emit("RAISE_VARARGS", 1)

        graph.nextBlock(super_call)
        graph.emit("LOAD_GLOBAL", "super")
        graph.emit("LOAD_CLASS", self.type_descr)
        graph.emit("LOAD_FAST", "self")
        graph.emit("LOAD_METHOD_SUPER", (method_name, False))
        graph.emit("LOAD_FAST", "name")
        if delete:
            graph.emit("CALL_METHOD", 1)
        else:
            graph.emit("LOAD_FAST", "value")
            graph.emit("CALL_METHOD", 2)
        graph.emit("POP_TOP")
        graph.emit("LOAD_CONST", None)
        graph.emit("RETURN_VALUE")

        self.emit_method(code_gen, graph, 0)

    def emit_dunder_hash(
        self,
        node: ClassDef,
        code_gen: Static38CodeGenerator,
    ) -> None:
        graph = self.flow_graph(
            node, code_gen, "__hash__", ("self",), self.type_env.int.type_descr
        )
        graph.emit("CHECK_ARGS", (0, self.inexact_type().type_descr))
        graph.emit("LOAD_GLOBAL", "hash")

        num_hash_fields = 0
        for field in self.true_fields.values():
            if field.compare if field.hash is None else field.hash:
                graph.emit("LOAD_FAST", "self")
                graph.emit("LOAD_FIELD", field.type_descr)
                num_hash_fields += 1

        if num_hash_fields:
            graph.emit("BUILD_TUPLE", num_hash_fields)
        else:
            graph.emit("LOAD_CONST", ())

        graph.emit("CALL_FUNCTION", 1)
        graph.emit("RETURN_VALUE")

        self.emit_method(code_gen, graph, 0)

    def emit_dunder_init(
        self,
        node: ClassDef,
        code_gen: Static38CodeGenerator,
    ) -> None:
        self_name = "__dataclass_self__" if "self" in self.fields else "self"

        graph = self.flow_graph(
            node,
            code_gen,
            "__init__",
            args=(self_name, *self.init_fields),
            return_type_descr=self.type_env.none.type_descr,
        )

        args = [0, self.inexact_type().type_descr]
        for i, field in enumerate(self.init_fields.values()):
            if field.default_factory is None:
                args.append(i + 1)
                args.append(field.unwrapped_descr)
        graph.emit("CHECK_ARGS", tuple(args))

        for name, field in self.true_fields.items():
            if field.default_factory is None:
                if field.init:
                    graph.emit("LOAD_FAST", name)
                    graph.emit("LOAD_FAST", self_name)
                    graph.emit("STORE_FIELD", field.type_descr)
            elif field.init:
                arg_passed = graph.newBlock()
                store = graph.newBlock()
                graph.emit("LOAD_FAST", name)
                graph.emit("LOAD_CLASS", self.type_descr)
                graph.emit("LOAD_ATTR", "_HAS_DEFAULT_FACTORY")
                graph.emit("IS_OP", 0)
                graph.emit("POP_JUMP_IF_FALSE", arg_passed)

                graph.nextBlock()
                graph.emit("LOAD_CLASS", self.type_descr)
                graph.emit("LOAD_METHOD", name)
                graph.emit("CALL_METHOD", 0)
                graph.emit("CAST", field.unwrapped_descr)
                graph.emit("JUMP_FORWARD", store)

                graph.nextBlock(arg_passed)
                graph.emit("LOAD_FAST", name)

                graph.nextBlock(store)
                graph.emit("LOAD_FAST", self_name)
                graph.emit("STORE_FIELD", field.type_descr)
            else:
                graph.emit("LOAD_CLASS", self.type_descr)
                graph.emit("LOAD_METHOD", name)
                graph.emit("CALL_METHOD", 0)
                graph.emit("CAST", field.unwrapped_descr)
                graph.emit("LOAD_FAST", self_name)
                graph.emit("STORE_FIELD", field.type_descr)

        if "__post_init__" in self.wrapped_class.members:
            initvar_names = [
                name
                for name, field in self.fields.items()
                if field.kind is DataclassFieldKind.INITVAR
            ]
            graph.emit("LOAD_FAST", self_name)
            graph.emit("LOAD_METHOD", "__post_init__")
            for name in initvar_names:
                graph.emit("LOAD_FAST", name)
            graph.emit("CALL_METHOD", len(initvar_names))
            graph.emit("POP_TOP")

        graph.emit("LOAD_CONST", None)
        graph.emit("RETURN_VALUE")

        num_defaults = 0
        for name, field in self.init_fields.items():
            if field.default is not None:
                num_defaults += 1
                field.load_default(self, code_gen)
            elif field.default_factory is not None:
                num_defaults += 1
                code_gen.emit("LOAD_NAME", "_HAS_DEFAULT_FACTORY")

        if num_defaults:
            code_gen.emit("BUILD_TUPLE", num_defaults)
            self.emit_method(code_gen, graph, 1)
        else:
            self.emit_method(code_gen, graph, 0)

    def emit_dunder_repr(
        self,
        node: ClassDef,
        code_gen: Static38CodeGenerator,
    ) -> None:
        graph = self.flow_graph(
            node, code_gen, "__repr__", ("self",), self.type_env.str.type_descr
        )
        graph.emit("CHECK_ARGS", (0, self.inexact_type().type_descr))
        graph.emit("LOAD_FAST", "self")
        graph.emit("LOAD_TYPE")
        graph.emit("LOAD_ATTR", "__qualname__")
        graph.emit("REFINE_TYPE", self.type_env.str.type_descr)

        num_repr_fields = 0
        for name, field in self.true_fields.items():
            if field.repr:
                graph.emit(
                    "LOAD_CONST", f", {name}=" if num_repr_fields else f"({name}="
                )
                graph.emit("LOAD_FAST", "self")
                graph.emit("LOAD_FIELD", field.type_descr)
                graph.emit("FORMAT_VALUE", FVC_REPR)
                num_repr_fields += 1

        graph.emit("LOAD_CONST", ")" if num_repr_fields else "()")
        graph.emit("BUILD_STRING", 2 + 2 * num_repr_fields)
        graph.emit("RETURN_VALUE")

        # Wrap the simple __repr__ function with reprlib.recursive_repr
        # to prevent infinite loops if any field contains a cycle
        code_gen.emit("LOAD_CONST", 0)
        code_gen.emit("LOAD_CONST", ("recursive_repr",))
        code_gen.emit("IMPORT_NAME", "reprlib")
        code_gen.emit("IMPORT_FROM", "recursive_repr")
        code_gen.emit("CALL_FUNCTION", 0)
        code_gen.emit("LOAD_CONST", graph)
        code_gen.emit("LOAD_CONST", f"{self.type_name.name}.{graph.name}")
        code_gen.emit("MAKE_FUNCTION", 0)
        code_gen.emit("CALL_FUNCTION", 1)
        code_gen.emit("STORE_NAME", "__repr__")
        code_gen.emit("POP_TOP")

    def emit_extra_members(
        self, node: ClassDef, code_gen: Static38CodeGenerator
    ) -> None:
        # import objects needed from dataclasses and store them on the class
        from_names: List[str] = ["_DataclassParams", "_FIELD", "field"]
        as_names: List[str] = ["_DataclassParams", "_FIELD", "_field"]
        field_values = self.fields.values()
        if any(field.kind is DataclassFieldKind.CLASSVAR for field in field_values):
            from_names.append("_FIELD_CLASSVAR")
            as_names.append("_FIELD_CLASSVAR")
        if any(field.kind is DataclassFieldKind.INITVAR for field in field_values):
            from_names.append("_FIELD_INITVAR")
            as_names.append("_FIELD_INITVAR")
        if any(field.default_factory is not None for field in field_values):
            from_names.append("_HAS_DEFAULT_FACTORY")
            as_names.append("_HAS_DEFAULT_FACTORY")
        if self.frozen:
            from_names.append("FrozenInstanceError")
            as_names.append("_FrozenInstanceError")

        code_gen.emit("LOAD_CONST", 0)
        code_gen.emit("LOAD_CONST", tuple(from_names))
        code_gen.emit("IMPORT_NAME", "dataclasses")
        for from_name, as_name in zip(from_names, as_names):
            code_gen.emit("IMPORT_FROM", from_name)
            code_gen.emit("STORE_NAME", as_name)
        code_gen.emit("POP_TOP")

        # set __dataclass_fields__
        for name, field in self.fields.items():
            code_gen.emit("LOAD_NAME", "_field")
            field_args = []

            # <class>.<field> contains the default or default_factory, if one exists
            if field.default is not None:
                field.load_default(self, code_gen)
                field_args.append("default")

            if field.default_factory is not None:
                field.load_default(self, code_gen)
                field_args.append("default_factory")

            if not field.init:
                code_gen.emit("LOAD_CONST", False)
                field_args.append("init")

            if not field.repr:
                code_gen.emit("LOAD_CONST", False)
                field_args.append("repr")

            if field.hash is not None:
                code_gen.emit("LOAD_CONST", field.hash)
                field_args.append("hash")

            if not field.compare:
                code_gen.emit("LOAD_CONST", False)
                field_args.append("compare")

            if field.metadata is not None:
                code_gen.visit(field.metadata)
                field_args.append("metadata")

            code_gen.emit("LOAD_CONST", field.kw_only)
            field_args.append("kw_only")

            code_gen.emit("LOAD_CONST", tuple(field_args))
            code_gen.emit("CALL_FUNCTION_KW", len(field_args))

            code_gen.emit("DUP_TOP")
            code_gen.emit("LOAD_CONST", name)
            code_gen.emit("ROT_TWO")
            code_gen.emit("STORE_ATTR", "name")

            code_gen.emit("DUP_TOP")
            code_gen.emit("LOAD_CONST", field.type_annotation)
            code_gen.emit("ROT_TWO")
            code_gen.emit("STORE_ATTR", "type")

            code_gen.emit("DUP_TOP")
            if field.kind is DataclassFieldKind.FIELD:
                code_gen.emit("LOAD_NAME", "_FIELD")
            elif field.kind is DataclassFieldKind.CLASSVAR:
                code_gen.emit("LOAD_NAME", "_FIELD_CLASSVAR")
            else:
                code_gen.emit("LOAD_NAME", "_FIELD_INITVAR")
            code_gen.emit("ROT_TWO")
            code_gen.emit("STORE_ATTR", "_field_type")

        code_gen.emit("LOAD_CONST", tuple(self.fields))
        code_gen.emit("BUILD_CONST_KEY_MAP", len(self.fields))
        code_gen.emit("STORE_NAME", "__dataclass_fields__")

        # set __dataclass_params__ with the arguments to @dataclass()
        code_gen.emit("LOAD_NAME", "_DataclassParams")
        code_gen.emit("LOAD_CONST", self.init)
        code_gen.emit("LOAD_CONST", self.repr)
        code_gen.emit("LOAD_CONST", self.eq)
        code_gen.emit("LOAD_CONST", self.order)
        code_gen.emit("LOAD_CONST", self.unsafe_hash)
        code_gen.emit("LOAD_CONST", self.frozen)
        code_gen.emit("CALL_FUNCTION", 6)
        code_gen.emit("STORE_NAME", "__dataclass_params__")

        if self.generate_init:
            self.emit_dunder_init(node, code_gen)

        if self.generate_repr:
            self.emit_dunder_repr(node, code_gen)

        compare_fields = [field for field in self.true_fields.values() if field.compare]
        if self.generate_eq:
            self.emit_dunder_comparison(node, code_gen, compare_fields, "__eq__", "==")

        if self.order:
            self.emit_dunder_comparison(node, code_gen, compare_fields, "__lt__", "<")
            self.emit_dunder_comparison(node, code_gen, compare_fields, "__le__", "<=")
            self.emit_dunder_comparison(node, code_gen, compare_fields, "__gt__", ">")
            self.emit_dunder_comparison(node, code_gen, compare_fields, "__ge__", ">=")

        if self.frozen:
            self.emit_dunder_delattr_or_setattr(node, code_gen, delete=False)
            self.emit_dunder_delattr_or_setattr(node, code_gen, delete=True)

        if self.unsafe_hash:
            self.emit_dunder_hash(node, code_gen)
        elif self.eq and "__hash__" not in self.wrapped_class.members:
            if self.frozen:
                self.emit_dunder_hash(node, code_gen)
            else:
                code_gen.emit("LOAD_CONST", None)
                code_gen.emit("STORE_NAME", "__hash__")

    def _create_exact_type(self) -> Class:
        klass = type(self)(
            type_env=self.type_env,
            klass=self.wrapped_class.exact_type(),
            init=self.init,
            repr=self.repr,
            eq=self.eq,
            order=self.order,
            unsafe_hash=self.unsafe_hash,
            frozen=self.frozen,
        )
        klass.members = self.members
        return klass


class DataclassInstance(Object[Dataclass]):
    def bind_attr(
        self, node: ast.Attribute, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        store = isinstance(node.ctx, ast.Store)
        delete = isinstance(node.ctx, ast.Del)
        if (
            self.klass.frozen
            and (store or delete)
            and any(name == node.attr for name in self.klass.fields)
        ):
            msg = "assign to" if store else "delete"
            visitor.syntax_error(
                f"cannot {msg} field {node.attr!r} "
                f"of frozen dataclass {self.klass.instance_name!r}",
                node,
            )

        super().bind_attr(node, visitor, type_ctx)


class BuiltinFunction(Callable[Class]):
    def __init__(
        self,
        func_name: str,
        module_name: str,
        klass: Optional[Class],
        type_env: TypeEnvironment,
        args: Optional[List[Parameter]] = None,
        return_type: Optional[TypeRef] = None,
    ) -> None:
        assert isinstance(return_type, (TypeRef, type(None)))
        args_by_name = (
            {}
            if args is None
            else {arg.name: arg for arg in args if arg.style is not ParamStyle.POSONLY}
        )
        super().__init__(
            type_env.builtin_method_desc,
            func_name,
            module_name,
            args,
            args_by_name,
            0,
            None,
            None,
            return_type or ResolvedTypeRef(type_env.dynamic),
        )
        self.set_container_type(klass)

    def can_override(self, override: Value, klass: Class, module: ModuleTable) -> bool:
        if not isinstance(override, Function):
            raise TypedSyntaxError(f"class cannot hide inherited member: {self!r}")

        return super().can_override(override, klass, module)

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if node.keywords:
            return super().emit_call(node, code_gen)

        code_gen.set_lineno(node)
        self.emit_call_self(node, code_gen)

    def make_generic(
        self, new_type: Class, name: GenericTypeName, type_env: TypeEnvironment
    ) -> Value:
        cur_args = self.args
        cur_ret_type = self.return_type
        if cur_args is not None and cur_ret_type is not None:
            new_args = list(arg.bind_generics(name, type_env) for arg in cur_args)
            new_ret_type = cur_ret_type.resolved().bind_generics(name, type_env)
            return BuiltinFunction(
                self.func_name,
                self.module_name,
                new_type,
                new_type.type_env,
                new_args,
                ResolvedTypeRef(new_ret_type),
            )
        else:
            return BuiltinFunction(
                self.func_name,
                self.module_name,
                new_type,
                new_type.type_env,
                None,
                self.return_type,
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
        arg_mapping = ArgMapping(
            self, node, visitor, self_expr, args_override, descr_override
        )
        arg_mapping.bind_args(visitor)
        ret_type = visitor.type_env.DYNAMIC
        if args_override:
            cls_type = visitor.get_type(args_override[0])
            if isinstance(cls_type, Class):
                ret_type = cls_type.instance
                if ret_type is self.klass.type_env.type:
                    # if we get a generic "type" then we don't really know
                    # what type we're producing
                    ret_type = visitor.type_env.DYNAMIC

        return arg_mapping, ret_type


class BuiltinMethodDescriptor(Callable[Class]):
    def __init__(
        self,
        func_name: str,
        container_type: Class,
        args: Optional[List[Parameter]] = None,
        return_type: Optional[TypeRef] = None,
        dynamic_dispatch: bool = False,
        valid_on_subclasses: bool = False,
    ) -> None:
        assert isinstance(return_type, (TypeRef, type(None)))
        self.type_env: TypeEnvironment = container_type.type_env
        args_by_name = (
            {}
            if args is None
            else {arg.name: arg for arg in args if arg.style is not ParamStyle.POSONLY}
        )
        super().__init__(
            self.type_env.builtin_method_desc,
            func_name,
            container_type.type_name.module,
            args,
            args_by_name,
            0,
            None,
            None,
            return_type or ResolvedTypeRef(container_type.type_env.dynamic),
        )
        # When `dynamic_dispatch` is True, we will not emit INVOKE_* on this
        # method.
        self.dynamic_dispatch = dynamic_dispatch
        self.set_container_type(container_type)
        self.valid_on_subclasses = valid_on_subclasses

    def can_override(self, override: Value, klass: Class, module: ModuleTable) -> bool:
        if not isinstance(override, (BuiltinMethodDescriptor, Function)):
            raise TypedSyntaxError(f"class cannot hide inherited member: {self!r}")

        return super().can_override(override, klass, module)

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

        visitor.set_type(node, visitor.type_env.DYNAMIC)
        for arg in node.args:
            visitor.visitExpectedType(
                arg, visitor.type_env.DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE
            )

        return NO_EFFECT

    def resolve_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: GenericVisitor[object],
    ) -> Optional[Value]:
        if inst is None:
            return self
        else:
            if self.dynamic_dispatch:
                return visitor.type_env.DYNAMIC
            if isinstance(self.return_type, SelfTypeRef):
                ret_type = ResolvedTypeRef(inst.klass)
                bound = self.return_type.resolved()
                assert bound.can_assign_from(inst.klass)
            else:
                ret_type = self.return_type
            # Type must either match exactly or the method must be explicitly
            # annotated as being valid on arbitrary subclasses, too.
            if not (inst.klass.is_exact or self.valid_on_subclasses):
                ret_type = ResolvedTypeRef(visitor.type_env.dynamic)
            return BuiltinMethod(self, node.value, ret_type)

    def make_generic(
        self, new_type: Class, name: GenericTypeName, type_env: TypeEnvironment
    ) -> Value:
        cur_args = self.args
        cur_ret_type = self.return_type
        if cur_args is not None and cur_ret_type is not None:
            new_args = list(arg.bind_generics(name, type_env) for arg in cur_args)
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
    def __init__(
        self,
        desc: BuiltinMethodDescriptor,
        target: ast.expr,
        return_type: TypeRef | None = None,
    ) -> None:
        super().__init__(
            desc.type_env.method,
            desc.func_name,
            desc.module_name,
            desc.args,
            desc.args_by_name,
            0,
            None,
            None,
            return_type or desc.return_type,
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
            return super().bind_call_self(node, visitor, type_ctx, self.target)
        if node.keywords:
            return Object.bind_call(self, node, visitor, type_ctx)

        visitor.set_type(node, self.return_type.resolved().instance)
        visitor.visit(self.target)
        for arg in node.args:
            visitor.visitExpectedType(
                arg, visitor.type_env.DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE
            )

        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if node.keywords:
            return super().emit_call(node, code_gen)

        code_gen.set_lineno(node)

        if self.args is not None:
            self.desc.emit_call_self(node, code_gen, self.target)
        else:
            # Untyped method, we can still do an INVOKE_METHOD

            code_gen.visit(self.target)

            code_gen.set_lineno(node)
            for arg in node.args:
                code_gen.visit(arg)

            klass = code_gen.get_type(self.target).klass
            if klass.is_exact or klass.is_final:
                code_gen.emit("INVOKE_FUNCTION", (self.type_descr, len(node.args) + 1))
            else:
                code_gen.emit_invoke_method(self.type_descr, len(node.args))
            return_type_descr = self.return_type.resolved().type_descr
            if return_type_descr != ("builtins", "object"):
                code_gen.emit("REFINE_TYPE", return_type_descr)


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


class Slot(Object[TClassInv]):
    assignment: Optional[AST] = None
    declared_on_class: bool = False
    assigned_on_class: bool = False

    def __init__(
        self,
        type_ref: Optional[TypeRef],
        name: str,
        container_type: Class,
        assignment: Optional[AST] = None,
        declared_on_class: bool = False,
    ) -> None:
        super().__init__(container_type.type_env.member)
        self.container_type = container_type
        self.slot_name = name
        self.type_ref = type_ref
        self.update(assignment, declared_on_class)

    def update(self, assignment: Optional[AST], declared_on_class: bool) -> None:
        if not self.assignment:
            self.assignment = assignment
        if not self.declared_on_class:
            self.declared_on_class = declared_on_class
        if not self.assigned_on_class:
            self.assigned_on_class: bool = declared_on_class and bool(assignment)

    def can_override(self, override: Value, klass: Class, module: ModuleTable) -> bool:
        if isinstance(override, Slot):
            # TODO we could allow covariant type overrides for Final attributes
            ot = override.type_ref
            it = self.type_ref
            if ot and it and ot.resolved(True) != (itr := it.resolved(True)):
                raise TypedSyntaxError(
                    f"Cannot change type of inherited attribute (inherited type '{itr.instance.name}')"
                )

            return True
        elif isinstance(override, PropertyMethod):
            ot = override.function.return_type
            it = self.type_ref
            if ot and it and ot.resolved(True) != (itr := it.resolved(True)):
                raise TypedSyntaxError(
                    f"Cannot change type of inherited attribute (inherited type '{itr.instance.name}')"
                )

            return True

        return super().can_override(override, klass, module)

    def finish_bind(self, module: ModuleTable, klass: Class | None) -> Value:
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
        visitor: GenericVisitor[object],
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
        return self.klass.type_env.dynamic

    @property
    def type_descr(self) -> TypeDescr:
        return self.decl_type.type_descr

    def emit_load_from_slot(self, code_gen: Static38CodeGenerator) -> None:
        if self.is_typed_descriptor_with_default_value():
            code_gen.emit_invoke_method(
                self.container_type.type_descr + ((self.slot_name, "fget"),), 0
            )
            return

        type_descr = self.container_type.type_descr
        type_descr += (self.slot_name,)
        code_gen.emit("LOAD_FIELD", type_descr)

    def emit_store_to_slot(self, code_gen: Static38CodeGenerator) -> None:
        if self.is_typed_descriptor_with_default_value():
            code_gen.emit("ROT_TWO")
            code_gen.emit_invoke_method(
                self.container_type.type_descr + ((self.slot_name, "fset"),), 1
            )
            # fset will return None, consume it
            code_gen.emit("POP_TOP")
            return

        type_descr = self.container_type.type_descr
        type_descr += (self.slot_name,)
        code_gen.emit("STORE_FIELD", type_descr)


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
            typ = (
                self.klass.type_env.bool
                if arg_type.constant == TYPED_BOOL
                else self.klass.type_env.int.exact_type()
            )
            visitor.set_type(node, typ.instance)
        elif isinstance(arg_type, CDoubleInstance):
            visitor.set_type(
                node,
                self.klass.type_env.float.exact_type().instance,
            )
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
            visitor.visitExpectedType(
                arg,
                visitor.type_env.DYNAMIC,
                CALL_ARGUMENT_CANNOT_BE_PRIMITIVE,
            )

        visitor.set_type(node, type_ctx or visitor.type_env.int64.instance)
        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.get_type(node).emit_unbox(node.args[0], code_gen)


class CRangeIterator(Object[Class]):
    def bind_forloop_target(self, target: ast.expr, visitor: TypeBinder) -> None:
        if not isinstance(target, ast.Name):
            visitor.syntax_error(
                f"cannot unpack multiple values from {self.name} while iterating",
                target,
            )
        visitor.visit(target)

    def get_iter_type(self, node: ast.expr, visitor: TypeBinder) -> Value:
        return visitor.type_env.int64.instance

    def _loop_var_name(self, node: ast.For) -> str:
        target = node.target

        # enforced by bind_forloop_target above
        assert isinstance(target, ast.Name)
        return target.id

    def emit_forloop(self, node: ast.For, code_gen: Static38CodeGenerator) -> None:
        start = code_gen.newBlock("crange_forloop_start")
        anchor = code_gen.newBlock("crange_forloop_anchor")
        after = code_gen.newBlock("crange_forloop_after")

        loop_idx = self._loop_var_name(node)
        descr = ("__static__", "int64", "#")

        code_gen.set_lineno(node)
        code_gen.push_loop(FOR_LOOP, start, after)

        # Put the iteration limit and start value on the stack (a primitive)
        code_gen.visit(node.iter)

        # Store the start value as the loop index
        code_gen.emit("STORE_LOCAL", (loop_idx, descr))

        code_gen.nextBlock(start)
        code_gen.emit("DUP_TOP")
        code_gen.emit("LOAD_LOCAL", (loop_idx, descr))
        code_gen.emit("PRIMITIVE_COMPARE_OP", PRIM_OP_GT_INT)
        code_gen.emit("POP_JUMP_IF_ZERO", anchor)
        code_gen.visit(node.body)
        code_gen.emit("LOAD_LOCAL", (loop_idx, descr))
        code_gen.emit("PRIMITIVE_LOAD_CONST", (1, TYPED_INT64))
        code_gen.emit("PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        code_gen.emit("STORE_LOCAL", (loop_idx, descr))
        code_gen.emit("JUMP_ABSOLUTE", start)
        code_gen.nextBlock(anchor)
        code_gen.emit("POP_TOP")  # Pop limit
        code_gen.pop_loop()

        if node.orelse:
            code_gen.visit(node.orelse)
        code_gen.nextBlock(after)


class CRangeFunction(Object[Class]):
    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        nargs = len(node.args)
        if nargs != 1 and nargs != 2:
            visitor.syntax_error("crange() accepts only 1 or 2 parameters", node)
        if node.keywords:
            visitor.syntax_error("crange() takes no keyword arguments", node)

        if (
            visitor.current_loop is None
            or cast(ast.For, visitor.current_loop).iter != node
        ):
            visitor.syntax_error(
                "crange() must be used as an iterator in a for loop", node
            )

        for arg in node.args:
            visitor.visit(arg)
            arg_type = visitor.get_type(arg)
            if isinstance(arg_type, CIntInstance):
                typ = (
                    self.klass.type_env.bool
                    if arg_type.constant == TYPED_BOOL
                    else self.klass.type_env.int.exact_type()
                )
                visitor.set_type(node, typ.instance)
            else:
                visitor.syntax_error(
                    f"can't use crange with arg: {arg_type.name}", node
                )

        visitor.set_type(node, visitor.type_env.crange_iterator)
        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        # This performs two stack pushes:
        #   PUSH(LIMIT)
        #   PUSH(START_VALUE)
        if len(node.args) == 2:
            code_gen.visit(node.args[1])
            code_gen.visit(node.args[0])
        else:
            code_gen.visit(node.args[0])
            # When start value is unspecified, we start from 0
            code_gen.emit("PRIMITIVE_LOAD_CONST", (0, TYPED_INT64))


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
        visitor.visitExpectedType(
            arg, visitor.type_env.DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE
        )
        arg_type = visitor.get_type(arg)
        if not self.boxed and arg_type.get_fast_len_type() is None:
            visitor.syntax_error(f"bad argument type '{arg_type.name}' for clen()", arg)

        output_type = (
            self.klass.type_env.int.exact_type().instance
            if self.boxed
            else self.klass.type_env.int64.instance
        )

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
            node.args[0], visitor.type_env.DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE
        )
        for kw in node.keywords:
            visitor.visitExpectedType(
                kw.value, visitor.type_env.DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE
            )

        visitor.set_type(node, self.klass.type_env.list.exact_type().instance)
        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        super().emit_call(node, code_gen)
        code_gen.emit(
            "REFINE_TYPE",
            self.klass.type_env.list.exact_type().type_descr,
        )


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
        self,
        node: ast.AST,
        prev: Value,
        inst: Value,
        visitor: TypeBinder,
    ) -> None:
        self.node = node
        self.prev = prev
        self.inst = inst
        reverse = prev
        if isinstance(prev, UnionInstance):
            type_args = tuple(
                ta for ta in prev.klass.type_args if not inst.klass.can_assign_from(ta)
            )
            reverse = visitor.type_env.get_union(type_args).instance
        self.rev: Value = reverse
        self.tmp_idx: int = visitor.refined_field_index(self.access_path)

    def apply(
        self,
        type_state: TypeState,
        type_state_nodes: Optional[Dict[str, ast.AST]] = None,
    ) -> None:
        self._refine_type(type_state, type_state_nodes, self.inst)

    def undo(self, type_state: TypeState) -> None:
        self._refine_type(type_state, None, self.prev)

    def reverse(
        self,
        type_state: TypeState,
        type_state_nodes: Optional[Dict[str, ast.AST]] = None,
    ) -> None:
        self._refine_type(type_state, type_state_nodes, self.rev)

    def _refine_type(
        self,
        type_state: TypeState,
        type_state_nodes: Optional[Dict[str, ast.AST]],
        to: Value,
    ) -> None:
        # When accessing self.x.y, build an access path stack of the form ["self", "x", "y"].
        access_path = self.access_path
        if len(access_path) == 0:
            return None
        if type_state_nodes is not None:
            type_state_nodes[".".join(access_path)] = self.node
        if len(access_path) == 1:
            # We're just refining a local name.
            type_state.local_types[access_path[0]] = to
        else:
            assert len(access_path) == 2
            base, attr = access_path
            type_state.refined_fields.setdefault(base, {})[attr] = (
                to,
                self.tmp_idx,
                {self.node},
            )

    @cached_property
    def access_path(self) -> List[str]:
        return access_path(self.node)


class IsInstanceFunction(Object[Class]):
    def __init__(self, type_env: TypeEnvironment) -> None:
        super().__init__(type_env.function)

    @property
    def name(self) -> str:
        return "isinstance function"

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if node.keywords:
            visitor.syntax_error("isinstance() does not accept keyword arguments", node)
        for arg in node.args:
            visitor.visitExpectedType(
                arg, visitor.type_env.DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE
            )

        visitor.set_type(node, self.klass.type_env.bool.instance)
        if len(node.args) == 2:
            arg0 = node.args[0]
            if not visitor.is_refinable(arg0):
                return NO_EFFECT

            arg1 = node.args[1]
            klass_type: Optional[Class] = None
            if isinstance(arg1, ast.Tuple):
                types = tuple(visitor.get_type(el) for el in arg1.elts)
                if all(isinstance(t, Class) for t in types):
                    klass_type = visitor.type_env.get_union(
                        cast(Tuple[Class, ...], types)
                    )
            else:
                arg1_type = visitor.get_type(node.args[1])
                if arg1_type == visitor.type_env.DYNAMIC:
                    # if we have `isinstance(x, SomeUnknownClass)`, `x` should
                    # be refined to `dynamic`.
                    klass_type = visitor.type_env.DYNAMIC.klass
                elif isinstance(arg1_type, Class):
                    klass_type = arg1_type.inexact()

            if klass_type is not None:
                return IsInstanceEffect(
                    arg0,
                    visitor.get_type(arg0),
                    klass_type.inexact_type().instance,
                    visitor,
                )

        return NO_EFFECT


class IsSubclassFunction(Object[Class]):
    def __init__(self, type_env: TypeEnvironment) -> None:
        super().__init__(type_env.function)

    @property
    def name(self) -> str:
        return "issubclass function"

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if node.keywords:
            visitor.syntax_error("issubclass() does not accept keyword arguments", node)
        for arg in node.args:
            visitor.visitExpectedType(
                arg, visitor.type_env.DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE
            )
        visitor.set_type(node, visitor.type_env.bool.instance)
        return NO_EFFECT


class RevealTypeFunction(Object[Class]):
    def __init__(self, type_env: TypeEnvironment) -> None:
        super().__init__(type_env.function)

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
        msg = f"reveal_type({to_expr(arg)}): '{arg_type.name_with_exact}'"
        if isinstance(arg, ast.Name) and arg.id in visitor.decl_types:
            decl_type = visitor.decl_types[arg.id].type
            local_type = visitor.type_state.local_types[arg.id]
            msg += f", '{arg.id}' has declared type '{decl_type.name_with_exact}' and local type '{local_type.name_with_exact}'"
        visitor.syntax_error(msg, node)
        return NO_EFFECT


class NumClass(Class):
    def __init__(
        self,
        name: TypeName,
        type_env: TypeEnvironment,
        pytype: Optional[Type[object]] = None,
        is_exact: bool = False,
        literal_value: Optional[int] = None,
        is_final: bool = False,
    ) -> None:
        bases: List[Class] = [type_env.object]
        if literal_value is not None:
            is_exact = True
            bases = [type_env.int.exact_type()]
        instance = NumExactInstance(self) if is_exact else NumInstance(self)
        super().__init__(
            name,
            type_env,
            bases,
            instance,
            pytype=pytype,
            is_exact=is_exact,
            is_final=is_final,
        )
        self.literal_value = literal_value

    def is_subclass_of(self, src: Class) -> bool:
        if isinstance(src, NumClass) and src.literal_value is not None:
            return src.literal_value == self.literal_value
        return super().is_subclass_of(src)

    def _create_exact_type(self) -> Class:
        return type(self)(
            self.type_name,
            self.type_env,
            pytype=self.pytype,
            is_exact=True,
            literal_value=self.literal_value,
            is_final=self.is_final,
        )

    def emit_type_check(self, src: Class, code_gen: Static38CodeGenerator) -> None:
        if self.literal_value is None or src is not self.type_env.dynamic:
            return super().emit_type_check(src, code_gen)
        common_literal_emit_type_check(self.literal_value, "COMPARE_OP", "==", code_gen)


class NumInstance(Object[NumClass]):
    def is_truthy_literal(self) -> bool:
        return bool(self.klass.literal_value)

    def make_literal(self, literal_value: object, type_env: TypeEnvironment) -> Value:
        assert isinstance(literal_value, int)
        klass = NumClass(
            self.klass.type_name,
            self.klass.type_env,
            pytype=self.klass.pytype,
            literal_value=literal_value,
        )
        return klass.instance

    def bind_unaryop(
        self, node: ast.UnaryOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        if isinstance(node.op, (ast.USub, ast.Invert, ast.UAdd)):
            visitor.set_type(node, self)
        else:
            assert isinstance(node.op, ast.Not)
            visitor.set_type(node, self.klass.type_env.bool.instance)

    def exact(self) -> Value:
        if self.klass.pytype is int:
            return self.klass.type_env.int.exact_type().instance
        if self.klass.pytype is float:
            return self.klass.type_env.float.exact_type().instance
        if self.klass.pytype is complex:
            return self.klass.type_env.complex.exact_type().instance
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

    @property
    def name_with_exact(self) -> str:
        if self.klass.literal_value is not None:
            return f"Literal[{self.klass.literal_value}]"
        return super().name_with_exact

    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        ltype = visitor.get_type(node.left)
        rtype = visitor.get_type(node.right)
        type_env = self.klass.type_env
        int_exact = type_env.int.exact_type()
        if int_exact.can_assign_from(ltype.klass) and int_exact.can_assign_from(
            rtype.klass
        ):
            if isinstance(node.op, ast.Div):
                visitor.set_type(
                    node,
                    type_env.float.exact_type().instance,
                )
            else:
                visitor.set_type(
                    node,
                    int_exact.instance,
                )
            return True
        return False

    def exact(self) -> Value:
        return self

    def inexact(self) -> Value:
        if self.klass.pytype is int:
            return self.klass.type_env.int.instance
        if self.klass.pytype is float:
            return self.klass.type_env.float.instance
        if self.klass.pytype is complex:
            return self.klass.type_env.complex.instance
        return self


def parse_param(
    info: Dict[str, object],
    idx: int,
    type_env: TypeEnvironment,
) -> Parameter:
    name = info.get("name", "")
    assert isinstance(name, str)

    return Parameter(
        name,
        idx,
        ResolvedTypeRef(parse_type(info, type_env)),
        "default" in info,
        info.get("default"),
        ParamStyle.POSONLY,
    )


def parse_typed_signature(
    sig: Dict[str, object],
    klass: Optional[Class],
    type_env: TypeEnvironment,
) -> Tuple[List[Parameter], Class]:
    args = sig["args"]
    assert isinstance(args, list)
    if klass is not None:
        signature = [
            Parameter(
                "self", 0, ResolvedTypeRef(klass), False, None, ParamStyle.POSONLY
            )
        ]
    else:
        signature = []

    for idx, arg in enumerate(args):
        signature.append(parse_param(arg, idx + 1, type_env))
    return_info = sig["return"]
    assert isinstance(return_info, dict)
    return_type = parse_type(return_info, type_env)
    return signature, return_type


def parse_type(info: Dict[str, object], type_env: TypeEnvironment) -> Class:
    optional = info.get("optional", False)
    type = info.get("type")
    if type:
        # pyre-ignore[6]: type is not known to be a str statically.
        klass = type_env.name_to_type.get(type)
        if klass is None:
            raise NotImplementedError("unsupported type: " + str(type))
    else:
        type_param = info.get("type_param")
        assert isinstance(type_param, int)
        klass = GenericParameter("T" + str(type_param), type_param, type_env)

    if optional:
        return type_env.get_generic_type(type_env.optional, (klass,))

    return klass


def reflect_method_desc(
    obj: MethodDescriptorType | WrapperDescriptorType,
    klass: Class,
    type_env: TypeEnvironment,
) -> BuiltinMethodDescriptor:
    sig = getattr(obj, "__typed_signature__", None)
    if sig is not None:
        signature, return_type = parse_typed_signature(sig, klass, type_env)

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
    obj: BuiltinFunctionType,
    klass: Optional[Class],
    type_env: TypeEnvironment,
) -> BuiltinFunction:
    sig = getattr(obj, "__typed_signature__", None)
    if sig is not None:
        signature, return_type = parse_typed_signature(sig, None, type_env)
        method = BuiltinFunction(
            obj.__name__,
            obj.__module__,
            klass,
            type_env,
            signature,
            ResolvedTypeRef(return_type),
        )
    else:
        if obj.__name__ == "__new__" and klass is not None:
            method = BuiltinNewFunction(obj.__name__, obj.__module__, klass, type_env)
        else:
            method = BuiltinFunction(obj.__name__, obj.__module__, klass, type_env)
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
        code_gen.emit("PRIMITIVE_BOX", TYPED_INT64)


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
    else:
        fast_len_oparg = FAST_LEN_LIST
    descr = ("__static__", "int64", "#")
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
            code_gen.emit("PRIMITIVE_BOX", TYPED_INT64)
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


def common_literal_emit_type_check(
    literal_value: object,
    comp_opname: str,
    comp_opcode: object,
    code_gen: Static38CodeGenerator,
) -> None:
    code_gen.emit("DUP_TOP")
    code_gen.emit("LOAD_CONST", literal_value)
    code_gen.emit(comp_opname, comp_opcode)
    end = code_gen.newBlock()
    code_gen.emit("POP_JUMP_IF_TRUE", end)
    code_gen.nextBlock()
    code_gen.emit("LOAD_GLOBAL", "TypeError")
    code_gen.emit("ROT_TWO")
    code_gen.emit("LOAD_CONST", f"expected {literal_value}, got ")
    code_gen.emit("ROT_TWO")
    code_gen.emit("FORMAT_VALUE")
    code_gen.emit("BUILD_STRING", 2)
    code_gen.emit("CALL_FUNCTION", 1)
    code_gen.emit("RAISE_VARARGS", 1)
    code_gen.nextBlock(end)


class TupleClass(Class):
    def __init__(
        self,
        type_env: TypeEnvironment,
        is_exact: bool = False,
    ) -> None:
        instance = TupleExactInstance(self) if is_exact else TupleInstance(self)
        super().__init__(
            type_name=TypeName("builtins", "tuple"),
            type_env=type_env,
            instance=instance,
            is_exact=is_exact,
            pytype=tuple,
        )
        self.members["__new__"] = BuiltinNewFunction(
            "__new__",
            "builtins",
            self,
            self.type_env,
            [
                Parameter(
                    "cls",
                    0,
                    ResolvedTypeRef(self.type_env.type),
                    False,
                    None,
                    ParamStyle.POSONLY,
                ),
                Parameter(
                    "x",
                    1,
                    ResolvedTypeRef(self.type_env.object),
                    True,
                    (),
                    ParamStyle.POSONLY,
                ),
            ],
            ResolvedTypeRef(self),
        )

    def _create_exact_type(self) -> Class:
        return type(self)(self.type_env, is_exact=True)


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
        return self.klass.type_env.tuple.exact_type().instance

    def inexact(self) -> Value:
        return self.klass.type_env.tuple.instance


class TupleExactInstance(TupleInstance):
    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        rtype = visitor.get_type(node.right).klass
        if isinstance(node.op, ast.Mult) and (
            self.klass.type_env.int.can_assign_from(rtype)
            or rtype in self.klass.type_env.signed_cint_types
        ):
            visitor.set_type(
                node,
                self.klass.type_env.tuple.exact_type().instance,
            )
            return True
        return super().bind_binop(node, visitor, type_ctx)

    def bind_reverse_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        ltype = visitor.get_type(node.left).klass
        if isinstance(node.op, ast.Mult) and (
            self.klass.type_env.int.can_assign_from(ltype)
            or ltype in self.klass.type_env.signed_cint_types
        ):
            visitor.set_type(
                node,
                self.klass.type_env.tuple.exact_type().instance,
            )
            return True
        return super().bind_reverse_binop(node, visitor, type_ctx)

    def emit_forloop(self, node: ast.For, code_gen: Static38CodeGenerator) -> None:
        if not isinstance(node.target, ast.Name):
            # We don't yet support `for a, b in my_tuple: ...`
            return super().emit_forloop(node, code_gen)

        return common_sequence_emit_forloop(node, code_gen, SEQ_TUPLE)


class FrozenSetClass(Class):
    def __init__(
        self,
        type_env: TypeEnvironment,
        is_exact: bool = False,
    ) -> None:
        super().__init__(
            type_name=TypeName("builtins", "frozenset"),
            type_env=type_env,
            is_exact=is_exact,
            pytype=frozenset,
        )

    def _create_exact_type(self) -> Class:
        return type(self)(self.type_env, is_exact=True)


class SuperClass(Class):
    def __init__(self, type_env: TypeEnvironment, is_exact: bool = True) -> None:
        super().__init__(
            type_name=TypeName("builtins", "super"),
            type_env=type_env,
            instance=SuperInstance(self),
            is_exact=is_exact,
            pytype=super,
        )

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        super().bind_call(node, visitor, type_ctx)
        if len(node.args):
            visitor.set_type(node, visitor.type_env.DYNAMIC)
        else:
            visitor.set_type(node, self.instance)
        return NO_EFFECT


class SuperInstance(Object[SuperClass]):
    def bind_attr(
        self, node: ast.Attribute, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        klass = visitor.maybe_get_current_enclosing_class()
        if klass is not None and not isinstance(klass, DynamicClass):
            for base in klass.mro[1:]:
                if isinstance(base, DynamicClass):
                    break
                member = base.members.get(node.attr)
                if isinstance(member, Function):
                    # Injecting an AST node here to represent the method that
                    # we are puling in from the parent class to replace the
                    # call to super
                    load = ast.Name(member.args[0].name, ast.Load())
                    copy_location(load, node.value)
                    method_type = SuperMethodType(base, load, member)
                    visitor.set_type(node, method_type)
                    return
        super().bind_attr(node, visitor, type_ctx)
        visitor.set_type(node, visitor.type_env.DYNAMIC)

    def emit_attr(self, node: ast.Attribute, code_gen: Static38CodeGenerator) -> None:
        if isinstance(node.ctx, ast.Load) and code_gen._is_super_call(node.value):
            code_gen.emit("LOAD_GLOBAL", "super")
            load_arg = code_gen._emit_args_for_super(node.value, node.attr)
            code_gen.emit("LOAD_ATTR_SUPER", load_arg)
            return
        super().emit_attr(node, code_gen)


class SuperMethodType(MethodType):
    def __init__(
        self,
        bound_type: Class,
        target: ast.expr,
        function: Function,
    ) -> None:
        super().__init__(bound_type.type_name, function.node, target, function)
        self.bound_type: Class = bound_type.exact_type()

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        # In order to statically set the type, we must create a specialized
        # MethodType such that we can explicitly set the type to an exact type
        # instance after the general bind_call logic
        result = super().bind_call(node, visitor, type_ctx)
        visitor.set_type(self.target, self.bound_type.instance)
        return result


class SetClass(Class):
    def __init__(
        self,
        type_env: TypeEnvironment,
        is_exact: bool = False,
    ) -> None:
        super().__init__(
            type_name=TypeName("builtins", "set"),
            type_env=type_env,
            instance=SetInstance(self),
            is_exact=is_exact,
            pytype=set,
        )

    def _create_exact_type(self) -> Class:
        return type(self)(self.type_env, is_exact=True)


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
            code_gen.emit("PRIMITIVE_BOX", TYPED_INT64)

    def emit_jumpif(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.visit(test)
        code_gen.emit("FAST_LEN", self.get_fast_len_type())
        code_gen.emit("POP_JUMP_IF_NONZERO" if is_if_true else "POP_JUMP_IF_ZERO", next)

    def exact(self) -> Value:
        return self.klass.type_env.set.exact_type().instance

    def inexact(self) -> Value:
        return self.klass.type_env.set.instance


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
        if code_gen.compiler.type_env.tuple.can_assign_from(seq_type):
            oparg = SEQ_TUPLE
        elif code_gen.compiler.type_env.list.can_assign_from(seq_type):
            oparg = SEQ_LIST
        if oparg is None:
            continue
        if num_type in code_gen.compiler.type_env.signed_cint_types:
            oparg |= SEQ_REPEAT_PRIMITIVE_NUM
        elif not code_gen.compiler.type_env.int.can_assign_from(num_type):
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
        visitor: GenericVisitor[object],
    ) -> Optional[Value]:
        if inst is None:
            return self
        else:
            return ListAppendBuiltinMethod(self, node.value)


class ListAppendBuiltinMethod(BuiltinMethod):
    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if len(node.args) == 1 and not node.keywords:
            code_gen.visit(self.target)
            code_gen.emit(
                "REFINE_TYPE", code_gen.get_type(self.target).klass.type_descr
            )
            code_gen.visit(node.args[0])
            code_gen.emit("LIST_APPEND", 1)
            return

        return super().emit_call(node, code_gen)


class ListClass(Class):
    def __init__(
        self,
        type_env: TypeEnvironment,
        is_exact: bool = False,
    ) -> None:
        instance = ListExactInstance(self) if is_exact else ListInstance(self)
        super().__init__(
            type_name=TypeName("builtins", "list"),
            type_env=type_env,
            instance=instance,
            is_exact=is_exact,
            pytype=list,
        )

    def _create_exact_type(self) -> Class:
        return type(self)(self.type_env, is_exact=True)

    def make_type_dict(self) -> None:
        super().make_type_dict()
        if self.is_exact:
            self.members["append"] = ListAppendMethod("append", self)
        # list inherits object.__new__
        del self.members["__new__"]
        self.members["__init__"] = BuiltinMethodDescriptor(
            "__init__",
            self,
            [
                Parameter(
                    "self", 0, ResolvedTypeRef(self), False, None, ParamStyle.POSONLY
                ),
                # Ideally we would mark this as Optional and allow calling without
                # providing the argument...
                Parameter(
                    "iterable",
                    1,
                    ResolvedTypeRef(self.type_env.object),
                    True,
                    (),
                    ParamStyle.POSONLY,
                ),
            ],
            ResolvedTypeRef(self.type_env.none),
        )


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
        if type.klass not in visitor.type_env.signed_cint_types:
            super().bind_subscr(node, type, visitor)
        visitor.set_type(node, visitor.type_env.DYNAMIC)

    def emit_load_subscr(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        index_type = code_gen.get_type(node.slice).klass
        env = self.klass.type_env
        if self.klass.is_exact and env.int.can_assign_from(index_type):
            code_gen.emit("PRIMITIVE_UNBOX", TYPED_INT64)
        elif index_type not in env.signed_cint_types:
            return super().emit_load_subscr(node, code_gen)
        code_gen.emit("SEQUENCE_GET", self.get_subscr_type())

    def emit_store_subscr(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        index_type = code_gen.get_type(node.slice).klass
        env = self.klass.type_env
        if self.klass.is_exact and env.int.can_assign_from(index_type):
            code_gen.emit("PRIMITIVE_UNBOX", TYPED_INT64)
        elif index_type not in env.signed_cint_types:
            return super().emit_store_subscr(node, code_gen)
        code_gen.emit("SEQUENCE_SET", self.get_subscr_type())

    def emit_delete_subscr(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        if (
            code_gen.get_type(node.slice).klass
            not in self.klass.type_env.signed_cint_types
        ):
            return super().emit_delete_subscr(node, code_gen)
        code_gen.emit("LIST_DEL", self.get_subscr_type())

    def emit_binop(self, node: ast.BinOp, code_gen: Static38CodeGenerator) -> None:
        if maybe_emit_sequence_repeat(node, code_gen):
            return
        code_gen.defaultVisit(node)

    def exact(self) -> Value:
        return self.klass.type_env.list.exact_type().instance

    def inexact(self) -> Value:
        return self.klass.type_env.list.instance


class ListExactInstance(ListInstance):
    def get_subscr_type(self) -> int:
        return SEQ_LIST

    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        rtype = visitor.get_type(node.right).klass
        if isinstance(node.op, ast.Mult) and (
            self.klass.type_env.int.can_assign_from(rtype)
            or rtype in self.klass.type_env.signed_cint_types
        ):
            visitor.set_type(
                node,
                self.klass.type_env.list.exact_type().instance,
            )
            return True
        return super().bind_binop(node, visitor, type_ctx)

    def bind_reverse_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        ltype = visitor.get_type(node.left).klass
        if isinstance(node.op, ast.Mult) and (
            self.klass.type_env.int.can_assign_from(ltype)
            or ltype in self.klass.type_env.signed_cint_types
        ):
            visitor.set_type(
                node,
                self.klass.type_env.list.exact_type().instance,
            )
            return True
        return super().bind_reverse_binop(node, visitor, type_ctx)

    def emit_forloop(self, node: ast.For, code_gen: Static38CodeGenerator) -> None:
        if not isinstance(node.target, ast.Name):
            # We don't yet support `for a, b in my_list: ...`
            return super().emit_forloop(node, code_gen)

        return common_sequence_emit_forloop(node, code_gen, SEQ_LIST)


class StrClass(Class):
    def __init__(
        self,
        type_env: TypeEnvironment,
        is_exact: bool = False,
    ) -> None:
        super().__init__(
            type_name=TypeName("builtins", "str"),
            type_env=type_env,
            instance=StrInstance(self),
            is_exact=is_exact,
            pytype=str,
        )

    def reflected_method_types(self, type_env: TypeEnvironment) -> Dict[str, Class]:
        str_exact = type_env.str.exact_type()
        return {
            "isdigit": type_env.bool,
            "join": str_exact,
            "lower": str_exact,
            "upper": str_exact,
        }

    def _create_exact_type(self) -> Class:
        return type(self)(self.type_env, is_exact=True)


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
        return self.klass.type_env.str.exact_type().instance

    def inexact(self) -> Value:
        return self.klass.type_env.str.instance


class DictClass(Class):
    def __init__(
        self,
        type_env: TypeEnvironment,
        is_exact: bool = False,
    ) -> None:
        super().__init__(
            type_name=TypeName("builtins", "dict"),
            type_env=type_env,
            instance=DictInstance(self),
            is_exact=is_exact,
            pytype=dict,
        )

    def _create_exact_type(self) -> Class:
        return type(self)(self.type_env, is_exact=True)


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
            code_gen.emit("PRIMITIVE_BOX", TYPED_INT64)

    def emit_jumpif(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.visit(test)
        code_gen.emit("FAST_LEN", self.get_fast_len_type())
        code_gen.emit("POP_JUMP_IF_NONZERO" if is_if_true else "POP_JUMP_IF_ZERO", next)

    def exact(self) -> Value:
        return self.klass.type_env.dict.exact_type().instance

    def inexact(self) -> Value:
        return self.klass.type_env.dict.instance


class BoolClass(Class):
    def __init__(
        self, type_env: TypeEnvironment, literal_value: bool | None = None
    ) -> None:
        bases: List[Class] = [type_env.int]
        if literal_value is not None:
            bases = [type_env.bool]
        super().__init__(
            TypeName("builtins", "bool"),
            type_env,
            bases,
            instance=BoolInstance(self),
            pytype=bool,
            is_exact=True,
            is_final=True,
        )
        self.literal_value = literal_value

    def make_type_dict(self) -> None:
        super().make_type_dict()
        self.members["__new__"] = BuiltinNewFunction(
            "__new__",
            "builtins",
            self,
            self.type_env,
            [
                Parameter(
                    "cls",
                    0,
                    ResolvedTypeRef(self.type_env.type),
                    False,
                    None,
                    ParamStyle.POSONLY,
                ),
                Parameter(
                    "x",
                    1,
                    ResolvedTypeRef(self.type_env.object),
                    True,
                    False,
                    ParamStyle.POSONLY,
                ),
            ],
            ResolvedTypeRef(self),
        )

    def is_subclass_of(self, src: Class) -> bool:
        if isinstance(src, BoolClass) and src.literal_value is not None:
            return src.literal_value == self.literal_value
        return super().is_subclass_of(src)

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if len(node.args) == 1 and not node.keywords:
            arg = node.args[0]
            visitor.visit(arg)
            arg_type = visitor.get_type(arg)
            if isinstance(arg_type, CIntInstance) and arg_type.constant == TYPED_BOOL:
                visitor.set_type(node, self.type_env.bool.instance)
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

    def emit_type_check(self, src: Class, code_gen: Static38CodeGenerator) -> None:
        if self.literal_value is None or src is not self.type_env.dynamic:
            return super().emit_type_check(src, code_gen)
        common_literal_emit_type_check(self.literal_value, "IS_OP", 0, code_gen)


class BoolInstance(Object[BoolClass]):
    def is_truthy_literal(self) -> bool:
        return bool(self.klass.literal_value)

    @property
    def name(self) -> str:
        if self.klass.literal_value is not None:
            return f"Literal[{self.klass.literal_value}]"
        return super().name

    def make_literal(self, literal_value: object, type_env: TypeEnvironment) -> Value:
        assert isinstance(literal_value, bool)
        klass = BoolClass(self.klass.type_env, literal_value=literal_value)
        return klass.instance


class AnnotatedType(Class):
    def resolve_subscr(
        self,
        node: ast.Subscript,
        type: Value,
        visitor: AnnotationVisitor,
    ) -> Optional[Value]:
        slice = node.slice

        if isinstance(slice, ast.Slice):
            visitor.syntax_error("can't slice generic types", node)
            return visitor.type_env.DYNAMIC

        if not isinstance(slice, ast.Tuple) or len(slice.elts) <= 1:
            visitor.syntax_error(
                "Annotated types must be parametrized by at least one annotation.", node
            )
            return None
        actual_type, *annotations = slice.elts
        actual_type = visitor.resolve_annotation(actual_type)
        if actual_type is None:
            return visitor.type_env.DYNAMIC
        if (
            len(annotations) == 1
            and isinstance(annotations[0], ast.Constant)
            and cast(ast.Constant, annotations[0]).value == "Exact"
            and isinstance(actual_type, Class)
        ):
            return self.type_env.exact.make_generic_type((actual_type,))
        return actual_type


class LiteralType(Class):
    def resolve_subscr(
        self,
        node: ast.Subscript,
        type: Value,
        visitor: AnnotationVisitor,
    ) -> Optional[Value]:
        slice = node.slice

        if isinstance(slice, ast.Slice):
            visitor.syntax_error("can't slice generic types", node)
            return visitor.type_env.DYNAMIC

        if isinstance(slice, ast.Tuple):
            # TODO support multi-value literal types
            return visitor.type_env.DYNAMIC
        if not isinstance(slice, ast.Constant):
            visitor.syntax_error("Literal must be parametrized by a constant", node)
            return visitor.type_env.DYNAMIC
        literal_value = slice.value
        if isinstance(literal_value, bool):
            return self.type_env.get_literal_type(
                self.type_env.bool.instance, literal_value
            ).klass
        elif isinstance(literal_value, int):
            return self.type_env.get_literal_type(
                self.type_env.int.instance, literal_value
            ).klass
        # TODO support more literal types
        return visitor.type_env.DYNAMIC


class TypeWrapper(GenericClass):
    def unwrap(self) -> Class:
        return self.type_args[0]


class FinalClass(TypeWrapper):
    pass


class ClassVar(TypeWrapper):
    pass


class InitVar(TypeWrapper):
    pass


class ExactClass(TypeWrapper):
    """This type wrapper indicates a user-specified exact type annotation. Normally, we
    relax exact types in annotations to be inexact to support passing in subclasses, but
    this class supports the case where a user does *not* want subclasses to be allowed.
    """

    pass


class UnionTypeName(GenericTypeName):
    def __init__(
        self,
        module: str,
        name: str,
        args: Tuple[Class, ...],
        type_env: TypeEnvironment,
    ) -> None:
        super().__init__(module, name, args)
        self.type_env = type_env

    @property
    def opt_type(self) -> Optional[Class]:
        """If we're an Optional (i.e. Union[T, None]), return T, otherwise None."""
        # Assumes well-formed union (no duplicate elements, >1 element)
        opt_type = None
        if len(self.args) == 2:
            if self.args[0] is self.type_env.none:
                opt_type = self.args[1]
            elif self.args[1] is self.type_env.none:
                opt_type = self.args[0]
        return opt_type

    @property
    def float_type(self) -> Optional[Class]:
        """Collapse `float | int` and `int | float` to `float`. Otherwise, return None."""
        if len(self.args) == 2:
            if (
                self.args[0] is self.type_env.float
                and self.args[1] is self.type_env.int
            ):
                return self.args[0]
            if (
                self.args[1] is self.type_env.float
                and self.args[0] is self.type_env.int
            ):
                return self.args[1]
        return None

    @property
    def type_descr(self) -> TypeDescr:
        opt_type = self.opt_type
        if opt_type is not None:
            return opt_type.type_descr + ("?",)
        # the runtime does not support unions beyond optional, so just fall back
        # to dynamic for runtime purposes
        return self.type_env.dynamic.type_descr

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
        type_env: TypeEnvironment,
        type_name: Optional[UnionTypeName] = None,
        type_def: Optional[GenericClass] = None,
        instance_type: Optional[Type[Object[Class]]] = None,
        is_instantiated: bool = False,
    ) -> None:
        instance_type = instance_type or UnionInstance
        super().__init__(
            type_name or UnionTypeName("typing", "Union", (), type_env),
            type_env,
            bases=[],
            instance=instance_type(self),
            type_def=type_def,
        )
        self.is_instantiated = is_instantiated

    @property
    def opt_type(self) -> Optional[Class]:
        return self.type_name.opt_type

    def exact_type(self) -> Class:
        if self.is_instantiated:
            return self.type_env.get_union(
                tuple(a.exact_type() for a in self.type_args)
            )
        return self

    def inexact_type(self) -> Class:
        if self.is_instantiated:
            return self.type_env.get_union(
                tuple(a.inexact_type() for a in self.type_args)
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
    ) -> Class:
        type_args = self._simplify_args(index)
        if len(type_args) == 1 and not type_args[0].is_generic_parameter:
            return type_args[0]
        type_name = UnionTypeName(
            self.type_name.module, self.type_name.name, type_args, self.type_env
        )
        if any(isinstance(a, CType) for a in type_args):
            raise TypedSyntaxError(
                f"invalid union type {type_name.friendly_name}; unions cannot include primitive types"
            )
        ThisUnionType = type(self)
        if type_name.opt_type is not None:
            ThisUnionType = OptionalType
        return ThisUnionType(
            self.type_env, type_name, type_def=self, is_instantiated=True
        )

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
    def nonliteral(self) -> Value:
        return self.klass.type_env.get_union(
            tuple(el.instance.nonliteral().klass for el in self.klass.type_args)
        ).instance

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

        union = visitor.type_env.get_union(tuple(result_types))
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
        type_env: TypeEnvironment,
        type_name: Optional[UnionTypeName] = None,
        type_def: Optional[GenericClass] = None,
        is_instantiated: bool = False,
    ) -> None:
        super().__init__(
            type_env,
            type_name
            or UnionTypeName(
                "typing",
                "Optional",
                (GenericParameter("T", 0, type_env),),
                type_env,
            ),
            type_def=type_def,
            instance_type=OptionalInstance,
            is_instantiated=is_instantiated,
        )

    @property
    def opt_type(self) -> Class:
        opt_type = self.type_name.opt_type
        if opt_type is None:
            params = ", ".join(t.name for t in self.type_args)
            raise TypeError(f"OptionalType has invalid type parameters {params}")
        return opt_type

    def make_generic_type(
        self,
        index: Tuple[Class, ...],
    ) -> Class:
        assert len(index) == 1
        if not index[0].is_generic_parameter:
            # Optional[T] is syntactic sugar for Union[T, None]
            index = index + (self.type_env.none,)
        return super().make_generic_type(index)


class OptionalInstance(UnionInstance):
    """Only exists for typing purposes (so we know .klass is OptionalType)."""

    klass: OptionalType


class ArrayInstance(Object["ArrayClass"]):
    def _seq_type(self) -> int:
        idx = self.klass.index
        if not isinstance(idx, CIntType):
            # should never happen
            raise SyntaxError(f"Invalid Array type: {idx}")
        if idx.size != 3 or not idx.signed:
            raise SyntaxError(f"Only int64 Arrays are currently supported")
        return SEQ_ARRAY_INT64

    def get_iter_type(self, node: ast.expr, visitor: TypeBinder) -> Value:
        return self.klass.index.instance

    def bind_subscr(
        self,
        node: ast.Subscript,
        type: Value,
        visitor: TypeBinder,
        type_ctx: Optional[Class] = None,
    ) -> None:
        if type == self.klass.type_env.slice.instance:
            visitor.syntax_error("Static arrays cannot be sliced", node)

        visitor.set_type(node, self.klass.index.instance)

    def _supported_index(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> bool:
        index_type = code_gen.get_type(node.slice)
        return self.klass.type_env.int.can_assign_from(index_type.klass) or isinstance(
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
            code_gen.emit("PRIMITIVE_UNBOX", TYPED_INT64)

    def emit_load_subscr(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        if self._supported_index(node, code_gen):
            self._maybe_unbox_index(node, code_gen)
            code_gen.emit("SEQUENCE_GET", self._seq_type())
        else:
            super().emit_load_subscr(node, code_gen)
            if code_gen.get_type(node.slice).klass != self.klass.type_env.slice:
                # Falling back to BINARY_SUBSCR here, so we need to unbox the output
                code_gen.emit("REFINE_TYPE", self.klass.index.boxed.type_descr)
                code_gen.emit("PRIMITIVE_UNBOX", TYPED_INT64)

    def emit_store_subscr(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        if self._supported_index(node, code_gen):
            self._maybe_unbox_index(node, code_gen)
            code_gen.emit("SEQUENCE_SET", self._seq_type())
        else:
            if code_gen.get_type(node.slice).klass != self.klass.type_env.slice:
                # Falling back to STORE_SUBSCR here, so need to box the value first
                code_gen.emit("ROT_THREE")
                code_gen.emit("ROT_THREE")
                code_gen.emit("PRIMITIVE_BOX", TYPED_INT64)
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
            code_gen.emit("PRIMITIVE_BOX", TYPED_INT64)

    def bind_forloop_target(self, target: ast.expr, visitor: TypeBinder) -> None:
        if not isinstance(target, ast.Name):
            visitor.syntax_error(
                f"cannot unpack multiple values from {self.name} while iterating",
                target,
            )
        visitor.visit(target)

    def emit_forloop(self, node: ast.For, code_gen: Static38CodeGenerator) -> None:
        # guaranteed by type-binder
        assert isinstance(node.target, ast.Name)
        return common_sequence_emit_forloop(node, code_gen, self._seq_type())


class ArrayClass(GenericClass):
    def __init__(
        self,
        type_name: GenericTypeName,
        type_env: TypeEnvironment,
        bases: Optional[List[Class]] = None,
        instance: Optional[Object[Class]] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        type_def: Optional[GenericClass] = None,
        is_exact: bool = False,
        pytype: Optional[Type[object]] = None,
        is_final: bool = False,
    ) -> None:
        default_bases: List[Class] = [type_env.object]
        default_instance: Object[Class] = ArrayInstance(self)
        super().__init__(
            type_name,
            type_env,
            bases or default_bases,
            instance or default_instance,
            klass,
            members,
            type_def,
            is_exact,
            pytype,
            is_final,
        )
        self.members["__new__"] = BuiltinNewFunction(
            "__new__",
            "__static__",
            self,
            self.type_env,
            [
                Parameter(
                    "cls",
                    0,
                    ResolvedTypeRef(self.type_env.type),
                    False,
                    None,
                    ParamStyle.POSONLY,
                ),
                Parameter(
                    "size",
                    1,
                    ResolvedTypeRef(self.type_env.int),
                    True,
                    (),
                    ParamStyle.POSONLY,
                ),
            ],
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
    ) -> Class:
        for tp in index:
            if tp not in self.type_env.allowed_array_types:
                raise TypedSyntaxError(
                    f"Invalid {self.gen_name.name} element type: {tp.instance.name}"
                )
        return super().make_generic_type(index)


class CheckedDict(GenericClass):
    def __init__(
        self,
        type_name: GenericTypeName,
        type_env: TypeEnvironment,
        bases: Optional[List[Class]] = None,
        instance: Optional[Object[Class]] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        type_def: Optional[GenericClass] = None,
        is_exact: bool = False,
        pytype: Optional[Type[object]] = None,
        is_final: bool = True,
    ) -> None:
        if instance is None:
            instance = CheckedDictInstance(self)
        super().__init__(
            type_name,
            type_env,
            bases,
            instance,
            klass,
            members,
            type_def,
            is_exact,
            pytype,
            is_final,
        )
        self.members["__init__"] = self.init_func = BuiltinFunction(
            "__init__",
            "builtins",
            self,
            self.type_env,
            [
                Parameter(
                    "cls",
                    0,
                    ResolvedTypeRef(self.type_env.type),
                    False,
                    None,
                    ParamStyle.POSONLY,
                ),
                Parameter(
                    "x",
                    1,
                    ResolvedTypeRef(self.type_env.object),
                    True,
                    (),
                    ParamStyle.POSONLY,
                ),
            ],
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
            code_gen.emit("PRIMITIVE_BOX", TYPED_INT64)

    def emit_jumpif(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.visit(test)
        code_gen.emit("FAST_LEN", self.get_fast_len_type())
        code_gen.emit("POP_JUMP_IF_NONZERO" if is_if_true else "POP_JUMP_IF_ZERO", next)


class CheckedList(GenericClass):
    def __init__(
        self,
        type_name: GenericTypeName,
        type_env: TypeEnvironment,
        bases: Optional[List[Class]] = None,
        instance: Optional[Object[Class]] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        type_def: Optional[GenericClass] = None,
        is_exact: bool = False,
        pytype: Optional[Type[object]] = None,
        is_final: bool = True,
    ) -> None:
        if instance is None:
            instance = CheckedListInstance(self)
        super().__init__(
            type_name,
            type_env,
            bases,
            instance,
            klass,
            members,
            type_def,
            is_exact,
            pytype,
            is_final,
        )
        self.members["__init__"] = self.init_func = BuiltinFunction(
            "__init__",
            "builtins",
            self,
            self.type_env,
            [
                Parameter(
                    "cls",
                    0,
                    ResolvedTypeRef(self.type_env.type),
                    False,
                    None,
                    ParamStyle.POSONLY,
                ),
                Parameter(
                    "x",
                    1,
                    ResolvedTypeRef(self.type_env.object),
                    True,
                    (),
                    ParamStyle.POSONLY,
                ),
            ],
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
        if type == self.klass.type_env.slice.instance:
            visitor.set_type(node, self)
        else:
            if type.klass not in self.klass.type_env.signed_cint_types:
                visitor.visitExpectedType(
                    node.slice, self.klass.type_env.int.instance, blame=node
                )
            visitor.set_type(node, self.elem_type)

    def get_iter_type(self, node: ast.expr, visitor: TypeBinder) -> Value:
        return self.elem_type

    def emit_load_subscr(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        # From slice
        if code_gen.get_type(node) == self:
            return super().emit_load_subscr(node, code_gen)

        index_is_ctype = (
            code_gen.get_type(node.slice).klass in self.klass.type_env.signed_cint_types
        )

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

        if index_type in self.klass.type_env.signed_cint_types:
            # TODO add CheckedList to SEQUENCE_SET so we can emit that instead
            # of having to box the index here
            code_gen.emit("PRIMITIVE_BOX", index_type.instance.as_oparg())

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
            code_gen.emit("PRIMITIVE_BOX", TYPED_INT64)

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
            visitor.visitExpectedType(
                arg, visitor.type_env.DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE
            )

        cast_type = visitor.module.resolve_annotation(node.args[0])
        if cast_type is None:
            visitor.syntax_error("cast to unknown type", node)
            cast_type = self.klass.type_env.dynamic

        visitor.set_type(node, cast_type.instance)
        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(node.args[1])
        code_gen.emit("CAST", code_gen.get_type(node).klass.type_descr)


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
        return self.klass.qualname

    @property
    def name_with_exact(self) -> str:
        return self.klass.instance_name_with_exact

    def binop_error(self, left: str, right: str, op: ast.operator) -> str:
        return f"cannot {self._op_name[type(op)]} {left} and {right}"

    def bind_reverse_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        visitor.visitExpectedType(
            node.left, self, self.binop_error("{}", "{}", node.op)
        )
        if isinstance(node.op, ast.Pow):
            visitor.set_type(node, self.klass.type_env.double.instance)
        else:
            visitor.set_type(node, self)
        return True

    def get_op_id(self, op: AST) -> int:
        raise NotImplementedError("Must be implemented in the subclass")

    def emit_binop(self, node: ast.BinOp, code_gen: Static38CodeGenerator) -> None:
        code_gen.set_lineno(node)
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

    def emit_init(self, node: ast.Name, code_gen: Static38CodeGenerator) -> None:
        raise NotImplementedError()


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

    @property
    def name_with_exact(self) -> str:
        return self.name

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

    def make_literal(self, literal_value: object, type_env: TypeEnvironment) -> Value:
        assert isinstance(literal_value, int)
        return CIntType(
            self.klass.constant, self.klass.type_env, literal_value=literal_value
        ).instance

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
                types = (
                    self.klass.type_env.signed_cint_types
                    if self.signed
                    else self.klass.type_env.unsigned_cint_types
                )
                return types[size].instance
            else:
                new_size = max(
                    self.size if self.signed else self.size + 1,
                    other.size if other.signed else other.size + 1,
                )

                if new_size <= TYPED_INT_64BIT:
                    # signs don't match, but we can promote to the next highest data type
                    return self.klass.type_env.signed_cint_types[new_size].instance

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
            visitor.set_type(op, self.klass.type_env.cbool.instance)
            visitor.set_type(node, self.klass.type_env.cbool.instance)
            return True

        compare_type = self.validate_mixed_math(other)
        if compare_type is None:
            visitor.syntax_error(
                f"can't compare {self.name} to {visitor.get_type(right).name}", node
            )
            compare_type = visitor.type_env.DYNAMIC

        visitor.set_type(op, compare_type)
        visitor.set_type(node, self.klass.type_env.cbool.instance)
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
        visitor.set_type(node, self.klass.type_env.cbool.instance)
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
            node_type = visitor.type_env.get_literal_type(self, node.value)
        elif type(node.value) is bool and self is self.klass.type_env.cbool.instance:
            assert self is self.klass.type_env.cbool.instance
            node_type = self
        else:
            node_type = visitor.type_env.constant_types[type(node.value)]

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
            if (
                rinst.klass == self.klass.type_env.list.exact_type()
                or rinst.klass == self.klass.type_env.tuple.exact_type()
            ):
                visitor.set_type(node, rinst.klass.instance)
                return True

            visitor.visit(node.right, type_ctx or visitor.type_env.int64.instance)

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
                type_ctx = visitor.type_env.DYNAMIC
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
            visitor.set_type(node, self.klass.type_env.double.instance)
        else:
            visitor.set_type(node, type_ctx)
        return True

    def emit_box(self, node: expr, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(node)
        type = code_gen.get_type(node)
        if isinstance(type, CIntInstance):
            code_gen.emit("PRIMITIVE_BOX", self.as_oparg())
        else:
            raise RuntimeError("unsupported box type: " + type.name)

    def emit_unbox(self, node: expr, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(node)
        ty = code_gen.get_type(node)
        target_ty = (
            self.klass.type_env.bool
            if self.klass is self.klass.type_env.cbool
            else self.klass.type_env.int
        )
        if target_ty.can_assign_from(ty.klass):
            code_gen.emit("REFINE_TYPE", ty.klass.type_descr)
        else:
            code_gen.emit("CAST", target_ty.type_descr)
        code_gen.emit("PRIMITIVE_UNBOX", self.as_oparg())

    def bind_unaryop(
        self, node: ast.UnaryOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        if isinstance(node.op, (ast.USub, ast.Invert, ast.UAdd)):
            visitor.set_type(node, self)
        else:
            assert isinstance(node.op, ast.Not)
            visitor.set_type(node, self.klass.type_env.cbool.instance)

    def emit_unaryop(self, node: ast.UnaryOp, code_gen: Static38CodeGenerator) -> None:
        code_gen.set_lineno(node)
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

    def emit_init(self, node: ast.Name, code_gen: Static38CodeGenerator) -> None:
        code_gen.emit("PRIMITIVE_LOAD_CONST", (0, self.as_oparg()))
        self.emit_store_name(node, code_gen)


class CIntType(CType):
    instance: CIntInstance

    def __init__(
        self,
        constant: int,
        type_env: TypeEnvironment,
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
            type_env,
            instance=CIntInstance(self, self.constant, self.size, self.signed),
        )

    @property
    def type_descr(self) -> TypeDescr:
        return self.type_name.type_descr + ("#",)

    @property
    def boxed(self) -> Class:
        return self.type_env.int

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
        if isinstance(arg, ast.Constant):
            # for numeric literals, set the type context to self.instance,
            # so they're properly inferred as a primitive type
            visitor.visit(arg, self.instance)
        else:
            visitor.visit(arg)

        arg_type = visitor.get_type(arg)
        if not self.is_valid_arg(arg_type):
            visitor.check_can_assign_from(self, arg_type.klass, arg)

        return NO_EFFECT

    def is_valid_arg(self, arg_type: Value) -> bool:
        if (
            arg_type is self.klass.type_env.DYNAMIC
            or arg_type is self.klass.type_env.OBJECT
        ):
            return True

        if self is self.type_env.cbool:
            if arg_type.klass is self.type_env.bool:
                return True
            return False

        if arg_type is self.type_env.int.instance or self.is_valid_exact_int(arg_type):
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

    def emit_type_check(self, src: Class, code_gen: Static38CodeGenerator) -> None:
        assert self.can_assign_from(src)
        self.instance.emit_convert(src.instance, code_gen)

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
        code_gen.set_lineno(node)
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
            if rtype == self.klass.type_env.float.exact_type().instance:
                visitor.visitExpectedType(right, self, f"can't compare {{}} to {{}}")
            else:
                visitor.syntax_error(f"can't compare {self.name} to {rtype.name}", node)

        visitor.set_type(op, self)
        visitor.set_type(node, self.klass.type_env.cbool.instance)
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
            if ltype == self.klass.type_env.float.exact_type().instance:
                visitor.visitExpectedType(left, self, f"can't compare {{}} to {{}}")
            else:
                visitor.syntax_error(f"can't compare {self.name} to {ltype.name}", node)

            visitor.set_type(op, self)
            visitor.set_type(node, self.klass.type_env.cbool.instance)
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
                type_ctx or self.klass.type_env.double.instance,
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
            code_gen.emit("PRIMITIVE_BOX", self.as_oparg())
        else:
            raise RuntimeError("unsupported box type: " + type.name)

    def emit_unbox(self, node: expr, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(node)
        node_ty = code_gen.get_type(node)
        if self.klass.type_env.float.can_assign_from(node_ty.klass):
            code_gen.emit("REFINE_TYPE", node_ty.klass.type_descr)
        else:
            code_gen.emit("CAST", self.klass.type_env.float.type_descr)
        code_gen.emit("PRIMITIVE_UNBOX", self.as_oparg())

    def emit_init(self, node: ast.Name, code_gen: Static38CodeGenerator) -> None:
        code_gen.emit("PRIMITIVE_LOAD_CONST", (float(0), self.as_oparg()))
        self.emit_store_name(node, code_gen)


class CDoubleType(CType):
    def __init__(self, type_env: TypeEnvironment) -> None:
        super().__init__(
            TypeName("__static__", "double"),
            type_env,
            instance=CDoubleInstance(self),
        )

    @property
    def type_descr(self) -> TypeDescr:
        return self.type_name.type_descr + ("#",)

    @property
    def boxed(self) -> Class:
        return self.type_env.float

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
        allowed_types = [self.type_env.float, self.type_env.int, self]
        if not (
            arg_type is self.type_env.DYNAMIC
            or any(typ.can_assign_from(arg_type.klass) for typ in allowed_types)
        ):
            visitor.syntax_error(
                f"type mismatch: double cannot be created from {arg_type.name}", node
            )

        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        assert len(node.args) == 1

        arg = node.args[0]
        arg_type = code_gen.get_type(arg)
        if self.can_assign_from(arg_type.klass):
            code_gen.visit(arg)
        else:
            self.instance.emit_unbox(arg, code_gen)


class ModuleType(Class):
    def __init__(self, type_env: TypeEnvironment) -> None:
        super().__init__(TypeName("types", "ModuleType"), type_env, is_exact=True)


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
        super().__init__(klass=compiler.type_env.module)

    def resolve_attr(
        self, node: ast.Attribute, visitor: GenericVisitor[object]
    ) -> Optional[Value]:
        if node.attr in self.SPECIAL_NAMES:
            return super().resolve_attr(node, visitor)

        module_table = self.compiler.modules.get(self.module_name)
        if module_table is None:
            return visitor.type_env.DYNAMIC

        return module_table.get_child(node.attr, visitor.type_env.DYNAMIC)


class ProdAssertFunction(Object[Class]):
    def __init__(self, type_env: TypeEnvironment) -> None:
        super().__init__(type_env.function)

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
            visitor.visitExpectedType(node.args[1], self.klass.type_env.str.instance)
        effect.apply(visitor.type_state)
        return NO_EFFECT


class ContextDecoratorClass(Class):
    def __init__(
        self,
        type_env: TypeEnvironment,
        name: TypeName,
        bases: Optional[List[Class]] = None,
        subclass: bool = False,
        is_exact: bool = False,
        members: Optional[Dict[str, Value]] = None,
    ) -> None:
        super().__init__(
            name,
            type_env,
            bases or [type_env.object],
            ContextDecoratorInstance(self),
            is_exact=is_exact,
            members=members,
        )
        self.subclass = subclass
        if (
            not subclass and not is_exact
        ):  # exact versions copy members dict, no need to redefine methods
            may_suppress = False
            if name.name == "ExcContextDecorator":
                may_suppress = True
                # only the base ExcContextDecorator needs this, ContextDecorator inherits
                self.members["_recreate_cm"] = BuiltinMethodDescriptor(
                    "_recreate_cm",
                    self,
                    [
                        Parameter(
                            "self",
                            0,
                            ResolvedTypeRef(self),
                            False,
                            None,
                            ParamStyle.POSONLY,
                        )
                    ],
                    SelfTypeRef(self),
                    valid_on_subclasses=True,
                )
            self.members["__exit__"] = BuiltinMethodDescriptor(
                "__exit__",
                self,
                [
                    Parameter(
                        "self",
                        0,
                        ResolvedTypeRef(self),
                        False,
                        None,
                        ParamStyle.POSONLY,
                    ),
                    Parameter(
                        "exc_type",
                        1,
                        ResolvedTypeRef(self.type_env.dynamic),
                        False,
                        None,
                        ParamStyle.POSONLY,
                    ),
                    Parameter(
                        "exc_value",
                        2,
                        ResolvedTypeRef(
                            self.type_env.get_union(
                                (self.type_env.base_exception, self.type_env.none)
                            )
                        ),
                        False,
                        None,
                        ParamStyle.POSONLY,
                    ),
                    Parameter(
                        "traceback",
                        3,
                        ResolvedTypeRef(self.type_env.dynamic),
                        False,
                        None,
                        ParamStyle.POSONLY,
                    ),
                ],
                ResolvedTypeRef(
                    self.type_env.bool
                    if may_suppress
                    else self.type_env.get_literal_type(
                        self.type_env.bool.instance, False
                    ).klass
                ),
                # TODO(T116056907): Don't mark __exit__ valid on subclasses
                valid_on_subclasses=True,
            )

    def make_subclass(self, name: TypeName, bases: List[Class]) -> Class:
        if len(bases) == 1:
            return ContextDecoratorClass(self.type_env, name, bases, subclass=True)
        return super().make_subclass(name, bases)

    @property
    def may_suppress(self) -> bool:
        for base in self.mro:
            exit_method = base.members.get("__exit__")
            if exit_method:
                if isinstance(exit_method, Callable):
                    exit_ret_type = exit_method.return_type.resolved()
                    if (
                        isinstance(exit_ret_type, BoolClass)
                        and exit_ret_type.literal_value == False
                    ):
                        return False
                return True
        return True

    def _create_exact_type(self) -> Class:
        return type(self)(
            type_env=self.type_env,
            name=self.type_name,
            bases=self.bases,
            subclass=self.subclass,
            is_exact=True,
            members=self.members,
        )


class ContextDecoratorInstance(Object[ContextDecoratorClass]):
    def resolve_decorate_function(
        self, fn: Function | DecoratedMethod, decorator: expr
    ) -> Optional[Function | DecoratedMethod]:
        if fn.klass is self.klass.type_env.function:
            return ContextDecoratedMethod(
                self.klass.type_env.function, fn, decorator, self
            )
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
        if isinstance(function, DecoratedMethod):
            real_func = function.real_function
        else:
            real_func = function
        self.body: List[ast.stmt] = self.make_function_body(
            function.get_function_body(), real_func, decorator
        )
        if ctx_dec.klass.may_suppress:
            # If we might suppress exceptions, then the return type of our
            # wrapped function needs to be Optional; not only the outward-facing
            # return type (for that we'd just override `return_type`) but also
            # the inward-facing one for type-checking the function body, since
            # we rewrite the body of the method and it may now return None
            real_func.return_type = ResolvedTypeRef(
                klass.type_env.get_union(
                    (real_func.return_type.resolved(), klass.type_env.none)
                )
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
        body: List[ast.stmt], fn: Function, decorator: expr
    ) -> List[ast.stmt]:

        node = fn.node
        klass = fn.container_type
        dec_name = ContextDecoratedMethod.get_temp_name(fn, decorator)

        if klass is not None:
            if ContextDecoratedMethod.can_load_from_class(klass, fn):
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

        with_statement = cast(ast.stmt, ast.With([with_item], body))
        ast.fix_missing_locations(with_statement)

        return [with_statement]

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
        if klass is None:
            visitor.binding_scope.declare(dec_name, self.ctx_dec, is_final=True)
        self.function.bind_function_inner(node, visitor)

    def finish_bind(
        self, module: ModuleTable, klass: Class | None
    ) -> ContextDecoratedMethod:
        dec_name = self.get_temp_name(self.real_function, self.decorator)
        if klass is not None:
            klass.define_slot(
                dec_name,
                self.decorator,
                ResolvedTypeRef(
                    module.compiler.type_env.get_generic_type(
                        self.klass.type_env.classvar, (self.ctx_dec.klass,)
                    )
                ),
                assignment=self.real_function.node,
            )
        return self

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
        visitor: GenericVisitor[object],
    ) -> Optional[Value]:
        return self.function.resolve_descr_get(node, inst, ctx, visitor)


class EnumType(Class):
    def __init__(
        self,
        type_env: TypeEnvironment,
        type_name: Optional[TypeName] = None,
        bases: Optional[List[Class]] = None,
        is_exact: bool = False,
    ) -> None:
        super().__init__(
            type_name=(type_name or TypeName("__static__", "Enum")),
            bases=bases,
            type_env=type_env,
            instance=EnumInstance(self),
            is_exact=is_exact,
        )
        self.values: Dict[str, EnumInstance] = {}

    def make_subclass(self, name: TypeName, bases: List[Class]) -> Class:
        if len(bases) > 1:
            raise TypedSyntaxError(
                f"Static Enum types cannot support multiple bases: {bases}",
            )
        if bases[0] is not self.type_env.enum:
            raise TypedSyntaxError("Static Enum types do not allow subclassing")
        return EnumType(self.type_env, name, bases)

    def bind_attr(
        self, node: ast.Attribute, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        if isinstance(node.ctx, (ast.Store, ast.Del)):
            visitor.syntax_error(
                "Static Enum values cannot be modified or deleted", node
            )

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
        visitor.visitExpectedType(
            arg, visitor.type_env.DYNAMIC, CALL_ARGUMENT_CANNOT_BE_PRIMITIVE
        )

        return NO_EFFECT

    def bind_enum_value(self, name: str, typ: Value) -> None:
        self.values[name] = EnumInstance(self, name, typ)

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if len(node.args) != 1:
            raise code_gen.syntax_error(
                f"{self.name} requires a single argument, given {len(node.args)}", node
            )

        arg = node.args[0]
        arg_type = code_gen.get_type(arg)
        if isinstance(arg_type, EnumInstance) and arg_type.klass is self:
            code_gen.visit(arg)
        else:
            code_gen.defaultVisit(node)

    def _create_exact_type(self) -> Class:
        exact = type(self)(
            self.type_env,
            self.type_name,
            self.bases,
            is_exact=True,
        )
        exact.values = self.values
        return exact


class EnumInstance(Object[EnumType]):
    def __init__(
        self,
        klass: EnumType,
        name: Optional[str] = None,
        value: Optional[Value] = None,
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

    def bind_attr(
        self, node: ast.Attribute, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        if isinstance(node.ctx, (ast.Store, ast.Del)):
            visitor.syntax_error("Enum values cannot be modified or deleted", node)

        if node.attr == "name":
            visitor.set_type(node, visitor.type_env.str.exact_type().instance)
            return
        if node.attr == "value":
            assert self.value is not None
            visitor.set_type(node, self.value)
            return

        super().bind_attr(node, visitor, type_ctx)


class IntEnumType(EnumType):
    def __init__(
        self,
        type_env: TypeEnvironment,
        type_name: Optional[TypeName] = None,
        bases: Optional[List[Class]] = None,
        is_exact: bool = False,
    ) -> None:
        super().__init__(
            type_name=(type_name or TypeName("__static__", "IntEnum")),
            bases=bases or cast(List[Class], [type_env.enum, type_env.int]),
            type_env=type_env,
            is_exact=is_exact,
        )

    def make_subclass(self, name: TypeName, bases: List[Class]) -> Class:
        if len(bases) > 1:
            raise TypedSyntaxError(
                f"Static IntEnum types cannot support multiple bases: {bases}",
            )
        if bases[0] is not self.type_env.int_enum:
            raise TypedSyntaxError("Static IntEnum types do not allow subclassing")
        return IntEnumType(self.type_env, name, bases)

    def bind_enum_value(self, name: str, typ: Value) -> None:
        if not self.type_env.int.can_assign_from(typ.klass):
            raise TypedSyntaxError(f"IntEnum values must be int, not {typ.name}")
        self.values[name] = EnumInstance(self, name, typ)


class StringEnumType(EnumType):
    def __init__(
        self,
        type_env: TypeEnvironment,
        type_name: Optional[TypeName] = None,
        bases: Optional[List[Class]] = None,
        is_exact: bool = False,
    ) -> None:
        super().__init__(
            type_name=(type_name or TypeName("__static__", "StringEnum")),
            bases=bases or cast(List[Class], [type_env.enum, type_env.str]),
            type_env=type_env,
            is_exact=is_exact,
        )

    def make_subclass(self, name: TypeName, bases: List[Class]) -> Class:
        if len(bases) > 1:
            raise TypedSyntaxError(
                f"Static StringEnum types cannot support multiple bases: {bases}",
            )
        if bases[0] is not self.type_env.string_enum:
            raise TypedSyntaxError("Static StringEnum types do not allow subclassing")
        return StringEnumType(self.type_env, name, bases)

    def bind_enum_value(self, name: str, typ: Value) -> None:
        if not self.type_env.str.can_assign_from(typ.klass):
            raise TypedSyntaxError(f"StringEnum values must be str, not {typ.name}")
        self.values[name] = EnumInstance(self, name, typ)


if spamobj is not None:

    class XXGeneric(GenericClass):
        def __init__(
            self,
            type_name: GenericTypeName,
            type_env: TypeEnvironment,
            bases: Optional[List[Class]] = None,
            instance: Optional[Object[Class]] = None,
            klass: Optional[Class] = None,
            members: Optional[Dict[str, Value]] = None,
            type_def: Optional[GenericClass] = None,
            is_exact: bool = False,
            pytype: Optional[Type[object]] = None,
            is_final: bool = False,
        ) -> None:
            super().__init__(
                type_name,
                type_env,
                bases,
                instance,
                klass,
                members,
                type_def,
                is_exact=is_exact,
                pytype=pytype,
                is_final=is_final,
            )

            self.members["foo"] = BuiltinMethodDescriptor(
                "foo",
                self,
                [
                    Parameter(
                        "self",
                        0,
                        ResolvedTypeRef(self),
                        False,
                        None,
                        ParamStyle.POSONLY,
                    ),
                    Parameter(
                        "t",
                        1,
                        ResolvedTypeRef(self.type_name.args[0]),
                        False,
                        None,
                        ParamStyle.POSONLY,
                    ),
                    Parameter(
                        "u",
                        2,
                        ResolvedTypeRef(self.type_name.args[1]),
                        False,
                        None,
                        ParamStyle.POSONLY,
                    ),
                ],
            )


def access_path(node: ast.AST) -> List[str]:
    path = []
    while not isinstance(node, ast.Name):
        if not isinstance(node, ast.Attribute):
            return []
        path.append(node.attr)
        node = node.value
    path.append(node.id)
    return list(reversed(path))

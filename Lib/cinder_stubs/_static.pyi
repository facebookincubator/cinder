from typing import Any, ClassVar

FAST_LEN_ARRAY: int
FAST_LEN_DICT: int
FAST_LEN_INEXACT: int
FAST_LEN_LIST: int
FAST_LEN_SET: int
FAST_LEN_STR: int
FAST_LEN_TUPLE: int
PRIM_OP_ADD_DBL: int
PRIM_OP_ADD_INT: int
PRIM_OP_AND_INT: int
PRIM_OP_DIV_DBL: int
PRIM_OP_DIV_INT: int
PRIM_OP_DIV_UN_INT: int
PRIM_OP_EQ_DBL: int
PRIM_OP_EQ_INT: int
PRIM_OP_GE_DBL: int
PRIM_OP_GE_INT: int
PRIM_OP_GE_UN_INT: int
PRIM_OP_GT_DBL: int
PRIM_OP_GT_INT: int
PRIM_OP_GT_UN_INT: int
PRIM_OP_INV_INT: int
PRIM_OP_LE_DBL: int
PRIM_OP_LE_INT: int
PRIM_OP_LE_UN_INT: int
PRIM_OP_LSHIFT_INT: int
PRIM_OP_LT_DBL: int
PRIM_OP_LT_INT: int
PRIM_OP_LT_UN_INT: int
PRIM_OP_MOD_DBL: int
PRIM_OP_MOD_INT: int
PRIM_OP_MOD_UN_INT: int
PRIM_OP_MUL_DBL: int
PRIM_OP_MUL_INT: int
PRIM_OP_NEG_DBL: int
PRIM_OP_NEG_INT: int
PRIM_OP_NE_DBL: int
PRIM_OP_NE_INT: int
PRIM_OP_NOT_INT: int
PRIM_OP_OR_INT: int
PRIM_OP_POW_DBL: int
PRIM_OP_POW_INT: int
PRIM_OP_POW_UN_INT: int
PRIM_OP_RSHIFT_INT: int
PRIM_OP_RSHIFT_UN_INT: int
PRIM_OP_SUB_DBL: int
PRIM_OP_SUB_INT: int
PRIM_OP_XOR_INT: int
RAND_MAX: int
SEQ_ARRAY_INT64: int
SEQ_CHECKED_LIST: int
SEQ_LIST: int
SEQ_LIST_INEXACT: int
SEQ_REPEAT_INEXACT_NUM: int
SEQ_REPEAT_INEXACT_SEQ: int
SEQ_REPEAT_PRIMITIVE_NUM: int
SEQ_REPEAT_REVERSED: int
SEQ_SUBSCR_UNCHECKED: int
SEQ_TUPLE: int
TYPED_ARRAY: int
TYPED_BOOL: int
TYPED_CHAR: int
TYPED_DOUBLE: int
TYPED_INT16: int
TYPED_INT32: int
TYPED_INT64: int
TYPED_INT8: int
TYPED_INT_16BIT: int
TYPED_INT_32BIT: int
TYPED_INT_64BIT: int
TYPED_INT_8BIT: int
TYPED_INT_SIGNED: int
TYPED_INT_UNSIGNED: int
TYPED_OBJECT: int
TYPED_SINGLE: int
TYPED_UINT16: int
TYPED_UINT32: int
TYPED_UINT64: int
TYPED_UINT8: int

chkdict = dict
chklist = list

class staticarray:
    @classmethod
    def __init__(cls, size: int) -> None: ...
    def __add__(self, other) -> Any: ...
    @classmethod
    def __class_getitem__(cls, *args, **kwargs) -> Any: ...
    def __delitem__(self, other) -> Any: ...
    def __getitem__(self, index: int) -> Any: ...
    def __len__(self) -> int: ...
    def __mul__(self, other) -> Any: ...
    def __rmul__(self, other) -> Any: ...
    def __setitem__(self, index: int, object) -> None: ...

def __build_cinder_class__(func, name, *bases, metaclass=..., **kwds) -> type: ...
def _clear_dlopen_cache(*args, **kwargs) -> Any: ...
def _clear_dlsym_cache(*args, **kwargs) -> Any: ...
def _property_missing_fget(*args, **kwargs) -> Any: ...
def _property_missing_fset(*args, **kwargs) -> Any: ...
def _sizeof_dlopen_cache(*args, **kwargs) -> Any: ...
def _sizeof_dlsym_cache(*args, **kwargs) -> Any: ...
def init_subclass(*args, **kwargs) -> Any: ...
def install_sp_audit_hook() -> None: ...
def is_type_static(t) -> bool: ...
def lookup_native_symbol(*args, **kwargs) -> Any: ...
def make_context_decorator_wrapper(*args, **kwargs) -> Any: ...
def make_recreate_cm(t: T) -> Callable[[], T]: ...
def posix_clock_gettime_ns() -> int: ...
def rand() -> int: ...
def resolve_primitive_descr(*args, **kwargs) -> Any: ...
def set_type_code(func, code) -> None: ...
def set_type_final(t: T) -> T: ...
def set_type_static(t: T) -> T: ...
def set_type_static_final(t: T) -> T: ...

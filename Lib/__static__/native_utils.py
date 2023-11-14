from __future__ import annotations

import ctypes
from typing import Tuple

from cinderx.static import (
    resolve_primitive_descr,
    TYPED_BOOL,
    TYPED_CHAR,
    TYPED_DOUBLE,
    TYPED_INT16,
    TYPED_INT32,
    TYPED_INT64,
    TYPED_INT8,
    TYPED_UINT16,
    TYPED_UINT32,
    TYPED_UINT64,
    TYPED_UINT8,
)

_static_to_ctype = {
    TYPED_BOOL: ctypes.c_bool,
    TYPED_CHAR: ctypes.c_char,
    # TODO(T130985738): Add support for doubles
    # TYPED_DOUBLE: ctypes.c_double,
    TYPED_INT8: ctypes.c_int8,
    TYPED_INT16: ctypes.c_int16,
    TYPED_INT32: ctypes.c_int32,
    TYPED_INT64: ctypes.c_int64,
    TYPED_UINT8: ctypes.c_uint8,
    TYPED_UINT16: ctypes.c_uint16,
    TYPED_UINT32: ctypes.c_uint32,
    TYPED_UINT64: ctypes.c_uint64,
}


def _create_args(
    signature: Tuple[Tuple[str, ...], ...], args: Tuple[object]
) -> List[object]:
    arg_descrs, ret_descr = signature[:-1], signature[-1]

    n = len(arg_descrs)
    if n != len(args):
        raise RuntimeError(f"invoke_native: {n} args required, got {len(args)}")

    call_args = []
    for i in range(n):
        descr = arg_descrs[i]
        arg = args[i]
        primitive_type = resolve_primitive_descr(descr)
        try:
            ctypes_type = _static_to_ctype[primitive_type]
        except KeyError as e:
            raise RuntimeError(f"Unsupported primitive type: {descr}") from e
        else:
            call_args.append(ctypes_type(arg))
    return call_args


def invoke_native(
    libname: str,
    symbol: str,
    signature: Tuple[Tuple[str, ...], ...],
    args: Tuple[object],
) -> int:
    # This is basically just a `dlopen()` under the hood
    lib = ctypes.CDLL(libname)

    # Python wrapper over `dlsym`
    fn = getattr(lib, symbol)

    call_args = _create_args(signature, args)

    res = fn(*call_args)
    return res

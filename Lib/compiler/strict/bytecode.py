# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
from __future__ import annotations

import _imp
import io
import marshal
from importlib.util import MAGIC_NUMBER as BASE_PYTHON_MAGIC_NUMBER
from types import CodeType
from typing import Any, Callable, Dict, Mapping, Tuple

from ..consts import CO_STATICALLY_COMPILED

# Force eager import of the consts module to prevent bootstrap issues with circular imports.
CO_STATICALLY_COMPILED = CO_STATICALLY_COMPILED


def _pack_uint32(x: int) -> bytes:
    """Convert a 32-bit integer to little-endian."""
    return (x & 0xFFFFFFFF).to_bytes(4, "little")


def _unpack_uint32(data: bytes) -> int:
    """Convert 4 bytes in little-endian to an integer."""
    assert len(data) == 4
    return int.from_bytes(data, "little")


def code_to_pyc(
    code_object: CodeType,
    source_bytes: bytes,
    source_mtime: int | None = None,
    source_hash: bytes | None = None,
    checked: bool = True,
) -> bytearray:
    is_static = bool(code_object.co_flags & CO_STATICALLY_COMPILED)
    if source_mtime is not None:
        return _code_to_timestamp_pyc(
            code_object,
            is_static=is_static,
            mtime=source_mtime,
            source_size=len(source_bytes),
        )
    # Hash based.
    if source_hash is None:
        # pyre-ignore[16]: source_hash is missing from the type stubs of _imp.
        source_hash = _imp.source_hash(source_bytes)
    return _code_to_hash_pyc(
        code_object, is_static=is_static, source_hash=source_hash, checked=checked
    )


def _code_to_timestamp_pyc(
    code: CodeType, is_static: bool, mtime: int = 0, source_size: int = 0
) -> bytearray:
    """This function was copied over from `importlib._bootstrap_external._code_to_timestamp_pyc`.
    It has been modified to support the static flag/dump dependencies.

    Produce the data for a timestamp-based pyc."""
    data = bytearray(BASE_PYTHON_MAGIC_NUMBER)
    flags = 0
    if is_static:
        flags |= 0b100
    data.extend(_pack_uint32(flags))
    data.extend(_pack_uint32(mtime))
    data.extend(_pack_uint32(source_size))
    data.extend(marshal.dumps(()))
    data.extend(marshal.dumps(code))
    return data


def _code_to_hash_pyc(
    code: CodeType, is_static: bool, source_hash: bytes, checked: bool = True
) -> bytearray:
    """This function was copied over from `importlib._bootstrap_external._code_to_timestamp_pyc`.
    It has been modified to support the static flag/dump dependencies.

    Produce the data for a hash-based pyc."""
    data = bytearray(BASE_PYTHON_MAGIC_NUMBER)
    flags = 0b1 | checked << 1
    if is_static:
        flags |= 0b100
    data.extend(_pack_uint32(flags))
    assert len(source_hash) == 8
    data.extend(source_hash)
    data.extend(marshal.dumps(()))
    data.extend(marshal.dumps(code))
    return data


def classify_pyc(data: bytes, name: str, exc_details: Dict[str, str]) -> int:
    """This function was copied over from `importlib._bootstrap_external._classify_pyc`.

    Perform basic validity checking of a pyc header and return the flags field,
    which determines how the pyc should be further validated against the source.

    *data* is the contents of the pyc file. (Only the first 16 bytes are
    required, though.)

    *name* is the name of the module being imported. It is used for logging.

    *exc_details* is a dictionary passed to ImportError if it raised for
    improved debugging.

    ImportError is raised when the magic number is incorrect or when the flags
    field is invalid. EOFError is raised when the data is found to be truncated.

    """
    magic = data[:4]
    if magic != BASE_PYTHON_MAGIC_NUMBER:
        message = f"bad magic number in {name!r}: {magic!r}"
        raise ImportError(message, **exc_details)
    if len(data) < 16:
        message = f"reached EOF while reading pyc header of {name!r}"
        raise EOFError(message)
    flags = _unpack_uint32(data[4:8])
    # Only the first three flags are defined.
    if flags & ~0b111:
        message = f"invalid flags {flags!r} in {name!r}"
        raise ImportError(message, **exc_details)
    return flags


def validate_timestamp_pyc(
    data: bytes,
    source_mtime: int,
    source_size: int,
    name: str,
    exc_details: Dict[str, str],
) -> None:
    """This function was copied over from `importlib._bootstrap_external._validate_timestamp_pyc.

    Validate a pyc against the source last-modified time.

    *data* is the contents of the pyc file. (Only the first 16 bytes are
    required.)

    *source_mtime* is the last modified timestamp of the source file.

    *source_size* is None or the size of the source file in bytes.

    *name* is the name of the module being imported. It is used for logging.

    *exc_details* is a dictionary passed to ImportError if it raised for
    improved debugging.

    An ImportError is raised if the bytecode is stale.

    """
    if _unpack_uint32(data[8:12]) != (source_mtime & 0xFFFFFFFF):
        message = f"bytecode is stale for {name!r}"
        raise ImportError(message, **exc_details)
    if source_size is not None and _unpack_uint32(data[12:16]) != (
        source_size & 0xFFFFFFFF
    ):
        raise ImportError(f"bytecode is stale for {name!r}", **exc_details)


def validate_hash_pyc(
    data: bytes, source_hash: bytes, name: str, exc_details: Dict[str, str]
) -> None:
    """This function was copied over from `importlib._bootstrap_external._validate_hash_pyc.

    Validate a hash-based pyc by checking the real source hash against the one in
    the pyc header.

    *data* is the contents of the pyc file. (Only the first 16 bytes are
    required.)

    *source_hash* is the importlib.util.source_hash() of the source file.

    *name* is the name of the module being imported. It is used for logging.

    *exc_details* is a dictionary passed to ImportError if it raised for
    improved debugging.

    An ImportError is raised if the bytecode is stale.

    """
    if data[8:12] != source_hash:
        raise ImportError(
            f"hash in bytecode doesn't match hash of source {name!r}",
            **exc_details,
        )


def compile_static_bytecode(
    data: bytes,
    name: str,
    bytecode_path: str,
    source_path: str | None = None,
) -> CodeType | None:
    """This function is an adaptation of `importlib._bootstrap_external._compile_bytecode` for the
    strict loader. Since strict pyc's have a different format, the compilation is adapted to reading
    the bytecode properly, and types were added.
    """
    datafile = io.BytesIO(data)
    dependencies = marshal.load(datafile)
    code = marshal.load(datafile)
    if isinstance(code, CodeType):
        if source_path is not None:
            # pyre-ignore[16]: The stubs are missing this function.
            _imp._fix_co_filename(code, source_path)
        return code
    else:
        raise ImportError(
            f"Non-code object in {bytecode_path}",
            name=name,
            path=bytecode_path,
        )

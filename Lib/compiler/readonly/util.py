from __future__ import annotations

import ast
from ast import AST, Subscript, Name, Call, Index
from typing import Any, Optional, Tuple, Union

READONLY_CALL: str = "readonly"
READONLY_FUNC: str = "readonly_func"
READONLY_FUNC_NONLOCAL: str = "readonly_closure"

READONLY_DECORATORS = ("readonly_func", "readonly_closure")


def is_readonly_wrapped(node: AST) -> bool:
    if not isinstance(node, ast.Call):
        return False
    return isinstance(node.func, Name) and node.func.id == READONLY_CALL

def is_tuple_wrapped(node: AST) -> bool:
    if not isinstance(node, ast.Call):
        return False
    return isinstance(node.func, Name) and node.func.id == "tuple"

def is_readonly_func(node: AST) -> bool:
    return isinstance(node, Name) and node.id == READONLY_FUNC

def is_readonly_func_nonlocal(node: AST) -> bool:
    return isinstance(node, Name) and node.id == READONLY_FUNC_NONLOCAL


def calc_function_readonly_mask(
    *,
    readonly_func: bool = True,
    returns_readonly: bool,
    readonly_nonlocal: bool,
    arg_tuple: Tuple[bool, ...]
) -> int:
    mask = 0
    if readonly_func:
        mask = mask | 0x8000000000000000
    bit = 1

    for readonly_arg in arg_tuple:
        if readonly_arg:
            mask = mask | bit

        bit = bit << 1

    if not returns_readonly:
        mask = mask | 0x4000000000000000

    if readonly_nonlocal:
        mask = mask | 0x2000000000000000

    return mask

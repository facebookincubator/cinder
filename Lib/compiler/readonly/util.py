from __future__ import annotations

import ast
import os
from ast import AST, Name

READONLY_CALL: str = "readonly"
READONLY_FUNC: str = "readonly_func"
READONLY_CLOSURE: str = "readonly_closure"

READONLY_DECORATORS = ("readonly_func", "readonly_closure")
FORCE_READONLY_ENV_VAR = "PYTHONFORCEREADONLYCHECKS"
USE_READONLY_COMPILER_ENV_VAR = "PYTHONUSEREADONLYCOMPILER"


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


def is_readonly_closure(node: AST) -> bool:
    return isinstance(node, Name) and node.id == READONLY_CLOSURE


def is_readonly_compile_forced() -> bool:
    return bool(os.getenv(FORCE_READONLY_ENV_VAR))


def is_readonly_compiler_used() -> bool:
    return bool(os.getenv(USE_READONLY_COMPILER_ENV_VAR))

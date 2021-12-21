from __future__ import annotations

from ast import AST, Subscript, Name, Call

READONLY_ANNOTATION: str = "Readonly"
READONLY_CALL: str = "readonly"
READONLY_FUNC: str = "readonly_func"


def is_readonly_annotation(node: AST) -> bool:
    return (
        isinstance(node, Subscript)
        and isinstance(node.value, Name)
        and node.value.id == READONLY_ANNOTATION
    )


def is_readonly_wrapped(node: AST) -> bool:
    return isinstance(node, Name) and node.id == READONLY_CALL


def is_readonly_func(node: AST) -> bool:
    return isinstance(node, Name) and node.id == READONLY_FUNC

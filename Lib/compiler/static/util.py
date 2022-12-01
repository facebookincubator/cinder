from __future__ import annotations

import ast
import sys
from typing import Callable, cast, Mapping, Tuple, Type

COMPARE_OPS: Mapping[Type[ast.cmpop], Callable[[object, object], bool]] = {
    ast.Gt: lambda a, b: a > b,
    ast.GtE: lambda a, b: a >= b,
    ast.Lt: lambda a, b: a < b,
    ast.LtE: lambda a, b: a <= b,
    ast.Eq: lambda a, b: a == b,
    ast.NotEq: lambda a, b: a != b,
    ast.In: lambda a, b: a in b,
    ast.Is: lambda a, b: a is b,
    ast.IsNot: lambda a, b: a is not b,
}


def _is_sys_hexversion_attr_load(node: ast.expr) -> bool:
    if isinstance(node, ast.Attribute):
        container = node.value
        if (
            isinstance(container, ast.Name)
            and container.id == "sys"
            and isinstance(node.ctx, ast.Load)
            and node.attr == "hexversion"
        ):
            return True
    return False


def _get_const_int(node: ast.expr) -> int | None:
    if isinstance(node, ast.Constant):
        value = node.value
        return value if isinstance(value, int) else None


def sys_hexversion_check(
    node: ast.If,
) -> bool | None:
    """
    A helper function, the result of this is used to determine whether
    we need to skip visiting dead code gated by sys.hexversion checks.
    """
    test_node = node.test
    if isinstance(test_node, ast.Compare):
        if len(test_node.comparators) != 1:
            return None

        assert len(test_node.ops) == 1

        left = test_node.left
        right = test_node.comparators[0]
        op = test_node.ops[0]

        if type(op) in (ast.In, ast.Is, ast.IsNot):
            return None

        if _is_sys_hexversion_attr_load(left):
            left_value = sys.hexversion
            right_value = _get_const_int(right)
        elif _is_sys_hexversion_attr_load(right):
            left_value = _get_const_int(left)
            right_value = sys.hexversion
        else:
            return None

        if left_value is None or right_value is None:
            return None

        return COMPARE_OPS[type(op)](left_value, right_value)

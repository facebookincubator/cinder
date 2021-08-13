# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
from __future__ import annotations

import ast
from ast import (
    AST,
    Call,
    Constant,
    Name,
)
from typing import List, Optional, Tuple, TypeVar

# pyre-fixme[21]
from _strictmodule import (
    MUTABLE_DECORATOR,
    LOOSE_SLOTS_DECORATOR,
    EXTRA_SLOTS_DECORATOR,
    ENABLE_SLOTS_DECORATOR,
    CACHED_PROP_DECORATOR,
)

ALL_INDICATORS: Tuple[str, ...] = (
    MUTABLE_DECORATOR,
    LOOSE_SLOTS_DECORATOR,
    EXTRA_SLOTS_DECORATOR,
    ENABLE_SLOTS_DECORATOR,
    CACHED_PROP_DECORATOR,
)


def is_indicator_dec(node: AST) -> bool:
    if isinstance(node, Name):
        return node.id in ALL_INDICATORS
    elif isinstance(node, Call):
        func = node.func
        if isinstance(func, Name):
            return func.id in ALL_INDICATORS
    return False


def is_strict_slots(node: AST) -> bool:
    return isinstance(node, Name) and node.id == ENABLE_SLOTS_DECORATOR


def is_loose_slots(node: AST) -> bool:
    return isinstance(node, Name) and node.id == LOOSE_SLOTS_DECORATOR


def is_mutable(node: AST) -> bool:
    return isinstance(node, Name) and node.id == MUTABLE_DECORATOR


def get_extra_slots(node: AST) -> Optional[List[str]]:
    if isinstance(node, Call):
        func = node.func
        if isinstance(func, Name) and func.id == EXTRA_SLOTS_DECORATOR:
            res: List[str] = []
            for a in node.args:
                assert isinstance(a, Constant), "<extra_slots> args should be strs"
                assert isinstance(a.value, str), "<extra_slots> args should be strs"
                res.append(a.value)
            return res

    return None


def get_cached_prop_value(node: AST) -> object:
    if isinstance(node, Call):
        func = node.func
        if isinstance(func, Name) and func.id == CACHED_PROP_DECORATOR:
            assert len(node.args) == 1, "<cached_property> should have exactly one arg"
            arg = node.args[0]
            assert isinstance(
                arg, Constant
            ), "<cached_property> should have a constant arg"
            return arg.value

    return None

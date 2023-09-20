# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

from __future__ import annotations

import ast
from ast import AST, Call, Constant, Name
from typing import List, Optional, Tuple, TypeVar


def is_mutable(node: AST) -> bool:
    return isinstance(node, Name) and node.id == "mutable"

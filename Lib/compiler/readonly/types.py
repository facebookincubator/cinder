from __future__ import annotations

from typing import List


class Value:
    def __init__(self, is_readonly: bool, bases: List[Value], name: str) -> None:
        """
        is_readonly: the Value is readonly and cannot be modified
        """
        self.is_readonly = is_readonly
        self.bases = bases
        self.name = name

    def can_convert_to(self, other: Value) -> bool:
        return other is self or other in self.bases

    def __str__(self) -> str:
        return self.name


READONLY = Value(True, [], "readonly")
MUTABLE = Value(False, [READONLY], "mutable")

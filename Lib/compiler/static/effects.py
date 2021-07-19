# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
from __future__ import annotations

import ast
from typing import Dict, Optional, Sequence, TYPE_CHECKING

if TYPE_CHECKING:
    from .type_binder import TypeBinder
    from .types import Value


class NarrowingEffect:
    """captures type narrowing effects on variables"""

    def and_(self, other: NarrowingEffect) -> NarrowingEffect:
        if other is NoEffect:
            return self

        return AndEffect(self, other)

    def or_(self, other: NarrowingEffect) -> NarrowingEffect:
        if other is NoEffect:
            return self

        return OrEffect(self, other)

    def not_(self) -> NarrowingEffect:
        return NegationEffect(self)

    def apply(
        self,
        local_types: Dict[str, Value],
        local_name_nodes: Optional[Dict[str, ast.Name]] = None,
    ) -> None:
        """applies the given effect in the target scope. if `local_name_nodes` is passed, populates
        it with the underlying name nodes"""
        pass

    def undo(self, local_types: Dict[str, Value]) -> None:
        """restores the type to its original value"""
        pass

    def reverse(self, local_types: Dict[str, Value]) -> None:
        """applies the reverse of the scope or reverts it if
        there is no reverse"""
        self.undo(local_types)


class AndEffect(NarrowingEffect):
    def __init__(self, *effects: NarrowingEffect) -> None:
        self.effects: Sequence[NarrowingEffect] = effects

    def and_(self, other: NarrowingEffect) -> NarrowingEffect:
        if other is NoEffect:
            return self
        elif isinstance(other, AndEffect):
            return AndEffect(*self.effects, *other.effects)

        return AndEffect(*self.effects, other)

    def apply(
        self,
        local_types: Dict[str, Value],
        local_name_nodes: Optional[Dict[str, ast.Name]] = None,
    ) -> None:
        for effect in self.effects:
            effect.apply(local_types, local_name_nodes)

    def undo(self, local_types: Dict[str, Value]) -> None:
        """restores the type to its original value"""
        for effect in self.effects:
            effect.undo(local_types)


class OrEffect(NarrowingEffect):
    def __init__(self, *effects: NarrowingEffect) -> None:
        self.effects: Sequence[NarrowingEffect] = effects

    def and_(self, other: NarrowingEffect) -> NarrowingEffect:
        if other is NoEffect:
            return self
        elif isinstance(other, OrEffect):
            return OrEffect(*self.effects, *other.effects)

        return OrEffect(*self.effects, other)

    def reverse(self, local_types: Dict[str, Value]) -> None:
        for effect in self.effects:
            effect.reverse(local_types)

    def undo(self, local_types: Dict[str, Value]) -> None:
        """restores the type to its original value"""
        for effect in self.effects:
            effect.undo(local_types)


class NoEffect(NarrowingEffect):
    def union(self, other: NarrowingEffect) -> NarrowingEffect:
        return other


# Singleton instance for no effects
NO_EFFECT = NoEffect()


class NegationEffect(NarrowingEffect):
    def __init__(self, negated: NarrowingEffect) -> None:
        self.negated = negated

    def not_(self) -> NarrowingEffect:
        return self.negated

    def apply(
        self,
        local_types: Dict[str, Value],
        local_name_nodes: Optional[Dict[str, ast.Name]] = None,
    ) -> None:
        self.negated.reverse(local_types)

    def undo(self, local_types: Dict[str, Value]) -> None:
        self.negated.undo(local_types)

    def reverse(self, local_types: Dict[str, Value]) -> None:
        self.negated.apply(local_types)

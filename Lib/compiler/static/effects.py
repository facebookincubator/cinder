# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

from __future__ import annotations

import ast
from dataclasses import dataclass
from typing import Dict, Optional, Sequence, Set, Tuple, TYPE_CHECKING

if TYPE_CHECKING:
    from .type_binder import TypeBinder
    from .types import Value

    RefinedFields = Dict[str, Tuple[Value, int, Set[ast.AST]]]


# A refined field consists of a refined type in addition to the node that
# refined the field. The node information is used to hoist reads during codegen.


class TypeState:
    def __init__(self) -> None:
        self.local_types: Dict[str, Value] = {}
        self.refined_fields: Dict[str, RefinedFields] = {}

    def copy(self) -> TypeState:
        type_state = TypeState()
        type_state.local_types = dict(self.local_types)
        type_state.refined_fields = dict(self.refined_fields)
        return type_state


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
        type_state: TypeState,
        type_state_nodes: Optional[Dict[str, ast.AST]] = None,
    ) -> None:
        """applies the given effect in the target scope. if `type_state_nodes` is passed, populates
        it with the underlying name or attribute nodes"""
        pass

    def undo(self, type_state: TypeState) -> None:
        """restores the type to its original value"""
        pass

    def reverse(
        self,
        type_state: TypeState,
        type_state_nodes: Optional[Dict[str, ast.AST]] = None,
    ) -> None:
        """applies the reverse of the scope or reverts it if
        there is no reverse"""
        self.undo(type_state)


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
        type_state: TypeState,
        type_state_nodes: Optional[Dict[str, ast.AST]] = None,
    ) -> None:
        for effect in self.effects:
            effect.apply(type_state, type_state_nodes)

    def undo(self, type_state: TypeState) -> None:
        """restores the type to its original value"""
        for effect in self.effects:
            effect.undo(type_state)


class OrEffect(NarrowingEffect):
    def __init__(self, *effects: NarrowingEffect) -> None:
        self.effects: Sequence[NarrowingEffect] = effects

    def and_(self, other: NarrowingEffect) -> NarrowingEffect:
        if other is NoEffect:
            return self
        elif isinstance(other, OrEffect):
            return OrEffect(*self.effects, *other.effects)

        return OrEffect(*self.effects, other)

    def reverse(
        self,
        type_state: TypeState,
        type_state_nodes: Optional[Dict[str, ast.AST]] = None,
    ) -> None:
        for effect in self.effects:
            effect.reverse(type_state, type_state_nodes)

    def undo(self, type_state: TypeState) -> None:
        """restores the type to its original value"""
        for effect in self.effects:
            effect.undo(type_state)


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
        type_state: TypeState,
        type_state_nodes: Optional[Dict[str, ast.AST]] = None,
    ) -> None:
        self.negated.reverse(type_state, type_state_nodes)

    def undo(self, type_state: TypeState) -> None:
        self.negated.undo(type_state)

    def reverse(
        self,
        type_state: TypeState,
        type_state_nodes: Optional[Dict[str, ast.AST]] = None,
    ) -> None:
        self.negated.apply(type_state, type_state_nodes)

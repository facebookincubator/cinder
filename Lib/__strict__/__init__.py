from __future__ import annotations

from typing import Type

try:
    from cinder import freeze_type as cinder_freeze
except ImportError:
    cinder_freeze = None


try:
    from cinder import warn_on_inst_dict
except ImportError:
    warn_on_inst_dict = None


__all__ = [
    "freeze_type",
    "loose_slots",
    "strict_slots",
    "mutable",
    "_mark_cached_property",
]

TYPE_FREEZE_ENABLED = True


def freeze_type(obj: Type[object]) -> Type[object]:
    if cinder_freeze is not None:
        if TYPE_FREEZE_ENABLED:
            cinder_freeze(obj)
        elif warn_on_inst_dict is not None:
            warn_on_inst_dict(obj)

    return obj


def loose_slots(obj: Type[object]) -> Type[object]:
    """Indicates that a type defined in a strict module should support assigning
    additional variables to __dict__ to support migration."""
    if warn_on_inst_dict is not None:
        warn_on_inst_dict(obj)
    return obj


def mutable(obj: Type[object]) -> Type[object]:
    """Marks a type defined in a strict module as supporting mutability"""
    return obj


def strict_slots(obj: Type[object]) -> Type[object]:
    """Marks a type defined in a strict module to get slots automatically
    and no __dict__ is created"""
    return obj


def _mark_cached_property(obj: object, is_async: bool, original_dec: object) -> object:
    return obj

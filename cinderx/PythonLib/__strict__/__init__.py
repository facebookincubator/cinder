# Copyright (c) Meta Platforms, Inc. and affiliates.

from cinderx.compiler.strict.runtime import (
    _mark_cached_property,
    extra_slot,
    freeze_type,
    loose_slots,
    mutable,
    set_freeze_enabled,
    strict_slots,
)

allow_side_effects = object()

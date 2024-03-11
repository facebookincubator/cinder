# Copyright (c) Meta Platforms, Inc. and affiliates.

from cinderx.compiler.strict.runtime import (
    freeze_type,
    loose_slots,
    strict_slots,
    mutable,
    extra_slot,
    _mark_cached_property,
    set_freeze_enabled,
)

allow_side_effects = object()

// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "cinderx/StrictModules/Objects/object_interface.h"
#include "cinderx/StrictModules/Objects/object_type.h"
#include "cinderx/StrictModules/sequence_map.h"
namespace strictmod::objects {
std::shared_ptr<BaseStrictObject> looseSlots(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> value);

std::shared_ptr<BaseStrictObject> strictSlots(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> value);

std::shared_ptr<BaseStrictObject> extraSlot(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> value,
    std::shared_ptr<BaseStrictObject> name);

std::shared_ptr<BaseStrictObject> setMutable(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> value);

std::shared_ptr<BaseStrictObject> markCachedProperty(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> value,
    std::shared_ptr<BaseStrictObject> isAsync,
    std::shared_ptr<BaseStrictObject> originalDec);

} // namespace strictmod::objects

// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "cinderx/StrictModules/Objects/object_type.h"
namespace strictmod::objects {
bool isTypingType(
    std::shared_ptr<BaseStrictObject> arg,
    const std::string& name);
}

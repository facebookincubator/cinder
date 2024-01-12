// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "cinderx/StrictModules/Objects/object_type.h"
namespace strictmod::objects {
bool isTypingType(
    std::shared_ptr<BaseStrictObject> arg,
    const std::string& name);
}

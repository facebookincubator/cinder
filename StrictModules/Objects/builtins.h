// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTM_BUILTINS_OBJ___
#define __STRICTM_BUILTINS_OBJ___

#include "StrictModules/Objects/object_interface.h"
#include "StrictModules/Objects/object_type.h"

namespace strictmod::objects {
std::shared_ptr<BaseStrictObject> reprImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> value);

std::shared_ptr<BaseStrictObject> isinstanceImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> clsInfo);

std::shared_ptr<BaseStrictObject> issubclassImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> cls,
    std::shared_ptr<BaseStrictObject> clsInfo);

} // namespace strictmod::objects

#endif // __STRICTM_BUILTINS_OBJ___

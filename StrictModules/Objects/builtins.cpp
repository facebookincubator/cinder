// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Objects/builtins.h"

#include "StrictModules/Objects/callable_wrapper.h"
#include "StrictModules/Objects/object_interface.h"
#include "StrictModules/Objects/objects.h"

#include "StrictModules/caller_context.h"
#include "StrictModules/caller_context_impl.h"
namespace strictmod::objects {
std::shared_ptr<BaseStrictObject> reprImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> value) {
  auto reprFunc = iLoadAttrOnType(
      value,
      kDunderRepr,
      makeUnknown(caller, "{}.__repr__", value->getTypeRef().getName()),
      caller);
  return iCall(std::move(reprFunc), kEmptyArgs, kEmptyArgNames, caller);
}
} // namespace strictmod::objects

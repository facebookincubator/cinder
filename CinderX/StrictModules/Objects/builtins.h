// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "StrictModules/Objects/object_interface.h"
#include "StrictModules/Objects/object_type.h"
#include "StrictModules/sequence_map.h"

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

std::shared_ptr<BaseStrictObject> lenImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> arg);

std::shared_ptr<BaseStrictObject> execImpl(
    std::shared_ptr<BaseStrictObject>,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    const CallerContext& caller);

std::shared_ptr<BaseStrictObject> evalImpl(
    std::shared_ptr<BaseStrictObject>,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    const CallerContext& caller);

std::shared_ptr<BaseStrictObject> iterImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> arg,
    std::shared_ptr<BaseStrictObject> sentinel = nullptr);

std::shared_ptr<BaseStrictObject> nextImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> iterator,
    std::shared_ptr<BaseStrictObject> defaultValue = nullptr);

std::shared_ptr<BaseStrictObject> reversedImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> arg);

std::shared_ptr<BaseStrictObject> enumerateImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> arg);

std::shared_ptr<BaseStrictObject> zipImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::vector<std::shared_ptr<BaseStrictObject>> args,
    sequence_map<std::string, std::shared_ptr<BaseStrictObject>>);

std::shared_ptr<BaseStrictObject> mapImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::vector<std::shared_ptr<BaseStrictObject>> args,
    sequence_map<std::string, std::shared_ptr<BaseStrictObject>>,
    std::shared_ptr<BaseStrictObject> func);

std::shared_ptr<BaseStrictObject> hashImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> arg);

std::shared_ptr<BaseStrictObject> absImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> arg);

std::shared_ptr<BaseStrictObject> roundImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> arg);

std::shared_ptr<BaseStrictObject> divmodImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> lhs,
    std::shared_ptr<BaseStrictObject> rhs);

std::shared_ptr<BaseStrictObject> chrImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> i);

std::shared_ptr<BaseStrictObject> ordImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> c);

std::shared_ptr<BaseStrictObject> getattrImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> name,
    std::shared_ptr<BaseStrictObject> defaultValue = nullptr);

std::shared_ptr<BaseStrictObject> setattrImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> name,
    std::shared_ptr<BaseStrictObject> value);

std::shared_ptr<BaseStrictObject> delattrImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> name);

std::shared_ptr<BaseStrictObject> hasattrImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> name);

std::shared_ptr<BaseStrictObject> isCallableImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> obj);

std::shared_ptr<BaseStrictObject> printImpl(
    std::shared_ptr<BaseStrictObject>,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    const CallerContext& caller);

std::shared_ptr<BaseStrictObject> inputImpl(
    std::shared_ptr<BaseStrictObject>,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    const CallerContext& caller);

std::shared_ptr<BaseStrictObject> maxImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::vector<std::shared_ptr<BaseStrictObject>> args,
    sequence_map<std::string, std::shared_ptr<BaseStrictObject>> kwargs,
    std::shared_ptr<BaseStrictObject> arg1);

std::shared_ptr<BaseStrictObject> minImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::vector<std::shared_ptr<BaseStrictObject>> args,
    sequence_map<std::string, std::shared_ptr<BaseStrictObject>> kwargs,
    std::shared_ptr<BaseStrictObject> arg1);

std::shared_ptr<BaseStrictObject> anyImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> iterable);

std::shared_ptr<BaseStrictObject> allImpl(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> iterable);

/** An version of isinstance that does not raise error
    for unknowns, but return False instead.
    This can only be used in stubs to imitate what otherwise
    can only be done when handcrafting an implementation such as:
        if not isinstance(obj, AbstractTuple):
            # put obj in an tuple even if obj is unknown
    This should be used sparingly
*/
std::shared_ptr<BaseStrictObject> looseIsinstance(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<BaseStrictObject> clsInfo);

/** deepcopy like operation. Does not work on all instances.
 * If copy failed, a strict module unsupport error is registered
 * and an unknown is returned
 */
std::shared_ptr<BaseStrictObject> strictCopy(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> inst);

/** Try getting a module by name, and return None if that module is
 *  not already imported.
 *  The implementation of this functin is empty, because the actual getting
 *  module require participation of the module loader. The actual
 *  handling of this logic is is visitCall (analyzer.cpp)
 */
std::shared_ptr<BaseStrictObject> strictTryImport(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> name);

/** Create an unknown object with given name. Callable
 *  from user code
 */
std::shared_ptr<BaseStrictObject> strictKnownUnknownObj(
    std::shared_ptr<BaseStrictObject>,
    const CallerContext& caller,
    std::shared_ptr<BaseStrictObject> name);

/** Create an unknown object with given func name and
 * arg, formatted as a function call. Callable
 *  from user code
 */
std::shared_ptr<BaseStrictObject> strictKnownUnknownCallable(
    std::shared_ptr<BaseStrictObject>,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& namedArgs,
    const CallerContext& caller);
} // namespace strictmod::objects

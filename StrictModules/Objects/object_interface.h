// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTM_OBJECT_INTERFACE_H__
#define __STRICTM_OBJECT_INTERFACE_H__

#include "StrictModules/caller_context.h"

#include "Python.h"

#include "Python-ast.h"

namespace strictmod::objects {
class BaseStrictObject;
class StrictType;
class StrictIteratorBase;

std::shared_ptr<BaseStrictObject> iGetDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<StrictType> type,
    const CallerContext& caller);

void iSetDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller);

void iDelDescr(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> inst,
    const CallerContext& caller);

std::shared_ptr<BaseStrictObject> iLoadAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    std::shared_ptr<BaseStrictObject> defaultValue,
    const CallerContext& caller);

std::shared_ptr<BaseStrictObject> iLoadAttrOnType(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    std::shared_ptr<BaseStrictObject> defaultValue,
    const CallerContext& caller);

void iStoreAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller);

void iDelAttr(
    std::shared_ptr<BaseStrictObject> obj,
    const std::string& key,
    const CallerContext& caller);

std::shared_ptr<BaseStrictObject> iBinOp(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> right,
    operator_ty op,
    const CallerContext& caller);

std::shared_ptr<BaseStrictObject> iReverseBinOp(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> left,
    operator_ty op,
    const CallerContext& caller);

std::shared_ptr<BaseStrictObject> iDoBinOp(
    std::shared_ptr<BaseStrictObject> left,
    std::shared_ptr<BaseStrictObject> right,
    operator_ty op,
    const CallerContext& caller);

std::shared_ptr<BaseStrictObject> iUnaryOp(
    std::shared_ptr<BaseStrictObject> obj,
    unaryop_ty op,
    const CallerContext& caller);

std::shared_ptr<BaseStrictObject> iBinCmpOp(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> right,
    cmpop_ty op,
    const CallerContext& caller);

std::shared_ptr<StrictIteratorBase> iGetElementsIter(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller);

std::vector<std::shared_ptr<BaseStrictObject>> iGetElementsVec(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller);

std::shared_ptr<BaseStrictObject> iGetElement(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> index,
    const CallerContext& caller);

void iSetElement(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> index,
    std::shared_ptr<BaseStrictObject> value,
    const CallerContext& caller);

void iDelElement(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> index,
    const CallerContext& caller);

bool iContainsElement(
    std::shared_ptr<BaseStrictObject> obj,
    std::shared_ptr<BaseStrictObject> index,
    const CallerContext& caller);

std::shared_ptr<BaseStrictObject> iCall(
    std::shared_ptr<BaseStrictObject> obj,
    const std::vector<std::shared_ptr<BaseStrictObject>>& args,
    const std::vector<std::string>& argNames,
    const CallerContext& caller);

std::shared_ptr<BaseStrictObject> iGetTruthValue(
    std::shared_ptr<BaseStrictObject> obj,
    const CallerContext& caller);

bool iStrictObjectEq(
    std::shared_ptr<BaseStrictObject> lhs,
    std::shared_ptr<BaseStrictObject> rhs,
    const CallerContext& caller);
} // namespace strictmod::objects

#endif // __STRICTM_OBJECT_INTERFACE_H__

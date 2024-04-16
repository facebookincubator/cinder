// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <string_view>

namespace jit::hir {

#define FOREACH_OPCODE(V)              \
  V(Assign)                            \
  V(BatchDecref)                       \
  V(BeginInlinedFunction)              \
  V(BinaryOp)                          \
  V(BitCast)                           \
  V(Branch)                            \
  V(BuildSlice)                        \
  V(BuildString)                       \
  V(CallCFunc)                         \
  V(CallEx)                            \
  V(CallExKw)                          \
  V(CallMethod)                        \
  V(CallStatic)                        \
  V(CallStaticRetVoid)                 \
  V(Cast)                              \
  V(CheckSequenceBounds)               \
  V(CheckErrOccurred)                  \
  V(CheckExc)                          \
  V(CheckNeg)                          \
  V(CheckVar)                          \
  V(CheckFreevar)                      \
  V(CheckField)                        \
  V(Compare)                           \
  V(CompareBool)                       \
  V(CopyDictWithoutKeys)               \
  V(CondBranch)                        \
  V(CondBranchIterNotDone)             \
  V(CondBranchCheckType)               \
  V(Decref)                            \
  V(DeleteAttr)                        \
  V(DeleteSubscr)                      \
  V(Deopt)                             \
  V(DeoptPatchpoint)                   \
  V(DictMerge)                         \
  V(DictSubscr)                        \
  V(DictUpdate)                        \
  V(DoubleBinaryOp)                    \
  V(EndInlinedFunction)                \
  V(FillTypeAttrCache)                 \
  V(FillTypeMethodCache)               \
  V(FormatValue)                       \
  V(GetAIter)                          \
  V(GetANext)                          \
  V(GetIter)                           \
  V(GetLength)                         \
  V(GetSecondOutput)                   \
  V(GetTuple)                          \
  V(Guard)                             \
  V(GuardIs)                           \
  V(GuardType)                         \
  V(HintType)                          \
  V(ImportFrom)                        \
  V(ImportName)                        \
  V(InPlaceOp)                         \
  V(Incref)                            \
  V(IndexUnbox)                        \
  V(InitialYield)                      \
  V(IntBinaryOp)                       \
  V(PrimitiveBoxBool)                  \
  V(PrimitiveBox)                      \
  V(PrimitiveCompare)                  \
  V(IntConvert)                        \
  V(PrimitiveUnaryOp)                  \
  V(PrimitiveUnbox)                    \
  V(InvokeIterNext)                    \
  V(InvokeMethod)                      \
  V(InvokeMethodStatic)                \
  V(IsInstance)                        \
  V(InvokeStaticFunction)              \
  V(IsNegativeAndErrOccurred)          \
  V(IsTruthy)                          \
  V(ListAppend)                        \
  V(ListExtend)                        \
  V(LoadArrayItem)                     \
  V(LoadFieldAddress)                  \
  V(LoadArg)                           \
  V(LoadAttr)                          \
  V(LoadAttrCached)                    \
  V(LoadAttrSpecial)                   \
  V(LoadAttrSuper)                     \
  V(LoadCellItem)                      \
  V(LoadConst)                         \
  V(LoadCurrentFunc)                   \
  V(LoadEvalBreaker)                   \
  V(LoadField)                         \
  V(LoadFunctionIndirect)              \
  V(LoadGlobalCached)                  \
  V(LoadGlobal)                        \
  V(LoadMethod)                        \
  V(LoadMethodCached)                  \
  V(LoadModuleMethodCached)            \
  V(LoadMethodSuper)                   \
  V(LoadSplitDictItem)                 \
  V(LoadTupleItem)                     \
  V(LoadTypeAttrCacheItem)             \
  V(LoadTypeMethodCacheEntryType)      \
  V(LoadTypeMethodCacheEntryValue)     \
  V(LoadVarObjectSize)                 \
  V(LongCompare)                       \
  V(LongBinaryOp)                      \
  V(MakeCheckedDict)                   \
  V(MakeCheckedList)                   \
  V(MakeCell)                          \
  V(MakeDict)                          \
  V(MakeFunction)                      \
  V(MakeList)                          \
  V(MakeTuple)                         \
  V(MakeSet)                           \
  V(MakeTupleFromList)                 \
  V(MatchClass)                        \
  V(MatchKeys)                         \
  V(MergeSetUnpack)                    \
  V(Phi)                               \
  V(Raise)                             \
  V(RaiseStatic)                       \
  V(RaiseAwaitableError)               \
  V(RefineType)                        \
  V(Return)                            \
  V(RunPeriodicTasks)                  \
  V(SetCellItem)                       \
  V(SetCurrentAwaiter)                 \
  V(SetDictItem)                       \
  V(SetFunctionAttr)                   \
  V(SetSetItem)                        \
  V(SetUpdate)                         \
  V(Snapshot)                          \
  V(StealCellItem)                     \
  V(StoreArrayItem)                    \
  V(StoreAttr)                         \
  V(StoreAttrCached)                   \
  V(StoreField)                        \
  V(StoreSubscr)                       \
  V(TpAlloc)                           \
  V(UnaryOp)                           \
  V(UnicodeCompare)                    \
  V(UnicodeConcat)                     \
  V(UnicodeRepeat)                     \
  V(UnicodeSubscr)                     \
  V(UnpackExToTuple)                   \
  V(Unreachable)                       \
  V(UseType)                           \
  V(VectorCall)                        \
  V(VectorCallStatic)                  \
  V(VectorCallKW)                      \
  V(WaitHandleLoadCoroOrResult)        \
  V(WaitHandleLoadWaiter)              \
  V(WaitHandleRelease)                 \
  V(XDecref)                           \
  V(XIncref)                           \
  V(YieldAndYieldFrom)                 \
  V(YieldFrom)                         \
  V(YieldFromHandleStopAsyncIteration) \
  V(YieldValue)

enum class Opcode {
#define DECLARE_OP(opname) k##opname,
  FOREACH_OPCODE(DECLARE_OP)
#undef DECLARE_OP
};

#define COUNT_OP(opname) +1
constexpr size_t kNumOpcodes = FOREACH_OPCODE(COUNT_OP);
#undef COUNT_OP

constexpr std::string_view opcodeName(Opcode op) {
  switch (op) {
#define NAME_OP(OP)   \
  case Opcode::k##OP: \
    return #OP;
    FOREACH_OPCODE(NAME_OP)
#undef NAME_OP
    default:
      return "<unknown>";
  }
}

} // namespace jit::hir

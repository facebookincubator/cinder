// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/lir/c_helper_translations.h"

#include "Jit/jit_rt.h"
#include "Jit/lir/c_helper_translations_auto.h"

#include <initializer_list>

namespace jit {
namespace lir {

#ifndef Py_TRACE_REFS
static_assert(offsetof(PyObject, ob_type) == 0x8);
static_assert(offsetof(PyTypeObject, tp_name) == 0x18);
#endif

static const std::initializer_list<std::pair<const uint64_t, std::string>>
    kCHelpersManual = {
// Hardcoded offset for PyObject::ob_type is invalid if Py_TRACE_REFS enabled
#ifndef Py_TRACE_REFS
        {reinterpret_cast<uint64_t>(JITRT_Cast), R"(Function:
BB %0 - succs: %2 %1
       %5:Object = LoadArg 0(0x0):Object
       %6:Object = LoadArg 1(0x1):Object
       %7:Object = Move [%5:Object + 0x8]:Object
       %8:Object = Equal %7:Object, %6:Object
                   CondBranch %8:Object

BB %1 - preds: %0 - succs: %2 %3
      %10:Object = Call PyType_IsSubtype, %7:Object, %6:Object
                   CondBranch %10:Object

BB %2 - preds: %0 %1 - succs: %4
                   Return %5:Object

BB %3 - preds: %1 - succs: %4
      %13:Object = Move [%7:Object + 0x18]:Object
      %14:Object = Move [%6:Object + 0x18]:Object
                   Call PyErr_Format, PyExc_TypeError, "expected '%s', got '%s'", %14:Object, %13:Object
      %16:Object = Move 0(0x0):Object
                   Return %16:Object

BB %4 - preds: %2 %3
)"},
#endif
        {reinterpret_cast<uint64_t>(JITRT_GetI32_FromArray), R"(Function:
BB %0 - succs: %7
       %1:Object = LoadArg 0(0x0):Object
        %2:64bit = LoadArg 1(0x1):Object
        %3:64bit = LoadArg 2(0x2):Object
        %4:64bit = Add %1:Object, %3:64bit
        %5:64bit = Move [%4:Object + %2:64bit * 8]:Object
                   Return %5:64bit

BB %7 - preds: %0
)"}};

const std::unordered_map<uint64_t, std::string> kCHelperMapping = [] {
  // Create map from the manual translations.
  std::unordered_map<uint64_t, std::string> map(kCHelpersManual);
  // Add the automatically generated translations.
  map.insert(kCHelperMappingAuto.begin(), kCHelperMappingAuto.end());
  return map;
}();

} // namespace lir
} // namespace jit

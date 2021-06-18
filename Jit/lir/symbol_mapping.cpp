// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/lir/symbol_mapping.h"
#include "Python.h"

namespace jit {
namespace lir {

const std::unordered_map<std::string, uint64_t> kSymbolMapping = {
    {"PyType_IsSubtype", reinterpret_cast<uint64_t>(PyType_IsSubtype)},
    {"PyErr_Format", reinterpret_cast<uint64_t>(PyErr_Format)},
    {"PyExc_TypeError", reinterpret_cast<uint64_t>(PyExc_TypeError)}};

} // namespace lir
} // namespace jit

// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/lir/symbol_mapping.h"

#include "Python.h"

namespace jit::lir {

const std::unordered_map<std::string, uint64_t> kSymbolMapping = {
    {"PyType_IsSubtype", reinterpret_cast<uint64_t>(PyType_IsSubtype)},
    {"PyErr_Format", reinterpret_cast<uint64_t>(PyErr_Format)},
    {"PyExc_TypeError", reinterpret_cast<uint64_t>(PyExc_TypeError)},
    {"PyLong_FromLong", reinterpret_cast<uint64_t>(PyLong_FromLong)},
    {"PyLong_FromUnsignedLong",
     reinterpret_cast<uint64_t>(PyLong_FromUnsignedLong)},
    {"PyLong_FromSsize_t", reinterpret_cast<uint64_t>(PyLong_FromSsize_t)},
    {"PyLong_FromSize_t", reinterpret_cast<uint64_t>(PyLong_FromSize_t)},
    {"PyLong_AsSize_t", reinterpret_cast<uint64_t>(PyLong_AsSize_t)},
    {"PyLong_AsSsize_t", reinterpret_cast<uint64_t>(PyLong_AsSsize_t)},
};

} // namespace jit::lir

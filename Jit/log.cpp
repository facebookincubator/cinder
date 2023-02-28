// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/log.h"

#include "Jit/threaded_compile.h"

namespace jit {

int g_debug = 0;
int g_debug_refcount = 0;
int g_debug_verbose = 0;
int g_dump_hir = 0;
int g_dump_hir_passes = 0;
const char* g_dump_hir_passes_json = nullptr;
int g_dump_final_hir = 0;
int g_dump_lir = 0;
int g_dump_lir_no_origin = 0;
int g_dump_c_helper = 0;
int g_dump_asm = 0;
int g_symbolize_funcs = 1;
int g_dump_stats = 0;
FILE* g_log_file = stderr;

std::string repr(BorrowedRef<> obj) {
  jit::ThreadedCompileSerialize guard;

  PyObject *t, *v, *tb;

  PyErr_Fetch(&t, &v, &tb);
  auto p_str =
      Ref<PyObject>::steal(PyObject_Repr(const_cast<PyObject*>(obj.get())));
  PyErr_Restore(t, v, tb);
  if (p_str == nullptr) {
    return fmt::format(
        "<failed to repr Python object of type %s>", Py_TYPE(obj)->tp_name);
  }
  Py_ssize_t len;
  const char* str = PyUnicode_AsUTF8AndSize(p_str, &len);
  if (str == nullptr) {
    return "<failed to get UTF8 from Python string>";
  }
  return {str, static_cast<std::string::size_type>(len)};
}

} // namespace jit

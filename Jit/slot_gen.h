#ifndef JIT_CODE_GEN_H
#define JIT_CODE_GEN_H

#include "Python.h"

#include "asmjit/asmjit.h"

#include "Jit/runtime.h"
#include "Jit/util.h"

namespace jit {

class SlotGen {
 public:
  SlotGen();

  /*
   * Generate a specialized slot function for a tp_call function that avoids the
   * lookups each time it's called.
   *
   * Returns NULL on error.
   */
  ternaryfunc genCallSlot(PyTypeObject* type, PyObject* call_func);

  /*
   * Generate a specialized slot function for a reprfunc (tp_repr or tp_str)
   * that avoids the method lookup each time it is called. repr_func - The
   * reprfunc method that should be called.
   *
   * Returns NULL on error.
   */
  reprfunc genReprFuncSlot(PyTypeObject* type, PyObject* repr_func);
  getattrofunc genGetAttrSlot(PyTypeObject* type, PyObject* call_func);
  descrgetfunc genGetDescrSlot(PyTypeObject* type, PyObject* get_func);

 private:
  DISALLOW_COPY_AND_ASSIGN(SlotGen);

  std::unique_ptr<asmjit::JitRuntime> jit_runtime_;
};

} // namespace jit

extern int g_gdb_stubs_support;

#endif /* JIT_CODE_GEN_H */

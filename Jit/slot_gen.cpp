// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
// Python.h includes pyconfig.h, which always defines _POSIX_C_SOURCE.  A
// number of libc headers define it if it hasn't yet been defined, so including
// Python.h first ensures we get the desired version with no warnings about
// redefinitions.
#include "Jit/slot_gen.h"

#include "Python.h"
#include "frameobject.h"
#include "opcode.h"

#include "Jit/jit_gdb_support.h"
#include "Jit/jit_rt.h"
#include "Jit/log.h"
#include "Jit/patternmatch.h"
#include "Jit/perf_jitdump.h"

#include <asmjit/asmjit.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <iostream>

using asmjit::Error;
using asmjit::ErrorHandler;
using asmjit::JitRuntime;
using asmjit::Label;
namespace x86 = asmjit::x86;

int g_gdb_stubs_support;

namespace jit {

SlotGen::SlotGen() {
  jit_runtime_ = std::make_unique<JitRuntime>();
}

class SimpleErrorHandler : public ErrorHandler {
 public:
  inline SimpleErrorHandler() : err(asmjit::kErrorOk) {}

  void handleError(
      Error err,
      const char* message,
      __attribute__((unused)) asmjit::BaseEmitter* origin) override {
    this->err = err;
    fprintf(stderr, "ERROR: %s\n", message);
  }

  Error err;
};

void* GenFunc(
    JitRuntime& jit,
    const char* name,
    const std::function<void(x86::Builder&)>& f) {
  asmjit::CodeHolder code;
  code.init(jit.codeInfo());
  SimpleErrorHandler eh;
  code.setErrorHandler(&eh);
  x86::Builder as(&code);

  f(as);

  as.finalize();

  void* func;
  Error err = jit.add(&func, &code);
  if (err) {
    return nullptr;
  }

  auto code_size = code.textSection()->realSize();
  if (g_gdb_stubs_support) {
    register_raw_debug_symbol(
        name, __FILE__, __LINE__, (void*)func, code_size, 0);
  }

  jit::perf::registerFunction(
      func, code_size, name, jit::perf::kFuncSymbolPrefix);

  return func;
}

/*
 * The standard C prologue and epilogue
 */
static void emit_prologue(x86::Builder& as) {
  as.push(x86::rbp);
  as.mov(x86::rbp, x86::rsp);
}

static void emit_epilogue(x86::Builder& as) {
  as.mov(x86::rsp, x86::rbp);
  as.pop(x86::rbp);
  as.ret();
}

void decref(x86::Builder& as, x86::Gp reg, x86::Gp tmp) {
  Label end_decref = as.newLabel();

#ifdef Py_DEBUG
  as.mov(tmp, reinterpret_cast<uint64_t>(&_Py_RefTotal));
  as.dec(x86::ptr(tmp, 0, sizeof(_Py_RefTotal)));
#endif
  as.mov(tmp, x86::ptr(reg, offsetof(PyObject, ob_refcnt)));
#ifdef Py_IMMORTAL_INSTANCES
  as.bt(tmp, kImmortalBitPos);
  as.jc(end_decref);
#endif
  as.sub(tmp, 1);
  as.mov(x86::ptr(reg, offsetof(PyObject, ob_refcnt)), tmp);
  as.jnz(end_decref);
  if (reg != x86::rdi) {
    as.mov(x86::rdi, reg);
  }
  as.call((size_t)JITRT_Dealloc);
  as.bind(end_decref);
}

static void incref(x86::Builder& as, x86::Gp reg, x86::Gp tmp) {
  Label end_decref = as.newLabel();

#ifdef Py_DEBUG
  as.mov(tmp, reinterpret_cast<uint64_t>(&_Py_RefTotal));
  as.inc(x86::ptr(tmp, 0, sizeof(_Py_RefTotal)));
#endif
  as.mov(tmp, x86::ptr(reg, offsetof(PyObject, ob_refcnt)));
#ifdef Py_IMMORTAL_INSTANCES
  as.bt(tmp, kImmortalBitPos);
  as.jc(end_decref);
#endif
  as.add(tmp, 1);
  as.mov(x86::ptr(reg, offsetof(PyObject, ob_refcnt)), tmp);
  as.bind(end_decref);
}

static void shiftargs_for_prepend(x86::Builder& as, PyObject* func) {
  as.mov(x86::rcx, x86::rdx);
  as.mov(x86::rdx, x86::rsi);
  as.mov(x86::rsi, x86::rdi);
  as.mov(x86::rdi, (uint64_t)func);
}

static void gen_fused_call_slot(x86::Builder& as, PyObject* callfunc) {
  shiftargs_for_prepend(as, callfunc);
  as.mov(x86::rax, (uint64_t)_PyObject_Call_Prepend);
  as.jmp(x86::rax);
}

ternaryfunc SlotGen::genCallSlot(
    PyTypeObject* /* type */,
    PyObject* call_func) {
  return (ternaryfunc)GenFunc(
      *jit_runtime_, "__call__", [&](x86::Builder& as) -> void {
        gen_fused_call_slot(as, call_func);
      });
}

static void gen_fused_reprfunc(x86::Builder& as, PyObject* repr_func) {
  /*
   * We're called with self in rdi and that's it. We need to set up for
   * func_entry_po, which takes:
   * rdi = function pointer
   * rsi = PyObject** to argument list
   * rdx = nargs (always 1 here)
   * kwnames - NULL
   */
  emit_prologue(as);
  as.sub(x86::rsp, 16);
  as.mov(x86::ptr(x86::rsp), x86::rdi);
  as.lea(x86::rsi, x86::ptr(x86::rsp));
  as.mov(x86::rdi, (uint64_t)repr_func);
  as.mov(x86::rdx, 1);
  as.mov(x86::rax, x86::ptr(x86::rdi, offsetof(PyFunctionObject, vectorcall)));
  as.xor_(x86::rcx, x86::rcx);
  as.call(x86::rax);
  emit_epilogue(as);
  as.ret();
}

reprfunc SlotGen::genReprFuncSlot(
    PyTypeObject* /* type */,
    PyObject* repr_func) {
  return (reprfunc)GenFunc(
      *jit_runtime_, "__repr__", [&](x86::Builder& as) -> void {
        gen_fused_reprfunc(as, repr_func);
      });
}

PyObject* getattr_fallback(PyObject* self, PyObject* func, PyObject* name) {
  if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
    PyErr_Clear();
    PyObject* args[2] = {self, name};
    return _PyFunction_FastCallDict(func, args, 2, NULL);
  }
  return NULL;
}

static void gen_fused_getattro_slot(x86::Builder& as, PyObject* callfunc) {
  emit_prologue(as);

  Label done = as.newLabel();
  as.push(x86::rdi); // self
  as.push(x86::rsi); // name

  // PyObject_GenericGetAttr can mutate the type and
  // eliminate the function (TODO: Could skip this if
  // the type is immutable).
  as.mov(x86::rax, (uint64_t)callfunc);
  incref(as, x86::rax, x86::rdx);

  as.mov(x86::rax, (uint64_t)&PyObject_GenericGetAttr);
  as.call(x86::rax);
  as.test(x86::rax, x86::rax);
  as.jnz(done);

  as.mov(x86::rdi, x86::ptr(x86::rsp, 8));
  as.mov(x86::rsi, (uint64_t)callfunc);
  as.mov(x86::rdx, x86::ptr(x86::rsp));
  as.mov(x86::rax, (uint64_t)getattr_fallback);
  as.call(x86::rax);

  as.bind(done);

  as.mov(x86::rsi, (uint64_t)callfunc);
  decref(as, x86::rsi, x86::rdi);

  emit_epilogue(as);
}

getattrofunc SlotGen::genGetAttrSlot(
    PyTypeObject* /* type */,
    PyObject* call_func) {
  return (getattrofunc)GenFunc(
      *jit_runtime_, "__getattr__", [&](x86::Builder& as) -> void {
        gen_fused_getattro_slot(as, call_func);
      });
}

static void gen_fused_get_slot(x86::Builder& as, PyObject* callfunc) {
  emit_prologue(as);

  // one extra push to keep the stack 16-byte aligned
  as.push(0);

  // push args for function call in reverse order
  // type
  Label type_set = as.newLabel();
  as.cmp(x86::rdx, 0);
  as.jne(type_set);
  as.mov(x86::rdx, (uint64_t)Py_None);
  as.bind(type_set);
  as.push(x86::rdx);

  // obj
  Label obj_set = as.newLabel();
  as.cmp(x86::rsi, 0);
  as.jne(obj_set);
  as.mov(x86::rsi, (uint64_t)Py_None);
  as.bind(obj_set);
  as.push(x86::rsi);

  // self
  as.push(x86::rdi);

  assert(PyFunction_Check(callfunc));

  // We indirect through the function object because it's probably
  // not JITed yet
  as.mov(x86::rdi, (uint64_t)callfunc);

  // kwnames should be NULL
  as.xor_(x86::rcx, x86::rcx);

  as.mov(x86::rax, x86::ptr(x86::rdi, offsetof(PyFunctionObject, vectorcall)));
  as.mov(x86::rsi, x86::rsp);
  as.mov(x86::rdx, 3);
  as.call(x86::rax);
  emit_epilogue(as);
}

descrgetfunc SlotGen::genGetDescrSlot(PyTypeObject* type, PyObject* get_func) {
  char name[181];
  snprintf(name, 181, "%s::__get__", type->tp_name);

  return (descrgetfunc)GenFunc(
      *jit_runtime_, name, [&](x86::Builder& as) -> void {
        gen_fused_get_slot(as, get_func);
      });
}

} // namespace jit

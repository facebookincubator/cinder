#include "Jit/jit_rt.h"

#include <Python.h>
#include "Jit/codegen/gen_asm.h"
#include "Jit/frame.h"
#include "Jit/log.h"
#include "Jit/pyjit.h"
#include "Jit/ref.h"
#include "Jit/runtime.h"
#include "Jit/util.h"

#include "Objects/dict-common.h"
#include "classloader.h"
#include "frameobject.h"
#include "listobject.h"
#include "object.h"
#include "pystate.h"
#include "switchboard.h"

// clang-format off
#include "internal/pycore_pyerrors.h"
#include "internal/pycore_pystate.h"
#include "internal/pycore_object.h"
#include "internal/pycore_tupleobject.h"
// clang-format on

// This is mostly taken from ceval.c _PyEval_EvalCodeWithName
// We use the same logic to turn **args, nargsf, and kwnames into
// **args / nargsf.
// One significant difference is we don't need to incref the args
// in the new array.
static int JITRT_BindKeywordArgs(
    PyFunctionObject* func,
    PyObject** args,
    size_t nargsf,
    PyObject* kwnames,
    PyObject** arg_space,
    Py_ssize_t total_args,
    Ref<PyObject>& kwdict,
    Ref<PyObject>& varargs) {
  PyCodeObject* co = (PyCodeObject*)func->func_code;
  Py_ssize_t argcount = PyVectorcall_NARGS(nargsf);

  for (int i = 0; i < total_args; i++) {
    arg_space[i] = NULL;
  }

  // Create a dictionary for keyword parameters (**kwags)
  if (co->co_flags & CO_VARKEYWORDS) {
    kwdict = Ref<>::steal(PyDict_New());
    if (kwdict == NULL) {
      return 0;
    }
    arg_space[total_args - 1] = kwdict;
  }

  // Copy all positional arguments into local variables
  Py_ssize_t n = std::min<Py_ssize_t>(argcount, co->co_argcount);
  for (Py_ssize_t j = 0; j < n; j++) {
    arg_space[j] = args[j];
  }

  // Pack other positional arguments into the *args argument
  if (co->co_flags & CO_VARARGS) {
    varargs = Ref<>::steal(_PyTuple_FromArray(args + n, argcount - n));
    if (varargs == NULL) {
      return 0;
    }

    Py_ssize_t i = total_args - 1;
    if (co->co_flags & CO_VARKEYWORDS) {
      i--;
    }
    arg_space[i] = varargs;
  }

  // Handle keyword arguments passed as two strided arrays
  if (kwnames != NULL) {
    for (Py_ssize_t i = 0; i < PyTuple_Size(kwnames); i++) {
      PyObject** co_varnames;
      PyObject* keyword = PyTuple_GET_ITEM(kwnames, i);
      PyObject* value = args[argcount + i];
      Py_ssize_t j;

      if (keyword == NULL || !PyUnicode_Check(keyword)) {
        return 0;
      }

      // Speed hack: do raw pointer compares. As names are
      //    normally interned this should almost always hit.
      co_varnames = ((PyTupleObject*)(co->co_varnames))->ob_item;
      for (j = co->co_posonlyargcount; j < total_args; j++) {
        PyObject* name = co_varnames[j];
        if (name == keyword) {
          goto kw_found;
        }
      }

      // Slow fallback, just in case
      for (j = co->co_posonlyargcount; j < total_args; j++) {
        PyObject* name = co_varnames[j];
        int cmp = PyObject_RichCompareBool(keyword, name, Py_EQ);
        if (cmp > 0) {
          goto kw_found;
        } else if (cmp < 0) {
          return 0;
        }
      }

      if (kwdict == NULL || PyDict_SetItem(kwdict, keyword, value) == -1) {
        return 0;
      }
      continue;

    kw_found:
      if (arg_space[j] != NULL) {
        return 0;
      }
      arg_space[j] = value;
    }
  }

  // Check the number of positional arguments
  if ((argcount > co->co_argcount) && !(co->co_flags & CO_VARARGS)) {
    return 0;
  }

  // Add missing positional arguments (copy default values from defs)
  if (argcount < co->co_argcount) {
    Py_ssize_t defcount;
    if (func->func_defaults != NULL) {
      defcount = PyTuple_Size(func->func_defaults);
    } else {
      defcount = 0;
    }
    Py_ssize_t m = co->co_argcount - defcount;
    Py_ssize_t missing = 0;
    for (Py_ssize_t i = argcount; i < m; i++) {
      if (arg_space[i] == NULL) {
        missing++;
      }
    }
    if (missing) {
      return 0;
    }

    if (defcount) {
      PyObject* const* defs =
          &((PyTupleObject*)func->func_defaults)->ob_item[0];
      for (Py_ssize_t i = std::max<Py_ssize_t>(n - m, 0); i < defcount; i++) {
        if (arg_space[m + i] == NULL) {
          PyObject* def = defs[i];
          arg_space[m + i] = def;
        }
      }
    }
  }

  // Add missing keyword arguments (copy default values from kwdefs)
  if (co->co_kwonlyargcount > 0) {
    Py_ssize_t missing = 0;
    PyObject* kwdefs = func->func_kwdefaults;
    for (Py_ssize_t i = co->co_argcount; i < total_args; i++) {
      PyObject* name;
      if (arg_space[i] != NULL)
        continue;
      name = PyTuple_GET_ITEM(co->co_varnames, i);
      if (kwdefs != NULL) {
        PyObject* def = PyDict_GetItemWithError(kwdefs, name);
        if (def) {
          arg_space[i] = def;
          continue;
        } else if (_PyErr_Occurred(_PyThreadState_GET())) {
          return 0;
        }
      }
      missing++;
    }
    if (missing) {
      return 0;
    }
  }

  return 1;
}

// This uses JITRT_BindKeywordArgs to get the newly bound keyword
// arguments.   We then turn around and dispatch to the
// JITed function with the newly packed args.
// Rather than copying over all of the error reporting we instead
// just dispatch to the normal _PyFunction_Vectorcall if anything
// goes wrong which is indicated by JITRT_BindKeywordArgs returning 0.
PyObject* JITRT_CallWithKeywordArgs(
    PyFunctionObject* func,
    PyObject** args,
    size_t nargsf,
    PyObject* kwnames) {
  PyCodeObject* co = (PyCodeObject*)func->func_code;
  const Py_ssize_t total_args = co->co_argcount + co->co_kwonlyargcount +
      ((co->co_flags & CO_VARKEYWORDS) ? 1 : 0) +
      ((co->co_flags & CO_VARARGS) ? 1 : 0);
  PyObject* arg_space[total_args];
  Ref<PyObject> kwdict, varargs;

  if (JITRT_BindKeywordArgs(
          func,
          args,
          nargsf,
          kwnames,
          arg_space,
          total_args,
          kwdict,
          varargs)) {
    return JITRT_GET_REENTRY(func->vectorcall)(
        (PyObject*)func,
        arg_space,
        total_args | (nargsf & (_Py_AWAITED_CALL_MARKER)),
        nullptr);
  }

  return _PyFunction_Vectorcall((PyObject*)func, args, nargsf, kwnames);
}

PyObject* JITRT_CallWithIncorrectArgcount(
    PyFunctionObject* func,
    PyObject** args,
    size_t nargsf,
    int argcount) {
  auto fail = [&] {
    // Fallback to the default _PyFunction_Vectorcall implementation
    // to produce an appropriate exception.
    return _PyFunction_Vectorcall((PyObject*)func, args, nargsf, NULL);
  };

  PyObject* defaults = func->func_defaults;
  if (defaults == nullptr) {
    // Function has no defaults; there's nothing we can do.
    return fail();
  }
  Py_ssize_t defcount = PyTuple_GET_SIZE(defaults);
  Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
  PyObject* arg_space[argcount];

  if (nargs + defcount < argcount || nargs > argcount) {
    // Not enough args with defaults, or too many args without defaults.
    return fail();
  }

  Py_ssize_t i;
  for (i = 0; i < nargs; i++) {
    arg_space[i] = *args++;
  }

  PyObject** def_items =
      &((PyTupleObject*)defaults)->ob_item[defcount - (argcount - nargs)];
  for (; i < argcount; i++) {
    arg_space[i] = *def_items++;
  }

  return JITRT_GET_REENTRY(func->vectorcall)(
      (PyObject*)func,
      arg_space,
      argcount | (nargsf & (_Py_AWAITED_CALL_MARKER)),
      NULL);
}

PyObject* JITRT_CallStaticallyWithPrimitiveSignatureWorker(
    PyFunctionObject* func,
    PyObject** args,
    size_t nargsf,
    _PyTypedArgsInfo* arg_info) {
  Py_ssize_t arg_index = 0;
  Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
  void* arg_space[nargs];
  int invoked_statically = (nargsf & _Py_VECTORCALL_INVOKED_STATICALLY) != 0;

  for (Py_ssize_t i = 0; i < nargs; i++) {
    if (arg_index < Py_SIZE(arg_info) &&
        arg_info->tai_args[arg_index].tai_argnum == i) {
      _PyTypedArgInfo* cur_arg = &arg_info->tai_args[arg_index];
      PyObject* arg = args[i];
      if (cur_arg->tai_primitive_type == -1) {
        if (!invoked_statically &&
            !_PyObject_TypeCheckOptional(
                arg, cur_arg->tai_type, cur_arg->tai_optional)) {
          goto fail;
        }
        arg_space[i] = arg;
      } else {
        // Primitive arg check
        if (Py_TYPE(arg) != &PyLong_Type ||
            !_PyClassLoader_OverflowCheck(
                arg, cur_arg->tai_primitive_type, (size_t*)&arg_space[i])) {
          goto fail;
        }
      }
      arg_index++;
      continue;
    }
    arg_space[i] = args[i];
  }

  return JITRT_GET_REENTRY(func->vectorcall)(
      (PyObject*)func, (PyObject**)arg_space, nargsf, NULL);

fail:
  return _PyFunction_Vectorcall((PyObject*)func, args, nargsf, NULL);
}

PyObject* JITRT_CallStaticallyWithPrimitiveSignature(
    PyFunctionObject* func,
    PyObject** args,
    size_t nargsf,
    PyObject* kwnames,
    _PyTypedArgsInfo* arg_info) {
  Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
  PyCodeObject* co = (PyCodeObject*)func->func_code;

  int invoked_statically = (nargsf & _Py_VECTORCALL_INVOKED_STATICALLY) != 0;
  if (!invoked_statically &&
      (kwnames || nargs != co->co_argcount ||
       co->co_flags & (CO_VARARGS | CO_VARKEYWORDS))) {
    // we need to fixup kwnames, defaults, etc...
    PyCodeObject* co = (PyCodeObject*)func->func_code;
    const Py_ssize_t total_args = co->co_argcount + co->co_kwonlyargcount +
        ((co->co_flags & CO_VARKEYWORDS) ? 1 : 0) +
        ((co->co_flags & CO_VARARGS) ? 1 : 0);
    PyObject* arg_space[total_args];
    Ref<PyObject> kwdict, varargs;

    if (JITRT_BindKeywordArgs(
            func,
            args,
            nargsf,
            kwnames,
            arg_space,
            total_args,
            kwdict,
            varargs)) {
      return JITRT_CallStaticallyWithPrimitiveSignatureWorker(
          func, arg_space, total_args | PyVectorcall_FLAGS(nargsf), arg_info);
    }

    return _PyFunction_Vectorcall((PyObject*)func, args, nargsf, kwnames);
  }

  return JITRT_CallStaticallyWithPrimitiveSignatureWorker(
      func, args, nargsf, arg_info);
}

PyObject* JITRT_ReportStaticArgTypecheckErrors(
    PyObject* func,
    PyObject** args,
    size_t nargsf,
    PyObject* kwnames) {
  JIT_CHECK(
      kwnames == nullptr,
      "typecheck error reporting must happen after keyword binding");
  auto code = reinterpret_cast<PyCodeObject*>(
      reinterpret_cast<PyFunctionObject*>(func)->func_code);
  int nkwonly = code->co_kwonlyargcount;
  if (code == nullptr || nkwonly == 0) {
    return _PyFunction_Vectorcall(func, args, nargsf, kwnames);
  }
  // This function is called after we've successfully bound all
  // arguments. However, we want to use the interpreter to construct the
  // typecheck error. If the function takes any keyword-only arguments we must
  // reconstruct kwnames so the the interpreted "prologue" in
  // _PyEval_EvalCodeWithName can validate that the keyword-only arguments were
  // passed as keywords.
  Ref<> new_kwnames = Ref<>::steal(PyTuple_New(nkwonly));
  if (new_kwnames == nullptr) {
    return nullptr;
  }
  for (Py_ssize_t i = code->co_argcount; i < code->co_argcount + nkwonly; i++) {
    Ref<> name(PyTuple_GetItem(code->co_varnames, i));
    PyTuple_SetItem(new_kwnames, i - code->co_argcount, std::move(name));
  }
  Py_ssize_t nargs = PyVectorcall_NARGS(nargsf) - nkwonly;
  if (code->co_flags & CO_VARKEYWORDS) {
    nargs -= 1;
  }
  Py_ssize_t flags = PyVectorcall_FLAGS(nargsf);
  return _PyFunction_Vectorcall(func, args, nargs | flags, new_kwnames);
}

PyThreadState* JITRT_AllocateAndLinkFrame(
    PyCodeObject* code,
    PyObject* globals) {
  PyThreadState* tstate = PyThreadState_GET();
  JIT_DCHECK(tstate != NULL, "thread state cannot be null");

  /* Allocate the frame object */
  if (code->co_zombieframe != NULL) {
    __builtin_prefetch(code->co_zombieframe);
  }
  /* TODO(T45035726) - This is doing more work than it needs to. Compiled code
   * doesn't use the frame object at all. It's only there to ensure PyPerf works
   * correctly, and PyPerf only needs access to the first argument.
   */
  PyObject* builtins = PyEval_GetBuiltins();
  if (builtins == NULL) {
    return NULL;
  }

  Py_INCREF(builtins);
  PyFrameObject* frame =
      _PyFrame_NewWithBuiltins_NoTrack(tstate, code, globals, builtins, NULL);

  if (frame == NULL) {
    Py_DECREF(builtins);
    return NULL;
  }

  /* Set the currently-executing flag on the frame */
  frame->f_executing = 1;

  tstate->frame = frame;

  return tstate;
}

PyThreadState* JITRT_AllocateAndLinkTinyFrame(
    PyCodeObject* code,
    PyObject* globals) {
  PyThreadState* tstate = PyThreadState_GET();
  JIT_DCHECK(tstate != NULL, "thread state cannot be null");

  TinyFrame* frame = JIT_TinyFrameAllocate(tstate, code, globals);

  tstate->frame = (PyFrameObject*)frame;

  return tstate;
}

void JITRT_UnlinkFrame(PyThreadState* tstate) {
  PyFrameObject* f = tstate->frame;
  f->f_executing = 0;

  if (Py_REFCNT(f) > 1) {
    Py_DECREF(f);
    if (!_PyObject_GC_IS_TRACKED(f)) {
      _PyObject_GC_TRACK(f);
    }
    tstate->frame = JIT_MaterializePrevFrame(f);
  } else {
    tstate->frame = f->f_back;
    Py_DECREF(f);
  }
}

void JITRT_UnlinkTinyFrame(PyThreadState* tstate) {
  PyFrameObject* frame = tstate->frame;
  if (!JIT_IsTinyFrame(frame)) {
    JITRT_UnlinkFrame(tstate);
    return;
  }
  TinyFrame* f = (TinyFrame*)frame;
  tstate->frame = f->t.f_back;
  Py_DECREF(f);
}

void JITRT_InitialYieldUnlinkFrame(PyThreadState* tstate) {
  PyFrameObject* f = tstate->frame;
  tstate->frame = f->f_back;
  Py_CLEAR(f->f_back);
  f->f_executing = 0;

  JIT_DCHECK(Py_REFCNT(f) > 1, "Generator object should reference frame");
  Py_DECREF(f);
  JIT_DCHECK(!_PyObject_GC_IS_TRACKED(f), "Frame should not yet be GC tracked");
  _PyObject_GC_TRACK(f);
}

void JITRT_InitialYieldUnlinkTinyFrame(PyThreadState*) {
  JIT_CHECK(false, "Tiny frame not supported with JIT generators");
}

PyObject*
JITRT_LoadGlobal(PyObject* globals, PyObject* builtins, PyObject* name) {
  PyObject* result =
      _PyDict_LoadGlobal((PyDictObject*)globals, (PyDictObject*)builtins, name);
  if ((result == NULL) && !_PyErr_OCCURRED()) {
    PyErr_Format(PyExc_NameError, "name '%.200U' is not defined", name);
  }
  Py_XINCREF(result);
  return result;
}

template <bool is_awaited>
static inline PyObject*
call_function(PyObject* func, PyObject** args, Py_ssize_t nargs) {
  size_t flags = PY_VECTORCALL_ARGUMENTS_OFFSET |
      (is_awaited ? _Py_AWAITED_CALL_MARKER : 0);
  return _PyObject_Vectorcall(func, args + 1, (nargs - 1) | flags, NULL);
}

PyObject*
JITRT_CallFunction(PyObject* func, PyObject** args, Py_ssize_t nargs) {
  return call_function<false>(func, args, nargs);
}

PyObject*
JITRT_CallFunctionAwaited(PyObject* func, PyObject** args, Py_ssize_t nargs) {
  return call_function<true>(func, args, nargs);
}

template <bool is_awaited>
static inline PyObject*
call_function_kwargs(PyObject* func, PyObject** args, Py_ssize_t nargs) {
  PyObject* kwargs = args[nargs - 1];
  JIT_DCHECK(PyTuple_CheckExact(kwargs), "Kwargs map must be a tuple");
  nargs--;
  Py_ssize_t nkwargs = PyTuple_GET_SIZE(kwargs);
  JIT_DCHECK(nkwargs < nargs, "Kwargs map too large");
  nargs -= nkwargs;
  size_t flags = PY_VECTORCALL_ARGUMENTS_OFFSET |
      (is_awaited ? _Py_AWAITED_CALL_MARKER : 0);
  return _PyObject_Vectorcall(func, args + 1, (nargs - 1) | flags, kwargs);
}

PyObject*
JITRT_CallFunctionKWArgs(PyObject* func, PyObject** args, Py_ssize_t nargs) {
  return call_function_kwargs<false>(func, args, nargs);
}

PyObject* JITRT_CallFunctionKWArgsAwaited(
    PyObject* func,
    PyObject** args,
    Py_ssize_t nargs) {
  return call_function_kwargs<true>(func, args, nargs);
}

template <bool is_awaited>
static inline PyObject*
call_function_ex(PyObject* func, PyObject* pargs, PyObject* kwargs) {
  // Normalize p + kw args to tuple and dict types exactly.
  Ref<> new_pargs;
  // Logically, I don't think this incref of kwargs is needed but not having it
  // breaks the C-version of functools.partial. The problem is a ref-count of 1
  // on "kw" going into partial_new() triggers an optimization where the kwargs
  // are not copied. This fails test_functoools.TestPartial*.test_kwargs_copy
  // which asserts it's not possible to alter the kwargs after the call. A
  // tempting alternative to this explicit ref managment is to set-up
  // the memory effects of CallEx to steal the kwargs input. Unfortunately this
  // breaks test_contextlib.ContextManagerTestCase.test_nokeepref by keeping
  // kwargs and their contents alive for longer than expected.
  Ref<> new_kwargs{kwargs};
  if (kwargs) {
    if (!PyDict_CheckExact(kwargs)) {
      PyObject* d = PyDict_New();
      if (d == NULL) {
        return NULL;
      }
      if (PyDict_Update(d, kwargs) != 0) {
        Py_DECREF(d);
        if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
          PyErr_Format(
              PyExc_TypeError,
              "%.200s%.200s argument after ** "
              "must be a mapping, not %.200s",
              PyEval_GetFuncName(func),
              PyEval_GetFuncDesc(func),
              kwargs->ob_type->tp_name);
        }
        return NULL;
      }
      kwargs = d;
      new_kwargs = Ref<>::steal(kwargs);
    }
    JIT_DCHECK(PyDict_CheckExact(kwargs), "Expect kwargs to be a dict");
  }
  if (!PyTuple_CheckExact(pargs)) {
    if (pargs->ob_type->tp_iter == NULL && !PySequence_Check(pargs)) {
      PyErr_Format(
          PyExc_TypeError,
          "%.200s%.200s argument after * "
          "must be an iterable, not %.200s",
          PyEval_GetFuncName(func),
          PyEval_GetFuncDesc(func),
          pargs->ob_type->tp_name);
      return NULL;
    }
    pargs = PySequence_Tuple(pargs);
    if (pargs == NULL) {
      return NULL;
    }
    new_pargs = Ref<>::steal(pargs);
  }
  JIT_DCHECK(PyTuple_CheckExact(pargs), "Expected pargs to be a tuple");

  // Make function call using normalized args.
  if (PyCFunction_Check(func)) {
    // TODO(jbower): For completeness we should use a vector-call if possible to
    // take into account is_awaited. My guess is there aren't going to be many C
    // functions which handle _Py_AWAITED_CALL_MARKER.
    return PyCFunction_Call(func, pargs, kwargs);
  }
  if (is_awaited && _PyVectorcall_Function(func) != NULL) {
    return _PyVectorcall_Call(func, pargs, kwargs, _Py_AWAITED_CALL_MARKER);
  }
  return PyObject_Call(func, pargs, kwargs);
}

PyObject* JITRT_LoadFunctionIndirect(PyObject** func, PyObject* descr) {
  PyObject* res = *func;
  if (!res) {
    res = _PyClassLoader_ResolveFunction(descr, NULL);
    if (res == NULL) {
      PyErr_Format(PyExc_TypeError, "unknown function %R", descr);
      return NULL;
    }
    Py_DECREF(res);
  }

  return res;
}

PyObject*
JITRT_CallFunctionEx(PyObject* func, PyObject* pargs, PyObject* kwargs) {
  return call_function_ex<false>(func, pargs, kwargs);
}

PyObject*
JITRT_CallFunctionExAwaited(PyObject* func, PyObject* pargs, PyObject* kwargs) {
  return call_function_ex<true>(func, pargs, kwargs);
}

template <bool is_awaited>
static inline PyObject*
invoke_function(PyObject* func, PyObject** args, Py_ssize_t nargs) {
  size_t flags = _Py_VECTORCALL_INVOKED_STATICALLY |
      PY_VECTORCALL_ARGUMENTS_OFFSET |
      (is_awaited ? _Py_AWAITED_CALL_MARKER : 0);
  return _PyObject_Vectorcall(func, args + 1, (nargs - 1) | flags, NULL);
}

PyObject*
JITRT_InvokeFunction(PyObject* func, PyObject** args, Py_ssize_t nargs) {
  return invoke_function<false>(func, args, nargs);
}

PyObject*
JITRT_InvokeFunctionAwaited(PyObject* func, PyObject** args, Py_ssize_t nargs) {
  return invoke_function<true>(func, args, nargs);
}

template <bool is_awaited>
static inline PyObject* call_method(
    PyObject* callable,
    PyObject** args,
    Py_ssize_t nargs,
    PyObject* kwnames,
    JITRT_CallMethodKind call_kind) {
  size_t is_awaited_flag = is_awaited ? _Py_AWAITED_CALL_MARKER : 0;
  switch (call_kind) {
    case JITRT_CALL_KIND_FUNC: {
      PyFunctionObject* func = (PyFunctionObject*)callable;
      return func->vectorcall(
          callable,
          args,
          nargs | _Py_VECTORCALL_INVOKED_METHOD | is_awaited_flag,
          kwnames);
    }
    case JITRT_CALL_KIND_METHOD_DESCR: {
      PyMethodDescrObject* func = (PyMethodDescrObject*)callable;
      return func->vectorcall(
          callable,
          args,
          nargs | _Py_VECTORCALL_INVOKED_METHOD | is_awaited_flag,
          kwnames);
    }
    case JITRT_CALL_KIND_METHOD_LIKE: {
      return _PyObject_Vectorcall(
          callable,
          args,
          nargs | _Py_VECTORCALL_INVOKED_METHOD | is_awaited_flag,
          kwnames);
    }
    case JITRT_CALL_KIND_WRAPPER_DESCR: {
      PyWrapperDescrObject* func = (PyWrapperDescrObject*)callable;
      return func->d_vectorcall(
          callable,
          args,
          nargs | _Py_VECTORCALL_INVOKED_METHOD | is_awaited_flag,
          kwnames);
    }
    default: {
      // Slow path, should rarely get here
      JIT_DCHECK(kwnames == nullptr, "kwnames not supported yet");
      return _PyObject_Vectorcall(
          callable,
          args + 1,
          (nargs - 1) | PY_VECTORCALL_ARGUMENTS_OFFSET | is_awaited_flag,
          kwnames);
    }
  }
}

PyObject* JITRT_CallMethod(
    PyObject* callable,
    PyObject** args,
    Py_ssize_t nargs,
    PyObject* kwnames,
    JITRT_CallMethodKind call_kind) {
  return call_method<false>(callable, args, nargs, kwnames, call_kind);
}

PyObject* JITRT_CallMethodAwaited(
    PyObject* callable,
    PyObject** args,
    Py_ssize_t nargs,
    PyObject* kwnames,
    JITRT_CallMethodKind call_kind) {
  return call_method<true>(callable, args, nargs, kwnames, call_kind);
}

void JITRT_Dealloc(PyObject* obj) {
  _Py_Dealloc(obj);
}

PyObject* JITRT_UnaryNot(PyObject* value) {
  int res = PyObject_IsTrue(value);
  if (res == 0) {
    Py_INCREF(Py_True);
    return Py_True;
  } else if (res > 0) {
    Py_INCREF(Py_False);
    return Py_False;
  }
  return NULL;
}

static void invalidate_load_method_cache(
    PyObject* handle,
    PyObject* capsule,
    PyObject* modified_type_weakref) {
  JITRT_LoadMethodCache* cache =
      static_cast<JITRT_LoadMethodCache*>(PyCapsule_GetPointer(capsule, NULL));

  PyObject* modified_type = PyWeakref_GetObject(modified_type_weakref);
  for (int i = 0; i < LOAD_METHOD_CACHE_SIZE; i++) {
    // If the type that was referenced went away, we clear all the cache
    // entries as we cannot be sure which ones are invalid.
    //
    // Otherwise, only clear the matching entry.
    if ((modified_type == Py_None) ||
        (((PyTypeObject*)modified_type) == cache->entries[i].type)) {
      cache->entries[i].type = NULL;
      cache->entries[i].value = NULL;
      cache->entries[i].call_kind = JITRT_CALL_KIND_OTHER;
    }
  }

  Switchboard_Unsubscribe((Switchboard*)_PyType_GetSwitchboard(), handle);
}

static void fill_method_cache(
    JITRT_LoadMethodCache* cache,
    PyObject* /* obj */,
    PyTypeObject* type,
    PyObject* value,
    JITRT_CallMethodKind call_kind) {
  JITRT_LoadMethodCacheEntry* to_fill = NULL;

  if (!PyType_HasFeature(type, Py_TPFLAGS_NO_SHADOWING_INSTANCES) &&
      (type->tp_dictoffset != 0)) {
    return;
  }

  for (int i = 0; i < LOAD_METHOD_CACHE_SIZE; i++) {
    if (cache->entries[i].type == NULL) {
      to_fill = &(cache->entries[i]);
      break;
    }
  }
  if (to_fill == NULL) {
    return;
  }

  PyObject* capsule = PyCapsule_New(cache, NULL, NULL);
  if (capsule == NULL) {
    return;
  }

  Switchboard* sb = (Switchboard*)_PyType_GetSwitchboard();
  PyObject* handle = Switchboard_Subscribe(
      sb, (PyObject*)type, invalidate_load_method_cache, capsule);
  Py_XDECREF(handle);
  Py_DECREF(capsule);
  if (handle == NULL) {
    return;
  }

  to_fill->type = type;
  to_fill->value = value;
  to_fill->call_kind = call_kind;
}

static PyObject* __attribute__((noinline)) get_method_slow_path(
    PyObject* obj,
    PyObject* name,
    JITRT_LoadMethodCache* cache,
    JITRT_CallMethodKind* call_kind) {
  PyTypeObject* tp = Py_TYPE(obj);
  PyObject* descr;
  descrgetfunc f = NULL;
  PyObject **dictptr, *dict;
  PyObject* attr;
  JITRT_CallMethodKind found_kind = JITRT_CALL_KIND_OTHER;

  if ((tp->tp_getattro != PyObject_GenericGetAttr)) {
    *call_kind = JITRT_CALL_KIND_OTHER;
    return PyObject_GetAttr(obj, name);
  } else if (tp->tp_dict == NULL && PyType_Ready(tp) < 0) {
    return NULL;
  }

  descr = _PyType_Lookup(tp, name);
  if (descr != NULL) {
    Py_INCREF(descr);
    if (PyFunction_Check(descr)) {
      found_kind = JITRT_CALL_KIND_FUNC;
    } else if (Py_TYPE(descr) == &PyMethodDescr_Type) {
      found_kind = JITRT_CALL_KIND_METHOD_DESCR;
    } else if (PyType_HasFeature(
                   Py_TYPE(descr), Py_TPFLAGS_METHOD_DESCRIPTOR)) {
      found_kind = JITRT_CALL_KIND_METHOD_LIKE;
    } else {
      f = descr->ob_type->tp_descr_get;
      if (f != NULL && PyDescr_IsData(descr)) {
        PyObject* result = f(descr, obj, (PyObject*)obj->ob_type);
        Py_DECREF(descr);
        *call_kind = JITRT_CALL_KIND_OTHER;
        return result;
      }
    }
  }

  dictptr = _PyObject_GetDictPtr(obj);
  if (dictptr != NULL && (dict = *dictptr) != NULL) {
    Py_INCREF(dict);
    attr = PyDict_GetItem(dict, name);
    if (attr != NULL) {
      Py_INCREF(attr);
      Py_DECREF(dict);
      Py_XDECREF(descr);
      *call_kind = JITRT_CALL_KIND_OTHER;
      return attr;
    }
    Py_DECREF(dict);
  }

  if (found_kind == JITRT_CALL_KIND_FUNC ||
      found_kind == JITRT_CALL_KIND_METHOD_DESCR ||
      found_kind == JITRT_CALL_KIND_METHOD_LIKE) {
    *call_kind = found_kind;
    fill_method_cache(cache, obj, tp, descr, found_kind);
    return descr;
  }

  if (f != NULL) {
    PyObject* result = f(descr, obj, (PyObject*)Py_TYPE(obj));
    Py_DECREF(descr);
    *call_kind = JITRT_CALL_KIND_OTHER;
    return result;
  }

  if (descr != NULL) {
    *call_kind = JITRT_CALL_KIND_OTHER;
    return descr;
  }

  PyErr_Format(
      PyExc_AttributeError,
      "'%.50s' object has no attribute '%U'",
      tp->tp_name,
      name);
  return NULL;
}

PyObject* __attribute__((hot)) JITRT_GetMethod(
    PyObject* obj,
    PyObject* name,
    JITRT_LoadMethodCache* cache,
    JITRT_CallMethodKind* call_kind) {
  PyTypeObject* tp = Py_TYPE(obj);

  for (int i = 0; i < LOAD_METHOD_CACHE_SIZE; i++) {
    if (cache->entries[i].type == tp) {
      PyObject* result = cache->entries[i].value;
      Py_INCREF(result);
      *call_kind = cache->entries[i].call_kind;
      return result;
    }
  }

  return get_method_slow_path(obj, name, cache, call_kind);
}

PyObject* JITRT_GetMethodFromSuper(
    PyObject* global_super,
    PyObject* type,
    PyObject* self,
    PyObject* name,
    bool no_args_in_super_call,
    JITRT_CallMethodKind* call_kind) {
  int meth_found = 0;
  PyObject* result = _PyEval_SuperLookupMethodOrAttr(
      PyThreadState_GET(),
      global_super,
      (PyTypeObject*)type,
      self,
      name,
      no_args_in_super_call,
      &meth_found);
  if (result == NULL) {
    return NULL;
  }
  if (meth_found) {
    if (PyFunction_Check(result)) {
      *call_kind = JITRT_CALL_KIND_FUNC;
    } else if (Py_TYPE(result) == &PyMethodDescr_Type) {
      *call_kind = JITRT_CALL_KIND_METHOD_DESCR;
    } else if (Py_TYPE(result) == &PyWrapperDescr_Type) {
      *call_kind = JITRT_CALL_KIND_WRAPPER_DESCR;
    } else if (PyType_HasFeature(
                   Py_TYPE(result), Py_TPFLAGS_METHOD_DESCRIPTOR)) {
      *call_kind = JITRT_CALL_KIND_METHOD_LIKE;
    } else {
      *call_kind = JITRT_CALL_KIND_OTHER;
    }
  } else {
    *call_kind = JITRT_CALL_KIND_OTHER;
  }
  return result;
}

PyObject* JITRT_GetAttrFromSuper(
    PyObject* global_super,
    PyObject* type,
    PyObject* self,
    PyObject* name,
    bool no_args_in_super_call) {
  return _PyEval_SuperLookupMethodOrAttr(
      PyThreadState_GET(),
      global_super,
      (PyTypeObject*)type,
      self,
      name,
      no_args_in_super_call,
      NULL);
}

void JITRT_InitLoadMethodCache(JITRT_LoadMethodCache* cache) {
  memset(cache, 0, sizeof(*cache));
}

PyObject* JITRT_InvokeMethod(
    Py_ssize_t slot,
    PyObject** args,
    Py_ssize_t nargs,
    PyObject* kwnames) {
  PyTypeObject* self_type = Py_TYPE(args[0]);
  _PyType_VTable* vtable = (_PyType_VTable*)self_type->tp_cache;

  PyObject* func = vtable->vt_entries[slot].vte_state;
  return vtable->vt_entries[slot].vte_entry(
      func, args, nargs | _Py_VECTORCALL_INVOKED_STATICALLY, kwnames);
}

PyObject* JITRT_Cast(PyObject* obj, PyTypeObject* type) {
  if (PyObject_TypeCheck(obj, type)) {
    return obj;
  }

  PyErr_Format(
      PyExc_TypeError,
      "expected '%s', got '%s'",
      type->tp_name,
      Py_TYPE(obj)->tp_name);

  return NULL;
}

PyObject* JITRT_CastOptional(PyObject* obj, PyTypeObject* type) {
  if (_PyObject_TypeCheckOptional(obj, type, 1)) {
    return obj;
  }

  PyErr_Format(
      PyExc_TypeError,
      "expected '%s', got '%s'",
      type->tp_name,
      Py_TYPE(obj)->tp_name);

  return NULL;
}

int64_t JITRT_ShiftLeft64(int64_t x, int64_t y) {
  return x << y;
}
int32_t JITRT_ShiftLeft32(int32_t x, int32_t y) {
  return x << y;
}

int64_t JITRT_ShiftRight64(int64_t x, int64_t y) {
  return x >> y;
}
int32_t JITRT_ShiftRight32(int32_t x, int32_t y) {
  return x >> y;
}

uint64_t JITRT_ShiftRightUnsigned64(uint64_t x, uint64_t y) {
  return x >> y;
}
uint32_t JITRT_ShiftRightUnsigned32(uint32_t x, uint32_t y) {
  return x >> y;
}

int64_t JITRT_Mod64(int64_t x, int64_t y) {
  return x % y;
}
int32_t JITRT_Mod32(int32_t x, int32_t y) {
  return x % y;
}

uint64_t JITRT_ModUnsigned64(uint64_t x, uint64_t y) {
  return x % y;
}
uint32_t JITRT_ModUnsigned32(uint32_t x, uint32_t y) {
  return x % y;
}

PyObject* JITRT_BoxI32(int32_t i) {
  return PyLong_FromLong(i);
}

PyObject* JITRT_BoxU32(uint32_t i) {
  return PyLong_FromUnsignedLong(i);
}

PyObject* JITRT_BoxBool(uint32_t i) {
  return PyBool_FromLong(i);
}

PyObject* JITRT_BoxI64(int64_t i) {
  return PyLong_FromSsize_t(i);
}

PyObject* JITRT_BoxU64(uint64_t i) {
  return PyLong_FromSize_t(i);
}

PyObject* JITRT_BoxDouble(double_t d) {
  return PyFloat_FromDouble(d);
}

uint64_t JITRT_IsNegativeAndErrOccured(int64_t i) {
  return (i == -1 && _PyErr_OCCURRED()) ? -1 : 0;
}

uint64_t JITRT_GetI8_FromArray(char* arr, int64_t idx) {
  long result = arr[idx];
  if (result >= 128)
    result -= 256;
  return result;
}

uint64_t JITRT_GetU8_FromArray(char* arr, int64_t idx) {
  long result = ((unsigned char*)arr)[idx];
  return result;
}

uint64_t JITRT_GetI16_FromArray(char* arr, int64_t idx) {
  return (long)((short*)arr)[idx];
}

uint64_t JITRT_GetU16_FromArray(char* arr, int64_t idx) {
  return (long)((unsigned short*)arr)[idx];
}

uint64_t JITRT_GetI32_FromArray(char* arr, int64_t idx) {
  return ((long*)arr)[idx];
}

uint64_t JITRT_GetU32_FromArray(char* arr, int64_t idx) {
  return ((unsigned long*)arr)[idx];
}

uint64_t JITRT_GetI64_FromArray(char* arr, int64_t idx) {
  return ((long long*)arr)[idx];
}

uint64_t JITRT_GetU64_FromArray(char* arr, int64_t idx) {
  return ((unsigned long long*)arr)[idx];
}

PyObject* JITRT_GetObj_FromArray(char* arr, int64_t idx) {
  return ((PyObject**)arr)[idx];
}

void JITRT_SetI8_InArray(char* arr, uint64_t val, int64_t idx) {
  arr[idx] = (char)val;
}

void JITRT_SetU8_InArray(char* arr, uint64_t val, int64_t idx) {
  arr[idx] = (unsigned char)val;
}

void JITRT_SetI16_InArray(char* arr, uint64_t val, int64_t idx) {
  ((short*)arr)[idx] = (short)val;
}

void JITRT_SetU16_InArray(char* arr, uint64_t val, int64_t idx) {
  ((unsigned short*)arr)[idx] = (unsigned short)val;
}

void JITRT_SetI32_InArray(char* arr, uint64_t val, int64_t idx) {
  ((int*)arr)[idx] = (int)val;
}

void JITRT_SetU32_InArray(char* arr, uint64_t val, int64_t idx) {
  ((unsigned int*)arr)[idx] = (unsigned int)val;
}

void JITRT_SetI64_InArray(char* arr, uint64_t val, int64_t idx) {
  ((long*)arr)[idx] = (long)val;
}

void JITRT_SetU64_InArray(char* arr, uint64_t val, int64_t idx) {
  ((unsigned long*)arr)[idx] = (unsigned long)val;
}

void JITRT_SetObj_InArray(char* arr, uint64_t val, int64_t idx) {
  ((PyObject**)arr)[idx] = (PyObject*)val;
}

uint64_t JITRT_UnboxU64(PyObject* obj) {
  return PyLong_AsSize_t(obj);
}

uint32_t JITRT_UnboxU32(PyObject* obj) {
  return PyLong_AsUnsignedLong(obj);
}

int64_t JITRT_UnboxI64(PyObject* obj) {
  return PyLong_AsSsize_t(obj);
}

int32_t JITRT_UnboxI32(PyObject* obj) {
  return PyLong_AsLong(obj);
}

PyObject* JITRT_ImportName(
    PyThreadState* tstate,
    PyObject* name,
    PyObject* fromlist,
    PyObject* level) {
  _Py_IDENTIFIER(__import__);
  PyObject *import_func, *res;
  PyObject* stack[5];
  PyFrameObject* f = tstate->frame;
  PyObject* globals = NULL;
  PyObject* builtins;

  if (JIT_IsTinyFrame(f)) {
    globals = ((TinyFrame*)f)->globals;
    builtins = PyEval_GetBuiltins();
  } else {
    globals = f->f_globals;
    builtins = f->f_builtins;
  }

  import_func = _PyDict_GetItemId(builtins, &PyId___import__);
  if (import_func == NULL) {
    PyErr_SetString(PyExc_ImportError, "__import__ not found");
    return NULL;
  }

  /* Fast path for not overloaded __import__. */
  if (import_func == tstate->interp->import_func) {
    int ilevel = _PyLong_AsInt(level);
    if (ilevel == -1 && _PyErr_Occurred(tstate)) {
      return NULL;
    }
    res = PyImport_ImportModuleLevelObject(
        name,
        globals,
        // Locals are not actually used by the builtin import.
        // This is documented behavior as of Python 3.7.
        Py_None,
        fromlist,
        ilevel);
    return res;
  }

  Py_INCREF(import_func);

  stack[0] = name;
  stack[1] = globals;
  // In this implementation we always pass None for locals as it's easier than
  // fully materializing them now. The CPython interpreter has strange
  // (probably broken) behavior - it will only pass a dictionary of locals to
  // __builtins__.__import__() if the  locals have been materialized already,
  // for example by a call to locals(). Reliance on this behavior is unlikely.
  stack[2] = Py_None;
  stack[3] = fromlist;
  stack[4] = level;
  res = _PyObject_FastCall(import_func, stack, 5);
  Py_DECREF(import_func);
  return res;
}

PyObject* JITRT_GetVarnameTuple(PyCodeObject* code, int* idx) {
  if (*idx < code->co_nlocals) {
    return code->co_varnames;
  }

  *idx -= code->co_nlocals;
  auto ncellvars = PyTuple_GET_SIZE(code->co_cellvars);
  if (*idx < ncellvars) {
    return code->co_cellvars;
  }

  *idx -= ncellvars;
  return code->co_freevars;
}

void JITRT_DoRaise(PyThreadState* tstate, PyObject* exc, PyObject* cause) {
  // If we re-raise with no error set, deliberately do nothing and let
  // prepareForDeopt() handle this. We can't let _Py_DoRaise() handle this by
  // raising a RuntimeError as this would mean prepareForDeopt() does not call
  // PyTraceBack_Here().
  if (exc == NULL) {
    auto* exc_info = _PyErr_GetTopmostException(tstate);
    auto type = exc_info->exc_type;
    if (type == Py_None || type == NULL) {
      return;
    }
  }
  // We deliberately discard the return value here. In the interpreter a return
  // value of 1 indicates a _valid_ re-raise which skips:
  // (1) Calling PyTraceBack_Here().
  // (2) Raising a SystemError if no exception is set (no need, _Py_DoRaise
  //     already handles this).
  // (3) Calling tstate->c_tracefunc.
  // We don't support (3) and handle (1) + (2) between the check above and in
  // prepareForDeopt().
  _Py_DoRaise(tstate, exc, cause);
}

// JIT generator data free-list globals
const size_t kGenDataFreeListMaxSize = 1024;
static size_t gen_data_free_list_size = 0;
static void* gen_data_free_list_tail;

static void* gen_data_allocate(size_t spill_words) {
  if (spill_words > jit::kMinGenSpillWords || !gen_data_free_list_size) {
    auto data =
        malloc(spill_words * sizeof(uint64_t) + sizeof(jit::GenDataFooter));
    auto footer = reinterpret_cast<jit::GenDataFooter*>(
        reinterpret_cast<uint64_t*>(data) + spill_words);
    footer->spillWords = spill_words;
    return data;
  }

  // All free list entries are spill-word size 89, so we don't need to set
  // footer->spillWords again, it should still be set to 89 from previous use.
  JIT_DCHECK(spill_words == jit::kMinGenSpillWords, "invalid size");

  gen_data_free_list_size--;
  auto res = gen_data_free_list_tail;
  gen_data_free_list_tail = *reinterpret_cast<void**>(gen_data_free_list_tail);
  return res;
}

void JITRT_GenJitDataFree(PyGenObject* gen) {
  auto gen_data_footer =
      reinterpret_cast<jit::GenDataFooter*>(gen->gi_jit_data);
  auto gen_data = reinterpret_cast<uint64_t*>(gen_data_footer) -
      gen_data_footer->spillWords;

  if (gen_data_footer->spillWords != jit::kMinGenSpillWords ||
      gen_data_free_list_size == kGenDataFreeListMaxSize) {
    free(gen_data);
    return;
  }

  if (gen_data_free_list_size) {
    *reinterpret_cast<void**>(gen_data) = gen_data_free_list_tail;
  }
  gen_data_free_list_size++;
  gen_data_free_list_tail = gen_data;
}

enum class MakeGenObjectMode {
  kAsyncGenerator,
  kCoroutine,
  kGenerator,
};

template <MakeGenObjectMode mode>
static inline PyObject* make_gen_object(
    GenResumeFunc resume_entry,
    PyThreadState* tstate,
    size_t spill_words,
    PyCodeObject* code) {
  PyGenObject* gen;
  if (_PyJIT_NoFrame() || code->co_flags & CO_NO_FRAME) {
    if (mode == MakeGenObjectMode::kCoroutine) {
      gen = reinterpret_cast<PyGenObject*>(_PyCoro_NewNoFrame(tstate, code));
    } else if (mode == MakeGenObjectMode::kAsyncGenerator) {
      gen = reinterpret_cast<PyGenObject*>(_PyAsyncGen_NewNoFrame(code));
    } else {
      gen = reinterpret_cast<PyGenObject*>(_PyGen_NewNoFrame(code));
    }
  } else {
    PyFrameObject* f = tstate->frame;
    Py_INCREF(f);
    if (mode == MakeGenObjectMode::kCoroutine) {
      gen = reinterpret_cast<PyGenObject*>(
          _PyCoro_NewTstate(tstate, f, code->co_name, code->co_qualname));

      JIT_MaterializeTopFrame(tstate);

      auto parent_f = JIT_MaterializePrevFrame(tstate->frame);
      auto UTF8_name = PyUnicode_AsUTF8(parent_f->f_code->co_name);
      if (!strcmp(UTF8_name, "<genexpr>") || !strcmp(UTF8_name, "<listcomp>") ||
          !strcmp(UTF8_name, "<dictcomp>")) {
        JIT_MaterializePrevFrame(parent_f);
        reinterpret_cast<PyCoroObject*>(gen)->creator = parent_f->f_back;
      } else {
        reinterpret_cast<PyCoroObject*>(gen)->creator = parent_f;
      }
    } else if (mode == MakeGenObjectMode::kAsyncGenerator) {
      gen = reinterpret_cast<PyGenObject*>(
          PyAsyncGen_New(f, code->co_name, code->co_qualname));
    } else {
      gen = reinterpret_cast<PyGenObject*>(
          PyGen_NewWithQualName(f, code->co_name, code->co_qualname));
    }
  }
  if (gen == NULL) {
    return NULL;
  }

  spill_words = std::max(spill_words, jit::kMinGenSpillWords);

  auto suspend_data = gen_data_allocate(spill_words);
  auto footer = reinterpret_cast<jit::GenDataFooter*>(
      reinterpret_cast<uint64_t*>(suspend_data) + spill_words);
  footer->resumeEntry = resume_entry;
  footer->yieldPoint = nullptr;
  footer->state = _PyJitGenState_JustStarted;
  footer->gen = gen;

  gen->gi_jit_data = reinterpret_cast<_PyJIT_GenData*>(footer);

  return reinterpret_cast<PyObject*>(gen);
}

PyObject* JITRT_MakeGenObject(
    GenResumeFunc resume_entry,
    PyThreadState* tstate,
    size_t spill_words,
    PyCodeObject* code) {
  return make_gen_object<MakeGenObjectMode::kGenerator>(
      resume_entry, tstate, spill_words, code);
}

PyObject* JITRT_MakeGenObjectAsyncGen(
    GenResumeFunc resume_entry,
    PyThreadState* tstate,
    size_t spill_words,
    PyCodeObject* code) {
  return make_gen_object<MakeGenObjectMode::kAsyncGenerator>(
      resume_entry, tstate, spill_words, code);
}

PyObject* JITRT_MakeGenObjectCoro(
    GenResumeFunc resume_entry,
    PyThreadState* tstate,
    size_t spill_words,
    PyCodeObject* code) {
  return make_gen_object<MakeGenObjectMode::kCoroutine>(
      resume_entry, tstate, spill_words, code);
}

JITRT_YieldFromRes JITRT_YieldFrom(
    PyObject* gen,
    PyObject* v,
    PyThreadState* tstate,
    uint64_t finish_yield_from) {
  if (v == NULL) {
    return {NULL, 1};
  }
  if (finish_yield_from) {
    return {v, 1};
  }
  PyObject* retval;
  PyObject* stopIterationResult = NULL;
  auto receiver = reinterpret_cast<PyGenObject*>(gen);
  if (PyGen_CheckExact(receiver) || PyCoro_CheckExact(receiver)) {
    if (_Py_LIKELY(!tstate->c_tracefunc)) {
      retval = _PyGen_Send_NoStopIteration(
          tstate, receiver, v, &stopIterationResult);
    } else {
      retval = _PyGen_Send(receiver, v);
    }

  } else {
    _Py_IDENTIFIER(send);
    if (v == Py_None) {
      if (PyType_HasFeature(Py_TYPE(receiver), Py_TPFLAGS_USE_GENNEXT)) {
        int is_return;
        retval = ((PyGenTypeObject*)Py_TYPE(receiver))
                     ->tp_gennext.g_gennext(gen, Py_None, &is_return);
        if (retval && is_return) {
          stopIterationResult = retval;
          retval = NULL;
        }
      } else {
        retval = Py_TYPE(receiver)->tp_iternext(gen);
      }
    } else {
      retval = _PyObject_CallMethodIdObjArgs(gen, &PyId_send, v, NULL);
    }
  }
  Py_DECREF(v);
  if (retval == NULL) {
    PyObject* val;
    if (stopIterationResult) {
      // steal the reference
      val = stopIterationResult;
    } else if (_PyGen_FetchStopIterationValue(&val) < 0) {
      return {NULL, 1};
    }
    return {val, 1};
  }
  return {retval, 0};
}

PyObject* JITRT_FormatValue(
    PyThreadState* tstate,
    PyObject* fmt_spec,
    PyObject* value,
    int conversion) {
  PyObject* (*conv_fn)(PyObject*);

  /* See if any conversion is specified. */
  switch (conversion) {
    case FVC_NONE:
      conv_fn = NULL;
      break;
    case FVC_STR:
      conv_fn = PyObject_Str;
      break;
    case FVC_REPR:
      conv_fn = PyObject_Repr;
      break;
    case FVC_ASCII:
      conv_fn = PyObject_ASCII;
      break;
    default:
      _PyErr_Format(
          tstate,
          PyExc_SystemError,
          "unexpected conversion flag %d",
          conversion);
      return NULL;
  }

  /* If there's a conversion function, call it and replace
     value with that result. Otherwise, just use value,
     without conversion. */
  Ref<> converted;
  if (conv_fn != NULL) {
    converted = Ref<>::steal(conv_fn(value));
    if (converted == nullptr) {
      return nullptr;
    }
    value = converted.get();
  }

  /* If value is a unicode object, and there's no fmt_spec,
     then we know the result of format(value) is value
     itself. In that case, skip calling format(). I plan to
     move this optimization in to PyObject_Format()
     itself. */
  if (PyUnicode_CheckExact(value) && fmt_spec == NULL) {
    /* Do nothing, just return. */
    Py_INCREF(value);
    return value;
  }

  /* Actually call format(). */
  return PyObject_Format(value, fmt_spec);
}

PyObject* JITRT_BuildString(
    void* /*unused*/,
    PyObject** args,
    size_t nargsf,
    void* /*unused*/) {
  size_t nargs = PyVectorcall_NARGS(nargsf);

  Ref<> empty = Ref<>::steal(PyUnicode_New(0, 0));
  if (empty == nullptr) {
    return nullptr;
  }

  return _PyUnicode_JoinArray(empty, args, nargs);
}

JITRT_StaticCallReturn
JITRT_CompileFunction(PyFunctionObject* func, PyObject** args, bool* compiled) {
  void* no_error = (void*)1;
  if (_PyJIT_IsCompiled((PyObject*)func) ||
      _PyJIT_CompileFunction(func) == PYJIT_RESULT_OK) {
    *compiled = 1;
    void** indirect =
        jit::codegen::NativeGenerator::runtime()->findFunctionEntryCache(func);
    *indirect = (void*)JITRT_GET_STATIC_ENTRY(func->vectorcall);
    return JITRT_StaticCallReturn{
        (void*)JITRT_GET_STATIC_ENTRY(func->vectorcall), no_error};
  }

  *compiled = 0;
  PyCodeObject* code = (PyCodeObject*)func->func_code;
  int total_args = code->co_argcount;
  if (code->co_flags & CO_VARARGS) {
    total_args++;
  }
  if (code->co_flags & CO_VARKEYWORDS) {
    total_args++;
  }

  // PyObject** args is:
  // arg0
  // arg1
  // arg2
  // arg3
  // arg4
  // arg5
  // &compiled
  // dummy
  // previous rbp
  // return address to JITed code
  // memory argument 0
  // memory argument 1
  // ...

  PyObject** dest_args;
  PyObject* final_args[total_args];
  if (total_args <= 6) {
    // no gap in args to worry about
    dest_args = args;
  } else {
    for (int i = 0; i < 6; i++) {
      final_args[i] = args[i];
    }
    for (int i = 6; i < total_args; i++) {
      final_args[i] = args[i + 4];
    }
    dest_args = final_args;
  }

  _PyTypedArgsInfo* arg_info =
      jit::codegen::NativeGenerator::runtime()->findFunctionPrimitiveArgInfo(
          func);
  PyObject* allocated_args[arg_info == nullptr ? 0 : Py_SIZE(arg_info)];
  int allocated_count = 0;

  if (arg_info != nullptr) {
    // We have primitive values that need to be converted into boxed values
    // to run the interpreter loop.
    for (Py_ssize_t i = 0; i < Py_SIZE(arg_info); i++) {
      if (arg_info->tai_args[i].tai_primitive_type != -1) {
        // primitive type, convert to int...
        int arg = arg_info->tai_args[i].tai_argnum;
        uint64_t arg_val;
        if (arg >= 6) {
          arg += 4;
        }
        arg_val = (uint64_t)args[arg];

        PyObject* new_val;
        switch (arg_info->tai_args[i].tai_primitive_type) {
          case TYPED_INT8:
            new_val = PyLong_FromLong((int8_t)arg_val);
            break;
          case TYPED_INT16:
            new_val = PyLong_FromLong((int16_t)arg_val);
            break;
          case TYPED_INT32:
            new_val = PyLong_FromLong((int32_t)arg_val);
            break;
          case TYPED_INT64:
            new_val = PyLong_FromSsize_t((Py_ssize_t)arg_val);
            break;
          case TYPED_UINT8:
            new_val = PyLong_FromUnsignedLong((uint8_t)arg_val);
            break;
          case TYPED_UINT16:
            new_val = PyLong_FromUnsignedLong((uint16_t)arg_val);
            break;
          case TYPED_UINT32:
            new_val = PyLong_FromUnsignedLong((uint32_t)arg_val);
            break;
          case TYPED_UINT64:
            new_val = PyLong_FromSize_t((size_t)arg_val);
            break;
          default:
            assert(false);
            PyErr_SetString(PyExc_RuntimeError, "unsupported primitive type");
            new_val = nullptr;
        }
        if (new_val == nullptr) {
          for (int i = 0; i < allocated_count; i++) {
            Py_DECREF(allocated_args[i]);
          }
          return JITRT_StaticCallReturn{nullptr, nullptr};
        }

        // we can update the incoming arg array, either it's
        // the pushed values on the stack by the trampoline, or
        // it's final_args we allocated above.
        dest_args[arg] = new_val;
        allocated_args[allocated_count++] = new_val;
      }
    }
  }

  PyObject* res = _PyFunctionCode_FastCall(
      (PyCodeObject*)func->func_code,
      dest_args,
      total_args,
      func->func_globals,
      func->func_name,
      func->func_qualname);

  for (int i = 0; i < allocated_count; i++) {
    Py_DECREF(allocated_args[i]);
  }

  // If there was an error, don't try to unbox null
  if (res == nullptr) {
    return JITRT_StaticCallReturn{res, nullptr};
  }

  // If we are supposed to be returning a primitive, it needs unboxing because
  // our caller expected this to be a static->static direct invoke, we just
  // failed to JIT the callee.
  int ret_type = _PyClassLoader_ResolvePrimitiveType(
      _PyClassLoader_GetReturnTypeDescr(func));
  if (ret_type != TYPED_OBJECT) {
    // we can always unbox to 64-bit, the JIT will just ignore the higher bits.
    // (TODO) This means that overflow here will give weird results, but
    // overflow in primitive ints in static python is undefined behavior right
    // now anyway, until we implement overflow checking. It doesn't make sense
    // to implement overflow checking just here in the "unjitable" code path,
    // when overflow won't be checked if the code is JITted.
    void* ival;
    if (ret_type & TYPED_INT_SIGNED) {
      ival = (void*)JITRT_UnboxI64(res);
    } else {
      ival = (void*)JITRT_UnboxU64(res);
    }
    return JITRT_StaticCallReturn{ival, no_error};
  }

  return JITRT_StaticCallReturn{res, no_error};
}

PyObject* JITRT_UnpackExToTuple(
    PyThreadState* tstate,
    PyObject* iterable,
    int before,
    int after) {
  JIT_DCHECK(iterable != nullptr, "The iterable cannot be null.");

  Ref<> it = Ref<>::steal(PyObject_GetIter(iterable));
  if (it == NULL) {
    if (_PyErr_ExceptionMatches(tstate, PyExc_TypeError) &&
        iterable->ob_type->tp_iter == NULL && !PySequence_Check(iterable)) {
      _PyErr_Format(
          tstate,
          PyExc_TypeError,
          "cannot unpack non-iterable %.200s object",
          iterable->ob_type->tp_name);
    }
    return nullptr;
  }

  int totalargs = before + after + 1;
  Ref<PyTupleObject> tuple = Ref<PyTupleObject>::steal(PyTuple_New(totalargs));
  if (tuple == nullptr) {
    return nullptr;
  }
  int ti = 0;

  for (int i = 0; i < before; i++) {
    PyObject* w = PyIter_Next(it);
    if (w == NULL) {
      /* Iterator done, via error or exhaustion. */
      if (!_PyErr_Occurred(tstate)) {
        if (after == -1) {
          _PyErr_Format(
              tstate,
              PyExc_ValueError,
              "not enough values to unpack "
              "(expected %d, got %d)",
              before,
              i);
        } else {
          _PyErr_Format(
              tstate,
              PyExc_ValueError,
              "not enough values to unpack "
              "(expected at least %d, got %d)",
              before + after,
              i);
        }
      }
      return nullptr;
    }
    tuple->ob_item[ti++] = w;
  }

  JIT_DCHECK(
      after >= 0,
      "This function should only be used for UNPACK_EX, where after >= 0.");

  PyObject* list = PySequence_List(it);
  if (list == NULL) {
    return nullptr;
  }
  tuple->ob_item[ti++] = list;

  ssize_t list_size = PyList_GET_SIZE(list);
  if (list_size < after) {
    _PyErr_Format(
        tstate,
        PyExc_ValueError,
        "not enough values to unpack (expected at least %d, got %zd)",
        before + after,
        before + list_size);
    return nullptr;
  }

  /* Pop the "after-variable" args off the list. */
  for (int j = after; j > 0; j--) {
    tuple->ob_item[ti++] = PyList_GET_ITEM(list, list_size - j);
  }
  /* Resize the list. */
  Py_SIZE(list) = list_size - after;

  return reinterpret_cast<PyObject*>(tuple.release());
}

int JITRT_UnicodeEquals(PyObject* s1, PyObject* s2, int equals) {
  // one of these must be unicode for the quality comparison to be okay
  assert(PyUnicode_CheckExact(s1) || PyUnicode_CheckExact(s2));
  if (s1 == s2) {
    return equals == Py_EQ;
  }

  if (PyUnicode_CheckExact(s1) && PyUnicode_CheckExact(s2)) {
    if (PyUnicode_READY(s1) < 0 || PyUnicode_READY(s2) < 0)
      return -1;

    Py_ssize_t length = PyUnicode_GET_LENGTH(s1);
    if (length != PyUnicode_GET_LENGTH(s2)) {
      return equals == Py_NE;
    }

    Py_hash_t hash1 = ((PyASCIIObject*)s1)->hash;
    Py_hash_t hash2 = ((PyASCIIObject*)s2)->hash;
    if (hash1 != hash2 && hash1 != -1 && hash2 != -1) {
      return equals == Py_NE;
    }

    int kind = PyUnicode_KIND(s1);
    if (kind != PyUnicode_KIND(s2)) {
      return equals == Py_NE;
    }
    void* data1 = PyUnicode_DATA(s1);
    void* data2 = PyUnicode_DATA(s2);
    if (PyUnicode_READ(kind, data1, 0) != PyUnicode_READ(kind, data2, 0)) {
      return equals == Py_NE;
    } else if (length == 1) {
      return equals == Py_EQ;
    } else {
      int result = memcmp(data1, data2, (size_t)(length * kind));
      return (equals == Py_EQ) ? (result == 0) : (result != 0);
    }
  }
  return PyObject_RichCompareBool(s1, s2, equals);
}

int JITRT_NotContains(PyObject* w, PyObject* v) {
  int res = PySequence_Contains(w, v);
  if (res == -1) {
    return -1;
  }
  return !res;
}

/* Perform a rich comparison with integer result.  This wraps
   PyObject_RichCompare(), returning -1 for error, 0 for false, 1 for true. */
int JITRT_RichCompareBool(PyObject* v, PyObject* w, int op) {
  Ref<> res = Ref<>::steal(PyObject_RichCompare(v, w, op));

  if (res == nullptr) {
    return -1;
  } else if (PyBool_Check(res)) {
    return res == Py_True;
  }

  return PyObject_IsTrue(res);
}

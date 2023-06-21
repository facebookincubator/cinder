// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/jit_rt.h"

#include "Objects/dict-common.h"
#include "Python.h"
#include "cinder/exports.h"
#include "classloader.h"
#include "frameobject.h"
#include "listobject.h"
#include "object.h"
#include "pycore_shadow_frame.h"
#include "pystate.h"

#include "Jit/codegen/gen_asm.h"
#include "Jit/frame.h"
#include "Jit/log.h"
#include "Jit/pyjit.h"
#include "Jit/ref.h"
#include "Jit/runtime.h"
#include "Jit/runtime_support.h"
#include "Jit/util.h"

// clang-format off
#include "internal/pycore_pyerrors.h"
#include "internal/pycore_pystate.h"
#include "internal/pycore_object.h"
#include "pycore_tuple.h"
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
        total_args | (nargsf & (Ci_Py_AWAITED_CALL_MARKER)),
        nullptr);
  }

  return _PyFunction_Vectorcall((PyObject*)func, args, nargsf, kwnames);
}

typedef JITRT_StaticCallReturn (*staticvectorcallfunc)(
    PyObject* callable,
    PyObject* const* args,
    size_t nargsf,
    PyObject* kwnames);

typedef JITRT_StaticCallFPReturn (*staticvectorcallfuncfp)(
    PyObject* callable,
    PyObject* const* args,
    size_t nargsf,
    PyObject* kwnames);

JITRT_StaticCallFPReturn JITRT_CallWithIncorrectArgcountFPReturn(
    PyFunctionObject* func,
    PyObject** args,
    size_t nargsf,
    int argcount) {
  PyObject* defaults = func->func_defaults;
  if (defaults == nullptr) {
    // Function has no defaults; there's nothing we can do.
    _PyFunction_Vectorcall((PyObject*)func, args, nargsf, NULL);
    return {0.0, 0.0};
  }
  Py_ssize_t defcount = PyTuple_GET_SIZE(defaults);
  Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
  PyObject* arg_space[argcount];
  Py_ssize_t defaulted_args = argcount - nargs;

  if (nargs + defcount < argcount || nargs > argcount) {
    // Not enough args with defaults, or too many args without defaults.
    _PyFunction_Vectorcall((PyObject*)func, args, nargsf, NULL);
    return {0.0, 0.0};
  }

  Py_ssize_t i;
  for (i = 0; i < nargs; i++) {
    arg_space[i] = *args++;
  }

  PyObject** def_items =
      &((PyTupleObject*)defaults)->ob_item[defcount - defaulted_args];
  for (; i < argcount; i++) {
    arg_space[i] = *def_items++;
  }

  return reinterpret_cast<staticvectorcallfuncfp>(
      JITRT_GET_REENTRY(func->vectorcall))(
      (PyObject*)func,
      arg_space,
      argcount | (nargsf & (Ci_Py_AWAITED_CALL_MARKER)),
      // We lie to C++ here, and smuggle in the number of defaulted args filled
      // in.
      (PyObject*)defaulted_args);
}

JITRT_StaticCallReturn JITRT_CallWithIncorrectArgcount(
    PyFunctionObject* func,
    PyObject** args,
    size_t nargsf,
    int argcount) {
  PyObject* defaults = func->func_defaults;
  if (defaults == nullptr) {
    // Function has no defaults; there's nothing we can do.
    // Fallback to the default _PyFunction_Vectorcall implementation
    // to produce an appropriate exception.
    return {_PyFunction_Vectorcall((PyObject*)func, args, nargsf, NULL), NULL};
  }
  Py_ssize_t defcount = PyTuple_GET_SIZE(defaults);
  Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
  PyObject* arg_space[argcount];
  Py_ssize_t defaulted_args = argcount - nargs;

  if (nargs + defcount < argcount || nargs > argcount) {
    // Not enough args with defaults, or too many args without defaults.
    return {_PyFunction_Vectorcall((PyObject*)func, args, nargsf, NULL), NULL};
  }

  Py_ssize_t i;
  for (i = 0; i < nargs; i++) {
    arg_space[i] = *args++;
  }

  PyObject** def_items =
      &((PyTupleObject*)defaults)->ob_item[defcount - defaulted_args];
  for (; i < argcount; i++) {
    arg_space[i] = *def_items++;
  }

  return reinterpret_cast<staticvectorcallfunc>(
      JITRT_GET_REENTRY(func->vectorcall))(
      (PyObject*)func,
      arg_space,
      argcount | (nargsf & (Ci_Py_AWAITED_CALL_MARKER)),
      // We lie to C++ here, and smuggle in the number of defaulted args filled
      // in.
      (PyObject*)defaulted_args);
}

bool JITRT_PackStaticArgs(
    PyObject** args,
    _PyTypedArgsInfo* arg_info,
    void** arg_space,
    Py_ssize_t nargs) {
  Py_ssize_t arg_index = 0;

  for (Py_ssize_t i = 0; i < nargs; i++) {
    if (arg_index < Py_SIZE(arg_info) &&
        arg_info->tai_args[arg_index].tai_argnum == i) {
      _PyTypedArgInfo* cur_arg = &arg_info->tai_args[arg_index];
      PyObject* arg = args[i];
      if (cur_arg->tai_primitive_type == -1) {
        if (!_PyObject_TypeCheckOptional(
                arg,
                cur_arg->tai_type,
                cur_arg->tai_optional,
                cur_arg->tai_exact)) {
          return true;
        }
        arg_space[i] = arg;
      } else if (cur_arg->tai_primitive_type == TYPED_BOOL) {
        if (Py_TYPE(arg) != &PyBool_Type) {
          return true;
        }
        arg_space[i] = (void*)(arg == Py_True);
      } else if (cur_arg->tai_primitive_type == TYPED_DOUBLE) {
        if (Py_TYPE(arg) != &PyFloat_Type) {
          return true;
        }
        arg_space[i] = bit_cast<void*>(PyFloat_AsDouble(arg));
      } else if (cur_arg->tai_primitive_type <= TYPED_INT64) {
        // Primitive arg check
        if (Py_TYPE(arg) != &PyLong_Type ||
            !_PyClassLoader_OverflowCheck(
                arg, cur_arg->tai_primitive_type, (size_t*)&arg_space[i])) {
          return true;
        }
      } else {
        JIT_CHECK(
            false,
            "unsupported primitive type %d",
            cur_arg->tai_primitive_type);
      }
      arg_index++;
      continue;
    }
    arg_space[i] = args[i];
  }
  return false;
}

template <typename TRetType, typename TVectorcall>
TRetType JITRT_CallStaticallyWithPrimitiveSignatureWorker(
    PyFunctionObject* func,
    PyObject** args,
    size_t nargsf,
    _PyTypedArgsInfo* arg_info) {
  Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
  void* arg_space[nargs];
  if (JITRT_PackStaticArgs(args, arg_info, arg_space, nargs)) {
    goto fail;
  }

  return reinterpret_cast<TVectorcall>(JITRT_GET_REENTRY(func->vectorcall))(
      (PyObject*)func, (PyObject**)arg_space, nargsf, NULL);

fail:
  PyObject* res = _PyFunction_Vectorcall((PyObject*)func, args, nargsf, NULL);
  JIT_DCHECK(res == NULL, "should alway be reporting an error");
  return TRetType();
}

static inline Py_ssize_t vectorcall_flags(size_t n) {
  return n & (Ci_Py_VECTORCALL_ARGUMENT_MASK | PY_VECTORCALL_ARGUMENTS_OFFSET);
}

// This can either be a static method returning a primitive or a Python object,
// so we use JITRT_StaticCallReturn.  If it's returning a primitive we'll return
// rdx from the function, or return NULL for rdx when we dispatch to
// _PyFunction_Vectorcall for error generation.  If it returns a Python object
// we'll return an additional garbage rdx from our caller, but our caller won't
// care about it either.
template <typename TRetType, typename TVectorcall>
TRetType JITRT_CallStaticallyWithPrimitiveSignatureTemplate(
    PyFunctionObject* func,
    PyObject** args,
    size_t nargsf,
    PyObject* kwnames,
    _PyTypedArgsInfo* arg_info) {
  Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
  PyCodeObject* co = (PyCodeObject*)func->func_code;

  if ((kwnames || nargs != co->co_argcount ||
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
      return JITRT_CallStaticallyWithPrimitiveSignatureWorker<
          TRetType,
          TVectorcall>(
          func, arg_space, total_args | vectorcall_flags(nargsf), arg_info);
    }

    _PyFunction_Vectorcall((PyObject*)func, args, nargsf, kwnames);
    return TRetType();
  }

  return JITRT_CallStaticallyWithPrimitiveSignatureWorker<
      TRetType,
      TVectorcall>(func, args, nargsf, arg_info);
}

JITRT_StaticCallReturn JITRT_CallStaticallyWithPrimitiveSignature(
    PyFunctionObject* func,
    PyObject** args,
    size_t nargsf,
    PyObject* kwnames,
    _PyTypedArgsInfo* arg_info) {
  return JITRT_CallStaticallyWithPrimitiveSignatureTemplate<
      JITRT_StaticCallReturn,
      staticvectorcallfunc>(func, args, nargsf, kwnames, arg_info);
}

JITRT_StaticCallFPReturn JITRT_CallStaticallyWithPrimitiveSignatureFP(
    PyFunctionObject* func,
    PyObject** args,
    size_t nargsf,
    PyObject* kwnames,
    _PyTypedArgsInfo* arg_info) {
  return JITRT_CallStaticallyWithPrimitiveSignatureTemplate<
      JITRT_StaticCallFPReturn,
      staticvectorcallfuncfp>(func, args, nargsf, kwnames, arg_info);
}

JITRT_StaticCallFPReturn JITRT_ReportStaticArgTypecheckErrorsWithDoubleReturn(
    PyObject* func,
    PyObject** args,
    size_t nargsf,
    PyObject* /* kwnames */) {
  PyObject* res =
      JITRT_ReportStaticArgTypecheckErrors(func, args, nargsf, NULL);
  JIT_CHECK(res == NULL, "should always return an error");
  return {0, 0};
}

JITRT_StaticCallReturn JITRT_ReportStaticArgTypecheckErrorsWithPrimitiveReturn(
    PyObject* func,
    PyObject** args,
    size_t nargsf,
    PyObject* /* kwnames */) {
  PyObject* res =
      JITRT_ReportStaticArgTypecheckErrors(func, args, nargsf, NULL);
  JIT_CHECK(res == NULL, "should always return an error");
  return {NULL, NULL};
}

PyObject* JITRT_ReportStaticArgTypecheckErrors(
    PyObject* func,
    PyObject** args,
    size_t nargsf,
    PyObject* /* kwnames */) {
  auto code = reinterpret_cast<PyCodeObject*>(
      reinterpret_cast<PyFunctionObject*>(func)->func_code);
  int nkwonly = code->co_kwonlyargcount;
  if (code == nullptr || nkwonly == 0) {
    // We explicitly pass in nullptr for kwnames as the default arg count can
    // be smuggled in to this function in place of kwnames.
    return _PyFunction_Vectorcall(func, args, nargsf, nullptr);
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
    auto name = Ref<>::create(PyTuple_GetItem(code->co_varnames, i));
    PyTuple_SetItem(new_kwnames, i - code->co_argcount, std::move(name));
  }
  Py_ssize_t nargs = PyVectorcall_NARGS(nargsf) - nkwonly;
  if (code->co_flags & CO_VARKEYWORDS) {
    nargs -= 1;
  }
  Py_ssize_t flags = vectorcall_flags(nargsf);
  return _PyFunction_Vectorcall(func, args, nargs | flags, new_kwnames);
}

static PyFrameObject* allocateFrame(
    PyThreadState* tstate,
    PyCodeObject* code,
    PyObject* builtins,
    PyObject* globals) {
  if (code->co_mutable->co_zombieframe != NULL) {
    __builtin_prefetch(code->co_mutable->co_zombieframe);
  }
  PyFrameConstructor frame_ctor = {};
  frame_ctor.fc_globals = globals;
  frame_ctor.fc_builtins = builtins;
  frame_ctor.fc_code = reinterpret_cast<PyObject*>(code);
  return _PyFrame_New_NoTrack(tstate, &frame_ctor, nullptr);
}

PyThreadState* JITRT_AllocateAndLinkFrame(
    PyCodeObject* code,
    PyObject* builtins,
    PyObject* globals) {
  PyThreadState* tstate = PyThreadState_GET();
  JIT_DCHECK(tstate != NULL, "thread state cannot be null");

  PyFrameObject* frame = allocateFrame(tstate, code, builtins, globals);
  if (frame == nullptr) {
    return nullptr;
  }
  frame->f_state = FRAME_EXECUTING;

  tstate->frame = frame;

  return tstate;
}

void JITRT_DecrefFrame(PyFrameObject* frame) {
  if (Py_REFCNT(frame) > 1) {
    // If the frame escaped it needs to be tracked
    Py_DECREF(frame);
    if (!_PyObject_GC_IS_TRACKED(frame)) {
      PyObject_GC_Track(frame);
    }
  } else {
    Py_DECREF(frame);
  }
}

void JITRT_UnlinkFrame(PyThreadState* tstate) {
  PyFrameObject* f = tstate->frame;

  f->f_state = FRAME_RETURNED;

  tstate->frame = f->f_back;
  JITRT_DecrefFrame(f);
}

PyObject*
JITRT_LoadGlobal(PyObject* globals, PyObject* builtins, PyObject* name) {
  PyObject* result =
      _PyDict_LoadGlobal((PyDictObject*)globals, (PyDictObject*)builtins, name);
  if ((result == NULL) && !PyErr_Occurred()) {
    // name is converted to a `char*` by format_exc_check_arg
    format_exc_check_arg(
        _PyThreadState_GET(),
        PyExc_NameError,
        "name '%.200s' is not defined",
        name);
  }
  Py_XINCREF(result);
  return result;
}

template <bool is_awaited>
static inline PyObject*
call_function(PyObject* func, PyObject** args, Py_ssize_t nargs) {
  size_t flags = PY_VECTORCALL_ARGUMENTS_OFFSET |
      (is_awaited ? Ci_Py_AWAITED_CALL_MARKER : 0);
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
      (is_awaited ? Ci_Py_AWAITED_CALL_MARKER : 0);
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
  auto new_kwargs = Ref<>::create(kwargs);
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

  if (_PyVectorcall_Function(func) != NULL) {
    return Ci_PyVectorcall_Call_WithFlags(
        func, pargs, kwargs, is_awaited ? Ci_Py_AWAITED_CALL_MARKER : 0);
  }
  return PyObject_Call(func, pargs, kwargs);
}

PyObject* JITRT_LoadFunctionIndirect(PyObject** func, PyObject* descr) {
  PyObject* res = *func;
  if (!res) {
    res = _PyClassLoader_ResolveFunction(descr, NULL);
    Py_XDECREF(res);
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
  size_t flags = PY_VECTORCALL_ARGUMENTS_OFFSET |
      (is_awaited ? Ci_Py_AWAITED_CALL_MARKER : 0);
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
    PyObject* kwnames) {
  size_t is_awaited_flag = is_awaited ? Ci_Py_AWAITED_CALL_MARKER : 0;
  if (callable != Py_None) {
    PyObject* res = _PyObject_Vectorcall(
        callable,
        args,
        (nargs) | PY_VECTORCALL_ARGUMENTS_OFFSET | is_awaited_flag,
        kwnames);
    return res;
  } else {
    PyObject* res = _PyObject_Vectorcall(
        args[0],
        args + 1,
        (nargs - 1) | PY_VECTORCALL_ARGUMENTS_OFFSET | is_awaited_flag,
        kwnames);
    return res;
  }
}

PyObject* JITRT_CallMethod(
    PyObject* callable,
    PyObject** args,
    Py_ssize_t nargs,
    PyObject* kwnames) {
  return call_method<false>(callable, args, nargs, kwnames);
}

PyObject* JITRT_CallMethodAwaited(
    PyObject* callable,
    PyObject** args,
    Py_ssize_t nargs,
    PyObject* kwnames) {
  return call_method<true>(callable, args, nargs, kwnames);
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

JITRT_LoadMethodResult JITRT_GetMethodFromSuper(
    PyObject* global_super,
    PyObject* type,
    PyObject* self,
    PyObject* name,
    bool no_args_in_super_call) {
  int meth_found = 0;
  PyObject* result = Ci_SuperLookupMethodOrAttr(
      PyThreadState_GET(),
      global_super,
      (PyTypeObject*)type,
      self,
      name,
      no_args_in_super_call,
      &meth_found);
  if (result == NULL) {
    return {NULL, nullptr};
  }
  if (meth_found) {
    if (!(PyFunction_Check(result) || Py_TYPE(result) == &PyMethodDescr_Type ||
          Py_TYPE(result) == &PyWrapperDescr_Type ||
          PyType_HasFeature(Py_TYPE(result), Py_TPFLAGS_METHOD_DESCRIPTOR))) {
      meth_found = 0;
    }
  } else {
    meth_found = 0;
  }
  if (meth_found) {
    Py_INCREF(self);
    return {result, self};
  }
  Py_INCREF(Py_None);
  return {Py_None, result};
}

PyObject* JITRT_GetAttrFromSuper(
    PyObject* global_super,
    PyObject* type,
    PyObject* self,
    PyObject* name,
    bool no_args_in_super_call) {
  return Ci_SuperLookupMethodOrAttr(
      PyThreadState_GET(),
      global_super,
      (PyTypeObject*)type,
      self,
      name,
      no_args_in_super_call,
      NULL);
}

PyObject* JITRT_InvokeMethod(
    Py_ssize_t slot,
    PyObject** args,
    Py_ssize_t nargs,
    PyObject*) {
  PyTypeObject* self_type = Py_TYPE(args[0]);
  _PyType_VTable* vtable = (_PyType_VTable*)self_type->tp_cache;

  return _PyClassLoader_InvokeMethod(vtable, slot, args, nargs);
}

PyObject* JITRT_InvokeClassMethod(
    Py_ssize_t slot,
    PyObject** args,
    Py_ssize_t nargs,
    PyObject*) {
  PyTypeObject* self_type = (PyTypeObject*)args[0];
  _PyType_VTable* vtable = (_PyType_VTable*)self_type->tp_cache;

  return _PyClassLoader_InvokeMethod(vtable, slot, args, nargs);
}

/* This function is inlined to LIR via kCHelpersManual, so changes here will
 * have no effect. */
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
  if (_PyObject_TypeCheckOptional(obj, type, /* opt */ 1, /* exact */ 0)) {
    return obj;
  }

  PyErr_Format(
      PyExc_TypeError,
      "expected '%s', got '%s'",
      type->tp_name,
      Py_TYPE(obj)->tp_name);

  return NULL;
}

PyObject* JITRT_CastExact(PyObject* obj, PyTypeObject* type) {
  if (_PyObject_TypeCheckOptional(obj, type, /* opt */ 0, /* exact */ 1)) {
    return obj;
  }

  PyErr_Format(
      PyExc_TypeError,
      "expected exactly '%s', got '%s'",
      type->tp_name,
      Py_TYPE(obj)->tp_name);

  return NULL;
}

PyObject* JITRT_CastOptionalExact(PyObject* obj, PyTypeObject* type) {
  if (_PyObject_TypeCheckOptional(obj, type, /* opt */ 1, /* exact */ 1)) {
    return obj;
  }

  PyErr_Format(
      PyExc_TypeError,
      "expected exactly '%s', got '%s'",
      type->tp_name,
      Py_TYPE(obj)->tp_name);

  return NULL;
}

/* Needed because cast to float does extra work that would be a pain to add to
 * the manual inlined LIR for JITRT_Cast. */
PyObject* JITRT_CastToFloat(PyObject* obj) {
  if (PyObject_TypeCheck(obj, &PyFloat_Type)) {
    // cast to float is not considered pass-through by refcount insertion (since
    // it may produce a new reference), so even if in fact it is pass-through
    // (because we got a float), we need to return a new reference.
    Py_INCREF(obj);
    return obj;
  } else if (PyObject_TypeCheck(obj, &PyLong_Type)) {
    // special case because Python typing pretends int subtypes float
    return PyFloat_FromDouble(PyLong_AsLong(obj));
  }

  PyErr_Format(
      PyExc_TypeError, "expected 'float', got '%s'", Py_TYPE(obj)->tp_name);

  return NULL;
}

PyObject* JITRT_CastToFloatOptional(PyObject* obj) {
  if (_PyObject_TypeCheckOptional(
          obj, &PyFloat_Type, /* opt */ 1, /* exact */ 0)) {
    // cast to float is not considered pass-through by refcount insertion (since
    // it may produce a new reference), so even if in fact it is pass-through
    // (because we got a float), we need to return a new reference.
    Py_INCREF(obj);
    return obj;
  } else if (PyObject_TypeCheck(obj, &PyLong_Type)) {
    // special case because Python typing pretends int subtypes float
    return PyFloat_FromDouble(PyLong_AsLong(obj));
  }

  PyErr_Format(
      PyExc_TypeError, "expected 'float', got '%s'", Py_TYPE(obj)->tp_name);

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
  if (i) {
    return Py_True;
  }
  return Py_False;
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

double JITRT_PowerDouble(double x, double y) {
  return pow(x, y);
}

double JITRT_Power32(int32_t x, int32_t y) {
  return pow(x, y);
}

double JITRT_PowerUnsigned32(uint32_t x, uint32_t y) {
  return pow(x, y);
}

double JITRT_Power64(int64_t x, int64_t y) {
  return pow(x, y);
}

double JITRT_PowerUnsigned64(uint64_t x, uint64_t y) {
  return pow(x, y);
}

uint64_t JITRT_GetI64_FromArray(char* arr, int64_t idx, ssize_t offset) {
  return (uint64_t)((int64_t*)(arr + offset))[idx];
}

PyObject* JITRT_GetObj_FromArray(char* arr, int64_t idx, ssize_t offset) {
  return ((PyObject**)(arr + offset))[idx];
}

void JITRT_SetI8_InArray(char* arr, uint64_t val, int64_t idx) {
  arr[idx] = (int8_t)val;
}

void JITRT_SetU8_InArray(char* arr, uint64_t val, int64_t idx) {
  arr[idx] = (uint8_t)val;
}

void JITRT_SetI16_InArray(char* arr, uint64_t val, int64_t idx) {
  ((int16_t*)arr)[idx] = (int16_t)val;
}

void JITRT_SetU16_InArray(char* arr, uint64_t val, int64_t idx) {
  ((uint16_t*)arr)[idx] = (uint16_t)val;
}

void JITRT_SetI32_InArray(char* arr, uint64_t val, int64_t idx) {
  ((int32_t*)arr)[idx] = (int32_t)val;
}

void JITRT_SetU32_InArray(char* arr, uint64_t val, int64_t idx) {
  ((uint32_t*)arr)[idx] = (uint32_t)val;
}

void JITRT_SetI64_InArray(char* arr, uint64_t val, int64_t idx) {
  ((int64_t*)arr)[idx] = (int64_t)val;
}

void JITRT_SetU64_InArray(char* arr, uint64_t val, int64_t idx) {
  ((uint64_t*)arr)[idx] = (uint64_t)val;
}

void JITRT_SetObj_InArray(char* arr, uint64_t val, int64_t idx) {
  ((PyObject**)arr)[idx] = (PyObject*)val;
}

template <typename T>
static T checkedUnboxImpl(PyObject* obj) {
  constexpr bool is_signed = std::is_signed_v<T>;
  std::conditional_t<is_signed, int64_t, uint64_t> res;
  if constexpr (is_signed) {
    res = PyLong_AsSsize_t(obj);
  } else {
    res = PyLong_AsSize_t(obj);
  }
  if (T(res) == res || (!is_signed && res == T(-1) && PyErr_Occurred())) {
    return res;
  }
  PyErr_SetString(PyExc_OverflowError, "int overflow");
  return -1;
}

uint64_t JITRT_UnboxU64(PyObject* obj) {
  return PyLong_AsSize_t(obj);
}

uint32_t JITRT_UnboxU32(PyObject* obj) {
  return checkedUnboxImpl<uint32_t>(obj);
}

uint16_t JITRT_UnboxU16(PyObject* obj) {
  return checkedUnboxImpl<uint16_t>(obj);
}

uint8_t JITRT_UnboxU8(PyObject* obj) {
  return checkedUnboxImpl<uint8_t>(obj);
}

int64_t JITRT_UnboxI64(PyObject* obj) {
  return PyLong_AsSsize_t(obj);
}

int32_t JITRT_UnboxI32(PyObject* obj) {
  return checkedUnboxImpl<int32_t>(obj);
}

int16_t JITRT_UnboxI16(PyObject* obj) {
  return checkedUnboxImpl<int16_t>(obj);
}

int8_t JITRT_UnboxI8(PyObject* obj) {
  return checkedUnboxImpl<int8_t>(obj);
}

PyObject* JITRT_ImportName(
    PyThreadState* tstate,
    PyObject* name,
    PyObject* fromlist,
    PyObject* level) {
  _Py_IDENTIFIER(__import__);
  PyObject *import_func, *res;
  PyObject* stack[5];
  PyObject* globals = PyEval_GetGlobals();
  PyObject* builtins = tstate->interp->builtins;

  import_func = _PyDict_GetItemIdWithError(builtins, &PyId___import__);
  JIT_DCHECK(
      import_func || !PyErr_Occurred(),
      "_PyDict_GetItemIdWithError should only fail with invalid identifiers");
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

void JITRT_DoRaise(PyThreadState* tstate, PyObject* exc, PyObject* cause) {
  // If we re-raise with no error set, deliberately do nothing and let
  // prepareForDeopt() handle this. We can't let do_raise() handle this by
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
  // (2) Raising a SystemError if no exception is set (no need, do_raise
  //     already handles this).
  // (3) Calling tstate->c_tracefunc.
  // We don't support (3) and handle (1) + (2) between the check above and in
  // prepareForDeopt().
  do_raise(tstate, exc, cause);
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
    jit::CodeRuntime* code_rt,
    PyCodeObject* code) {
  PyGenObject* gen = nullptr;
  if (_PyJIT_ShadowFrame()) {
    if (mode == MakeGenObjectMode::kCoroutine) {
      gen = reinterpret_cast<PyGenObject*>(CiCoro_New_NoFrame(tstate, code));
    } else if (mode == MakeGenObjectMode::kAsyncGenerator) {
      gen = reinterpret_cast<PyGenObject*>(CiAsyncGen_New_NoFrame(code));
    } else {
      gen = reinterpret_cast<PyGenObject*>(CiGen_New_NoFrame(code));
    }
  } else {
    PyFrameObject* f = allocateFrame(
        tstate,
        code,
        code_rt->frameState()->builtins(),
        code_rt->frameState()->globals());
    // This clearing of f_back only when returning a generator matches
    // CPython's generator handling in _PyEval_EvalCodeWithName; it also avoids
    // keeping the parent frame alive longer than necessary if the caller
    // finishes before the genereator is resumed.
    Py_CLEAR(f->f_back);
    if (mode == MakeGenObjectMode::kCoroutine) {
      gen = reinterpret_cast<PyGenObject*>(
          PyCoro_New(f, code->co_name, code->co_qualname));
    } else if (mode == MakeGenObjectMode::kAsyncGenerator) {
      gen = reinterpret_cast<PyGenObject*>(
          PyAsyncGen_New(f, code->co_name, code->co_qualname));
    } else {
      gen = reinterpret_cast<PyGenObject*>(
          PyGen_NewWithQualName(f, code->co_name, code->co_qualname));
    }
  }
  if (gen == nullptr) {
    return nullptr;
  }

  gen->gi_shadow_frame.data = gen->gi_frame == nullptr
      ? _PyShadowFrame_MakeData(code_rt, PYSF_CODE_RT, PYSF_JIT)
      : _PyShadowFrame_MakeData(gen->gi_frame, PYSF_PYFRAME, PYSF_JIT);

  spill_words = std::max(spill_words, jit::kMinGenSpillWords);

  auto suspend_data = gen_data_allocate(spill_words);
  auto footer = reinterpret_cast<jit::GenDataFooter*>(
      reinterpret_cast<uint64_t*>(suspend_data) + spill_words);
  footer->resumeEntry = resume_entry;
  footer->yieldPoint = nullptr;
  footer->state = Ci_JITGenState_JustStarted;
  footer->gen = gen;
  footer->code_rt = code_rt;

  gen->gi_jit_data = reinterpret_cast<Ci_JITGenData*>(footer);

  return reinterpret_cast<PyObject*>(gen);
}

PyObject* JITRT_MakeGenObject(
    GenResumeFunc resume_entry,
    PyThreadState* tstate,
    size_t spill_words,
    jit::CodeRuntime* code_rt,
    PyCodeObject* code) {
  return make_gen_object<MakeGenObjectMode::kGenerator>(
      resume_entry, tstate, spill_words, code_rt, code);
}

PyObject* JITRT_MakeGenObjectAsyncGen(
    GenResumeFunc resume_entry,
    PyThreadState* tstate,
    size_t spill_words,
    jit::CodeRuntime* code_rt,
    PyCodeObject* code) {
  return make_gen_object<MakeGenObjectMode::kAsyncGenerator>(
      resume_entry, tstate, spill_words, code_rt, code);
}

PyObject* JITRT_MakeGenObjectCoro(
    GenResumeFunc resume_entry,
    PyThreadState* tstate,
    size_t spill_words,
    jit::CodeRuntime* code_rt,
    PyCodeObject* code) {
  return make_gen_object<MakeGenObjectMode::kCoroutine>(
      resume_entry, tstate, spill_words, code_rt, code);
}

void JITRT_SetCurrentAwaiter(PyObject* awaitable, PyThreadState* ts) {
  _PyShadowFrame* sf = ts->shadow_frame;
  // TODO(bsimmers): This may need to change when we support eager evaluation of
  // coroutines.
  auto awaiter = reinterpret_cast<PyObject*>(_PyShadowFrame_GetGen(sf));
  _PyAwaitable_SetAwaiter(awaitable, awaiter);
}

JITRT_YieldFromRes
JITRT_YieldFrom(PyObject* gen, PyObject* v, uint64_t finish_yield_from) {
  if (v == NULL) {
    return {NULL, 1};
  }
  if (finish_yield_from) {
    Py_INCREF(v);
    return {v, 1};
  }
  PyObject* retval;
  auto gen_status = PyIter_Send(gen, v, &retval);

  if (gen_status == PYGEN_RETURN) {
    return {retval, 1};
  }
  if (gen_status == PYGEN_ERROR) {
    return {NULL, 1};
  }
  JIT_DCHECK(gen_status == PYGEN_NEXT, "Unexpected gen_status:", gen_status);
  return {retval, 0};
}

JITRT_YieldFromRes JITRT_YieldFromHandleStopAsyncIteration(
    PyObject* gen,
    PyObject* v,
    uint64_t finish_yield_from) {
  JITRT_YieldFromRes res = JITRT_YieldFrom(gen, v, finish_yield_from);
  if ((res.retval == NULL) && (res.done == 1) &&
      PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
    PyErr_Clear();
    Py_INCREF(&jit::g_iterDoneSentinel);
    res.retval = &jit::g_iterDoneSentinel;
  }
  return res;
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

JITRT_StaticCallReturn JITRT_FailedDeferredCompileShim(
    PyFunctionObject* func,
    PyObject** args) {
  void* no_error = reinterpret_cast<void*>(1);

  PyCodeObject* code = (PyCodeObject*)func->func_code;
  int total_args = code->co_argcount;
  if (code->co_flags & CO_VARARGS) {
    total_args++;
  }
  if (code->co_flags & CO_VARKEYWORDS) {
    total_args++;
  }

  // PyObject** args is:
  // arg0 - function object
  // arg1 - first real argument
  // arg2
  // arg3
  // arg4
  // arg5
  // previous rbp
  // return address to JITed code
  // memory argument 0 (6th real argument)
  // memory argument 1
  // ...

  PyObject** dest_args;
  PyObject* final_args[total_args];
  if (total_args <= 5) {
    // no gap in args to worry about
    dest_args = args + 1;
  } else {
    for (int i = 0; i < 5; i++) {
      final_args[i] = args[i + 1];
    }
    for (int i = 5; i < total_args; i++) {
      final_args[i] = args[i + 3];
    }
    dest_args = final_args;
  }

  _PyTypedArgsInfo* arg_info =
      jit::Runtime::get()->findFunctionPrimitiveArgInfo(func);
  PyObject* allocated_args[arg_info == nullptr ? 0 : Py_SIZE(arg_info)];
  int allocated_count = 0;

  if (arg_info != nullptr) {
    // We have primitive values that need to be converted into boxed values
    // to run the interpreter loop.
    for (Py_ssize_t i = 0; i < Py_SIZE(arg_info); i++) {
      if (arg_info->tai_args[i].tai_primitive_type != -1) {
        // primitive type, box...
        int arg = arg_info->tai_args[i].tai_argnum + 1;
        uint64_t arg_val;
        if (arg >= 6) {
          arg += 4;
        }
        arg_val = (uint64_t)args[arg];

        PyObject* new_val = _PyClassLoader_Box(
            arg_val, arg_info->tai_args[i].tai_primitive_type);

        if (new_val == nullptr) {
          for (int i = 0; i < allocated_count; i++) {
            Py_DECREF(allocated_args[i]);
          }
          return JITRT_StaticCallReturn{nullptr, nullptr};
        }

        // we can update the incoming arg array, either it's
        // the pushed values on the stack by the trampoline, or
        // it's final_args we allocated above.
        dest_args[arg - 1] = new_val;
        allocated_args[allocated_count++] = new_val;
      }
    }
  }

  PyObject* res =
      _PyObject_Vectorcall((PyObject*)func, dest_args, total_args, NULL);

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
  int optional, exact;
  PyTypeObject* ret_type = _PyClassLoader_ResolveType(
      _PyClassLoader_GetReturnTypeDescr(func), &optional, &exact);
  int ret_code = _PyClassLoader_GetTypeCode(ret_type);
  Py_DECREF(ret_type);
  if (ret_code != TYPED_OBJECT) {
    // we can always unbox to 64-bit, the JIT will just ignore the higher bits.
    // (TODO) This means that overflow here will give weird results, but
    // overflow in primitive ints in static python is undefined behavior right
    // now anyway, until we implement overflow checking. It doesn't make sense
    // to implement overflow checking just here in the "unjitable" code path,
    // when overflow won't be checked if the code is JITted.
    void* ival;
    if (ret_code == TYPED_BOOL) {
      ival = (void*)(res == Py_True);
    } else if (ret_code & TYPED_INT_SIGNED) {
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

PyObject* JITRT_SequenceContains(PyObject* haystack, PyObject* needle) {
  int result = PySequence_Contains(haystack, needle);
  if (result < 0) {
    return nullptr;
  }
  if (result) {
    Py_RETURN_TRUE;
  }
  Py_RETURN_FALSE;
}

PyObject* JITRT_SequenceNotContains(PyObject* haystack, PyObject* needle) {
  int result = PySequence_Contains(haystack, needle);
  if (result < 0) {
    return nullptr;
  }
  if (result) {
    Py_RETURN_FALSE;
  }
  Py_RETURN_TRUE;
}

int JITRT_NotContainsBool(PyObject* w, PyObject* v) {
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

PyObject* JITRT_CompareIs(PyObject* left, PyObject* right, int op) {
  if (op == static_cast<int>(jit::hir::CompareOp::kIs)) {
    if (left == right) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
  }
  if (op == static_cast<int>(jit::hir::CompareOp::kIsNot)) {
    if (left != right) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
  }
  JIT_ABORT("bad comparison op %d", op);
}

/* perform a batch decref to the objects in args */
void JITRT_BatchDecref(PyObject** args, int nargs) {
  for (int i = 0; i < nargs; i++) {
    Py_DECREF(args[i]);
  }
}

Py_ssize_t JITRT_CheckSequenceBounds(PyObject* s, Py_ssize_t i) {
  i = i < 0 ? i + Py_SIZE(s) : i;
  if (i < 0 || i >= Py_SIZE(s)) {
    PyErr_SetString(PyExc_IndexError, "index out of range");
    return -1;
  }
  return i;
}

PyObject* JITRT_GetLength(PyObject* obj) {
  // Same as GET_LEN handler in Python/ceval.c
  Py_ssize_t len = PyObject_Length(obj);
  if (len < 0) {
    return nullptr;
  }
  return PyLong_FromSsize_t(len);
}

int JITRT_DictUpdate(PyThreadState* tstate, PyObject* dict, PyObject* update) {
  if (PyDict_Update(dict, update) < 0) {
    if (_PyErr_ExceptionMatches(tstate, PyExc_AttributeError)) {
      _PyErr_Format(
          tstate,
          PyExc_TypeError,
          "'%.200s' object is not a mapping",
          Py_TYPE(update)->tp_name);
    }
    return -1;
  }
  return 0;
}

int JITRT_DictMerge(
    PyThreadState* tstate,
    PyObject* dict,
    PyObject* update,
    PyObject* func) {
  if (_PyDict_MergeEx(dict, update, 2) < 0) {
    format_kwargs_error(tstate, func, update);
    return -1;
  }
  return 0;
}

PyObject* JITRT_CopyDictWithoutKeys(PyObject* subject, PyObject* keys) {
  // Copied from Python/ceval.c implementation of COPY_DICT_WITHOUT_KEYS.
  Ref<> rest(Ref<>::steal(PyDict_New()));
  if (rest == nullptr || PyDict_Update(rest, subject)) {
    return nullptr;
  }
  JIT_DCHECK(PyTuple_CheckExact(keys), "Expected keys to be an exact tuple");
  for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(keys); i++) {
    if (PyDict_DelItem(rest, PyTuple_GET_ITEM(keys, i))) {
      return nullptr;
    }
  }
  return rest.release();
}

#include "cinderx/Interpreter/opcode.h"

#define CINDERX_INTERPRETER
#ifdef FBCODE_BUILD
#include "ceval.c"
#else
#include "../../Python/ceval.c"
#endif

#include "cinderx/Jit/pyjit.h"
#include "cinderx/Shadowcode/shadowcode.h"
#include "cinderx/StaticPython/checked_dict.h"
#include "cinderx/StaticPython/checked_list.h"
#include "cinderx/StaticPython/classloader.h"
#include "cinderx/StaticPython/static_array.h"

#define PYSHADOW_INIT_THRESHOLD 50

PyObject *Ci_GetAIter(PyThreadState *tstate, PyObject *obj) {
    unaryfunc getter = NULL;
    PyObject *iter = NULL;
    PyTypeObject *type = Py_TYPE(obj);

    if (type->tp_as_async != NULL) {
        getter = type->tp_as_async->am_aiter;
    }

    if (getter != NULL) {
        iter = (*getter)(obj);
        if (iter == NULL) {
            return NULL;
        }
    }
    else {
        _PyErr_Format(tstate, PyExc_TypeError,
                        "'async for' requires an object with "
                        "__aiter__ method, got %.100s",
                        type->tp_name);
        return NULL;
    }

    if (Py_TYPE(iter)->tp_as_async == NULL ||
            Py_TYPE(iter)->tp_as_async->am_anext == NULL) {

        _PyErr_Format(tstate, PyExc_TypeError,
                        "'async for' received an object from __aiter__ "
                        "that does not implement __anext__: %.100s",
                        Py_TYPE(iter)->tp_name);
        Py_DECREF(iter);
        return NULL;
    }
    return iter;
}

PyObject *Ci_GetANext(PyThreadState *tstate, PyObject *aiter) {
    unaryfunc getter = NULL;
    PyObject *next_iter = NULL;
    PyObject *awaitable = NULL;
    PyTypeObject *type = Py_TYPE(aiter);

    if (PyAsyncGen_CheckExact(aiter)) {
        awaitable = type->tp_as_async->am_anext(aiter);
        if (awaitable == NULL) {
            return NULL;
        }
    } else {
        if (type->tp_as_async != NULL){
            getter = type->tp_as_async->am_anext;
        }

        if (getter != NULL) {
            next_iter = (*getter)(aiter);
            if (next_iter == NULL) {
                return NULL;
            }
        }
        else {
            _PyErr_Format(tstate, PyExc_TypeError,
                            "'async for' requires an iterator with "
                            "__anext__ method, got %.100s",
                            type->tp_name);
            return NULL;
        }

        awaitable = _PyCoro_GetAwaitableIter(next_iter);
        if (awaitable == NULL) {
            _PyErr_FormatFromCause(
                PyExc_TypeError,
                "'async for' received an invalid object "
                "from __anext__: %.100s",
                Py_TYPE(next_iter)->tp_name);

            Py_DECREF(next_iter);
            return NULL;
        } else {
            Py_DECREF(next_iter);
        }
    }
    return awaitable;
}

// These are used to truncate primitives/check signed bits when converting between them
static uint64_t trunc_masks[] = {0xFF, 0xFFFF, 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF};
static uint64_t signed_bits[] = {0x80, 0x8000, 0x80000000, 0x8000000000000000};
static uint64_t signex_masks[] = {0xFFFFFFFFFFFFFF00, 0xFFFFFFFFFFFF0000,
                                  0xFFFFFFFF00000000, 0x0};

// #ifdef HAVE_ERRNO_H
// #include <errno.h>
// #endif
// #include "ceval_gil.h"

PyAPI_DATA(int) Py_LazyImportsFlag;

static inline int8_t
unbox_primitive_bool_and_decref(PyObject *x)
{
    assert(PyBool_Check(x));
    int8_t res = (x == Py_True) ? 1 : 0;
    Py_DECREF(x);
    return res;
}

static inline Py_ssize_t
unbox_primitive_int_and_decref(PyObject *x)
{
    assert(PyLong_Check(x));
    Py_ssize_t res = (Py_ssize_t)PyLong_AsVoidPtr(x);
    Py_DECREF(x);
    return res;
}

static inline void
store_field(int field_type, void *addr, PyObject *value)
{
    switch (field_type) {
    case TYPED_BOOL:
        *(int8_t *)addr = (int8_t)unbox_primitive_bool_and_decref(value);
        break;
    case TYPED_INT8:
        *(int8_t *)addr = (int8_t)unbox_primitive_int_and_decref(value);
        break;
    case TYPED_INT16:
        *(int16_t *)addr = (int16_t)unbox_primitive_int_and_decref(value);
        break;
    case TYPED_INT32:
        *(int32_t *)addr = (int32_t)unbox_primitive_int_and_decref(value);
        break;
    case TYPED_INT64:
        *(int64_t *)addr = (int64_t)unbox_primitive_int_and_decref(value);
        break;
    case TYPED_UINT8:
        *(uint8_t *)addr = (uint8_t)unbox_primitive_int_and_decref(value);
        break;
    case TYPED_UINT16:
        *(uint16_t *)addr = (uint16_t)unbox_primitive_int_and_decref(value);
        break;
    case TYPED_UINT32:
        *(uint32_t *)addr = (uint32_t)unbox_primitive_int_and_decref(value);
        break;
    case TYPED_UINT64:
        *(uint64_t *)addr = (uint64_t)unbox_primitive_int_and_decref(value);
        break;
    case TYPED_DOUBLE:
        *((double*)addr) = PyFloat_AsDouble(value);
        Py_DECREF(value);
        break;
    default:
        PyErr_SetString(PyExc_RuntimeError, "unsupported field type");
    }
}

static inline PyObject *
load_field(int field_type, void *addr)
{
    PyObject *value;
    switch (field_type) {
    case TYPED_BOOL:
        value = PyBool_FromLong(*(int8_t *)addr);
        break;
    case TYPED_INT8:
        value = PyLong_FromVoidPtr((void *)(Py_ssize_t) * ((int8_t *)addr));
        break;
    case TYPED_INT16:
        value = PyLong_FromVoidPtr((void *)(Py_ssize_t) * ((int16_t *)addr));
        break;
    case TYPED_INT32:
        value = PyLong_FromVoidPtr((void *)(Py_ssize_t) * ((int32_t *)addr));
        break;
    case TYPED_INT64:
        value = PyLong_FromVoidPtr((void *)(Py_ssize_t) * ((int64_t *)addr));
        break;
    case TYPED_UINT8:
        value = PyLong_FromVoidPtr((void *)(Py_ssize_t) * ((uint8_t *)addr));
        break;
    case TYPED_UINT16:
        value = PyLong_FromVoidPtr((void *)(Py_ssize_t) * ((uint16_t *)addr));
        break;
    case TYPED_UINT32:
        value = PyLong_FromVoidPtr((void *)(Py_ssize_t) * ((uint32_t *)addr));
        break;
    case TYPED_UINT64:
        value = PyLong_FromVoidPtr((void *)(Py_ssize_t) * ((uint64_t *)addr));
        break;
    case TYPED_DOUBLE:
        value = PyFloat_FromDouble(*(double *)addr);
        break;
    default:
        PyErr_SetString(PyExc_RuntimeError, "unsupported field type");
        return NULL;
    }
    return value;
}

static inline PyObject *
box_primitive(int type, Py_ssize_t value)
{
    switch (type) {
    case TYPED_BOOL:
        return PyBool_FromLong((int8_t)value);
    case TYPED_INT8:
    case TYPED_CHAR:
        return PyLong_FromSsize_t((int8_t)value);
    case TYPED_INT16:
        return PyLong_FromSsize_t((int16_t)value);
    case TYPED_INT32:
        return PyLong_FromSsize_t((int32_t)value);
    case TYPED_INT64:
        return PyLong_FromSsize_t((int64_t)value);
    case TYPED_UINT8:
        return PyLong_FromSize_t((uint8_t)value);
    case TYPED_UINT16:
        return PyLong_FromSize_t((uint16_t)value);
    case TYPED_UINT32:
        return PyLong_FromSize_t((uint32_t)value);
    case TYPED_UINT64:
        return PyLong_FromSize_t((uint64_t)value);
    default:
        assert(0);
        return NULL;
    }
}

static PyObject *
invoke_static_function(PyObject *func, PyObject **args, Py_ssize_t nargs, int awaited) {
    return _PyObject_Vectorcall(
        func,
        args,
        (awaited ? Ci_Py_AWAITED_CALL_MARKER : 0) | nargs,
        NULL);
}


static inline void try_profile_next_instr(PyFrameObject* f,
                                          PyObject** stack_pointer,
                                          const _Py_CODEUNIT* next_instr) {
    int opcode, oparg;
    NEXTOPARG();
    while (opcode == EXTENDED_ARG) {
        int oldoparg = oparg;
        NEXTOPARG();
        oparg |= oldoparg << 8;
    }

    /* _PyJIT_ProfileCurrentInstr owns the canonical list of which instructions
     * we want to record types for. To save a little work, filter out a few
     * opcodes that we know the JIT will never care about and account for
     * roughly 50% of dynamic bytecodes. */
    switch (opcode) {
        case LOAD_FAST:
        case STORE_FAST:
        case LOAD_CONST:
        case RETURN_VALUE: {
            break;
        }
        default: {
            _PyJIT_ProfileCurrentInstr(f, stack_pointer, opcode, oparg);
            break;
        }
    }
}

// Disable UBSAN integer overflow checks etc. as these are not compatible with
// some tests for Static Python which are asserting overflow behavior.
__attribute__((no_sanitize("integer")))
PyObject* _Py_HOT_FUNCTION
Ci_EvalFrame(PyThreadState *tstate, PyFrameObject *f, int throwflag)
{
    _Py_EnsureTstateNotNULL(tstate);

#if USE_COMPUTED_GOTOS
/* Import the static jump table */
#define CINDERX_INTERPRETER
#include "cinderx/Interpreter/cinderx_opcode_targets.h"
#endif

#ifdef DXPAIRS
    int lastopcode = 0;
#endif
    PyObject **stack_pointer;  /* Next free slot in value stack */
    const _Py_CODEUNIT *next_instr;
    int opcode;        /* Current opcode */
    int oparg;         /* Current opcode argument, if any */
    PyObject **fastlocals, **freevars;
    PyObject *retval = NULL;            /* Return value */
    _Py_atomic_int * const eval_breaker = &tstate->interp->ceval.eval_breaker;
    PyCodeObject *co;
    _PyShadowFrame shadow_frame;
    Py_ssize_t profiled_instrs = 0;

    const _Py_CODEUNIT *first_instr;
    PyObject *names;
    PyObject *consts;
    _PyShadow_EvalState shadow = {}; /* facebook T39538061 */

#ifdef LLTRACE
    _Py_IDENTIFIER(__ltrace__);
#endif

    if (_Py_EnterRecursiveCall(tstate, "")) {
        return NULL;
    }

    PyTraceInfo trace_info;
    /* Mark trace_info as uninitialized */
    trace_info.code = NULL;

    /* WARNING: Because the CFrame lives on the C stack,
     * but can be accessed from a heap allocated object (tstate)
     * strict stack discipline must be maintained.
     */
    CFrame *prev_cframe = tstate->cframe;
    trace_info.cframe.use_tracing = prev_cframe->use_tracing;
    trace_info.cframe.previous = prev_cframe;
    tstate->cframe = &trace_info.cframe;

    /*
     * When shadow-frame mode is active, `tstate->frame` may have changed
     * between when `f` was allocated and now. Reset `f->f_back` to point to
     * the top-most frame if so.
     */
    if (f->f_back != tstate->frame) {
      Py_XINCREF(tstate->frame);
      Py_XSETREF(f->f_back, tstate->frame);
    }

    /* push frame */
    tstate->frame = f;
    co = f->f_code;
    co->co_mutable->curcalls++;

    // Generator shadow frames are managed by the send implementation.
    if (f->f_gen == NULL) {
        _PyShadowFrame_PushInterp(tstate, &shadow_frame, f);
    }

    if (trace_info.cframe.use_tracing) {
        if (tstate->c_tracefunc != NULL) {
            /* tstate->c_tracefunc, if defined, is a
               function that will be called on *every* entry
               to a code block.  Its return value, if not
               None, is a function that will be called at
               the start of each executed line of code.
               (Actually, the function must return itself
               in order to continue tracing.)  The trace
               functions are called with three arguments:
               a pointer to the current frame, a string
               indicating why the function is called, and
               an argument which depends on the situation.
               The global trace function is also called
               whenever an exception is detected. */
            if (call_trace_protected(tstate->c_tracefunc,
                                     tstate->c_traceobj,
                                     tstate, f, &trace_info,
                                     PyTrace_CALL, Py_None)) {
                /* Trace function raised an error */
                goto exit_eval_frame;
            }
        }
        if (tstate->c_profilefunc != NULL) {
            /* Similar for c_profilefunc, except it needn't
               return itself and isn't called for "line" events */
            if (call_trace_protected(tstate->c_profilefunc,
                                     tstate->c_profileobj,
                                     tstate, f, &trace_info,
                                     PyTrace_CALL, Py_None)) {
                /* Profile function raised an error */
                goto exit_eval_frame;
            }
        }
    }

    if (PyDTrace_FUNCTION_ENTRY_ENABLED())
        dtrace_function_entry(f);

    /* facebook begin t39538061 */
    /* Initialize the inline cache after the code object is "hot enough" */
    if (!tstate->profile_interp && co->co_mutable->shadow == NULL &&
        Ci_cinderx_initialized && _PyEval_ShadowByteCodeEnabled) {
        if (++(co->co_mutable->ncalls) > PYSHADOW_INIT_THRESHOLD) {
            if (_PyShadow_InitCache(co) == -1) {
                goto error;
            }
            INLINE_CACHE_CREATED(co->co_mutable);
        }
    }
    /* facebook end t39538061 */

    int profiling_candidate = 0;
    if (tstate->profile_interp) {
      profiling_candidate = _PyJIT_IsProfilingCandidate(co);
    }

    names = co->co_names;
    consts = co->co_consts;
    fastlocals = f->f_localsplus;
    freevars = f->f_localsplus + co->co_nlocals;
    assert(PyBytes_Check(co->co_code));
    assert(PyBytes_GET_SIZE(co->co_code) <= INT_MAX);
    assert(PyBytes_GET_SIZE(co->co_code) % sizeof(_Py_CODEUNIT) == 0);
    assert(_Py_IS_ALIGNED(PyBytes_AS_STRING(co->co_code), sizeof(_Py_CODEUNIT)));

    /* facebook begin t39538061 */
    shadow.code = co;
    shadow.first_instr = &first_instr;
    assert(PyDict_CheckExact(f->f_builtins));
    PyObject ***global_cache = NULL;
    if (co->co_mutable->shadow != NULL && PyDict_CheckExact(f->f_globals)) {
        shadow.shadow = co->co_mutable->shadow;
        global_cache = shadow.shadow->globals;
        first_instr = &shadow.shadow->code[0];
    } else {
        first_instr = (_Py_CODEUNIT *)PyBytes_AS_STRING(co->co_code);
    }
    /* facebook end t39538061 */

    /*
       f->f_lasti refers to the index of the last instruction,
       unless it's -1 in which case next_instr should be first_instr.

       YIELD_FROM sets f_lasti to itself, in order to repeatedly yield
       multiple values.

       When the PREDICT() macros are enabled, some opcode pairs follow in
       direct succession without updating f->f_lasti.  A successful
       prediction effectively links the two codes together as if they
       were a single new opcode; accordingly,f->f_lasti will point to
       the first code in the pair (for instance, GET_ITER followed by
       FOR_ITER is effectively a single opcode and f->f_lasti will point
       to the beginning of the combined pair.)
    */
    assert(f->f_lasti >= -1);
    next_instr = first_instr + f->f_lasti + 1;
    stack_pointer = f->f_valuestack + f->f_stackdepth;
    /* Set f->f_stackdepth to -1.
     * Update when returning or calling trace function.
       Having f_stackdepth <= 0 ensures that invalid
       values are not visible to the cycle GC.
       We choose -1 rather than 0 to assist debugging.
     */
    f->f_stackdepth = -1;
    f->f_state = FRAME_EXECUTING;

#ifdef LLTRACE
    {
        int r = _PyDict_ContainsId(f->f_globals, &PyId___ltrace__);
        if (r < 0) {
            goto exit_eval_frame;
        }
        lltrace = r;
    }
#endif

    if (throwflag) { /* support for generator.throw() */
        goto error;
    }

#ifdef Py_DEBUG
    /* _PyEval_EvalFrameDefault() must not be called with an exception set,
       because it can clear it (directly or indirectly) and so the
       caller loses its exception */
    assert(!_PyErr_Occurred(tstate));
#endif

f->lazy_imports = -1;
f->lazy_imports_cache = 0;
f->lazy_imports_cache_seq = -1;

main_loop:
    for (;;) {
        assert(stack_pointer >= f->f_valuestack); /* else underflow */
        assert(STACK_LEVEL() <= co->co_stacksize);  /* else overflow */
        assert(!_PyErr_Occurred(tstate));

        /* Do periodic things.  Doing this every time through
           the loop would add too much overhead, so we do it
           only every Nth instruction.  We also do it if
           ``pending.calls_to_do'' is set, i.e. when an asynchronous
           event needs attention (e.g. a signal handler or
           async I/O handler); see Py_AddPendingCall() and
           Py_MakePendingCalls() above. */

        if (_Py_atomic_load_relaxed(eval_breaker)) {
            opcode = _Py_OPCODE(*next_instr);
            if (opcode != SETUP_FINALLY &&
                opcode != SETUP_WITH &&
                opcode != BEFORE_ASYNC_WITH &&
                opcode != YIELD_FROM) {
                /* Few cases where we skip running signal handlers and other
                   pending calls:
                   - If we're about to enter the 'with:'. It will prevent
                     emitting a resource warning in the common idiom
                     'with open(path) as file:'.
                   - If we're about to enter the 'async with:'.
                   - If we're about to enter the 'try:' of a try/finally (not
                     *very* useful, but might help in some cases and it's
                     traditional)
                   - If we're resuming a chain of nested 'yield from' or
                     'await' calls, then each frame is parked with YIELD_FROM
                     as its next opcode. If the user hit control-C we want to
                     wait until we've reached the innermost frame before
                     running the signal handler and raising KeyboardInterrupt
                     (see bpo-30039).
                */
                if (Cix_eval_frame_handle_pending(tstate) != 0) {
                    goto error;
                }
             }
        }

    tracing_dispatch:
    {
        int instr_prev = f->f_lasti;
        f->f_lasti = INSTR_OFFSET();
        NEXTOPARG();

        struct _ceval_state *ceval = &tstate->interp->ceval;

        if (tstate->profile_interp != 0) {
          int do_profile = 0;

          // Profile if we're we've hit the global sampling period.
          if (ceval->profile_instr_period > 0 &&
              ++ceval->profile_instr_counter == ceval->profile_instr_period) {
            ceval->profile_instr_counter = 0;
            do_profile = 1;
          }

          // Profile if the code object has been marked as hot by AutoJIT.
          if (profiling_candidate) {
            do_profile = 1;
          }

          if (do_profile) {
            profiled_instrs++;
            try_profile_next_instr(f, stack_pointer, next_instr - 1);
          }
        }

        if (PyDTrace_LINE_ENABLED())
            maybe_dtrace_line(f, &trace_info, instr_prev);

        /* line-by-line tracing support */

        if (trace_info.cframe.use_tracing &&
            tstate->c_tracefunc != NULL && !tstate->tracing) {
            int err;
            /* see maybe_call_line_trace()
               for expository comments */
            f->f_stackdepth = (int)(stack_pointer - f->f_valuestack);

            err = maybe_call_line_trace(tstate->c_tracefunc,
                                        tstate->c_traceobj,
                                        tstate, f,
                                        &trace_info, instr_prev);
            /* Reload possibly changed frame fields */
            JUMPTO(f->f_lasti);
            stack_pointer = f->f_valuestack+f->f_stackdepth;
            f->f_stackdepth = -1;
            if (err) {
                /* trace function raised an exception */
                goto error;
            }
            NEXTOPARG();
        }
    }

#ifdef LLTRACE
        /* Instruction tracing */

        if (lltrace) {
            if (HAS_ARG(opcode)) {
                printf("%d: %d, %d\n",
                       f->f_lasti, opcode, oparg);
            }
            else {
                printf("%d: %d\n",
                       f->f_lasti, opcode);
            }
        }
#endif
#if USE_COMPUTED_GOTOS == 0
    goto dispatch_opcode;

    predispatch:
        if (trace_info.cframe.use_tracing OR_DTRACE_LINE OR_LLTRACE) {
            goto tracing_dispatch;
        }
        f->f_lasti = INSTR_OFFSET();
        NEXTOPARG();
#endif
    dispatch_opcode:
#ifdef DYNAMIC_EXECUTION_PROFILE
#ifdef DXPAIRS
        dxpairs[lastopcode][opcode]++;
        lastopcode = opcode;
#endif
        dxp[opcode]++;
#endif

        switch (opcode) {

        /* BEWARE!
           It is essential that any operation that fails must goto error
           and that all operation that succeed call DISPATCH() ! */

        case TARGET(NOP): {
            DISPATCH();
        }

        case TARGET(LOAD_FAST): {
            PyObject *value = GETLOCAL(oparg);
            if (value == NULL) {
                format_exc_check_arg(tstate, PyExc_UnboundLocalError,
                                     UNBOUNDLOCAL_ERROR_MSG,
                                     PyTuple_GetItem(co->co_varnames, oparg));
                goto error;
            }
            Py_INCREF(value);
            PUSH(value);
            DISPATCH();
        }

        case TARGET(LOAD_CONST): {
            PREDICTED(LOAD_CONST);
            PyObject *value = GETITEM(consts, oparg);
            Py_INCREF(value);
            PUSH(value);
            DISPATCH();
        }

        case TARGET(STORE_FAST): {
            PREDICTED(STORE_FAST);
            PyObject *value = POP();
            SETLOCAL(oparg, value);
            DISPATCH();
        }

        case TARGET(POP_TOP): {
            PyObject *value = POP();
            Py_DECREF(value);
            DISPATCH();
        }

        case TARGET(ROT_TWO): {
            PyObject *top = TOP();
            PyObject *second = SECOND();
            SET_TOP(second);
            SET_SECOND(top);
            DISPATCH();
        }

        case TARGET(ROT_THREE): {
            PyObject *top = TOP();
            PyObject *second = SECOND();
            PyObject *third = THIRD();
            SET_TOP(second);
            SET_SECOND(third);
            SET_THIRD(top);
            DISPATCH();
        }

        case TARGET(ROT_FOUR): {
            PyObject *top = TOP();
            PyObject *second = SECOND();
            PyObject *third = THIRD();
            PyObject *fourth = FOURTH();
            SET_TOP(second);
            SET_SECOND(third);
            SET_THIRD(fourth);
            SET_FOURTH(top);
            DISPATCH();
        }

        case TARGET(DUP_TOP): {
            PyObject *top = TOP();
            Py_INCREF(top);
            PUSH(top);
            DISPATCH();
        }

        case TARGET(DUP_TOP_TWO): {
            PyObject *top = TOP();
            PyObject *second = SECOND();
            Py_INCREF(top);
            Py_INCREF(second);
            STACK_GROW(2);
            SET_TOP(top);
            SET_SECOND(second);
            DISPATCH();
        }

        case TARGET(UNARY_POSITIVE): {
            PyObject *value = TOP();
            PyObject *res = PyNumber_Positive(value);
            Py_DECREF(value);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(UNARY_NEGATIVE): {
            PyObject *value = TOP();
            PyObject *res = PyNumber_Negative(value);
            Py_DECREF(value);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(UNARY_NOT): {
            PyObject *value = TOP();
            int err = PyObject_IsTrue(value);
            Py_DECREF(value);
            if (err == 0) {
                Py_INCREF(Py_True);
                SET_TOP(Py_True);
                DISPATCH();
            }
            else if (err > 0) {
                Py_INCREF(Py_False);
                SET_TOP(Py_False);
                DISPATCH();
            }
            STACK_SHRINK(1);
            goto error;
        }

        case TARGET(UNARY_INVERT): {
            PyObject *value = TOP();
            PyObject *res = PyNumber_Invert(value);
            Py_DECREF(value);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_POWER): {
            PyObject *exp = POP();
            PyObject *base = TOP();
            PyObject *res = PyNumber_Power(base, exp, Py_None);
            Py_DECREF(base);
            Py_DECREF(exp);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_MULTIPLY): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_Multiply(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_MATRIX_MULTIPLY): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_MatrixMultiply(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_TRUE_DIVIDE): {
            PyObject *divisor = POP();
            PyObject *dividend = TOP();
            PyObject *quotient = PyNumber_TrueDivide(dividend, divisor);
            Py_DECREF(dividend);
            Py_DECREF(divisor);
            SET_TOP(quotient);
            if (quotient == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_FLOOR_DIVIDE): {
            PyObject *divisor = POP();
            PyObject *dividend = TOP();
            PyObject *quotient = PyNumber_FloorDivide(dividend, divisor);
            Py_DECREF(dividend);
            Py_DECREF(divisor);
            SET_TOP(quotient);
            if (quotient == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_MODULO): {
            PyObject *divisor = POP();
            PyObject *dividend = TOP();
            PyObject *res;
            if (PyUnicode_CheckExact(dividend) && (
                  !PyUnicode_Check(divisor) || PyUnicode_CheckExact(divisor))) {
              // fast path; string formatting, but not if the RHS is a str subclass
              // (see issue28598)
              res = PyUnicode_Format(dividend, divisor);
            } else {
              res = PyNumber_Remainder(dividend, divisor);
            }
            Py_DECREF(divisor);
            Py_DECREF(dividend);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_ADD): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *sum;
            /* NOTE(vstinner): Please don't try to micro-optimize int+int on
               CPython using bytecode, it is simply worthless.
               See http://bugs.python.org/issue21955 and
               http://bugs.python.org/issue10044 for the discussion. In short,
               no patch shown any impact on a realistic benchmark, only a minor
               speedup on microbenchmarks. */
            if (PyUnicode_CheckExact(left) &&
                     PyUnicode_CheckExact(right)) {
                sum = unicode_concatenate(tstate, left, right, f, next_instr);
                /* unicode_concatenate consumed the ref to left */
            }
            else {
                sum = PyNumber_Add(left, right);
                Py_DECREF(left);
            }
            Py_DECREF(right);
            SET_TOP(sum);
            if (sum == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_SUBTRACT): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *diff = PyNumber_Subtract(left, right);
            Py_DECREF(right);
            Py_DECREF(left);
            SET_TOP(diff);
            if (diff == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_SUBSCR): {
            PyObject *res;
            PyObject *sub = POP();
            PyObject *container = TOP();
#ifdef INLINE_CACHE_PROFILE
            char type_names[81];
            snprintf(type_names,
                     sizeof(type_names),
                     "%s[%s]",
                     Py_TYPE(container)->tp_name,
                     Py_TYPE(sub)->tp_name);
            INLINE_CACHE_INCR("binary_subscr_types", type_names);

#endif
            res = shadow.shadow == NULL
                      ? PyObject_GetItem(container, sub)
                      : _PyShadow_BinarySubscrWithCache(
                            &shadow, next_instr, container, sub, oparg);
            Py_DECREF(container);
            Py_DECREF(sub);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_LSHIFT): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_Lshift(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_RSHIFT): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_Rshift(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_AND): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_And(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_XOR): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_Xor(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_OR): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_Or(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(LIST_APPEND): {
            PyObject *v = POP();
            PyObject *list = PEEK(oparg);
            int err;
            err = Ci_ListOrCheckedList_Append((PyListObject *) list, v);
            Py_DECREF(v);
            if (err != 0)
                goto error;
            PREDICT(JUMP_ABSOLUTE);
            DISPATCH();
        }

        case TARGET(SET_ADD): {
            PyObject *v = POP();
            PyObject *set = PEEK(oparg);
            int err;
            err = PySet_Add(set, v);
            Py_DECREF(v);
            if (err != 0)
                goto error;
            PREDICT(JUMP_ABSOLUTE);
            DISPATCH();
        }

        case TARGET(INPLACE_POWER): {
            PyObject *exp = POP();
            PyObject *base = TOP();
            PyObject *res = PyNumber_InPlacePower(base, exp, Py_None);
            Py_DECREF(base);
            Py_DECREF(exp);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_MULTIPLY): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_InPlaceMultiply(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_MATRIX_MULTIPLY): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_InPlaceMatrixMultiply(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_TRUE_DIVIDE): {
            PyObject *divisor = POP();
            PyObject *dividend = TOP();
            PyObject *quotient = PyNumber_InPlaceTrueDivide(dividend, divisor);
            Py_DECREF(dividend);
            Py_DECREF(divisor);
            SET_TOP(quotient);
            if (quotient == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_FLOOR_DIVIDE): {
            PyObject *divisor = POP();
            PyObject *dividend = TOP();
            PyObject *quotient = PyNumber_InPlaceFloorDivide(dividend, divisor);
            Py_DECREF(dividend);
            Py_DECREF(divisor);
            SET_TOP(quotient);
            if (quotient == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_MODULO): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *mod = PyNumber_InPlaceRemainder(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(mod);
            if (mod == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_ADD): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *sum;
            if (PyUnicode_CheckExact(left) && PyUnicode_CheckExact(right)) {
                sum = unicode_concatenate(tstate, left, right, f, next_instr);
                /* unicode_concatenate consumed the ref to left */
            }
            else {
                sum = PyNumber_InPlaceAdd(left, right);
                Py_DECREF(left);
            }
            Py_DECREF(right);
            SET_TOP(sum);
            if (sum == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_SUBTRACT): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *diff = PyNumber_InPlaceSubtract(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(diff);
            if (diff == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_LSHIFT): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_InPlaceLshift(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_RSHIFT): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_InPlaceRshift(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_AND): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_InPlaceAnd(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_XOR): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_InPlaceXor(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(INPLACE_OR): {
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyNumber_InPlaceOr(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(STORE_SUBSCR): {
            PyObject *sub = TOP();
            PyObject *container = SECOND();
            PyObject *v = THIRD();
            int err;
            STACK_SHRINK(3);
            /* container[sub] = v */
            err = PyObject_SetItem(container, sub, v);
            Py_DECREF(v);
            Py_DECREF(container);
            Py_DECREF(sub);
            if (err != 0)
                goto error;
            DISPATCH();
        }

        case TARGET(DELETE_SUBSCR): {
            PyObject *sub = TOP();
            PyObject *container = SECOND();
            int err;
            STACK_SHRINK(2);
            /* del container[sub] */
            err = PyObject_DelItem(container, sub);
            Py_DECREF(container);
            Py_DECREF(sub);
            if (err != 0)
                goto error;
            DISPATCH();
        }

        case TARGET(PRINT_EXPR): {
            _Py_IDENTIFIER(displayhook);
            PyObject *value = POP();
            PyObject *hook = _PySys_GetObjectId(&PyId_displayhook);
            PyObject *res;
            if (hook == NULL) {
                _PyErr_SetString(tstate, PyExc_RuntimeError,
                                 "lost sys.displayhook");
                Py_DECREF(value);
                goto error;
            }
            res = PyObject_CallOneArg(hook, value);
            Py_DECREF(value);
            if (res == NULL)
                goto error;
            Py_DECREF(res);
            DISPATCH();
        }

        case TARGET(RAISE_VARARGS): {
            PyObject *cause = NULL, *exc = NULL;
            switch (oparg) {
            case 2:
                cause = POP(); /* cause */
                /* fall through */
            case 1:
                exc = POP(); /* exc */
                /* fall through */
            case 0:
                if (do_raise(tstate, exc, cause)) {
                    goto exception_unwind;
                }
                break;
            default:
                _PyErr_SetString(tstate, PyExc_SystemError,
                                 "bad RAISE_VARARGS oparg");
                break;
            }
            goto error;
        }

        case TARGET(RETURN_VALUE): {
            retval = POP();
            assert(f->f_iblock == 0);
            assert(EMPTY());
            f->f_state = FRAME_RETURNED;
            f->f_stackdepth = 0;
            goto exiting;
        }

        case TARGET(GET_AITER): {
            PyObject *obj = TOP();
            PyObject *iter = Ci_GetAIter(tstate, obj);
            Py_DECREF(obj);
            SET_TOP(iter);
            if (iter == NULL) {
                goto error;
            }
            DISPATCH();
        }

        case TARGET(GET_ANEXT): {
            PyObject *awaitable = Ci_GetANext(tstate, TOP());
            if (awaitable == NULL) {
                goto error;
            }
            PUSH(awaitable);
            PREDICT(LOAD_CONST);
            DISPATCH();
        }

        case TARGET(GET_AWAITABLE): {
            PREDICTED(GET_AWAITABLE);
            PyObject *iterable = TOP();
            PyObject *iter = _PyCoro_GetAwaitableIter(iterable);

            if (iter == NULL) {
                int opcode_at_minus_3 = 0;
                if ((next_instr - first_instr) > 2) {
                    opcode_at_minus_3 = _Py_OPCODE(next_instr[-3]);
                }
                format_awaitable_error(tstate, Py_TYPE(iterable),
                                       opcode_at_minus_3,
                                       _Py_OPCODE(next_instr[-2]));
            }

            Py_DECREF(iterable);

            if (iter != NULL && PyCoro_CheckExact(iter)) {
                PyObject *yf = _PyGen_yf((PyGenObject*)iter);
                if (yf != NULL) {
                    /* `iter` is a coroutine object that is being
                       awaited, `yf` is a pointer to the current awaitable
                       being awaited on. */
                    Py_DECREF(yf);
                    Py_CLEAR(iter);
                    _PyErr_SetString(tstate, PyExc_RuntimeError,
                                     "coroutine is being awaited already");
                    /* The code below jumps to `error` if `iter` is NULL. */
                }
            }

            SET_TOP(iter); /* Even if it's NULL */

            if (iter == NULL) {
                goto error;
            }

            PREDICT(LOAD_CONST);
            DISPATCH();
        }

        case TARGET(YIELD_FROM): {
            PyObject *v = POP();
            PyObject *receiver = TOP();
            PySendResult gen_status;
            if (f->f_gen && (co->co_flags & CO_COROUTINE)) {
                _PyAwaitable_SetAwaiter(receiver, f->f_gen);
            }
            if (tstate->c_tracefunc == NULL) {
                gen_status = PyIter_Send(receiver, v, &retval);
            } else {
                _Py_IDENTIFIER(send);
                if (Py_IsNone(v) && PyIter_Check(receiver)) {
                    retval = Py_TYPE(receiver)->tp_iternext(receiver);
                }
                else {
                    retval = _PyObject_CallMethodIdOneArg(receiver, &PyId_send, v);
                }
                if (retval == NULL) {
                    if (tstate->c_tracefunc != NULL
                            && _PyErr_ExceptionMatches(tstate, PyExc_StopIteration))
                        call_exc_trace(tstate->c_tracefunc, tstate->c_traceobj, tstate, f, &trace_info);
                    if (_PyGen_FetchStopIterationValue(&retval) == 0) {
                        gen_status = PYGEN_RETURN;
                    }
                    else {
                        gen_status = PYGEN_ERROR;
                    }
                }
                else {
                    gen_status = PYGEN_NEXT;
                }
            }
            Py_DECREF(v);
            if (gen_status == PYGEN_ERROR) {
                assert (retval == NULL);
                goto error;
            }
            if (gen_status == PYGEN_RETURN) {
                assert (retval != NULL);

                Py_DECREF(receiver);
                SET_TOP(retval);
                retval = NULL;
                DISPATCH();
            }
            assert (gen_status == PYGEN_NEXT);
            /* receiver remains on stack, retval is value to be yielded */
            /* and repeat... */
            assert(f->f_lasti > 0);
            f->f_lasti -= 1;
            f->f_state = FRAME_SUSPENDED;
            f->f_stackdepth = (int)(stack_pointer - f->f_valuestack);
            goto exiting;
        }

        case TARGET(YIELD_VALUE): {
            retval = POP();

            if (co->co_flags & CO_ASYNC_GENERATOR) {
                PyObject *w = _PyAsyncGenValueWrapperNew(retval);
                Py_DECREF(retval);
                if (w == NULL) {
                    retval = NULL;
                    goto error;
                }
                retval = w;
            }
            f->f_state = FRAME_SUSPENDED;
            f->f_stackdepth = (int)(stack_pointer - f->f_valuestack);
            goto exiting;
        }

        case TARGET(GEN_START): {
            PyObject *none = POP();
            assert(none == Py_None);
            assert(oparg < 3);
            Py_DECREF(none);
            DISPATCH();
        }

        case TARGET(POP_EXCEPT): {
            PyObject *type, *value, *traceback;
            _PyErr_StackItem *exc_info;
            PyTryBlock *b = PyFrame_BlockPop(f);
            if (b->b_type != EXCEPT_HANDLER) {
                _PyErr_SetString(tstate, PyExc_SystemError,
                                 "popped block is not an except handler");
                goto error;
            }
            assert(STACK_LEVEL() >= (b)->b_level + 3 &&
                   STACK_LEVEL() <= (b)->b_level + 4);
            exc_info = tstate->exc_info;
            type = exc_info->exc_type;
            value = exc_info->exc_value;
            traceback = exc_info->exc_traceback;
            exc_info->exc_type = POP();
            exc_info->exc_value = POP();
            exc_info->exc_traceback = POP();
            Py_XDECREF(type);
            Py_XDECREF(value);
            Py_XDECREF(traceback);
            DISPATCH();
        }

        case TARGET(POP_BLOCK): {
            PyFrame_BlockPop(f);
            DISPATCH();
        }

        case TARGET(RERAISE): {
            assert(f->f_iblock > 0);
            if (oparg) {
                f->f_lasti = f->f_blockstack[f->f_iblock-1].b_handler;
            }
            PyObject *exc = POP();
            PyObject *val = POP();
            PyObject *tb = POP();
            assert(PyExceptionClass_Check(exc));
            _PyErr_Restore(tstate, exc, val, tb);
            goto exception_unwind;
        }

        case TARGET(END_ASYNC_FOR): {
            PyObject *exc = POP();
            assert(PyExceptionClass_Check(exc));
            if (PyErr_GivenExceptionMatches(exc, PyExc_StopAsyncIteration)) {
                PyTryBlock *b = PyFrame_BlockPop(f);
                assert(b->b_type == EXCEPT_HANDLER);
                Py_DECREF(exc);
                UNWIND_EXCEPT_HANDLER(b);
                Py_DECREF(POP());
                JUMPBY(oparg);
                DISPATCH();
            }
            else {
                PyObject *val = POP();
                PyObject *tb = POP();
                _PyErr_Restore(tstate, exc, val, tb);
                goto exception_unwind;
            }
        }

        case TARGET(LOAD_ASSERTION_ERROR): {
            PyObject *value = PyExc_AssertionError;
            Py_INCREF(value);
            PUSH(value);
            DISPATCH();
        }

        case TARGET(LOAD_BUILD_CLASS): {
            _Py_IDENTIFIER(__build_class__);

            PyObject *bc;
            if (PyDict_CheckExact(f->f_builtins)) {
                bc = _PyDict_GetItemIdWithError(f->f_builtins, &PyId___build_class__);
                if (bc == NULL) {
                    if (!_PyErr_Occurred(tstate)) {
                        _PyErr_SetString(tstate, PyExc_NameError,
                                         "__build_class__ not found");
                    }
                    goto error;
                }
                Py_INCREF(bc);
            }
            else {
                PyObject *build_class_str = _PyUnicode_FromId(&PyId___build_class__);
                if (build_class_str == NULL)
                    goto error;
                bc = PyObject_GetItem(f->f_builtins, build_class_str);
                if (bc == NULL) {
                    if (_PyErr_ExceptionMatches(tstate, PyExc_KeyError))
                        _PyErr_SetString(tstate, PyExc_NameError,
                                         "__build_class__ not found");
                    goto error;
                }
            }
            PUSH(bc);
            DISPATCH();
        }

        case TARGET(STORE_NAME): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *v = POP();
            PyObject *ns = f->f_locals;
            int err;
            if (ns == NULL) {
                _PyErr_Format(tstate, PyExc_SystemError,
                              "no locals found when storing %R", name);
                Py_DECREF(v);
                goto error;
            }
            if (PyDict_CheckExact(ns)) {
                err = PyDict_SetItem(ns, name, v);
            } else {
                err = PyObject_SetItem(ns, name, v);
            }
            Py_DECREF(v);
            if (err != 0)
                goto error;
            DISPATCH();
        }

        case TARGET(DELETE_NAME): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *ns = f->f_locals;
            int err;
            if (ns == NULL) {
                _PyErr_Format(tstate, PyExc_SystemError,
                              "no locals when deleting %R", name);
                goto error;
            }
            err = PyObject_DelItem(ns, name);
            if (err != 0) {
                format_exc_check_arg(tstate, PyExc_NameError,
                                     NAME_ERROR_MSG,
                                     name);
                goto error;
            }
            DISPATCH();
        }

        case TARGET(UNPACK_SEQUENCE): {
            PREDICTED(UNPACK_SEQUENCE);
            PyObject *seq = POP(), *item, **items;
            if (PyTuple_CheckExact(seq) &&
                PyTuple_GET_SIZE(seq) == oparg) {
                items = ((PyTupleObject *)seq)->ob_item;
                while (oparg--) {
                    item = items[oparg];
                    Py_INCREF(item);
                    PUSH(item);
                }
            } else if (PyList_CheckExact(seq) &&
                       PyList_GET_SIZE(seq) == oparg) {
                items = ((PyListObject *)seq)->ob_item;
                while (oparg--) {
                    item = items[oparg];
                    Py_INCREF(item);
                    PUSH(item);
                }
            } else if (unpack_iterable(tstate, seq, oparg, -1,
                                       stack_pointer + oparg)) {
                STACK_GROW(oparg);
            } else {
                /* unpack_iterable() raised an exception */
                Py_DECREF(seq);
                goto error;
            }
            Py_DECREF(seq);
            DISPATCH();
        }

        case TARGET(UNPACK_EX): {
            int totalargs = 1 + (oparg & 0xFF) + (oparg >> 8);
            PyObject *seq = POP();

            if (unpack_iterable(tstate, seq, oparg & 0xFF, oparg >> 8,
                                stack_pointer + totalargs)) {
                stack_pointer += totalargs;
            } else {
                Py_DECREF(seq);
                goto error;
            }
            Py_DECREF(seq);
            DISPATCH();
        }

        case TARGET(STORE_ATTR): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *owner = TOP();
            PyObject *v = SECOND();
#ifdef INLINE_CACHE_PROFILE
            _PyShadow_LogLocation(&shadow, next_instr, "STORE_ATTR");
            char type_name[81];
            snprintf(type_name,
                     sizeof(type_name),
                     "STORE_ATTR_TYPE[%s]",
                     Py_TYPE(owner)->tp_name);
            _PyShadow_LogLocation(&shadow, next_instr, type_name);
#endif
            int err;
            STACK_SHRINK(2);
            err = shadow.shadow == NULL
                      ? PyObject_SetAttr(owner, name, v)
                      : _PyShadow_StoreAttrWithCache(
                            &shadow, next_instr, owner, name, v);
            Py_DECREF(v);
            Py_DECREF(owner);
            if (err != 0)
                goto error;
            DISPATCH();
        }

        case TARGET(DELETE_ATTR): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *owner = POP();
            int err;
            err = PyObject_SetAttr(owner, name, (PyObject *)NULL);
            Py_DECREF(owner);
            if (err != 0)
                goto error;
            DISPATCH();
        }

        case TARGET(STORE_GLOBAL): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *v = POP();
            int err;
            err = PyDict_SetItem(f->f_globals, name, v);
            Py_DECREF(v);
            if (err != 0)
                goto error;
            DISPATCH();
        }

        case TARGET(DELETE_GLOBAL): {
            PyObject *name = GETITEM(names, oparg);
            int err;
            err = PyDict_DelItem(f->f_globals, name);
            if (err != 0) {
                if (_PyErr_ExceptionMatches(tstate, PyExc_KeyError)) {
                    format_exc_check_arg(tstate, PyExc_NameError,
                                         NAME_ERROR_MSG, name);
                }
                goto error;
            }
            DISPATCH();
        }

        case TARGET(LOAD_NAME): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *locals = f->f_locals;
            PyObject *v;
            if (locals == NULL) {
                _PyErr_Format(tstate, PyExc_SystemError,
                              "no locals when loading %R", name);
                goto error;
            }
            if (PyDict_CheckExact(locals)) {
                v = PyDict_GetItemWithError(locals, name);
                if (v != NULL) {
                    Py_INCREF(v);
                }
                else if (_PyErr_Occurred(tstate)) {
                    goto error;
                }
            }
            else {
                v = PyObject_GetItem(locals, name);
                if (v == NULL) {
                    if (!_PyErr_ExceptionMatches(tstate, PyExc_KeyError))
                        goto error;
                    _PyErr_Clear(tstate);
                }
            }
            if (v == NULL) {
                v = PyDict_GetItemWithError(f->f_globals, name);
                if (v != NULL) {
                    Py_INCREF(v);
                }
                else if (_PyErr_Occurred(tstate)) {
                    goto error;
                }
                else {
                    if (PyDict_CheckExact(f->f_builtins)) {
                        v = PyDict_GetItemWithError(f->f_builtins, name);
                        if (v == NULL) {
                            if (!_PyErr_Occurred(tstate)) {
                                format_exc_check_arg(
                                        tstate, PyExc_NameError,
                                        NAME_ERROR_MSG, name);
                            }
                            goto error;
                        }
                        Py_INCREF(v);
                    }
                    else {
                        v = PyObject_GetItem(f->f_builtins, name);
                        if (v == NULL) {
                            if (_PyErr_ExceptionMatches(tstate, PyExc_KeyError)) {
                                format_exc_check_arg(
                                            tstate, PyExc_NameError,
                                            NAME_ERROR_MSG, name);
                            }
                            goto error;
                        }
                    }
                }
            }
            PUSH(v);
            DISPATCH();
        }

        case TARGET(LOAD_GLOBAL): {
            PyObject *name;
            PyObject *v;
            if (PyDict_CheckExact(f->f_globals)) {
                assert(PyDict_CheckExact(f->f_builtins));
                name = GETITEM(names, oparg);
                v = _PyDict_LoadGlobal((PyDictObject *)f->f_globals,
                                       (PyDictObject *)f->f_builtins,
                                       name);
                if (v == NULL) {
                    if (!_PyErr_Occurred(tstate)) {
                        /* _PyDict_LoadGlobal() returns NULL without raising
                         * an exception if the key doesn't exist */
                        format_exc_check_arg(tstate, PyExc_NameError,
                                             NAME_ERROR_MSG, name);
                    }
                    goto error;
                }

                if (shadow.shadow != NULL) {
                    _PyShadow_InitGlobal(&shadow,
                                         next_instr,
                                         f->f_globals,
                                         f->f_builtins,
                                         name);
                }

                Py_INCREF(v);
                /* facebook end */
            }
            else {
                /* Slow-path if globals or builtins is not a dict */

                /* namespace 1: globals */
                name = GETITEM(names, oparg);
                v = PyObject_GetItem(f->f_globals, name);
                if (v == NULL) {
                    if (!_PyErr_ExceptionMatches(tstate, PyExc_KeyError)) {
                        goto error;
                    }
                    _PyErr_Clear(tstate);

                    /* namespace 2: builtins */
                    v = PyObject_GetItem(f->f_builtins, name);
                    if (v == NULL) {
                        if (_PyErr_ExceptionMatches(tstate, PyExc_KeyError)) {
                            format_exc_check_arg(
                                        tstate, PyExc_NameError,
                                        NAME_ERROR_MSG, name);
                        }
                        goto error;
                    }
                }
            }
            PUSH(v);
            DISPATCH();
        }

        case TARGET(DELETE_FAST): {
            PyObject *v = GETLOCAL(oparg);
            if (v != NULL) {
                SETLOCAL(oparg, NULL);
            }
            DISPATCH();
        }

        case TARGET(DELETE_DEREF): {
            PyObject *cell = freevars[oparg];
            PyObject *oldobj = PyCell_GET(cell);
            if (oldobj != NULL) {
                PyCell_SET(cell, NULL);
                Py_DECREF(oldobj);
                DISPATCH();
            }
            format_exc_unbound(tstate, co, oparg);
            goto error;
        }

        case TARGET(LOAD_CLOSURE): {
            PyObject *cell = freevars[oparg];
            Py_INCREF(cell);
            PUSH(cell);
            DISPATCH();
        }

        case TARGET(LOAD_CLASSDEREF): {
            PyObject *name, *value, *locals = f->f_locals;
            Py_ssize_t idx;
            assert(locals);
            assert(oparg >= PyTuple_GET_SIZE(co->co_cellvars));
            idx = oparg - PyTuple_GET_SIZE(co->co_cellvars);
            assert(idx >= 0 && idx < PyTuple_GET_SIZE(co->co_freevars));
            name = PyTuple_GET_ITEM(co->co_freevars, idx);
            if (PyDict_CheckExact(locals)) {
                value = PyDict_GetItemWithError(locals, name);
                if (value != NULL) {
                    Py_INCREF(value);
                }
                else if (_PyErr_Occurred(tstate)) {
                    goto error;
                }
            }
            else {
                value = PyObject_GetItem(locals, name);
                if (value == NULL) {
                    if (!_PyErr_ExceptionMatches(tstate, PyExc_KeyError)) {
                        goto error;
                    }
                    _PyErr_Clear(tstate);
                }
            }
            if (!value) {
                PyObject *cell = freevars[oparg];
                value = PyCell_GET(cell);
                if (value == NULL) {
                    format_exc_unbound(tstate, co, oparg);
                    goto error;
                }
                Py_INCREF(value);
            }
            PUSH(value);
            DISPATCH();
        }

        case TARGET(LOAD_DEREF): {
            PyObject *cell = freevars[oparg];
            PyObject *value = PyCell_GET(cell);
            if (value == NULL) {
                format_exc_unbound(tstate, co, oparg);
                goto error;
            }
            Py_INCREF(value);
            PUSH(value);
            DISPATCH();
        }

        case TARGET(STORE_DEREF): {
            PyObject *v = POP();
            PyObject *cell = freevars[oparg];
            PyObject *oldobj = PyCell_GET(cell);
            PyCell_SET(cell, v);
            Py_XDECREF(oldobj);
            DISPATCH();
        }

        case TARGET(BUILD_STRING): {
            PyObject *str;
            PyObject *empty = PyUnicode_New(0, 0);
            if (empty == NULL) {
                goto error;
            }
            str = _PyUnicode_JoinArray(empty, stack_pointer - oparg, oparg);
            Py_DECREF(empty);
            if (str == NULL)
                goto error;
            while (--oparg >= 0) {
                PyObject *item = POP();
                Py_DECREF(item);
            }
            PUSH(str);
            DISPATCH();
        }

        case TARGET(BUILD_TUPLE): {
            PyObject *tup = PyTuple_New(oparg);
            if (tup == NULL)
                goto error;
            while (--oparg >= 0) {
                PyObject *item = POP();
                PyTuple_SET_ITEM(tup, oparg, item);
            }
            PUSH(tup);
            DISPATCH();
        }

        case TARGET(BUILD_LIST): {
            PyObject *list =  PyList_New(oparg);
            if (list == NULL)
                goto error;
            while (--oparg >= 0) {
                PyObject *item = POP();
                PyList_SET_ITEM(list, oparg, item);
            }
            PUSH(list);
            DISPATCH();
        }

        case TARGET(LIST_TO_TUPLE): {
            PyObject *list = POP();
            PyObject *tuple = PyList_AsTuple(list);
            Py_DECREF(list);
            if (tuple == NULL) {
                goto error;
            }
            PUSH(tuple);
            DISPATCH();
        }

        case TARGET(LIST_EXTEND): {
            PyObject *iterable = POP();
            PyObject *list = PEEK(oparg);
            PyObject *none_val = _PyList_Extend((PyListObject *)list, iterable);
            if (none_val == NULL) {
                if (_PyErr_ExceptionMatches(tstate, PyExc_TypeError) &&
                   (Py_TYPE(iterable)->tp_iter == NULL && !PySequence_Check(iterable)))
                {
                    _PyErr_Clear(tstate);
                    _PyErr_Format(tstate, PyExc_TypeError,
                          "Value after * must be an iterable, not %.200s",
                          Py_TYPE(iterable)->tp_name);
                }
                Py_DECREF(iterable);
                goto error;
            }
            Py_DECREF(none_val);
            Py_DECREF(iterable);
            DISPATCH();
        }

        case TARGET(SET_UPDATE): {
            PyObject *iterable = POP();
            PyObject *set = PEEK(oparg);
            int err = _PySet_Update(set, iterable);
            Py_DECREF(iterable);
            if (err < 0) {
                goto error;
            }
            DISPATCH();
        }

        case TARGET(BUILD_SET): {
            PyObject *set = PySet_New(NULL);
            int err = 0;
            int i;
            if (set == NULL)
                goto error;
            for (i = oparg; i > 0; i--) {
                PyObject *item = PEEK(i);
                if (err == 0)
                    err = PySet_Add(set, item);
                Py_DECREF(item);
            }
            STACK_SHRINK(oparg);
            if (err != 0) {
                Py_DECREF(set);
                goto error;
            }
            PUSH(set);
            DISPATCH();
        }

#define Ci_BUILD_DICT(map_size, set_item)                                     \
                                                                              \
    for (Py_ssize_t i = map_size; i > 0; i--) {                               \
        int err;                                                              \
        PyObject *key = PEEK(2 * i);                                          \
        PyObject *value = PEEK(2 * i - 1);                                    \
        err = set_item(map, key, value);                                      \
        if (err != 0) {                                                       \
            Py_DECREF(map);                                                   \
            goto error;                                                       \
        }                                                                     \
    }                                                                         \
                                                                              \
    while (map_size--) {                                                      \
        Py_DECREF(POP());                                                     \
        Py_DECREF(POP());                                                     \
    }                                                                         \
    PUSH(map);

        case TARGET(BUILD_MAP): {
            PyObject *map = _PyDict_NewPresized((Py_ssize_t)oparg);
            if (map == NULL)
                goto error;

            Ci_BUILD_DICT(oparg, Ci_DictOrChecked_SetItem);

            DISPATCH();
        }

        case TARGET(SETUP_ANNOTATIONS): {
            _Py_IDENTIFIER(__annotations__);
            int err;
            PyObject *ann_dict;
            if (f->f_locals == NULL) {
                _PyErr_Format(tstate, PyExc_SystemError,
                              "no locals found when setting up annotations");
                goto error;
            }
            /* check if __annotations__ in locals()... */
            if (PyDict_CheckExact(f->f_locals)) {
                ann_dict = _PyDict_GetItemIdWithError(f->f_locals,
                                             &PyId___annotations__);
                if (ann_dict == NULL) {
                    if (_PyErr_Occurred(tstate)) {
                        goto error;
                    }
                    /* ...if not, create a new one */
                    ann_dict = PyDict_New();
                    if (ann_dict == NULL) {
                        goto error;
                    }
                    err = _PyDict_SetItemId(f->f_locals,
                                            &PyId___annotations__, ann_dict);
                    Py_DECREF(ann_dict);
                    if (err != 0) {
                        goto error;
                    }
                }
            }
            else {
                /* do the same if locals() is not a dict */
                PyObject *ann_str = _PyUnicode_FromId(&PyId___annotations__);
                if (ann_str == NULL) {
                    goto error;
                }
                ann_dict = PyObject_GetItem(f->f_locals, ann_str);
                if (ann_dict == NULL) {
                    if (!_PyErr_ExceptionMatches(tstate, PyExc_KeyError)) {
                        goto error;
                    }
                    _PyErr_Clear(tstate);
                    ann_dict = PyDict_New();
                    if (ann_dict == NULL) {
                        goto error;
                    }
                    err = PyObject_SetItem(f->f_locals, ann_str, ann_dict);
                    Py_DECREF(ann_dict);
                    if (err != 0) {
                        goto error;
                    }
                }
                else {
                    Py_DECREF(ann_dict);
                }
            }
            DISPATCH();
        }

        case TARGET(BUILD_CONST_KEY_MAP): {
            Py_ssize_t i;
            PyObject *map;
            PyObject *keys = TOP();
            if (!PyTuple_CheckExact(keys) ||
                PyTuple_GET_SIZE(keys) != (Py_ssize_t)oparg) {
                _PyErr_SetString(tstate, PyExc_SystemError,
                                 "bad BUILD_CONST_KEY_MAP keys argument");
                goto error;
            }
            map = _PyDict_NewPresized((Py_ssize_t)oparg);
            if (map == NULL) {
                goto error;
            }
            for (i = oparg; i > 0; i--) {
                int err;
                PyObject *key = PyTuple_GET_ITEM(keys, oparg - i);
                PyObject *value = PEEK(i + 1);
                err = PyDict_SetItem(map, key, value);
                if (err != 0) {
                    Py_DECREF(map);
                    goto error;
                }
            }

            Py_DECREF(POP());
            while (oparg--) {
                Py_DECREF(POP());
            }
            PUSH(map);
            DISPATCH();
        }

        case TARGET(DICT_UPDATE): {
            PyObject *update = POP();
            PyObject *dict = PEEK(oparg);
            if (PyDict_Update(dict, update) < 0) {
                if (_PyErr_ExceptionMatches(tstate, PyExc_AttributeError)) {
                    _PyErr_Format(tstate, PyExc_TypeError,
                                    "'%.200s' object is not a mapping",
                                    Py_TYPE(update)->tp_name);
                }
                Py_DECREF(update);
                goto error;
            }
            Py_DECREF(update);
            DISPATCH();
        }

        case TARGET(DICT_MERGE): {
            PyObject *update = POP();
            PyObject *dict = PEEK(oparg);

            if (_PyDict_MergeEx(dict, update, 2) < 0) {
                format_kwargs_error(tstate, PEEK(2 + oparg), update);
                Py_DECREF(update);
                goto error;
            }
            Py_DECREF(update);
            PREDICT(CALL_FUNCTION_EX);
            DISPATCH();
        }

        case TARGET(MAP_ADD): {
            PyObject *value = TOP();
            PyObject *key = SECOND();
            PyObject *map;
            int err;
            STACK_SHRINK(2);
            map = PEEK(oparg);                      /* dict */
            assert(PyDict_CheckExact(map) || Ci_CheckedDict_Check(map));
            err = Ci_DictOrChecked_SetItem(map, key, value);  /* map[key] = value */
            Py_DECREF(value);
            Py_DECREF(key);
            if (err != 0)
                goto error;
            PREDICT(JUMP_ABSOLUTE);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *owner = TOP();
            PyObject *res = shadow.shadow == NULL
                                ? PyObject_GetAttr(owner, name)
                                : _PyShadow_LoadAttrWithCache(
                                      &shadow, next_instr, owner, name);
            Py_DECREF(owner);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(COMPARE_OP): {
            assert(oparg <= Py_GE);
            PyObject *right = POP();
            PyObject *left = TOP();
            PyObject *res = PyObject_RichCompare(left, right, oparg);
            SET_TOP(res);
            Py_DECREF(left);
            Py_DECREF(right);
            if (res == NULL)
                goto error;
            PREDICT(POP_JUMP_IF_FALSE);
            PREDICT(POP_JUMP_IF_TRUE);
            DISPATCH();
        }

        case TARGET(IS_OP): {
            PyObject *right = POP();
            PyObject *left = TOP();
            int res = Py_Is(left, right) ^ oparg;
            PyObject *b = res ? Py_True : Py_False;
            Py_INCREF(b);
            SET_TOP(b);
            Py_DECREF(left);
            Py_DECREF(right);
            PREDICT(POP_JUMP_IF_FALSE);
            PREDICT(POP_JUMP_IF_TRUE);
            DISPATCH();
        }

        case TARGET(CONTAINS_OP): {
            PyObject *right = POP();
            PyObject *left = POP();
            int res = PySequence_Contains(right, left);
            Py_DECREF(left);
            Py_DECREF(right);
            if (res < 0) {
                goto error;
            }
            PyObject *b = (res^oparg) ? Py_True : Py_False;
            Py_INCREF(b);
            PUSH(b);
            PREDICT(POP_JUMP_IF_FALSE);
            PREDICT(POP_JUMP_IF_TRUE);
            DISPATCH();
        }

#define CANNOT_CATCH_MSG "catching classes that do not inherit from "\
                         "BaseException is not allowed"

        case TARGET(JUMP_IF_NOT_EXC_MATCH): {
            PyObject *right = POP();
            PyObject *left = POP();
            if (PyTuple_Check(right)) {
                Py_ssize_t i, length;
                length = PyTuple_GET_SIZE(right);
                for (i = 0; i < length; i++) {
                    PyObject *exc = PyTuple_GET_ITEM(right, i);
                    if (!PyExceptionClass_Check(exc)) {
                        _PyErr_SetString(tstate, PyExc_TypeError,
                                        CANNOT_CATCH_MSG);
                        Py_DECREF(left);
                        Py_DECREF(right);
                        goto error;
                    }
                }
            }
            else {
                if (!PyExceptionClass_Check(right)) {
                    _PyErr_SetString(tstate, PyExc_TypeError,
                                    CANNOT_CATCH_MSG);
                    Py_DECREF(left);
                    Py_DECREF(right);
                    goto error;
                }
            }
            int res = PyErr_GivenExceptionMatches(left, right);
            Py_DECREF(left);
            Py_DECREF(right);
            if (res > 0) {
                /* Exception matches -- Do nothing */;
            }
            else if (res == 0) {
                JUMPTO(oparg);
            }
            else {
                goto error;
            }
            DISPATCH();
        }

        case TARGET(IMPORT_NAME): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *fromlist = POP();
            PyObject *level = TOP();
            PyObject *res;

            if (f->f_globals == f->f_locals &&
                f->f_iblock == 0 &&
                _PyImport_IsLazyImportsEnabled(tstate)) {
                res = _PyImport_LazyImportName(f->f_builtins,
                                               f->f_globals,
                                               f->f_locals == NULL ? Py_None : f->f_locals,
                                               name,
                                               fromlist,
                                               level);
            }
            else {
                res = _PyImport_ImportName(f->f_builtins,
                                           f->f_globals,
                                           f->f_locals == NULL ? Py_None : f->f_locals,
                                           name,
                                           fromlist,
                                           level);
            }

            Py_DECREF(level);
            Py_DECREF(fromlist);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(IMPORT_STAR): {
            PyObject *from = POP(), *locals;
            int err;
            if (PyLazyImport_CheckExact(from)) {
                PyObject *mod = _PyImport_LoadLazyImportTstate(tstate, from, 1);
                Py_DECREF(from);
                if (mod == NULL) {
                    if (!_PyErr_Occurred(tstate)) {
                        _PyErr_SetString(tstate, PyExc_SystemError,
                                         "Lazy Import cycle");
                    }
                    goto error;
                }
                from = mod;
            }

            if (PyFrame_FastToLocalsWithError(f) < 0) {
                Py_DECREF(from);
                goto error;
            }

            locals = f->f_locals;
            if (locals == NULL) {
                _PyErr_SetString(tstate, PyExc_SystemError,
                                 "no locals found during 'import *'");
                Py_DECREF(from);
                goto error;
            }
            err = import_all_from(tstate, locals, from);
            Py_DECREF(from);
            if (err != 0)
                goto error;
            PyFrame_LocalsToFast(f, 0);
            DISPATCH();
        }

        case TARGET(IMPORT_FROM): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *from = TOP();
            PyObject *res;
            if (PyLazyImport_CheckExact(from)) {
                res = _PyImport_LazyImportFrom(tstate, from, name);
            } else {
                res = _PyImport_ImportFrom(tstate, from, name);
            }
            PUSH(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(JUMP_FORWARD): {
            JUMPBY(oparg);
            DISPATCH();
        }

        case TARGET(POP_JUMP_IF_FALSE): {
            PREDICTED(POP_JUMP_IF_FALSE);
            PyObject *cond = POP();
            int err;
            if (Py_IsTrue(cond)) {
                Py_DECREF(cond);
                DISPATCH();
            }
            if (Py_IsFalse(cond)) {
                Py_DECREF(cond);
                JUMPTO(oparg);
                CHECK_EVAL_BREAKER();
                DISPATCH();
            }
            err = PyObject_IsTrue(cond);
            Py_DECREF(cond);
            if (err > 0)
                ;
            else if (err == 0) {
                JUMPTO(oparg);
                CHECK_EVAL_BREAKER();
            }
            else
                goto error;
            DISPATCH();
        }

        case TARGET(POP_JUMP_IF_TRUE): {
            PREDICTED(POP_JUMP_IF_TRUE);
            PyObject *cond = POP();
            int err;
            if (Py_IsFalse(cond)) {
                Py_DECREF(cond);
                DISPATCH();
            }
            if (Py_IsTrue(cond)) {
                Py_DECREF(cond);
                JUMPTO(oparg);
                CHECK_EVAL_BREAKER();
                DISPATCH();
            }
            err = PyObject_IsTrue(cond);
            Py_DECREF(cond);
            if (err > 0) {
                JUMPTO(oparg);
                CHECK_EVAL_BREAKER();
            }
            else if (err == 0)
                ;
            else
                goto error;
            DISPATCH();
        }

        case TARGET(JUMP_IF_FALSE_OR_POP): {
            PyObject *cond = TOP();
            int err;
            if (Py_IsTrue(cond)) {
                STACK_SHRINK(1);
                Py_DECREF(cond);
                DISPATCH();
            }
            if (Py_IsFalse(cond)) {
                JUMPTO(oparg);
                DISPATCH();
            }
            err = PyObject_IsTrue(cond);
            if (err > 0) {
                STACK_SHRINK(1);
                Py_DECREF(cond);
            }
            else if (err == 0)
                JUMPTO(oparg);
            else
                goto error;
            DISPATCH();
        }

        case TARGET(JUMP_IF_TRUE_OR_POP): {
            PyObject *cond = TOP();
            int err;
            if (Py_IsFalse(cond)) {
                STACK_SHRINK(1);
                Py_DECREF(cond);
                DISPATCH();
            }
            if (Py_IsTrue(cond)) {
                JUMPTO(oparg);
                DISPATCH();
            }
            err = PyObject_IsTrue(cond);
            if (err > 0) {
                JUMPTO(oparg);
            }
            else if (err == 0) {
                STACK_SHRINK(1);
                Py_DECREF(cond);
            }
            else
                goto error;
            DISPATCH();
        }

        case TARGET(JUMP_ABSOLUTE): {
            PREDICTED(JUMP_ABSOLUTE);
            JUMPTO(oparg);
            CHECK_EVAL_BREAKER();
            DISPATCH();
        }

        case TARGET(GET_LEN): {
            // PUSH(len(TOS))
            Py_ssize_t len_i = PyObject_Length(TOP());
            if (len_i < 0) {
                goto error;
            }
            PyObject *len_o = PyLong_FromSsize_t(len_i);
            if (len_o == NULL) {
                goto error;
            }
            PUSH(len_o);
            DISPATCH();
        }

        case TARGET(MATCH_CLASS): {
            // Pop TOS. On success, set TOS to True and TOS1 to a tuple of
            // attributes. On failure, set TOS to False.
            PyObject *names = POP();
            PyObject *type = TOP();
            PyObject *subject = SECOND();
            assert(PyTuple_CheckExact(names));
            PyObject *attrs = match_class(tstate, subject, type, oparg, names);
            Py_DECREF(names);
            if (attrs) {
                // Success!
                assert(PyTuple_CheckExact(attrs));
                Py_DECREF(subject);
                SET_SECOND(attrs);
            }
            else if (_PyErr_Occurred(tstate)) {
                goto error;
            }
            Py_DECREF(type);
            SET_TOP(PyBool_FromLong(!!attrs));
            DISPATCH();
        }

        case TARGET(MATCH_MAPPING): {
            PyObject *subject = TOP();
            int match = Py_TYPE(subject)->tp_flags & Py_TPFLAGS_MAPPING;
            PyObject *res = match ? Py_True : Py_False;
            Py_INCREF(res);
            PUSH(res);
            DISPATCH();
        }

        case TARGET(MATCH_SEQUENCE): {
            PyObject *subject = TOP();
            int match = Py_TYPE(subject)->tp_flags & Py_TPFLAGS_SEQUENCE;
            PyObject *res = match ? Py_True : Py_False;
            Py_INCREF(res);
            PUSH(res);
            DISPATCH();
        }

        case TARGET(MATCH_KEYS): {
            // On successful match for all keys, PUSH(values) and PUSH(True).
            // Otherwise, PUSH(None) and PUSH(False).
            PyObject *keys = TOP();
            PyObject *subject = SECOND();
            PyObject *values_or_none = match_keys(tstate, subject, keys);
            if (values_or_none == NULL) {
                goto error;
            }
            PUSH(values_or_none);
            if (Py_IsNone(values_or_none)) {
                Py_INCREF(Py_False);
                PUSH(Py_False);
                DISPATCH();
            }
            assert(PyTuple_CheckExact(values_or_none));
            Py_INCREF(Py_True);
            PUSH(Py_True);
            DISPATCH();
        }

        case TARGET(COPY_DICT_WITHOUT_KEYS): {
            // rest = dict(TOS1)
            // for key in TOS:
            //     del rest[key]
            // SET_TOP(rest)
            PyObject *keys = TOP();
            PyObject *subject = SECOND();
            PyObject *rest = PyDict_New();
            if (rest == NULL || PyDict_Update(rest, subject)) {
                Py_XDECREF(rest);
                goto error;
            }
            // This may seem a bit inefficient, but keys is rarely big enough to
            // actually impact runtime.
            assert(PyTuple_CheckExact(keys));
            for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(keys); i++) {
                if (PyDict_DelItem(rest, PyTuple_GET_ITEM(keys, i))) {
                    Py_DECREF(rest);
                    goto error;
                }
            }
            Py_DECREF(keys);
            SET_TOP(rest);
            DISPATCH();
        }

        case TARGET(GET_ITER): {
            /* before: [obj]; after [getiter(obj)] */
            PyObject *iterable = TOP();
            PyObject *iter = PyObject_GetIter(iterable);
            Py_DECREF(iterable);
            SET_TOP(iter);
            if (iter == NULL)
                goto error;
            PREDICT(FOR_ITER);
            PREDICT(CALL_FUNCTION);
            DISPATCH();
        }

        case TARGET(GET_YIELD_FROM_ITER): {
            /* before: [obj]; after [getiter(obj)] */
            PyObject *iterable = TOP();
            PyObject *iter;
            if (PyCoro_CheckExact(iterable)) {
                /* `iterable` is a coroutine */
                if (!(co->co_flags & (CO_COROUTINE | CO_ITERABLE_COROUTINE))) {
                    /* and it is used in a 'yield from' expression of a
                       regular generator. */
                    Py_DECREF(iterable);
                    SET_TOP(NULL);
                    _PyErr_SetString(tstate, PyExc_TypeError,
                                     "cannot 'yield from' a coroutine object "
                                     "in a non-coroutine generator");
                    goto error;
                }
            }
            else if (!PyGen_CheckExact(iterable)) {
                /* `iterable` is not a generator. */
                iter = PyObject_GetIter(iterable);
                Py_DECREF(iterable);
                SET_TOP(iter);
                if (iter == NULL)
                    goto error;
            }
            PREDICT(LOAD_CONST);
            DISPATCH();
        }

        case TARGET(FOR_ITER): {
            PREDICTED(FOR_ITER);
            /* before: [iter]; after: [iter, iter()] *or* [] */
            PyObject *iter = TOP();
            PyObject *next = (*Py_TYPE(iter)->tp_iternext)(iter);
            if (next != NULL) {
                PUSH(next);
                PREDICT(STORE_FAST);
                PREDICT(UNPACK_SEQUENCE);
                DISPATCH();
            }
            if (_PyErr_Occurred(tstate)) {
                if (!_PyErr_ExceptionMatches(tstate, PyExc_StopIteration)) {
                    goto error;
                }
                else if (tstate->c_tracefunc != NULL) {
                    call_exc_trace(tstate->c_tracefunc, tstate->c_traceobj, tstate, f, &trace_info);
                }
                _PyErr_Clear(tstate);
            }
            /* iterator ended normally */
            STACK_SHRINK(1);
            Py_DECREF(iter);
            JUMPBY(oparg);
            DISPATCH();
        }

        case TARGET(SETUP_FINALLY): {
            PyFrame_BlockSetup(f, SETUP_FINALLY, INSTR_OFFSET() + oparg,
                               STACK_LEVEL());
            DISPATCH();
        }

        case TARGET(BEFORE_ASYNC_WITH): {
            _Py_IDENTIFIER(__aenter__);
            _Py_IDENTIFIER(__aexit__);
            PyObject *mgr = TOP();
            PyObject *enter = special_lookup(tstate, mgr, &PyId___aenter__);
            PyObject *res;
            if (enter == NULL) {
                goto error;
            }
            PyObject *exit = special_lookup(tstate, mgr, &PyId___aexit__);
            if (exit == NULL) {
                Py_DECREF(enter);
                goto error;
            }
            SET_TOP(exit);
            Py_DECREF(mgr);
            res = _PyObject_CallNoArg(enter);
            Py_DECREF(enter);
            if (res == NULL)
                goto error;
            PUSH(res);
            PREDICT(GET_AWAITABLE);
            DISPATCH();
        }

        case TARGET(SETUP_ASYNC_WITH): {
            PyObject *res = POP();
            /* Setup the finally block before pushing the result
               of __aenter__ on the stack. */
            PyFrame_BlockSetup(f, SETUP_FINALLY, INSTR_OFFSET() + oparg,
                               STACK_LEVEL());
            PUSH(res);
            DISPATCH();
        }

        case TARGET(SETUP_WITH): {
            _Py_IDENTIFIER(__enter__);
            _Py_IDENTIFIER(__exit__);
            PyObject *mgr = TOP();
            PyObject *enter = special_lookup(tstate, mgr, &PyId___enter__);
            PyObject *res;
            if (enter == NULL) {
                goto error;
            }
            PyObject *exit = special_lookup(tstate, mgr, &PyId___exit__);
            if (exit == NULL) {
                Py_DECREF(enter);
                goto error;
            }
            SET_TOP(exit);
            Py_DECREF(mgr);
            res = _PyObject_CallNoArg(enter);
            Py_DECREF(enter);
            if (res == NULL)
                goto error;
            /* Setup the finally block before pushing the result
               of __enter__ on the stack. */
            PyFrame_BlockSetup(f, SETUP_FINALLY, INSTR_OFFSET() + oparg,
                               STACK_LEVEL());

            PUSH(res);
            DISPATCH();
        }

        case TARGET(WITH_EXCEPT_START): {
            /* At the top of the stack are 7 values:
               - (TOP, SECOND, THIRD) = exc_info()
               - (FOURTH, FIFTH, SIXTH) = previous exception for EXCEPT_HANDLER
               - SEVENTH: the context.__exit__ bound method
               We call SEVENTH(TOP, SECOND, THIRD).
               Then we push again the TOP exception and the __exit__
               return value.
            */
            PyObject *exit_func;
            PyObject *exc, *val, *tb, *res;

            exc = TOP();
            val = SECOND();
            tb = THIRD();
            assert(!Py_IsNone(exc));
            assert(!PyLong_Check(exc));
            exit_func = PEEK(7);
            PyObject *stack[4] = {NULL, exc, val, tb};
            res = PyObject_Vectorcall(exit_func, stack + 1,
                    3 | PY_VECTORCALL_ARGUMENTS_OFFSET, NULL);
            if (res == NULL)
                goto error;

            PUSH(res);
            DISPATCH();
        }

        case TARGET(LOAD_METHOD): {
            /* Designed to work in tandem with CALL_METHOD. */
            PyObject *name = GETITEM(names, oparg);
            PyObject *obj = TOP();
            PyObject *meth = NULL;

            int meth_found = shadow.shadow == NULL
                                 ? _PyObject_GetMethod(obj, name, &meth)
                                 : _PyShadow_LoadMethodWithCache(
                                       &shadow, next_instr, obj, name, &meth);

            if (meth == NULL) {
                /* Most likely attribute wasn't found. */
                goto error;
            }

            if (meth_found) {
                /* We can bypass temporary bound method object.
                   meth is unbound method and obj is self.

                   meth | self | arg1 | ... | argN
                 */
                SET_TOP(meth);
                PUSH(obj);  // self
            }
            else {
                /* meth is not an unbound method (but a regular attr, or
                   something was returned by a descriptor protocol).  Set
                   the second element of the stack to NULL, to signal
                   CALL_METHOD that it's not a method call.

                   NULL | meth | arg1 | ... | argN
                */
                SET_TOP(NULL);
                Py_DECREF(obj);
                PUSH(meth);
            }
            DISPATCH();
        }

        case TARGET(CALL_METHOD): {
            /* Designed to work in tamdem with LOAD_METHOD. */
            PyObject **sp, *res, *meth;

            sp = stack_pointer;
            int awaited = IS_AWAITED();

            meth = PEEK(oparg + 2);
            if (meth == NULL) {
                /* `meth` is NULL when LOAD_METHOD thinks that it's not
                   a method call.

                   Stack layout:

                       ... | NULL | callable | arg1 | ... | argN
                                                            ^- TOP()
                                               ^- (-oparg)
                                    ^- (-oparg-1)
                             ^- (-oparg-2)

                   `callable` will be POPed by call_function.
                   NULL will will be POPed manually later.
                */
                res = call_function(tstate,
                    &trace_info,
                    &sp,
                    oparg,
                    NULL,
                    awaited ? Ci_Py_AWAITED_CALL_MARKER : 0);
                stack_pointer = sp;
                (void)POP(); /* POP the NULL. */
            }
            else {
                /* This is a method call.  Stack layout:

                     ... | method | self | arg1 | ... | argN
                                                        ^- TOP()
                                           ^- (-oparg)
                                    ^- (-oparg-1)
                           ^- (-oparg-2)

                  `self` and `method` will be POPed by call_function.
                  We'll be passing `oparg + 1` to call_function, to
                  make it accept the `self` as a first argument.
                */
                res = call_function(tstate,
                                    &trace_info,
                                    &sp,
                                    oparg + 1,
                                    NULL,
                                    (awaited ? Ci_Py_AWAITED_CALL_MARKER : 0) |
                                        Ci_Py_VECTORCALL_INVOKED_METHOD);
                stack_pointer = sp;
            }
            if (res == NULL) {
                PUSH(NULL);
                goto error;
            }
            if (awaited && Ci_PyWaitHandle_CheckExact(res)) {
                DISPATCH_EAGER_CORO_RESULT(res, PUSH);
            }
            assert(!Ci_PyWaitHandle_CheckExact(res));
            PUSH(res);
            CHECK_EVAL_BREAKER();
            DISPATCH();
        }

        case TARGET(CALL_FUNCTION): {
            PREDICTED(CALL_FUNCTION);
            PyObject **sp, *res;
            sp = stack_pointer;
            int awaited = IS_AWAITED();
            res = call_function(tstate,
                                &trace_info,
                                &sp,
                                oparg,
                                NULL,
                                awaited ? Ci_Py_AWAITED_CALL_MARKER : 0);
            stack_pointer = sp;
            if (res == NULL) {
                PUSH(NULL);
                goto error;
            }
            if (awaited && Ci_PyWaitHandle_CheckExact(res)) {
                DISPATCH_EAGER_CORO_RESULT(res, PUSH);
            }
            assert(!Ci_PyWaitHandle_CheckExact(res));
            PUSH(res);
            CHECK_EVAL_BREAKER();
            DISPATCH();
        }

        case TARGET(CALL_FUNCTION_KW): {
            PyObject **sp, *res, *names;

            names = POP();

            assert(PyTuple_Check(names));
            assert(PyTuple_GET_SIZE(names) <= oparg);
            /* We assume without checking that names contains only strings */
            sp = stack_pointer;
            int awaited = IS_AWAITED();
            res = call_function(tstate,
                                &trace_info,
                                &sp,
                                oparg,
                                names,
                                awaited ? Ci_Py_AWAITED_CALL_MARKER : 0);
            stack_pointer = sp;
            Py_DECREF(names);

            if (res == NULL) {
                PUSH(NULL);
                goto error;
            }
            if (awaited && Ci_PyWaitHandle_CheckExact(res)) {
                DISPATCH_EAGER_CORO_RESULT(res, PUSH);
            }
            assert(!Ci_PyWaitHandle_CheckExact(res));

            PUSH(res);
            CHECK_EVAL_BREAKER();
            DISPATCH();
        }

        case TARGET(CALL_FUNCTION_EX): {
            PREDICTED(CALL_FUNCTION_EX);
            PyObject *func, *callargs, *kwargs = NULL, *result;
            if (oparg & 0x01) {
                kwargs = POP();
                if (!PyDict_CheckExact(kwargs)) {
                    PyObject *d = PyDict_New();
                    if (d == NULL)
                        goto error;
                    if (_PyDict_MergeEx(d, kwargs, 2) < 0) {
                        Py_DECREF(d);
                        format_kwargs_error(tstate, SECOND(), kwargs);
                        Py_DECREF(kwargs);
                        goto error;
                    }
                    Py_DECREF(kwargs);
                    kwargs = d;
                }
                assert(PyDict_CheckExact(kwargs));
            }
            callargs = POP();
            func = TOP();
            if (!PyTuple_CheckExact(callargs)) {
                if (check_args_iterable(tstate, func, callargs) < 0) {
                    Py_DECREF(callargs);
                    goto error;
                }
                Py_SETREF(callargs, PySequence_Tuple(callargs));
                if (callargs == NULL) {
                    goto error;
                }
            }
            assert(PyTuple_CheckExact(callargs));
            int awaited = IS_AWAITED();
            result = do_call_core(tstate, &trace_info, func, callargs, kwargs, awaited);
            Py_DECREF(func);
            Py_DECREF(callargs);
            Py_XDECREF(kwargs);

            if (result == NULL) {
                SET_TOP(NULL);
                goto error;
            }
            if (awaited && Ci_PyWaitHandle_CheckExact(result)) {
                DISPATCH_EAGER_CORO_RESULT(result, SET_TOP);
            }
            assert(!Ci_PyWaitHandle_CheckExact(result));
            SET_TOP(result);
            CHECK_EVAL_BREAKER();
            DISPATCH();
        }

        case TARGET(MAKE_FUNCTION): {
            PyObject *qualname = POP();
            PyObject *codeobj = POP();
            PyFunctionObject *func = (PyFunctionObject *)
                PyFunction_NewWithQualName(codeobj, f->f_globals, qualname);

            Py_DECREF(codeobj);
            Py_DECREF(qualname);
            if (func == NULL) {
                goto error;
            }

            if (oparg & 0x08) {
                assert(PyTuple_CheckExact(TOP()));
                func->func_closure = POP();
            }
            if (oparg & 0x04) {
                assert(PyTuple_CheckExact(TOP()));
                func->func_annotations = POP();
            }
            if (oparg & 0x02) {
                assert(PyDict_CheckExact(TOP()));
                func->func_kwdefaults = POP();
            }
            if (oparg & 0x01) {
                assert(PyTuple_CheckExact(TOP()));
                func->func_defaults = POP();
            }

            PUSH((PyObject *)func);
            DISPATCH();
        }

        case TARGET(BUILD_SLICE): {
            PyObject *start, *stop, *step, *slice;
            if (oparg == 3)
                step = POP();
            else
                step = NULL;
            stop = POP();
            start = TOP();
            slice = PySlice_New(start, stop, step);
            Py_DECREF(start);
            Py_DECREF(stop);
            Py_XDECREF(step);
            SET_TOP(slice);
            if (slice == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(FORMAT_VALUE): {
            /* Handles f-string value formatting. */
            PyObject *result;
            PyObject *fmt_spec;
            PyObject *value;
            PyObject *(*conv_fn)(PyObject *);
            int which_conversion = oparg & FVC_MASK;
            int have_fmt_spec = (oparg & FVS_MASK) == FVS_HAVE_SPEC;

            fmt_spec = have_fmt_spec ? POP() : NULL;
            value = POP();

            /* See if any conversion is specified. */
            switch (which_conversion) {
            case FVC_NONE:  conv_fn = NULL;           break;
            case FVC_STR:   conv_fn = PyObject_Str;   break;
            case FVC_REPR:  conv_fn = PyObject_Repr;  break;
            case FVC_ASCII: conv_fn = PyObject_ASCII; break;
            default:
                _PyErr_Format(tstate, PyExc_SystemError,
                              "unexpected conversion flag %d",
                              which_conversion);
                goto error;
            }

            /* If there's a conversion function, call it and replace
               value with that result. Otherwise, just use value,
               without conversion. */
            if (conv_fn != NULL) {
                result = conv_fn(value);
                Py_DECREF(value);
                if (result == NULL) {
                    Py_XDECREF(fmt_spec);
                    goto error;
                }
                value = result;
            }

            /* If value is a unicode object, and there's no fmt_spec,
               then we know the result of format(value) is value
               itself. In that case, skip calling format(). I plan to
               move this optimization in to PyObject_Format()
               itself. */
            if (PyUnicode_CheckExact(value) && fmt_spec == NULL) {
                /* Do nothing, just transfer ownership to result. */
                result = value;
            } else {
                /* Actually call format(). */
                result = PyObject_Format(value, fmt_spec);
                Py_DECREF(value);
                Py_XDECREF(fmt_spec);
                if (result == NULL) {
                    goto error;
                }
            }

            PUSH(result);
            DISPATCH();
        }

        case TARGET(ROT_N): {
            PyObject *top = TOP();
            memmove(&PEEK(oparg - 1), &PEEK(oparg),
                    sizeof(PyObject*) * (oparg - 1));
            PEEK(oparg) = top;
            DISPATCH();
        }

        case TARGET(SHADOW_NOP): {
            DISPATCH();
        }

        case TARGET(LOAD_GLOBAL_CACHED): {
            PyObject *name;
            PyObject *v = *global_cache[(unsigned int)oparg];

            if (v == NULL) {
                name = _PyShadow_GetOriginalName(&shadow, next_instr);
                v = _PyDict_LoadGlobal((PyDictObject *)f->f_globals,
                                       (PyDictObject *)f->f_builtins,
                                       name);
                if (v == NULL) {
                    if (!PyErr_Occurred()) {
                        /* _PyDict_LoadGlobal() returns NULL without raising
                         * an exception if the key doesn't exist */
                        format_exc_check_arg(
                            tstate, PyExc_NameError, NAME_ERROR_MSG, name);
                    }
                    goto error;
                }
            }
            Py_INCREF(v);
            PUSH(v);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_NO_DICT_DESCR): {
            PyObject *owner = TOP();
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            PyObject *res = _PyShadow_LoadAttrNoDictDescr(
                &shadow, next_instr, entry, owner);
            if (res == NULL)
                goto error;

            Py_DECREF(owner);
            SET_TOP(res);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_DICT_DESCR): {
            PyObject *owner = TOP();
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            PyObject *res =
                _PyShadow_LoadAttrDictDescr(&shadow, next_instr, entry, owner);
            if (res == NULL)
                goto error;

            Py_DECREF(owner);
            SET_TOP(res);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_DICT_NO_DESCR): {
            PyObject *owner = TOP();
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            PyObject *res = _PyShadow_LoadAttrDictNoDescr(
                &shadow, next_instr, entry, owner);
            if (res == NULL)
                goto error;

            Py_DECREF(owner);
            SET_TOP(res);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_SLOT): {
            PyObject *owner = TOP();
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            PyObject *res =
                _PyShadow_LoadAttrSlot(&shadow, next_instr, entry, owner);
            if (res == NULL)
                goto error;

            SET_TOP(res);
            Py_DECREF(owner);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_SPLIT_DICT): {
            PyObject *owner = TOP();
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            PyObject *res =
                _PyShadow_LoadAttrSplitDict(&shadow, next_instr, entry, owner);
            if (res == NULL)
                goto error;

            SET_TOP(res);
            Py_DECREF(owner);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_SPLIT_DICT_DESCR): {
            /* Normal descriptor + split dict.  We're probably looking up a
             * method and likely have a splitoffset of -1 */
            PyObject *owner = TOP();
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            PyObject *res = _PyShadow_LoadAttrSplitDictDescr(
                &shadow, next_instr, entry, owner);
            if (res == NULL)
                goto error;

            Py_DECREF(owner);
            SET_TOP(res);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_TYPE): {
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            PyObject *owner = TOP();
            PyObject *res =
                _PyShadow_LoadAttrType(&shadow, next_instr, entry, owner);
            if (res == NULL)
                goto error;

            Py_DECREF(owner);
            SET_TOP(res);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_MODULE): {
            PyObject *owner = TOP();
            _PyShadow_ModuleAttrEntry *entry =
                _PyShadow_GetModuleAttr(&shadow, oparg);
            PyObject *res =
                _PyShadow_LoadAttrModule(&shadow, next_instr, entry, owner);
            if (res == NULL)
                goto error;

            Py_DECREF(owner);
            SET_TOP(res);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_S_MODULE): {
            PyObject *owner = TOP();
            _PyShadow_ModuleAttrEntry *entry =
                _PyShadow_GetStrictModuleAttr(&shadow, oparg);
            PyObject *res =
                _PyShadow_LoadAttrStrictModule(&shadow, next_instr, entry, owner);
            if (res == NULL)
                goto error;

            Py_DECREF(owner);
            SET_TOP(res);
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_UNCACHABLE): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *owner = TOP();
            INLINE_CACHE_UNCACHABLE_TYPE(Py_TYPE(owner));

            INLINE_CACHE_RECORD_STAT(LOAD_ATTR_UNCACHABLE, hits);
            PyObject *res = PyObject_GetAttr(owner, name);
            Py_DECREF(owner);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_POLYMORPHIC): {
            PyObject *owner = TOP();
            PyObject *res;
            _PyShadow_InstanceAttrEntry **entries =
                _PyShadow_GetPolymorphicAttr(&shadow, oparg);
            PyTypeObject *type = Py_TYPE(owner);
            for (int i = 0; i < POLYMORPHIC_CACHE_SIZE; i++) {
                _PyShadow_InstanceAttrEntry *entry = entries[i];
                if (entry == NULL) {
                    continue;
                } else if (entry->type != type) {
                    if (entry->type == NULL) {
                        Py_CLEAR(entries[i]);
                    }
                    continue;
                }

                switch (((_PyCacheType *)Py_TYPE(entry))->load_attr_opcode) {
                case LOAD_ATTR_NO_DICT_DESCR:
                    res = _PyShadow_LoadAttrNoDictDescrHit(entry, owner);
                    break;
                case LOAD_ATTR_DICT_DESCR:
                    res = _PyShadow_LoadAttrDictDescrHit(entry, owner);
                    break;
                case LOAD_ATTR_DICT_NO_DESCR:
                    res = _PyShadow_LoadAttrDictNoDescrHit(entry, owner);
                    break;
                case LOAD_ATTR_SLOT:
                    res = _PyShadow_LoadAttrSlotHit(entry, owner);
                    break;
                case LOAD_ATTR_SPLIT_DICT:
                    res = _PyShadow_LoadAttrSplitDictHit(entry, owner);
                    break;
                case LOAD_ATTR_SPLIT_DICT_DESCR:
                    res = _PyShadow_LoadAttrSplitDictDescrHit(entry, owner);
                    break;
                default:
                    Py_UNREACHABLE();
                    return NULL;
                }
                if (res == NULL)
                    goto error;

                Py_DECREF(owner);
                SET_TOP(res);
                DISPATCH();
            }
            res = _PyShadow_LoadAttrPolymorphic(
                &shadow, next_instr, entries, owner);

            if (res == NULL)
                goto error;

            Py_DECREF(owner);
            SET_TOP(res);
            DISPATCH();
        }

        case TARGET(STORE_ATTR_UNCACHABLE): {
            PyObject *name = GETITEM(names, oparg);
            PyObject *owner = TOP();
            PyObject *v = SECOND();
            int err;
            STACK_SHRINK(2);
            err = PyObject_SetAttr(owner, name, v);
            Py_DECREF(v);
            Py_DECREF(owner);
            if (err != 0)
                goto error;
            DISPATCH();
        }

        case TARGET(STORE_ATTR_DICT): {
            PyObject *owner = TOP();
            PyObject *v = SECOND();
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            if (_PyShadow_StoreAttrDict(
                    &shadow, next_instr, entry, owner, v)) {
                goto error;
            }

            STACK_SHRINK(2);
            Py_DECREF(v);
            Py_DECREF(owner);
            DISPATCH();
        }

        case TARGET(STORE_ATTR_DESCR): {
            PyObject *owner = TOP();
            PyObject *v = SECOND();
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            if (_PyShadow_StoreAttrDescr(
                    &shadow, next_instr, entry, owner, v)) {
                goto error;
            }

            STACK_SHRINK(2);
            Py_DECREF(v);
            Py_DECREF(owner);
            DISPATCH();
        }

        case TARGET(STORE_ATTR_SPLIT_DICT): {
            PyObject *owner = TOP();
            PyObject *v = SECOND();
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            if (_PyShadow_StoreAttrSplitDict(
                    &shadow, next_instr, entry, owner, v)) {
                goto error;
            }

            STACK_SHRINK(2);
            Py_DECREF(v);
            Py_DECREF(owner);
            DISPATCH();
        }

        case TARGET(STORE_ATTR_SLOT): {
            PyObject *owner = TOP();
            PyObject *v = SECOND();
            _PyShadow_InstanceAttrEntry *entry =
                _PyShadow_GetInstanceAttr(&shadow, oparg);
            if (_PyShadow_StoreAttrSlot(
                    &shadow, next_instr, entry, owner, v)) {
                goto error;
            }

            STACK_SHRINK(2);
            Py_DECREF(v);
            Py_DECREF(owner);
            DISPATCH();
        }

#define SHADOW_LOAD_METHOD(func, type, helper)                                \
    PyObject *obj = TOP();                                                    \
    PyObject *meth = NULL;                                                    \
    type *entry = helper(&shadow, oparg);                                     \
    int meth_found = func(&shadow, next_instr, entry, obj, &meth);            \
    if (meth == NULL) {                                                       \
        /* Most likely attribute wasn't found. */                             \
        goto error;                                                           \
    }                                                                         \
    if (meth_found) {                                                         \
        SET_TOP(meth);                                                        \
        PUSH(obj);                                                            \
    } else {                                                                  \
        SET_TOP(NULL);                                                        \
        Py_DECREF(obj);                                                       \
        PUSH(meth);                                                           \
    }                                                                         \
    DISPATCH();

        case TARGET(LOAD_METHOD_MODULE): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodModule,
                               _PyShadow_ModuleAttrEntry,
                               _PyShadow_GetModuleAttr);
        }

        case TARGET(LOAD_METHOD_S_MODULE): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodStrictModule,
                               _PyShadow_ModuleAttrEntry,
                               _PyShadow_GetStrictModuleAttr);
        }


        case TARGET(LOAD_METHOD_SPLIT_DICT_DESCR): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodSplitDictDescr,
                               _PyShadow_InstanceAttrEntry,
                               _PyShadow_GetInstanceAttr);
        }

        case TARGET(LOAD_METHOD_DICT_DESCR): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodDictDescr,
                               _PyShadow_InstanceAttrEntry,
                               _PyShadow_GetInstanceAttr);
        }

        case TARGET(LOAD_METHOD_NO_DICT_DESCR): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodNoDictDescr,
                               _PyShadow_InstanceAttrEntry,
                               _PyShadow_GetInstanceAttr);
        }

        case TARGET(LOAD_METHOD_TYPE): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodType,
                               _PyShadow_InstanceAttrEntry,
                               _PyShadow_GetInstanceAttr);
        }

        case TARGET(LOAD_METHOD_TYPE_METHODLIKE): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodTypeMethodLike,
                               _PyShadow_InstanceAttrEntry,
                               _PyShadow_GetInstanceAttr);
        }

        case TARGET(LOAD_METHOD_DICT_METHOD): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodDictMethod,
                               _PyShadow_InstanceAttrEntry,
                               _PyShadow_GetInstanceAttr);
        }

        case TARGET(LOAD_METHOD_SPLIT_DICT_METHOD): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodSplitDictMethod,
                               _PyShadow_InstanceAttrEntry,
                               _PyShadow_GetInstanceAttr);
        }

        case TARGET(LOAD_METHOD_NO_DICT_METHOD): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodNoDictMethod,
                               _PyShadow_InstanceAttrEntry,
                               _PyShadow_GetInstanceAttr);
        }

        case TARGET(LOAD_METHOD_UNSHADOWED_METHOD): {
            SHADOW_LOAD_METHOD(_PyShadow_LoadMethodUnshadowedMethod,
                               _PyShadow_InstanceAttrEntry,
                               _PyShadow_GetInstanceAttr);
        }

        case TARGET(LOAD_METHOD_UNCACHABLE): {
            /* Designed to work in tandem with CALL_METHOD. */
            PyObject *name = GETITEM(names, oparg);
            PyObject *obj = TOP();
            PyObject *meth = NULL;

            int meth_found = _PyObject_GetMethod(obj, name, &meth);

            if (meth == NULL) {
                /* Most likely attribute wasn't found. */
                goto error;
            }

            if (meth_found) {
                /* We can bypass temporary bound method object.
                   meth is unbound method and obj is self.

                   meth | self | arg1 | ... | argN
                 */
                SET_TOP(meth);
                PUSH(obj); // self
            } else {
                /* meth is not an unbound method (but a regular attr, or
                   something was returned by a descriptor protocol).  Set
                   the second element of the stack to NULL, to signal
                   CALL_METHOD that it's not a method call.

                   NULL | meth | arg1 | ... | argN
                */
                SET_TOP(NULL);
                Py_DECREF(obj);
                PUSH(meth);
            }
            DISPATCH();
        }

        case TARGET(BINARY_SUBSCR_TUPLE_CONST_INT): {
            PyObject *container = TOP();
            PyObject *res;
            PyObject *sub;
            if (PyTuple_CheckExact(container)) {
                Py_ssize_t i = (Py_ssize_t)oparg;
                if (i < 0) {
                    i += PyTuple_GET_SIZE(container);
                }
                if (i < 0 || i >= Py_SIZE(container)) {
                    PyErr_SetString(PyExc_IndexError,
                                    "tuple index out of range");
                    res = NULL;
                } else {
                    res = ((PyTupleObject *)container)->ob_item[oparg];
                    Py_INCREF(res);
                }
            } else {
                sub = PyLong_FromLong(oparg);
                res = PyObject_GetItem(container, sub);
                Py_DECREF(sub);
            }
            Py_DECREF(container);

            SET_TOP(res);
            if (res == NULL)
                goto error;
            // This shadow code is applied when we have
            //      LOAD_CONST i
            //      BINARY_SUBSCR
            // And is patched into BINARY_SUBSCR_TUPLE_CONST_INT i
            // at the position of LOAD_CONST.
            // This means that we should always skip the next instruction
            // (i.e. the BINARY_SUBSCR)
            NEXTOPARG();

            DISPATCH();
        }
        case TARGET(BINARY_SUBSCR_DICT_STR): {
            PyObject *sub = POP();
            PyObject *container = TOP();
            PyObject *res;
            if (PyDict_CheckExact(container) && PyUnicode_CheckExact(sub)) {
                res = _PyDict_GetItem_Unicode(container, sub);
                if (res == NULL) {
                    _PyErr_SetKeyError(sub);
                } else {
                    Py_INCREF(res);
                }
            } else {
                _PyShadow_PatchByteCode(
                    &shadow, next_instr, BINARY_SUBSCR, oparg);
                res = PyObject_GetItem(container, sub);
            }

            Py_DECREF(container);
            Py_DECREF(sub);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_SUBSCR_TUPLE): {
            PyObject *sub = POP();
            PyObject *container = TOP();
            PyObject *res;
            if (PyTuple_CheckExact(container)) {
                res = Ci_tuple_subscript(container, sub);
            } else {
                _PyShadow_PatchByteCode(
                    &shadow, next_instr, BINARY_SUBSCR, oparg);
                res = PyObject_GetItem(container, sub);
            }

            Py_DECREF(container);
            Py_DECREF(sub);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_SUBSCR_LIST): {
            PyObject *sub = POP();
            PyObject *container = TOP();
            PyObject *res;
            if (PyList_CheckExact(container)) {
                res = Ci_list_subscript(container, sub);
            } else {
                _PyShadow_PatchByteCode(
                    &shadow, next_instr, BINARY_SUBSCR, oparg);
                res = PyObject_GetItem(container, sub);
            }

            Py_DECREF(container);
            Py_DECREF(sub);
            SET_TOP(res);
            if (res == NULL)
                goto error;
            DISPATCH();
        }

        case TARGET(BINARY_SUBSCR_DICT): {
            PyObject *sub = POP();
            PyObject *container = TOP();
            PyObject *res;
            if (PyDict_CheckExact(container)) {
                res = Ci_dict_subscript(container, sub);
            } else {
                _PyShadow_PatchByteCode(
                    &shadow, next_instr, BINARY_SUBSCR, oparg);
                res = PyObject_GetItem(container, sub);
            }

            Py_DECREF(container);
            Py_DECREF(sub);
            SET_TOP(res);
            if (res == NULL)
                goto error;

            DISPATCH();
        }

        case TARGET(EXTENDED_ARG): {
            int oldoparg = oparg;
            NEXTOPARG();
            oparg |= oldoparg << 8;
            goto dispatch_opcode;
        }

#define _POST_INVOKE_CLEANUP_PUSH_DISPATCH(nargs, awaited, res)   \
            while (nargs--) {                                     \
                Py_DECREF(POP());                                 \
            }                                                     \
            if (res == NULL) {                                    \
                goto error;                                       \
            }                                                     \
            if (awaited && Ci_PyWaitHandle_CheckExact(res)) {     \
                DISPATCH_EAGER_CORO_RESULT(res, PUSH);            \
            }                                                     \
            assert(!Ci_PyWaitHandle_CheckExact(res));             \
            PUSH(res);                                            \
            DISPATCH();                                           \

        case TARGET(INVOKE_METHOD): {
            PyObject *value = GETITEM(consts, oparg);
            Py_ssize_t nargs = PyLong_AsLong(PyTuple_GET_ITEM(value, 1)) + 1;
            PyObject *target = PyTuple_GET_ITEM(value, 0);
            int is_classmethod = PyTuple_GET_SIZE(value) == 3 && (PyTuple_GET_ITEM(value, 2) == Py_True);

            Py_ssize_t slot = _PyClassLoader_ResolveMethod(target);
            if (slot == -1) {
                while (nargs--) {
                    Py_DECREF(POP());
                }
                goto error;
            }

            assert(*(next_instr - 2) == EXTENDED_ARG);
            if (shadow.shadow != NULL && nargs < 0x80) {
                PyMethodDescrObject *method;
                if ((method = _PyClassLoader_ResolveMethodDef(target)) != NULL) {
                    int offset =
                        _PyShadow_CacheCastType(&shadow, (PyObject *)method);
                    if (offset != -1) {
                        _PyShadow_PatchByteCode(
                            &shadow, next_instr, INVOKE_FUNCTION_CACHED, (nargs<<8) | offset);
                    }
                } else {
                  /* We smuggle in the information about whether the invocation was a classmethod
                   * in the low bit of the oparg. This is necessary, as without, the runtime won't
                   * be able to get the correct vtable from self when the type is passed in.
                   */
                    _PyShadow_PatchByteCode(&shadow,
                                            next_instr,
                                            INVOKE_METHOD_CACHED,
                                            (slot << 9) | (nargs << 1) | (is_classmethod ? 1 : 0));
                }
            }

            PyObject **stack = stack_pointer - nargs;
            PyObject *self = *stack;


            _PyType_VTable *vtable;
            if (is_classmethod) {
                vtable = (_PyType_VTable *)(((PyTypeObject *)self)->tp_cache);
            }
            else {
                vtable = (_PyType_VTable *)self->ob_type->tp_cache;
            }

            assert(!PyErr_Occurred());

            int awaited = IS_AWAITED();
            PyObject *res = _PyClassLoader_InvokeMethod(vtable, slot, stack, nargs | (awaited ? Ci_Py_AWAITED_CALL_MARKER : 0));

            _POST_INVOKE_CLEANUP_PUSH_DISPATCH(nargs, awaited, res);
        }

#define FIELD_OFFSET(self, offset) (PyObject **)(((char *)self) + offset)
        case TARGET(LOAD_FIELD): {
            PyObject *field = GETITEM(consts, oparg);
            int field_type;
            Py_ssize_t offset =
                _PyClassLoader_ResolveFieldOffset(field, &field_type);
            if (offset == -1) {
                goto error;
            }
            PyObject *self = TOP();
            PyObject *value;
            if (field_type == TYPED_OBJECT) {
                value = *FIELD_OFFSET(self, offset);
                if (shadow.shadow != NULL) {
                    assert(offset % sizeof(PyObject *) == 0);
                    _PyShadow_PatchByteCode(&shadow,
                                            next_instr,
                                            LOAD_OBJ_FIELD,
                                            offset / sizeof(PyObject *));
                }

                if (value == NULL) {
                    PyObject *name = PyTuple_GET_ITEM(field, PyTuple_GET_SIZE(field) - 1);
                    PyErr_Format(PyExc_AttributeError,
                                 "'%.50s' object has no attribute '%U'",
                                 Py_TYPE(self)->tp_name, name);
                    goto error;
                }
                Py_INCREF(value);
            } else {
                if (shadow.shadow != NULL) {
                    int pos =
                        _PyShadow_CacheFieldType(&shadow, offset, field_type);
                    if (pos != -1) {
                        _PyShadow_PatchByteCode(
                            &shadow, next_instr, LOAD_PRIMITIVE_FIELD, pos);
                    }
                }

                value =
                    load_field(field_type, (char *)FIELD_OFFSET(self, offset));
                if (value == NULL) {
                    goto error;
                }
            }
            Py_DECREF(self);
            SET_TOP(value);
            DISPATCH();
        }

        case TARGET(STORE_FIELD): {
            PyObject *field = GETITEM(consts, oparg);
            int field_type;
            Py_ssize_t offset =
                _PyClassLoader_ResolveFieldOffset(field, &field_type);
            if (offset == -1) {
                goto error;
            }

            PyObject *self = POP();
            PyObject *value = POP();
            PyObject **addr = FIELD_OFFSET(self, offset);

            if (field_type == TYPED_OBJECT) {
                Py_XDECREF(*addr);
                *addr = value;
                if (shadow.shadow != NULL) {
                    assert(offset % sizeof(PyObject *) == 0);
                    _PyShadow_PatchByteCode(&shadow,
                                           next_instr,
                                           STORE_OBJ_FIELD,
                                           offset / sizeof(PyObject *));
                }
            } else {
                if (shadow.shadow != NULL) {
                    int pos =
                        _PyShadow_CacheFieldType(&shadow, offset, field_type);
                    if (pos != -1) {
                        _PyShadow_PatchByteCode(
                            &shadow, next_instr, STORE_PRIMITIVE_FIELD, pos);
                    }
                }
                store_field(field_type, (char *)addr, value);
            }
            Py_DECREF(self);
            DISPATCH();
        }

#define CAST_COERCE_OR_ERROR(val, type, exact)                              \
    if (type == &PyFloat_Type && PyObject_TypeCheck(val, &PyLong_Type)) {   \
        long lval = PyLong_AsLong(val);                                     \
        Py_DECREF(val);                                                     \
        SET_TOP(PyFloat_FromDouble(lval));                                  \
    } else {                                                                \
        PyErr_Format(PyExc_TypeError,                                       \
                    exact ? "expected exactly '%s', got '%s'"               \
                     : "expected '%s', got '%s'",                           \
                    type->tp_name,                                          \
                    Py_TYPE(val)->tp_name);                                 \
        Py_DECREF(type);                                                    \
        goto error;                                                         \
    }

        case TARGET(CAST): {
            PyObject *val = TOP();
            int optional;
            int exact;
            PyTypeObject *type = _PyClassLoader_ResolveType(GETITEM(consts, oparg), &optional, &exact);
            if (type == NULL) {
                goto error;
            }
            if (!_PyObject_TypeCheckOptional(val, type, optional, exact)) {
                CAST_COERCE_OR_ERROR(val, type, exact);
            }

            if (shadow.shadow != NULL) {
                int offset =
                    _PyShadow_CacheCastType(&shadow, (PyObject *)type);
                if (offset != -1) {
                    if (optional) {
                        if (exact) {
                            _PyShadow_PatchByteCode(
                                &shadow, next_instr, CAST_CACHED_OPTIONAL_EXACT, offset);
                        } else {
                            _PyShadow_PatchByteCode(
                                &shadow, next_instr, CAST_CACHED_OPTIONAL, offset);
                        }
                    } else if (exact) {
                        _PyShadow_PatchByteCode(
                            &shadow, next_instr, CAST_CACHED_EXACT, offset);
                    } else {
                        _PyShadow_PatchByteCode(
                            &shadow, next_instr, CAST_CACHED, offset);
                    }
                }
            }
            Py_DECREF(type);
            DISPATCH();
        }

        case TARGET(LOAD_LOCAL): {
            int index =
                _PyLong_AsInt(PyTuple_GET_ITEM(GETITEM(consts, oparg), 0));

            PyObject *value = GETLOCAL(index);
            if (value == NULL) {
                value = PyLong_FromLong(0);
                SETLOCAL(index, value); /* will steal the ref */
            }
            PUSH(value);
            Py_INCREF(value);

            DISPATCH();
        }

        case TARGET(STORE_LOCAL): {
            PyObject *local = GETITEM(consts, oparg);
            int index = _PyLong_AsInt(PyTuple_GET_ITEM(local, 0));
            int type =
                _PyClassLoader_ResolvePrimitiveType(PyTuple_GET_ITEM(local, 1));

            if (type < 0) {
                goto error;
            }

            if (type == TYPED_DOUBLE) {
                SETLOCAL(index, POP());
            } else {
                Py_ssize_t val = unbox_primitive_int_and_decref(POP());
                SETLOCAL(index, box_primitive(type, val));
            }
            if (shadow.shadow != NULL) {
                assert(type < 8);
                _PyShadow_PatchByteCode(
                    &shadow, next_instr, PRIMITIVE_STORE_FAST, (index << 4) | type);
            }

            DISPATCH();
        }

        case TARGET(PRIMITIVE_BOX): {
            if ((oparg & (TYPED_INT_SIGNED)) && oparg != (TYPED_DOUBLE)) {
                /* We have a boxed value on the stack already, but we may have to
                 * deal with sign extension */
                PyObject *val = TOP();
                size_t ival = (size_t)PyLong_AsVoidPtr(val);
                if (ival & ((size_t)1) << 63) {
                    SET_TOP(PyLong_FromSsize_t((int64_t)ival));
                    Py_DECREF(val);
                }
            }
            DISPATCH();
        }

        case TARGET(POP_JUMP_IF_ZERO): {
            PyObject *cond = POP();
            int is_nonzero = Py_SIZE(cond);
            Py_DECREF(cond);
            if (!is_nonzero) {
                JUMPTO(oparg);
            }
            DISPATCH();
        }

        case TARGET(POP_JUMP_IF_NONZERO): {
            PyObject *cond = POP();
            int is_nonzero = Py_SIZE(cond);
            Py_DECREF(cond);
            if (is_nonzero) { JUMPTO(oparg); }
            DISPATCH();
        }

        case TARGET(PRIMITIVE_UNBOX): {
            /* We always box values in the interpreter loop, so this just does
             * overflow checking here. Oparg indicates the type of the unboxed
             * value. */
            PyObject *top = TOP();
            if (PyLong_CheckExact(top)) {
                size_t value;
                if (!_PyClassLoader_OverflowCheck(top, oparg, &value)) {
                    PyErr_SetString(PyExc_OverflowError, "int overflow");
                    goto error;
                }
            }

            DISPATCH();
        }

#define INT_BIN_OPCODE_UNSIGNED(opid, op)                                     \
    case opid: {                                                              \
        r = POP();                                                            \
        l = POP();                                                            \
        PUSH(PyLong_FromVoidPtr((void *)(((size_t)PyLong_AsVoidPtr(l))op(     \
            (size_t)PyLong_AsVoidPtr(r)))));                                  \
        Py_DECREF(r);                                                         \
        Py_DECREF(l);                                                         \
        DISPATCH();                                                           \
    }

#define INT_BIN_OPCODE_SIGNED(opid, op)                                       \
    case opid: {                                                              \
        r = POP();                                                            \
        l = POP();                                                            \
        PUSH(PyLong_FromVoidPtr((void *)(((Py_ssize_t)PyLong_AsVoidPtr(l))op( \
            (Py_ssize_t)PyLong_AsVoidPtr(r)))));                              \
        Py_DECREF(r);                                                         \
        Py_DECREF(l);                                                         \
        DISPATCH();                                                           \
    }

#define DOUBLE_BIN_OPCODE(opid, op)                                           \
    case opid: {                                                              \
        r = POP();                                                            \
        l = POP();                                                            \
        PUSH((PyFloat_FromDouble((PyFloat_AS_DOUBLE(l))op(PyFloat_AS_DOUBLE(r))))); \
        Py_DECREF(r);                                                         \
        Py_DECREF(l);                                                         \
        DISPATCH();                                                           \
    }

        case TARGET(PRIMITIVE_BINARY_OP): {
            PyObject *l, *r;
            switch (oparg) {
                INT_BIN_OPCODE_SIGNED(PRIM_OP_ADD_INT, +)
                INT_BIN_OPCODE_SIGNED(PRIM_OP_SUB_INT, -)
                INT_BIN_OPCODE_SIGNED(PRIM_OP_MUL_INT, *)
                INT_BIN_OPCODE_SIGNED(PRIM_OP_DIV_INT, /)
                INT_BIN_OPCODE_SIGNED(PRIM_OP_MOD_INT, %)
            case PRIM_OP_POW_INT: {
                r = POP();
                l = POP();
                double power = pow((Py_ssize_t)PyLong_AsVoidPtr(l), (Py_ssize_t) PyLong_AsVoidPtr(r));
                PUSH(PyFloat_FromDouble(power));
                Py_DECREF(r);
                Py_DECREF(l);
                DISPATCH();
              }
            case PRIM_OP_POW_UN_INT: {
                r = POP();
                l = POP();
                double power = pow((size_t)PyLong_AsVoidPtr(l), (size_t) PyLong_AsVoidPtr(r));
                PUSH(PyFloat_FromDouble(power));
                Py_DECREF(r);
                Py_DECREF(l);
                DISPATCH();
              }

                INT_BIN_OPCODE_SIGNED(PRIM_OP_LSHIFT_INT, <<)
                INT_BIN_OPCODE_SIGNED(PRIM_OP_RSHIFT_INT, >>)
                INT_BIN_OPCODE_SIGNED(PRIM_OP_XOR_INT, ^)
                INT_BIN_OPCODE_SIGNED(PRIM_OP_OR_INT, |)
                INT_BIN_OPCODE_SIGNED(PRIM_OP_AND_INT, &)
                INT_BIN_OPCODE_UNSIGNED(PRIM_OP_MOD_UN_INT, %)
                INT_BIN_OPCODE_UNSIGNED(PRIM_OP_DIV_UN_INT, /)
                INT_BIN_OPCODE_UNSIGNED(PRIM_OP_RSHIFT_UN_INT, >>)
                DOUBLE_BIN_OPCODE(PRIM_OP_ADD_DBL, +)
                DOUBLE_BIN_OPCODE(PRIM_OP_SUB_DBL, -)
                DOUBLE_BIN_OPCODE(PRIM_OP_MUL_DBL, *)
                DOUBLE_BIN_OPCODE(PRIM_OP_DIV_DBL, /)
            case PRIM_OP_POW_DBL: {
                r = POP();
                l = POP();
                double power = pow(PyFloat_AsDouble(l), PyFloat_AsDouble(r));
                PUSH(PyFloat_FromDouble(power));
                Py_DECREF(r);
                Py_DECREF(l);
                DISPATCH();
              }
            }

            PyErr_SetString(PyExc_RuntimeError, "unknown op");
            goto error;
        }

#define INT_UNARY_OPCODE(opid, op)                                            \
    case opid: {                                                              \
        val = POP();                                                          \
        PUSH(PyLong_FromVoidPtr((void *)(op(size_t) PyLong_AsVoidPtr(val)))); \
        Py_DECREF(val);                                                       \
        DISPATCH();                                                      \
    }

#define DBL_UNARY_OPCODE(opid, op)                                            \
    case opid: {                                                              \
        val = POP();                                                          \
        PUSH(PyFloat_FromDouble(op(PyFloat_AS_DOUBLE(val))));                 \
        Py_DECREF(val);                                                       \
        DISPATCH();                                                      \
    }

        case TARGET(PRIMITIVE_UNARY_OP): {
            PyObject *val;
            switch (oparg) {
                INT_UNARY_OPCODE(PRIM_OP_NEG_INT, -)
                INT_UNARY_OPCODE(PRIM_OP_INV_INT, ~)
                DBL_UNARY_OPCODE(PRIM_OP_NEG_DBL, -)
                case PRIM_OP_NOT_INT: {
                    val = POP();
                    PyObject *res = PyLong_AsVoidPtr(val) ? Py_False : Py_True;
                    Py_INCREF(res);
                    PUSH(res);
                    Py_DECREF(val);
                    DISPATCH();
                }
            }
            PyErr_SetString(PyExc_RuntimeError, "unknown op");
            goto error;
        }

#define INT_CMP_OPCODE_UNSIGNED(opid, op)                                     \
    case opid: {                                                              \
        r = POP();                                                            \
        l = POP();                                                            \
        right = (size_t)PyLong_AsVoidPtr(r);                                  \
        left = (size_t)PyLong_AsVoidPtr(l);                                   \
        Py_DECREF(r);                                                         \
        Py_DECREF(l);                                                         \
        res = (left op right) ? Py_True : Py_False;                           \
        Py_INCREF(res);                                                       \
        PUSH(res);                                                            \
        DISPATCH();                                                           \
    }

#define INT_CMP_OPCODE_SIGNED(opid, op)                                       \
    case opid: {                                                              \
        r = POP();                                                            \
        l = POP();                                                            \
        sright = (Py_ssize_t)PyLong_AsVoidPtr(r);                             \
        sleft = (Py_ssize_t)PyLong_AsVoidPtr(l);                              \
        Py_DECREF(r);                                                         \
        Py_DECREF(l);                                                         \
        res = (sleft op sright) ? Py_True : Py_False;                         \
        Py_INCREF(res);                                                       \
        PUSH(res);                                                            \
        DISPATCH();                                                           \
    }

#define DBL_CMP_OPCODE(opid, op)                                              \
    case opid: {                                                              \
        r = POP();                                                            \
        l = POP();                                                            \
        res = ((PyFloat_AS_DOUBLE(l) op PyFloat_AS_DOUBLE(r)) ?               \
                Py_True : Py_False);                                          \
        Py_DECREF(r);                                                         \
        Py_DECREF(l);                                                         \
        Py_INCREF(res);                                                       \
        PUSH(res);                                                            \
        DISPATCH();                                                           \
    }

        case TARGET(PRIMITIVE_COMPARE_OP): {
            PyObject *l, *r, *res;
            Py_ssize_t sleft, sright;
            size_t left, right;
            switch (oparg) {
                INT_CMP_OPCODE_SIGNED(PRIM_OP_EQ_INT, ==)
                INT_CMP_OPCODE_SIGNED(PRIM_OP_NE_INT, !=)
                INT_CMP_OPCODE_SIGNED(PRIM_OP_LT_INT, <)
                INT_CMP_OPCODE_SIGNED(PRIM_OP_GT_INT, >)
                INT_CMP_OPCODE_SIGNED(PRIM_OP_LE_INT, <=)
                INT_CMP_OPCODE_SIGNED(PRIM_OP_GE_INT, >=)
                INT_CMP_OPCODE_UNSIGNED(PRIM_OP_LT_UN_INT, <)
                INT_CMP_OPCODE_UNSIGNED(PRIM_OP_GT_UN_INT, >)
                INT_CMP_OPCODE_UNSIGNED(PRIM_OP_LE_UN_INT, <=)
                INT_CMP_OPCODE_UNSIGNED(PRIM_OP_GE_UN_INT, >=)
                DBL_CMP_OPCODE(PRIM_OP_EQ_DBL, ==)
                DBL_CMP_OPCODE(PRIM_OP_NE_DBL, !=)
                DBL_CMP_OPCODE(PRIM_OP_LT_DBL, <)
                DBL_CMP_OPCODE(PRIM_OP_GT_DBL, >)
                DBL_CMP_OPCODE(PRIM_OP_LE_DBL, <=)
                DBL_CMP_OPCODE(PRIM_OP_GE_DBL, >=)
            }
            PyErr_SetString(PyExc_RuntimeError, "unknown op");
            goto error;
        }

        case TARGET(LOAD_ITERABLE_ARG): {
            // TODO: Revisit this opcode, and perhaps get it to load all
            // elements of an iterable to a stack. That'll help with the
            // compiled code size.
            PyObject *tup = POP();
            int idx = oparg;
            if (!PyTuple_CheckExact(tup)) {
                if (tup->ob_type->tp_iter == NULL && !PySequence_Check(tup)) {
                    PyErr_Format(PyExc_TypeError,
                                 "argument after * "
                                 "must be an iterable, not %.200s",
                                 tup->ob_type->tp_name);
                    Py_DECREF(tup);
                    goto error;
                }
                Py_SETREF(tup, PySequence_Tuple(tup));
                if (tup == NULL) {
                    goto error;
                }
            }
            PyObject *element = PyTuple_GetItem(tup, idx);
            if (!element) {
                Py_DECREF(tup);
                goto error;
            }
            Py_INCREF(element);
            PUSH(element);
            PUSH(tup);
            DISPATCH();
        }

        case TARGET(LOAD_MAPPING_ARG): {
            PyObject *name = POP();
            PyObject *mapping = POP();

            if (!PyDict_Check(mapping) && !Ci_CheckedDict_Check(mapping)) {
                PyErr_Format(PyExc_TypeError,
                             "argument after ** "
                             "must be a dict, not %.200s",
                             mapping->ob_type->tp_name);
                Py_DECREF(name);
                Py_DECREF(mapping);
                goto error;
            }

            PyObject *value = PyDict_GetItemWithError(mapping, name);
            if (value == NULL) {
                if (_PyErr_Occurred(tstate)) {
                    Py_DECREF(name);
                    Py_DECREF(mapping);
                    goto error;
                } else if (oparg == 2) {
                    PyErr_Format(PyExc_TypeError, "missing argument %U", name);
                    goto error;
                } else {
                    /* Default value is on the stack */
                    Py_DECREF(name);
                    Py_DECREF(mapping);
                    DISPATCH();
                }
            } else if (oparg == 3) {
                /* Remove default value */
                Py_DECREF(POP());
            }
            Py_XINCREF(value);
            Py_DECREF(name);
            Py_DECREF(mapping);
            PUSH(value);
            DISPATCH();
        }
        case TARGET(INVOKE_FUNCTION): {
            PyObject *value = GETITEM(consts, oparg);
            Py_ssize_t nargs = PyLong_AsLong(PyTuple_GET_ITEM(value, 1));
            PyObject *target = PyTuple_GET_ITEM(value, 0);
            PyObject *container;
            PyObject *func = _PyClassLoader_ResolveFunction(target, &container);
            if (func == NULL) {
                goto error;
            }
             int awaited = IS_AWAITED();
            PyObject **sp = stack_pointer - nargs;
            PyObject *res = invoke_static_function(func, sp, nargs, awaited);

            if (shadow.shadow != NULL && nargs < 0x80) {
                if (_PyClassLoader_IsImmutable(container)) {
                    /* frozen type, we don't need to worry about indirecting */
                    int offset =
                        _PyShadow_CacheCastType(&shadow, func);
                    if (offset != -1) {
                        _PyShadow_PatchByteCode(
                            &shadow, next_instr, INVOKE_FUNCTION_CACHED, (nargs<<8) | offset);
                    }
                } else {
                    PyObject **funcptr = _PyClassLoader_ResolveIndirectPtr(target);
                    int offset =
                        _PyShadow_CacheFunction(&shadow, funcptr);
                    if (offset != -1) {
                        _PyShadow_PatchByteCode(
                            &shadow, next_instr, INVOKE_FUNCTION_INDIRECT_CACHED, (nargs<<8) | offset);
                    }
                }
            }

            Py_DECREF(func);
            Py_DECREF(container);

            _POST_INVOKE_CLEANUP_PUSH_DISPATCH(nargs, awaited, res);
        }

        case TARGET(INVOKE_NATIVE): {
            PyObject *value = GETITEM(consts, oparg);
            assert(PyTuple_CheckExact(value));
            PyObject *target = PyTuple_GET_ITEM(value, 0);
            PyObject *name = PyTuple_GET_ITEM(target, 0);
            PyObject *symbol = PyTuple_GET_ITEM(target, 1);
            PyObject *signature = PyTuple_GET_ITEM(value, 1);
            Py_ssize_t nargs = PyTuple_GET_SIZE(signature) - 1;
            PyObject **sp = stack_pointer - nargs;
            PyObject *res = _PyClassloader_InvokeNativeFunction(name, symbol, signature, sp, nargs);
            _POST_INVOKE_CLEANUP_PUSH_DISPATCH(nargs, 0, res);
        }

        case TARGET(JUMP_IF_ZERO_OR_POP): {
            PyObject *cond = TOP();
            int is_nonzero = Py_SIZE(cond);
            if (is_nonzero) {
                STACK_SHRINK(1);
                Py_DECREF(cond);
            } else {
                JUMPTO(oparg);
            }
            DISPATCH();
        }

        case TARGET(JUMP_IF_NONZERO_OR_POP): {
            PyObject *cond = TOP();
            int is_nonzero = Py_SIZE(cond);
            if (!is_nonzero) {
                STACK_SHRINK(1);
                Py_DECREF(cond);
            } else {
                JUMPTO(oparg);
            }
            DISPATCH()
        }

        case TARGET(FAST_LEN): {
            PyObject *collection = POP(), *length = NULL;
            int inexact = oparg & FAST_LEN_INEXACT;
            oparg &= ~FAST_LEN_INEXACT;
            assert(FAST_LEN_LIST <= oparg && oparg <= FAST_LEN_STR);
            if (inexact) {
              if ((oparg == FAST_LEN_LIST && PyList_CheckExact(collection)) ||
                  (oparg == FAST_LEN_DICT && PyDict_CheckExact(collection)) ||
                  (oparg == FAST_LEN_SET && PyAnySet_CheckExact(collection)) ||
                  (oparg == FAST_LEN_TUPLE && PyTuple_CheckExact(collection)) ||
                  (oparg == FAST_LEN_ARRAY && PyStaticArray_CheckExact(collection)) ||
                  (oparg == FAST_LEN_STR && PyUnicode_CheckExact(collection))) {
                inexact = 0;
              }
            }
            if (inexact) {
              Py_ssize_t res = PyObject_Size(collection);
              if (res >= 0) {
                length = PyLong_FromSsize_t(res);
              }
            } else if (oparg == FAST_LEN_DICT) {
              length = PyLong_FromLong(((PyDictObject*)collection)->ma_used);
            } else if (oparg == FAST_LEN_SET) {
              length = PyLong_FromLong(((PySetObject*)collection)->used);
            } else {
              // lists, tuples, arrays are all PyVarObject and use ob_size
              length = PyLong_FromLong(Py_SIZE(collection));
            }
            Py_DECREF(collection);
            if (length == NULL) {
                goto error;
            }
            PUSH(length);
            DISPATCH();
        }

        case TARGET(CONVERT_PRIMITIVE): {
            Py_ssize_t from_type = oparg & 0xFF;
            Py_ssize_t to_type = oparg >> 4;
            Py_ssize_t extend_sign = (from_type & TYPED_INT_SIGNED) && (to_type & TYPED_INT_SIGNED);
            int size = to_type >> 1;
            PyObject *val = TOP();
            size_t ival = (size_t)PyLong_AsVoidPtr(val);

            ival &= trunc_masks[size];

            // Extend the sign if needed
            if (extend_sign != 0 && (ival & signed_bits[size])) {
                ival |= (signex_masks[size]);
            }

            Py_DECREF(val);
            SET_TOP(PyLong_FromSize_t(ival));
            DISPATCH();
        }

        case TARGET(LOAD_CLASS): {
            PyObject *type_descr = GETITEM(consts, oparg);
            int optional;
            int exact;
            PyTypeObject *type = _PyClassLoader_ResolveType(type_descr, &optional, &exact);
            if (type == NULL) {
                goto error;
            }
            PUSH((PyObject*)type);
            DISPATCH();
        }

        case TARGET(BUILD_CHECKED_MAP): {
            PyObject *map_info = GETITEM(consts, oparg);
            PyObject *map_type = PyTuple_GET_ITEM(map_info, 0);
            Py_ssize_t map_size = PyLong_AsLong(PyTuple_GET_ITEM(map_info, 1));

            int optional;
            int exact;
            PyTypeObject *type = _PyClassLoader_ResolveType(map_type, &optional, &exact);
            assert(!optional);

            if (shadow.shadow != NULL) {
                PyObject *cache = PyTuple_New(2);
                if (cache == NULL) {
                    goto error;
                }
                PyTuple_SET_ITEM(cache, 0, (PyObject *)type);
                Py_INCREF(type);
                PyObject *size = PyLong_FromLong(map_size);
                if (size == NULL) {
                    Py_DECREF(cache);
                    goto error;
                }
                PyTuple_SET_ITEM(cache, 1, size);

                int offset = _PyShadow_CacheCastType(&shadow, cache);
                Py_DECREF(cache);
                if (offset != -1) {
                    _PyShadow_PatchByteCode(
                        &shadow, next_instr, BUILD_CHECKED_MAP_CACHED, offset);
                }
            }

            PyObject *map = Ci_CheckedDict_NewPresized(type, map_size);
            if (map == NULL) {
                goto error;
            }
            Py_DECREF(type);

            Ci_BUILD_DICT(map_size, Ci_CheckedDict_SetItem);
            DISPATCH();
        }

        case TARGET(SEQUENCE_GET): {
            PyObject *idx = POP(), *sequence, *item;

            Py_ssize_t val = (Py_ssize_t)PyLong_AsVoidPtr(idx);

            if (val == -1 && _PyErr_Occurred(tstate)) {
                Py_DECREF(idx);
                goto error;
            }

            sequence = POP();

            // Adjust index
            if (val < 0) {
                val += Py_SIZE(sequence);
            }

            oparg &= ~SEQ_SUBSCR_UNCHECKED;

            if (oparg == SEQ_LIST) {
                item = PyList_GetItem(sequence, val);
                Py_DECREF(sequence);
                if (item == NULL) {
                    Py_DECREF(idx);
                    goto error;
                }
                Py_INCREF(item);
            } else if (oparg == SEQ_LIST_INEXACT) {
                if (PyList_CheckExact(sequence) ||
                    Py_TYPE(sequence)->tp_as_sequence->sq_item == PyList_Type.tp_as_sequence->sq_item) {
                    item = PyList_GetItem(sequence, val);
                    Py_DECREF(sequence);
                    if (item == NULL) {
                        Py_DECREF(idx);
                        goto error;
                    }
                    Py_INCREF(item);
                } else {
                    item = PyObject_GetItem(sequence, idx);
                    Py_DECREF(sequence);
                    if (item == NULL) {
                        Py_DECREF(idx);
                        goto error;
                    }
                }
            } else if (oparg == SEQ_CHECKED_LIST) {
                item = Ci_CheckedList_GetItem(sequence, val);
                Py_DECREF(sequence);
                if (item == NULL) {
                    Py_DECREF(idx);
                    goto error;
                }
            } else if (oparg == SEQ_ARRAY_INT64) {
                item = _Ci_StaticArray_Get(sequence, val);
                Py_DECREF(sequence);
                if (item == NULL) {
                    Py_DECREF(idx);
                    goto error;
                }
            } else {
                PyErr_Format(PyExc_SystemError, "bad oparg for SEQUENCE_GET: %d",
                    oparg);
                Py_DECREF(idx);
                goto error;
            }

            Py_DECREF(idx);
            PUSH(item);
            DISPATCH();
        }

        case TARGET(SEQUENCE_SET): {
            PyObject *subscr = TOP();
            PyObject *sequence = SECOND();
            PyObject *v = THIRD();
            int err;
            STACK_SHRINK(3);

            Py_ssize_t idx = (Py_ssize_t)PyLong_AsVoidPtr(subscr);
            Py_DECREF(subscr);

            if (idx == -1 && _PyErr_Occurred(tstate)) {
                Py_DECREF(v);
                Py_DECREF(sequence);
                goto error;
            }

            // Adjust index
            if (idx < 0) {
                idx += Py_SIZE(sequence);
            }

            if (oparg == SEQ_LIST) {
                err = PyList_SetItem(sequence, idx, v);

                Py_DECREF(sequence);
                if (err != 0) {
                    Py_DECREF(v);
                    goto error;
                }
            } else if (oparg == SEQ_LIST_INEXACT) {
                if (PyList_CheckExact(sequence) ||
                    Py_TYPE(sequence)->tp_as_sequence->sq_ass_item == PyList_Type.tp_as_sequence->sq_ass_item) {
                    err = PyList_SetItem(sequence, idx, v);

                    Py_DECREF(sequence);
                    if (err != 0) {
                        Py_DECREF(v);
                        goto error;
                    }
                } else {
                    err = PyObject_SetItem(sequence, subscr, v);
                    Py_DECREF(v);
                    Py_DECREF(sequence);
                    if (err != 0) {
                        goto error;
                    }
                }
            } else if (oparg == SEQ_ARRAY_INT64) {
                err = _Ci_StaticArray_Set(sequence, idx, v);

                Py_DECREF(sequence);
                if (err != 0) {
                    Py_DECREF(v);
                    goto error;
                }
            } else {
                PyErr_Format(PyExc_SystemError, "bad oparg for SEQUENCE_SET: %d",
                    oparg);
                goto error;
            }
            DISPATCH();
        }

        case TARGET(LIST_DEL): {
            PyObject *subscr = TOP();
            PyObject *list = SECOND();
            int err;
            STACK_SHRINK(2);

            Py_ssize_t idx = PyLong_AsLong(subscr);
            Py_DECREF(subscr);

            if (idx == -1 && _PyErr_Occurred(tstate)) {
                Py_DECREF(list);
                goto error;
            }

            err = PyList_SetSlice(list, idx, idx+1, NULL);

            Py_DECREF(list);
            if (err != 0) {
                goto error;
            }
            DISPATCH();
        }

        case TARGET(REFINE_TYPE): {
            DISPATCH();
        }

        case TARGET(PRIMITIVE_LOAD_CONST): {
            PyObject* val = PyTuple_GET_ITEM(GETITEM(consts, oparg), 0);
            Py_INCREF(val);
            PUSH(val);
            DISPATCH();
        }

        case TARGET(RETURN_PRIMITIVE): {
            retval = POP();

            /* In the interpreter, we always return a boxed int. We have a boxed
             * value on the stack already, but we may have to deal with sign
             * extension. */
            if (oparg & TYPED_INT_SIGNED && oparg != TYPED_DOUBLE) {
                size_t ival = (size_t)PyLong_AsVoidPtr(retval);
                if (ival & ((size_t)1) << 63) {
                    Py_DECREF(retval);
                    retval = PyLong_FromSsize_t((int64_t)ival);
                }
            }

            assert(f->f_iblock == 0);
            goto exiting;
        }

        case TARGET(LOAD_METHOD_SUPER): {
            PyObject *pair = GETITEM(consts, oparg);
            PyObject *name_obj = PyTuple_GET_ITEM(pair, 0);
            int name_idx = _PyLong_AsInt(name_obj);
            PyObject *name = GETITEM(names, name_idx);

            assert (PyBool_Check(PyTuple_GET_ITEM(pair, 1)));
            int call_no_args = PyTuple_GET_ITEM(pair, 1) == Py_True;

            PyObject *self = POP();
            PyObject *type = POP();
            PyObject *global_super = POP();

            int meth_found = 0;
            PyObject *attr = super_lookup_method_or_attr(
                tstate, global_super, (PyTypeObject *)type, self, name, call_no_args, &meth_found);
            Py_DECREF(type);
            Py_DECREF(global_super);

            if (attr == NULL) {
                Py_DECREF(self);
                goto error;
            }
            if (meth_found) {
                PUSH(attr);
                PUSH(self);
            }
            else {
                Py_DECREF(self);

                PUSH(NULL);
                PUSH(attr);
            }
            DISPATCH();
        }

        case TARGET(LOAD_ATTR_SUPER): {
            PyObject *pair = GETITEM(consts, oparg);
            PyObject *name_obj = PyTuple_GET_ITEM(pair, 0);
            int name_idx = _PyLong_AsInt(name_obj);
            PyObject *name = GETITEM(names, name_idx);

            assert (PyBool_Check(PyTuple_GET_ITEM(pair, 1)));

            int call_no_args = PyTuple_GET_ITEM(pair, 1) == Py_True;

            PyObject *self = POP();
            PyObject *type = POP();
            PyObject *global_super = POP();
            PyObject *attr = super_lookup_method_or_attr(
                tstate, global_super, (PyTypeObject *)type, self, name, call_no_args, NULL);
            Py_DECREF(type);
            Py_DECREF(self);
            Py_DECREF(global_super);

            if (attr == NULL) {
                goto error;
            }
            PUSH(attr);
            DISPATCH();
        }

        case TARGET(TP_ALLOC): {
            int optional;
            int exact;
            PyTypeObject *type = _PyClassLoader_ResolveType(GETITEM(consts, oparg), &optional, &exact);
            assert(!optional);
            if (type == NULL) {
                goto error;
            }

            PyObject *inst = type->tp_alloc(type, 0);
            if (inst == NULL) {
                Py_DECREF(type);
                goto error;
            }
            PUSH(inst);

            if (shadow.shadow != NULL) {
                int offset =
                    _PyShadow_CacheCastType(&shadow, (PyObject *)type);
                if (offset != -1) {
                    _PyShadow_PatchByteCode(
                        &shadow, next_instr, TP_ALLOC_CACHED, offset);
                }
            }
            Py_DECREF(type);
            DISPATCH();
        }

        case TARGET(BUILD_CHECKED_LIST): {
            PyObject *list_info = GETITEM(consts, oparg);
            PyObject *list_type = PyTuple_GET_ITEM(list_info, 0);
            Py_ssize_t list_size = PyLong_AsLong(PyTuple_GET_ITEM(list_info, 1));

            int optional;
            int exact;
            PyTypeObject *type = _PyClassLoader_ResolveType(list_type, &optional, &exact);
            assert(!optional);

            if (shadow.shadow != NULL) {
                PyObject *cache = PyTuple_New(2);
                if (cache == NULL) {
                    goto error;
                }
                PyTuple_SET_ITEM(cache, 0, (PyObject *)type);
                Py_INCREF(type);
                PyObject *size = PyLong_FromLong(list_size);
                if (size == NULL) {
                    Py_DECREF(cache);
                    goto error;
                }
                PyTuple_SET_ITEM(cache, 1, size);

                int offset = _PyShadow_CacheCastType(&shadow, cache);
                Py_DECREF(cache);
                if (offset != -1) {
                    _PyShadow_PatchByteCode(
                        &shadow, next_instr, BUILD_CHECKED_LIST_CACHED, offset);
                }
            }

            PyObject *list = Ci_CheckedList_New(type, list_size);
            if (list == NULL) {
                goto error;
            }
            Py_DECREF(type);

            while (--list_size >= 0) {
              PyObject *item = POP();
              Ci_ListOrCheckedList_SET_ITEM(list, list_size, item);
            }
            PUSH(list);
            DISPATCH();
        }

        case TARGET(LOAD_TYPE): {
            PyObject *instance = TOP();
            Py_INCREF(Py_TYPE(instance));
            SET_TOP((PyObject *)Py_TYPE(instance));
            Py_DECREF(instance);
            DISPATCH();
        }

        case TARGET(BUILD_CHECKED_LIST_CACHED): {
            PyObject *cache = _PyShadow_GetCastType(&shadow, oparg);
            PyTypeObject *type = (PyTypeObject *)PyTuple_GET_ITEM(cache, 0);
            Py_ssize_t list_size = PyLong_AsLong(PyTuple_GET_ITEM(cache, 1));

            PyObject *list = Ci_CheckedList_New(type, list_size);
            if (list == NULL) {
                goto error;
            }

            while (--list_size >= 0) {
              PyObject *item = POP();
              PyList_SET_ITEM(list, list_size, item);
            }
            PUSH(list);
            DISPATCH();
        }

        case TARGET(TP_ALLOC_CACHED): {
            PyTypeObject *type = (PyTypeObject *)_PyShadow_GetCastType(&shadow, oparg);
            PyObject *inst = type->tp_alloc(type, 0);
            if (inst == NULL) {
                goto error;
            }

            PUSH(inst);
            DISPATCH();
        }

        case TARGET(INVOKE_FUNCTION_CACHED): {
            PyObject *func = _PyShadow_GetCastType(&shadow, oparg & 0xff);
            Py_ssize_t nargs = oparg >> 8;
            int awaited = IS_AWAITED();

            PyObject **sp = stack_pointer - nargs;
            PyObject *res = invoke_static_function(func, sp, nargs, awaited);

            _POST_INVOKE_CLEANUP_PUSH_DISPATCH(nargs, awaited, res);
        }

        case TARGET(INVOKE_FUNCTION_INDIRECT_CACHED): {
            PyObject **funcref = _PyShadow_GetFunction(&shadow, oparg & 0xff);
            Py_ssize_t nargs = oparg >> 8;
            int awaited = IS_AWAITED();

            PyObject **sp = stack_pointer - nargs;
            PyObject *func = *funcref;
            PyObject *res;
            /* For indirect calls we just use _PyObject_Vectorcall, which will
            * handle non-vector call objects as well.  We expect in high-perf
            * situations to either have frozen types or frozen strict modules */
            if (func == NULL) {
                PyObject *target = PyTuple_GET_ITEM(_PyShadow_GetOriginalConst(&shadow, next_instr), 0);
                func = _PyClassLoader_ResolveFunction(target, NULL);
                if (func == NULL) {
                    goto error;
                }

                res = _PyObject_VectorcallTstate(
                    tstate,
                    func,
                    sp,
                    (awaited ? Ci_Py_AWAITED_CALL_MARKER : 0) | nargs,
                    NULL
                );
                Py_DECREF(func);
            } else {
                res = _PyObject_VectorcallTstate(
                    tstate,
                    func,
                    sp,
                    (awaited ? Ci_Py_AWAITED_CALL_MARKER : 0) | nargs,
                    NULL
                );
            }

            _POST_INVOKE_CLEANUP_PUSH_DISPATCH(nargs, awaited, res);
        }

        case TARGET(BUILD_CHECKED_MAP_CACHED): {
            PyObject *cache = _PyShadow_GetCastType(&shadow, oparg);
            PyTypeObject *type = (PyTypeObject *)PyTuple_GET_ITEM(cache, 0);
            Py_ssize_t map_size = PyLong_AsLong(PyTuple_GET_ITEM(cache, 1));

            PyObject *map = Ci_CheckedDict_NewPresized(type, map_size);
            if (map == NULL) {
                goto error;
            }

            Ci_BUILD_DICT(map_size, Ci_CheckedDict_SetItem);
            DISPATCH();
        }

        case TARGET(PRIMITIVE_STORE_FAST): {
            int type = oparg & 0xF;
            int idx = oparg >> 4;
            PyObject *value = POP();
            if (type == TYPED_DOUBLE) {
                SETLOCAL(idx, POP());
            } else {
                Py_ssize_t val = unbox_primitive_int_and_decref(value);
                SETLOCAL(idx, box_primitive(type, val));
            }

            DISPATCH();
        }

        case TARGET(CAST_CACHED_OPTIONAL): {
            PyObject *val = TOP();
            PyTypeObject *type = (PyTypeObject *)_PyShadow_GetCastType(&shadow, oparg);
            if (!_PyObject_TypeCheckOptional(val, type, /* opt */ 1, /* exact */ 0)) {
                CAST_COERCE_OR_ERROR(val, type, /* exact */ 0);
            }
            DISPATCH();
        }

        case TARGET(CAST_CACHED): {
            PyObject *val = TOP();
            PyTypeObject *type = (PyTypeObject *)_PyShadow_GetCastType(&shadow, oparg);
            if (!PyObject_TypeCheck(val, type)) {
                CAST_COERCE_OR_ERROR(val, type, /* exact */ 0);
            }
            DISPATCH();
        }

        case TARGET(CAST_CACHED_EXACT): {
            PyObject *val = TOP();
            PyTypeObject *type = (PyTypeObject *)_PyShadow_GetCastType(&shadow, oparg);
            if (Py_TYPE(val) != type) {
                CAST_COERCE_OR_ERROR(val, type, /* exact */ 1);
            }
            DISPATCH();
        }

        case TARGET(CAST_CACHED_OPTIONAL_EXACT): {
            PyObject *val = TOP();
            PyTypeObject *type = (PyTypeObject *)_PyShadow_GetCastType(&shadow, oparg);
            if (!_PyObject_TypeCheckOptional(val, type, /* opt */ 1, /* exact */ 1)) {
                CAST_COERCE_OR_ERROR(val, type, /* exact */ 1);
            }
            DISPATCH();
        }

        case TARGET(LOAD_PRIMITIVE_FIELD): {
            _FieldCache *cache = _PyShadow_GetFieldCache(&shadow, oparg);
            PyObject *value = load_field(cache->type, ((char *)TOP()) + cache->offset);
            if (value == NULL) {
                goto error;
            }

            Py_DECREF(TOP());
            SET_TOP(value);
            DISPATCH();
        }

        case TARGET(STORE_PRIMITIVE_FIELD): {
            _FieldCache *cache = _PyShadow_GetFieldCache(&shadow, oparg);
            PyObject *self = POP();
            PyObject *value = POP();
            store_field(cache->type, ((char *)self) + cache->offset, value);
            Py_DECREF(self);
            DISPATCH();
        }

        case TARGET(LOAD_OBJ_FIELD): {
            PyObject *self = TOP();
            PyObject **addr = FIELD_OFFSET(self, oparg * sizeof(PyObject *));
            PyObject *value = *addr;
            if (value == NULL) {
                PyErr_Format(PyExc_AttributeError,
                             "'%.50s' object has no attribute",
                             Py_TYPE(self)->tp_name);
                goto error;
            }

            Py_INCREF(value);
            Py_DECREF(self);
            SET_TOP(value);
            DISPATCH();
        }

        case TARGET(STORE_OBJ_FIELD): {
            Py_ssize_t offset = oparg * sizeof(PyObject *);
            PyObject *self = POP();
            PyObject *value = POP();
            PyObject **addr = FIELD_OFFSET(self, offset);
            Py_XDECREF(*addr);
            *addr = value;
            Py_DECREF(self);
            DISPATCH();
        }

        case TARGET(INVOKE_METHOD_CACHED): {
            int is_classmethod = oparg & 1;
            Py_ssize_t nargs = (oparg >> 1) & 0xff;
            PyObject **stack = stack_pointer - nargs;
            PyObject *self = *stack;
            _PyType_VTable *vtable;
            if (is_classmethod) {
                vtable = (_PyType_VTable *)(((PyTypeObject *)self)->tp_cache);
            }
            else {
                vtable = (_PyType_VTable *)self->ob_type->tp_cache;
            }

            Py_ssize_t slot = oparg >> 9;

            int awaited = IS_AWAITED();

            assert(!PyErr_Occurred());
            PyObject *res = _PyClassLoader_InvokeMethod(vtable, slot, stack, nargs | (awaited ? Ci_Py_AWAITED_CALL_MARKER : 0));

            _POST_INVOKE_CLEANUP_PUSH_DISPATCH(nargs, awaited, res);
        }

#if USE_COMPUTED_GOTOS
        _unknown_opcode:
#endif
        default:
            fprintf(stderr,
                "XXX lineno: %d, opcode: %d\n",
                PyFrame_GetLineNumber(f),
                opcode);
            _PyErr_SetString(tstate, PyExc_SystemError, "unknown opcode");
            goto error;

        } /* switch */

        /* This should never be reached. Every opcode should end with DISPATCH()
           or goto error. */
        Py_UNREACHABLE();

error:
        /* Double-check exception status. */
#ifdef NDEBUG
        if (!_PyErr_Occurred(tstate)) {
            _PyErr_SetString(tstate, PyExc_SystemError,
                             "error return without exception set");
        }
#else
        assert(_PyErr_Occurred(tstate));
#endif

        /* Log traceback info. */
        PyTraceBack_Here(f);

        if (tstate->c_tracefunc != NULL) {
            /* Make sure state is set to FRAME_EXECUTING for tracing */
            assert(f->f_state == FRAME_EXECUTING);
            f->f_state = FRAME_UNWINDING;
            call_exc_trace(tstate->c_tracefunc, tstate->c_traceobj,
                           tstate, f, &trace_info);
        }
exception_unwind:
        f->f_state = FRAME_UNWINDING;
        /* Unwind stacks if an exception occurred */
        while (f->f_iblock > 0) {
            /* Pop the current block. */
            PyTryBlock *b = &f->f_blockstack[--f->f_iblock];

            if (b->b_type == EXCEPT_HANDLER) {
                UNWIND_EXCEPT_HANDLER(b);
                continue;
            }
            UNWIND_BLOCK(b);
            if (b->b_type == SETUP_FINALLY) {
                PyObject *exc, *val, *tb;
                int handler = b->b_handler;
                _PyErr_StackItem *exc_info = tstate->exc_info;
                /* Beware, this invalidates all b->b_* fields */
                PyFrame_BlockSetup(f, EXCEPT_HANDLER, f->f_lasti, STACK_LEVEL());
                PUSH(exc_info->exc_traceback);
                PUSH(exc_info->exc_value);
                if (exc_info->exc_type != NULL) {
                    PUSH(exc_info->exc_type);
                }
                else {
                    Py_INCREF(Py_None);
                    PUSH(Py_None);
                }
                _PyErr_Fetch(tstate, &exc, &val, &tb);
                /* Make the raw exception data
                   available to the handler,
                   so a program can emulate the
                   Python main loop. */
                _PyErr_NormalizeException(tstate, &exc, &val, &tb);
                if (tb != NULL)
                    PyException_SetTraceback(val, tb);
                else
                    PyException_SetTraceback(val, Py_None);
                Py_INCREF(exc);
                exc_info->exc_type = exc;
                Py_INCREF(val);
                exc_info->exc_value = val;
                exc_info->exc_traceback = tb;
                if (tb == NULL)
                    tb = Py_None;
                Py_INCREF(tb);
                PUSH(tb);
                PUSH(val);
                PUSH(exc);
                JUMPTO(handler);
                /* Resume normal execution */
                f->f_state = FRAME_EXECUTING;
                goto main_loop;
            }
        } /* unwind stack */

        /* End the loop as we still have an error */
        break;
    } /* main loop */

    assert(retval == NULL);
    assert(_PyErr_Occurred(tstate));

    /* Pop remaining stack entries. */
    while (!EMPTY()) {
        PyObject *o = POP();
        Py_XDECREF(o);
    }
    f->f_stackdepth = 0;
    f->f_state = FRAME_RAISED;
exiting:
    if (trace_info.cframe.use_tracing) {
        if (tstate->c_tracefunc) {
            if (call_trace_protected(tstate->c_tracefunc, tstate->c_traceobj,
                                     tstate, f, &trace_info, PyTrace_RETURN, retval)) {
                Py_CLEAR(retval);
            }
        }
        if (tstate->c_profilefunc) {
            if (call_trace_protected(tstate->c_profilefunc, tstate->c_profileobj,
                                     tstate, f, &trace_info, PyTrace_RETURN, retval)) {
                Py_CLEAR(retval);
            }
        }
    }

    /* pop frame */
exit_eval_frame:
    /* Restore previous cframe */
    tstate->cframe = trace_info.cframe.previous;
    tstate->cframe->use_tracing = trace_info.cframe.use_tracing;

    if (profiled_instrs != 0) {
        _PyJIT_CountProfiledInstrs(f->f_code, profiled_instrs);
    }

    if (f->f_gen == NULL) {
        _PyShadowFrame_Pop(tstate, &shadow_frame);
    }

    if (PyDTrace_FUNCTION_RETURN_ENABLED())
        dtrace_function_return(f);
    _Py_LeaveRecursiveCall(tstate);
    tstate->frame = f->f_back;
    co->co_mutable->curcalls--;

    return _Py_CheckFunctionResult(tstate, NULL, retval, __func__);
}

static PyObject *
_CiStaticEval_Vector(PyThreadState *tstate, PyFrameConstructor *con,
               PyObject *locals,
               PyObject* const* args, size_t argcountf,
               PyObject *kwnames, int check_args);

PyObject *_Py_HOT_FUNCTION
Ci_PyFunction_CallStatic(PyFunctionObject *func,
                       PyObject* const* args,
                       Py_ssize_t nargsf,
                       PyObject *kwnames)
{
    assert(PyFunction_Check(func));
#ifdef Py_DEBUG
    PyCodeObject *co = (PyCodeObject *)func->func_code;

    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    assert(nargs == 0 || args != NULL);
#endif
    PyFrameConstructor *con = PyFunction_AS_FRAME_CONSTRUCTOR(func);
    PyThreadState *tstate = _PyThreadState_GET();
    assert(tstate != NULL);

    /* We are bound to a specific function that is known at compile time, and
     * all of the arguments are guaranteed to be provided */
#ifdef Py_DEBUG
    assert(co->co_argcount == nargs);
    assert(co->co_flags & CO_STATICALLY_COMPILED);
    assert(co->co_flags & CO_OPTIMIZED);
    assert(kwnames == NULL);
#endif

    return _CiStaticEval_Vector(tstate, con, NULL, args, nargsf, NULL, 0);
}

static int _Ci_CheckArgs(PyThreadState *tstate, PyFrameObject *f, PyCodeObject *co) {
    // In the future we can use co_extra to store the cached arg info
    PyObject **freevars = (f->f_localsplus + f->f_code->co_nlocals);
    PyObject **fastlocals = f->f_localsplus;
    if (co->co_mutable->shadow == NULL) {
        // This funciton hasn't been optimized yet, we'll do it the slow way.
        PyObject* checks = _PyClassLoader_GetCodeArgumentTypeDescrs(co);
        PyObject *local;
        PyObject *type_descr;
        PyTypeObject *type;
        int optional;
        int exact;
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(checks); i += 2) {
            local = PyTuple_GET_ITEM(checks, i);
            type_descr = PyTuple_GET_ITEM(checks, i + 1);
            long idx = PyLong_AsLong(local);
            PyObject *val;
            // Look in freevars if necessary
            if (idx < 0) {
                assert(!_PyErr_Occurred(tstate));
                val = PyCell_GET(freevars[-(idx + 1)]);
            } else {
                val = fastlocals[idx];
            }

            type = _PyClassLoader_ResolveType(type_descr, &optional, &exact);
            if (type == NULL) {
                return -1;
            }

            int primitive = _PyClassLoader_GetTypeCode(type);
            if (primitive == TYPED_BOOL) {
                optional = 0;
                Py_DECREF(type);
                type = &PyBool_Type;
                Py_INCREF(type);
            } else if (primitive <= TYPED_INT64) {
                optional = 0;
                Py_DECREF(type);
                type = &PyLong_Type;
                Py_INCREF(type);
            } else if (primitive == TYPED_DOUBLE) {
                optional = 0;
                Py_DECREF(type);
                type = &PyFloat_Type;
                Py_INCREF(type);
            } else {
                assert(primitive == TYPED_OBJECT);
            }

            if (!_PyObject_TypeCheckOptional(val, type, optional, exact)) {
                PyErr_Format(
                    PyExc_TypeError,
                    "%U expected '%s' for argument %U, got '%s'",
                    co->co_name,
                    type->tp_name,
                    idx < 0 ?
                        PyTuple_GetItem(co->co_cellvars, -(idx + 1)) :
                        PyTuple_GetItem(co->co_varnames, idx),
                    Py_TYPE(val)->tp_name);
                Py_DECREF(type);
                return -1;
            }

            Py_DECREF(type);

            if (primitive <= TYPED_INT64) {
                size_t value;
                if (!_PyClassLoader_OverflowCheck(val, primitive, &value)) {
                    PyErr_SetString(
                        PyExc_OverflowError,
                        "int overflow"
                    );
                    return -1;
                }
            }
        }
        return 0;
    }

    _PyTypedArgsInfo *checks = (_PyTypedArgsInfo *)co->co_mutable->shadow->arg_checks;
    if (checks == NULL) {
        // Shadow code is initialized, but we haven't cached the checks yet...
        checks = _PyClassLoader_GetTypedArgsInfo(co, 0);
        if (checks == NULL) {
            return -1;
        }
        co->co_mutable->shadow->arg_checks = (PyObject *)checks;
    }

    for (int i = 0; i < Py_SIZE(checks); i++) {
        _PyTypedArgInfo *check = &checks->tai_args[i];
        long idx = check->tai_argnum;
        PyObject *val;
        // Look in freevars if necessary
        if (idx < 0) {
            assert(!_PyErr_Occurred(tstate));
            val = PyCell_GET(freevars[-(idx + 1)]);
        } else {
            val = fastlocals[idx];
        }

        if (!_PyObject_TypeCheckOptional(val, check->tai_type, check->tai_optional, check->tai_exact)) {
            PyErr_Format(
                PyExc_TypeError,
                "%U expected '%s' for argument %U, got '%s'",
                co->co_name,
                check->tai_type->tp_name,
                idx < 0 ?
                    PyTuple_GetItem(co->co_cellvars, -(idx + 1)) :
                    PyTuple_GetItem(co->co_varnames, idx),
                Py_TYPE(val)->tp_name);
            return -1;
        }

        if (check->tai_primitive_type != TYPED_OBJECT) {
            size_t value;
            if (!_PyClassLoader_OverflowCheck(val, check->tai_primitive_type, &value)) {
                PyErr_SetString(
                    PyExc_OverflowError,
                    "int overflow"
                );

                return -1;
            }
        }
    }
    return 0;
}

static PyObject *
_CiStaticEval_Vector(PyThreadState *tstate, PyFrameConstructor *con,
               PyObject *locals,
               PyObject* const* args, size_t argcountf,
               PyObject *kwnames, int check_args)
{
    Py_ssize_t argcount = PyVectorcall_NARGS(argcountf);
    Py_ssize_t awaited = Ci_Py_AWAITED_CALL(argcountf);
    PyFrameObject *f = Cix_PyEval_MakeFrameVector(
        tstate, con, locals, args, argcount, kwnames);
    if (f == NULL) {
        return NULL;
    }

    PyCodeObject *co = (PyCodeObject*)con->fc_code;
    assert(co->co_flags & CO_STATICALLY_COMPILED);
    if (check_args && _Ci_CheckArgs(tstate, f, co) < 0) {
        Py_DECREF(f);
        return NULL;
    }

    const int co_flags = ((PyCodeObject *)con->fc_code)->co_flags;
    if (awaited && (co_flags & CO_COROUTINE)) {
        return _PyEval_EvalEagerCoro(tstate, f, f->f_code->co_name, con->fc_qualname);
    }
    if (co_flags & (CO_GENERATOR | CO_COROUTINE | CO_ASYNC_GENERATOR)) {
        return make_coro(con, f);
    }
    PyObject *retval = _PyEval_EvalFrame(tstate, f, 0);

    /* decref'ing the frame can cause __del__ methods to get invoked,
       which can call back into Python.  While we're done with the
       current Python frame (f), the associated C stack is still in use,
       so recursion_depth must be boosted for the duration.
    */
    if (Py_REFCNT(f) > 1) {
        Py_DECREF(f);
        _PyObject_GC_TRACK(f);
    }
    else {
        ++tstate->recursion_depth;
        Py_DECREF(f);
        --tstate->recursion_depth;
    }
    return retval;
}

PyObject *
Ci_StaticFunction_Vectorcall(PyObject *func, PyObject* const* stack,
                       size_t nargsf, PyObject *kwnames)
{
    assert(PyFunction_Check(func));
    PyFrameConstructor *f = PyFunction_AS_FRAME_CONSTRUCTOR(func);
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    Py_ssize_t awaited = Ci_Py_AWAITED_CALL(nargsf);
    assert(nargs >= 0);
    PyThreadState *tstate = _PyThreadState_GET();
    assert(nargs == 0 || stack != NULL);
    if (((PyCodeObject *)f->fc_code)->co_flags & CO_OPTIMIZED) {
        return _CiStaticEval_Vector(tstate, f, NULL, stack, nargs | awaited, kwnames, 1);
    }
    else {
        return _CiStaticEval_Vector(tstate, f, f->fc_globals, stack, nargs | awaited, kwnames, 1);
    }
}

static void
PyEntry_initnow(PyFunctionObject *func)
{
    // Check that func hasn't already been initialized.
    assert(func->vectorcall == (vectorcallfunc)PyEntry_LazyInit);
    if (((PyCodeObject *)func->func_code)->co_flags & CO_STATICALLY_COMPILED) {
        func->vectorcall = (vectorcallfunc)Ci_StaticFunction_Vectorcall;
    } else {
        func->vectorcall = (vectorcallfunc)_PyFunction_Vectorcall;
    }
}

PyObject *
PyEntry_LazyInit(PyFunctionObject *func,
                 PyObject **stack,
                 Py_ssize_t nargsf,
                 PyObject *kwnames)
{
  if (!_PyJIT_IsEnabled()) {
    PyEntry_initnow(func);
  } else {
    _PyJIT_Result result = _PyJIT_CompileFunction(func);
    if (result == PYJIT_RESULT_PYTHON_EXCEPTION) {
        return NULL;
    } else if (result != PYJIT_RESULT_OK) {
        PyEntry_initnow(func);
    }
  }
  assert(func->vectorcall != (vectorcallfunc)PyEntry_LazyInit);
  return func->vectorcall((PyObject *)func, stack, nargsf, kwnames);
}

static unsigned int count_calls(PyCodeObject* code) {
  // The interpreter will only increment up to the shadowcode threshold
  // PYSHADOW_INIT_THRESHOLD. After that, it will stop incrementing. If someone
  // sets -X jit-auto above the PYSHADOW_INIT_THRESHOLD, we still have to keep
  // counting.
  unsigned int ncalls = code->co_mutable->ncalls;
  if (ncalls > PYSHADOW_INIT_THRESHOLD) {
    ncalls++;
    code->co_mutable->ncalls = ncalls;
  }
  return ncalls;
}

static PyObject*
PyEntry_AutoJIT(PyFunctionObject *func,
                PyObject **stack,
                Py_ssize_t nargsf,
                PyObject *kwnames) {
    PyCodeObject* code = (PyCodeObject*)func->func_code;

    unsigned ncalls = count_calls(code);
    unsigned hot_threshold = _PyJIT_AutoJITThreshold();
    unsigned jit_threshold = hot_threshold + _PyJIT_AutoJITProfileThreshold();

    // If the function is found to be hot then register it to be profiled, and
    // enable interpreter profiling if it's not already enabled.
    if (ncalls == hot_threshold && hot_threshold != jit_threshold) {
      _PyJIT_MarkProfilingCandidate(code);
      PyThreadState *tstate = _PyThreadState_GET();
      if (!tstate->profile_interp) {
        tstate->profile_interp = 1;
        tstate->cframe->use_tracing = _Py_ThreadStateHasTracing(tstate);
      }
    }

    if (ncalls <= jit_threshold) {
      return _PyFunction_Vectorcall((PyObject *)func, stack, nargsf, kwnames);
    }

    // Function is about to be compiled, can stop profiling it now.  Disable
    // interpreter profiling if this is the last profiling candidate and we're
    // not profiling all bytecodes globally.
    if (hot_threshold != jit_threshold) {
      _PyJIT_UnmarkProfilingCandidate(code);
      PyThreadState *tstate = _PyThreadState_GET();
      if (tstate->profile_interp &&
          tstate->interp->ceval.profile_instr_period == 0 &&
          _PyJIT_NumProfilingCandidates() == 0) {
        tstate->profile_interp = 0;
        tstate->cframe->use_tracing = _Py_ThreadStateHasTracing(tstate);
      }
    }

    _PyJIT_Result result = _PyJIT_CompileFunction(func);
    if (result == PYJIT_RESULT_PYTHON_EXCEPTION) {
        return NULL;
    } else if (result != PYJIT_RESULT_OK) {
      func->vectorcall = (vectorcallfunc)PyEntry_LazyInit;
      PyEntry_initnow(func);
    }
    assert(func->vectorcall != (vectorcallfunc)PyEntry_AutoJIT);
    return func->vectorcall((PyObject *)func, stack, nargsf, kwnames);
}

void
PyEntry_init(PyFunctionObject *func)
{
  assert(!_PyJIT_IsCompiled(func));
  if (_PyJIT_IsAutoJITEnabled()) {
    func->vectorcall = (vectorcallfunc)PyEntry_AutoJIT;
    return;
  }
  func->vectorcall = (vectorcallfunc)PyEntry_LazyInit;
  if (!_PyJIT_RegisterFunction(func)) {
    PyEntry_initnow(func);
  }
}

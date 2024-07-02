#ifdef _Py_TIER2

#include "Python.h"
#include "opcode.h"
#include "pycore_interp.h"
#include "pycore_backoff.h"
#include "pycore_bitutils.h"        // _Py_popcount32()
#include "pycore_object.h"          // _PyObject_GC_UNTRACK()
#include "pycore_opcode_metadata.h" // _PyOpcode_OpName[]
#include "pycore_opcode_utils.h"  // MAX_REAL_OPCODE
#include "pycore_optimizer.h"     // _Py_uop_analyze_and_optimize()
#include "pycore_pystate.h"       // _PyInterpreterState_GET()
#include "pycore_uop_ids.h"
#include "pycore_jit.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define NEED_OPCODE_METADATA
#include "pycore_uop_metadata.h" // Uop tables
#undef NEED_OPCODE_METADATA

#define MAX_EXECUTORS_SIZE 256

#ifdef Py_DEBUG
static int
base_opcode(PyCodeObject *code, int offset)
{
    int opcode = _Py_GetBaseOpcode(code, offset);
    if (opcode == ENTER_EXECUTOR) {
        int oparg = _PyCode_CODE(code)[offset].op.arg;
        _PyExecutorObject *ex = code->co_executors->executors[oparg];
        return ex->vm_data.opcode;
    }
    return opcode;
}
#endif

static bool
has_space_for_executor(PyCodeObject *code, _Py_CODEUNIT *instr)
{
    if (instr->op.code == ENTER_EXECUTOR) {
        return true;
    }
    if (code->co_executors == NULL) {
        return true;
    }
    return code->co_executors->size < MAX_EXECUTORS_SIZE;
}

static int32_t
get_index_for_executor(PyCodeObject *code, _Py_CODEUNIT *instr)
{
    if (instr->op.code == ENTER_EXECUTOR) {
        return instr->op.arg;
    }
    _PyExecutorArray *old = code->co_executors;
    int size = 0;
    int capacity = 0;
    if (old != NULL) {
        size = old->size;
        capacity = old->capacity;
        assert(size < MAX_EXECUTORS_SIZE);
    }
    assert(size <= capacity);
    if (size == capacity) {
        /* Array is full. Grow array */
        int new_capacity = capacity ? capacity * 2 : 4;
        _PyExecutorArray *new = PyMem_Realloc(
            old,
            offsetof(_PyExecutorArray, executors) +
            new_capacity * sizeof(_PyExecutorObject *));
        if (new == NULL) {
            return -1;
        }
        new->capacity = new_capacity;
        new->size = size;
        code->co_executors = new;
    }
    assert(size < code->co_executors->capacity);
    return size;
}

static void
insert_executor(PyCodeObject *code, _Py_CODEUNIT *instr, int index, _PyExecutorObject *executor)
{
    Py_INCREF(executor);
    if (instr->op.code == ENTER_EXECUTOR) {
        assert(index == instr->op.arg);
        _Py_ExecutorDetach(code->co_executors->executors[index]);
    }
    else {
        assert(code->co_executors->size == index);
        assert(code->co_executors->capacity > index);
        code->co_executors->size++;
    }
    executor->vm_data.opcode = instr->op.code;
    executor->vm_data.oparg = instr->op.arg;
    executor->vm_data.code = code;
    executor->vm_data.index = (int)(instr - _PyCode_CODE(code));
    code->co_executors->executors[index] = executor;
    assert(index < MAX_EXECUTORS_SIZE);
    instr->op.code = ENTER_EXECUTOR;
    instr->op.arg = index;
}


static int
never_optimize(
    _PyOptimizerObject* self,
    _PyInterpreterFrame *frame,
    _Py_CODEUNIT *instr,
    _PyExecutorObject **exec,
    int Py_UNUSED(stack_entries))
{
    // This may be called if the optimizer is reset
    return 0;
}

PyTypeObject _PyDefaultOptimizer_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "noop_optimizer",
    .tp_basicsize = sizeof(_PyOptimizerObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
};

static _PyOptimizerObject _PyOptimizer_Default = {
    PyObject_HEAD_INIT(&_PyDefaultOptimizer_Type)
    .optimize = never_optimize,
};

_PyOptimizerObject *
_Py_GetOptimizer(void)
{
    PyInterpreterState *interp = _PyInterpreterState_GET();
    if (interp->optimizer == &_PyOptimizer_Default) {
        return NULL;
    }
    Py_INCREF(interp->optimizer);
    return interp->optimizer;
}

static _PyExecutorObject *
make_executor_from_uops(_PyUOpInstruction *buffer, int length, const _PyBloomFilter *dependencies);

static int
init_cold_exit_executor(_PyExecutorObject *executor, int oparg);

/* It is impossible for the number of exits to reach 1/4 of the total length,
 * as the number of exits cannot reach 1/3 of the number of non-exits, due to
 * the presence of CHECK_VALIDITY checks and instructions to produce the values
 * being checked in exits. */
#define COLD_EXIT_COUNT (UOP_MAX_TRACE_LENGTH/4)

static int cold_exits_initialized = 0;
static _PyExecutorObject COLD_EXITS[COLD_EXIT_COUNT] = { 0 };

static const _PyBloomFilter EMPTY_FILTER = { 0 };

_PyOptimizerObject *
_Py_SetOptimizer(PyInterpreterState *interp, _PyOptimizerObject *optimizer)
{
    if (optimizer == NULL) {
        optimizer = &_PyOptimizer_Default;
    }
    else if (cold_exits_initialized == 0) {
        cold_exits_initialized = 1;
        for (int i = 0; i < COLD_EXIT_COUNT; i++) {
            if (init_cold_exit_executor(&COLD_EXITS[i], i)) {
                return NULL;
            }
        }
    }
    _PyOptimizerObject *old = interp->optimizer;
    if (old == NULL) {
        old = &_PyOptimizer_Default;
    }
    Py_INCREF(optimizer);
    interp->optimizer = optimizer;
    return old;
}

int
_Py_SetTier2Optimizer(_PyOptimizerObject *optimizer)
{
    PyInterpreterState *interp = _PyInterpreterState_GET();
    _PyOptimizerObject *old = _Py_SetOptimizer(interp, optimizer);
    Py_XDECREF(old);
    return old == NULL ? -1 : 0;
}

/* Returns 1 if optimized, 0 if not optimized, and -1 for an error.
 * If optimized, *executor_ptr contains a new reference to the executor
 */
int
_PyOptimizer_Optimize(
    _PyInterpreterFrame *frame, _Py_CODEUNIT *start,
    PyObject **stack_pointer, _PyExecutorObject **executor_ptr)
{
    PyCodeObject *code = _PyFrame_GetCode(frame);
    assert(PyCode_Check(code));
    PyInterpreterState *interp = _PyInterpreterState_GET();
    if (!has_space_for_executor(code, start)) {
        return 0;
    }
    _PyOptimizerObject *opt = interp->optimizer;
    int err = opt->optimize(opt, frame, start, executor_ptr, (int)(stack_pointer - _PyFrame_Stackbase(frame)));
    if (err <= 0) {
        return err;
    }
    assert(*executor_ptr != NULL);
    int index = get_index_for_executor(code, start);
    if (index < 0) {
        /* Out of memory. Don't raise and assume that the
         * error will show up elsewhere.
         *
         * If an optimizer has already produced an executor,
         * it might get confused by the executor disappearing,
         * but there is not much we can do about that here. */
        Py_DECREF(*executor_ptr);
        return 0;
    }
    insert_executor(code, start, index, *executor_ptr);
    assert((*executor_ptr)->vm_data.valid);
    return 1;
}

_PyExecutorObject *
_Py_GetExecutor(PyCodeObject *code, int offset)
{
    int code_len = (int)Py_SIZE(code);
    for (int i = 0 ; i < code_len;) {
        if (_PyCode_CODE(code)[i].op.code == ENTER_EXECUTOR && i*2 == offset) {
            int oparg = _PyCode_CODE(code)[i].op.arg;
            _PyExecutorObject *res = code->co_executors->executors[oparg];
            Py_INCREF(res);
            return res;
        }
        i += _PyInstruction_GetLength(code, i);
    }
    PyErr_SetString(PyExc_ValueError, "no executor at given byte offset");
    return NULL;
}

static PyObject *
is_valid(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    return PyBool_FromLong(((_PyExecutorObject *)self)->vm_data.valid);
}

static PyObject *
get_opcode(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    return PyLong_FromUnsignedLong(((_PyExecutorObject *)self)->vm_data.opcode);
}

static PyObject *
get_oparg(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    return PyLong_FromUnsignedLong(((_PyExecutorObject *)self)->vm_data.oparg);
}

static PyMethodDef executor_methods[] = {
    { "is_valid", is_valid, METH_NOARGS, NULL },
    { "get_opcode", get_opcode, METH_NOARGS, NULL },
    { "get_oparg", get_oparg, METH_NOARGS, NULL },
    { NULL, NULL },
};

///////////////////// Experimental UOp Optimizer /////////////////////

static int executor_clear(_PyExecutorObject *executor);
static void unlink_executor(_PyExecutorObject *executor);

static void
uop_dealloc(_PyExecutorObject *self) {
    _PyObject_GC_UNTRACK(self);
    assert(self->vm_data.code == NULL);
    unlink_executor(self);
#ifdef _Py_JIT
    _PyJIT_Free(self);
#endif
    PyObject_GC_Del(self);
}

const char *
_PyUOpName(int index)
{
    if (index < 0 || index > MAX_UOP_ID) {
        return NULL;
    }
    return _PyOpcode_uop_name[index];
}

#ifdef Py_DEBUG
void
_PyUOpPrint(const _PyUOpInstruction *uop)
{
    const char *name = _PyUOpName(uop->opcode);
    if (name == NULL) {
        printf("<uop %d>", uop->opcode);
    }
    else {
        printf("%s", name);
    }
    switch(uop->format) {
        case UOP_FORMAT_TARGET:
            printf(" (%d, target=%d, operand=%#" PRIx64,
                uop->oparg,
                uop->target,
                (uint64_t)uop->operand);
            break;
        case UOP_FORMAT_JUMP:
            printf(" (%d, jump_target=%d, operand=%#" PRIx64,
                uop->oparg,
                uop->jump_target,
                (uint64_t)uop->operand);
            break;
        case UOP_FORMAT_EXIT:
            printf(" (%d, exit_index=%d, operand=%#" PRIx64,
                uop->oparg,
                uop->exit_index,
                (uint64_t)uop->operand);
            break;
        default:
            printf(" (%d, Unknown format)", uop->oparg);
    }
    if (_PyUop_Flags[uop->opcode] & HAS_ERROR_FLAG) {
        printf(", error_target=%d", uop->error_target);
    }

    printf(")");
}
#endif

static Py_ssize_t
uop_len(_PyExecutorObject *self)
{
    return self->code_size;
}

static PyObject *
uop_item(_PyExecutorObject *self, Py_ssize_t index)
{
    Py_ssize_t len = uop_len(self);
    if (index < 0 || index >= len) {
        PyErr_SetNone(PyExc_IndexError);
        return NULL;
    }
    const char *name = _PyUOpName(self->trace[index].opcode);
    if (name == NULL) {
        name = "<nil>";
    }
    PyObject *oname = _PyUnicode_FromASCII(name, strlen(name));
    if (oname == NULL) {
        return NULL;
    }
    PyObject *oparg = PyLong_FromUnsignedLong(self->trace[index].oparg);
    if (oparg == NULL) {
        Py_DECREF(oname);
        return NULL;
    }
    PyObject *target = PyLong_FromUnsignedLong(self->trace[index].target);
    if (oparg == NULL) {
        Py_DECREF(oparg);
        Py_DECREF(oname);
        return NULL;
    }
    PyObject *operand = PyLong_FromUnsignedLongLong(self->trace[index].operand);
    if (operand == NULL) {
        Py_DECREF(target);
        Py_DECREF(oparg);
        Py_DECREF(oname);
        return NULL;
    }
    PyObject *args[4] = { oname, oparg, target, operand };
    return _PyTuple_FromArraySteal(args, 4);
}

PySequenceMethods uop_as_sequence = {
    .sq_length = (lenfunc)uop_len,
    .sq_item = (ssizeargfunc)uop_item,
};

static int
executor_traverse(PyObject *o, visitproc visit, void *arg)
{
    _PyExecutorObject *executor = (_PyExecutorObject *)o;
    for (uint32_t i = 0; i < executor->exit_count; i++) {
        Py_VISIT(executor->exits[i].executor);
    }
    return 0;
}

static PyObject *
get_jit_code(PyObject *self, PyObject *Py_UNUSED(ignored))
{
#ifndef _Py_JIT
    PyErr_SetString(PyExc_RuntimeError, "JIT support not enabled.");
    return NULL;
#else
    _PyExecutorObject *executor = (_PyExecutorObject *)self;
    if (executor->jit_code == NULL || executor->jit_size == 0) {
        Py_RETURN_NONE;
    }
    return PyBytes_FromStringAndSize(executor->jit_code, executor->jit_size);
#endif
}

static PyMethodDef uop_executor_methods[] = {
    { "is_valid", is_valid, METH_NOARGS, NULL },
    { "get_jit_code", get_jit_code, METH_NOARGS, NULL},
    { "get_opcode", get_opcode, METH_NOARGS, NULL },
    { "get_oparg", get_oparg, METH_NOARGS, NULL },
    { NULL, NULL },
};

static int
executor_is_gc(PyObject *o)
{
    return !_Py_IsImmortal(o);
}

PyTypeObject _PyUOpExecutor_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "uop_executor",
    .tp_basicsize = offsetof(_PyExecutorObject, exits),
    .tp_itemsize = 1,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION | Py_TPFLAGS_HAVE_GC,
    .tp_dealloc = (destructor)uop_dealloc,
    .tp_as_sequence = &uop_as_sequence,
    .tp_methods = uop_executor_methods,
    .tp_traverse = executor_traverse,
    .tp_clear = (inquiry)executor_clear,
    .tp_is_gc = executor_is_gc,
};

/* TO DO -- Generate these tables */
static const uint16_t
_PyUOp_Replacements[MAX_UOP_ID + 1] = {
    [_ITER_JUMP_RANGE] = _GUARD_NOT_EXHAUSTED_RANGE,
    [_ITER_JUMP_LIST] = _GUARD_NOT_EXHAUSTED_LIST,
    [_ITER_JUMP_TUPLE] = _GUARD_NOT_EXHAUSTED_TUPLE,
    [_FOR_ITER] = _FOR_ITER_TIER_TWO,
};

static const uint8_t
is_for_iter_test[MAX_UOP_ID + 1] = {
    [_GUARD_NOT_EXHAUSTED_RANGE] = 1,
    [_GUARD_NOT_EXHAUSTED_LIST] = 1,
    [_GUARD_NOT_EXHAUSTED_TUPLE] = 1,
    [_FOR_ITER_TIER_TWO] = 1,
};

static const uint16_t
BRANCH_TO_GUARD[4][2] = {
    [POP_JUMP_IF_FALSE - POP_JUMP_IF_FALSE][0] = _GUARD_IS_TRUE_POP,
    [POP_JUMP_IF_FALSE - POP_JUMP_IF_FALSE][1] = _GUARD_IS_FALSE_POP,
    [POP_JUMP_IF_TRUE - POP_JUMP_IF_FALSE][0] = _GUARD_IS_FALSE_POP,
    [POP_JUMP_IF_TRUE - POP_JUMP_IF_FALSE][1] = _GUARD_IS_TRUE_POP,
    [POP_JUMP_IF_NONE - POP_JUMP_IF_FALSE][0] = _GUARD_IS_NOT_NONE_POP,
    [POP_JUMP_IF_NONE - POP_JUMP_IF_FALSE][1] = _GUARD_IS_NONE_POP,
    [POP_JUMP_IF_NOT_NONE - POP_JUMP_IF_FALSE][0] = _GUARD_IS_NONE_POP,
    [POP_JUMP_IF_NOT_NONE - POP_JUMP_IF_FALSE][1] = _GUARD_IS_NOT_NONE_POP,
};


#define CONFIDENCE_RANGE 1000
#define CONFIDENCE_CUTOFF 333

#ifdef Py_DEBUG
#define DPRINTF(level, ...) \
    if (lltrace >= (level)) { printf(__VA_ARGS__); }
#else
#define DPRINTF(level, ...)
#endif


static inline int
add_to_trace(
    _PyUOpInstruction *trace,
    int trace_length,
    uint16_t opcode,
    uint16_t oparg,
    uint64_t operand,
    uint32_t target)
{
    trace[trace_length].opcode = opcode;
    trace[trace_length].format = UOP_FORMAT_TARGET;
    trace[trace_length].target = target;
    trace[trace_length].oparg = oparg;
    trace[trace_length].operand = operand;
    return trace_length + 1;
}

#ifdef Py_DEBUG
#define ADD_TO_TRACE(OPCODE, OPARG, OPERAND, TARGET) \
    assert(trace_length < max_length); \
    trace_length = add_to_trace(trace, trace_length, (OPCODE), (OPARG), (OPERAND), (TARGET)); \
    if (lltrace >= 2) { \
        printf("%4d ADD_TO_TRACE: ", trace_length); \
        _PyUOpPrint(&trace[trace_length-1]); \
        printf("\n"); \
    }
#else
#define ADD_TO_TRACE(OPCODE, OPARG, OPERAND, TARGET) \
    assert(trace_length < max_length); \
    trace_length = add_to_trace(trace, trace_length, (OPCODE), (OPARG), (OPERAND), (TARGET));
#endif

#define INSTR_IP(INSTR, CODE) \
    ((uint32_t)((INSTR) - ((_Py_CODEUNIT *)(CODE)->co_code_adaptive)))

// Reserve space for n uops
#define RESERVE_RAW(n, opname) \
    if (trace_length + (n) > max_length) { \
        DPRINTF(2, "No room for %s (need %d, got %d)\n", \
                (opname), (n), max_length - trace_length); \
        OPT_STAT_INC(trace_too_long); \
        goto done; \
    }

// Reserve space for N uops, plus 3 for _SET_IP, _CHECK_VALIDITY and _EXIT_TRACE
#define RESERVE(needed) RESERVE_RAW((needed) + 3, _PyUOpName(opcode))

// Trace stack operations (used by _PUSH_FRAME, _POP_FRAME)
#define TRACE_STACK_PUSH() \
    if (trace_stack_depth >= TRACE_STACK_SIZE) { \
        DPRINTF(2, "Trace stack overflow\n"); \
        OPT_STAT_INC(trace_stack_overflow); \
        trace_length = 0; \
        goto done; \
    } \
    assert(func == NULL || func->func_code == (PyObject *)code); \
    trace_stack[trace_stack_depth].func = func; \
    trace_stack[trace_stack_depth].code = code; \
    trace_stack[trace_stack_depth].instr = instr; \
    trace_stack_depth++;
#define TRACE_STACK_POP() \
    if (trace_stack_depth <= 0) { \
        Py_FatalError("Trace stack underflow\n"); \
    } \
    trace_stack_depth--; \
    func = trace_stack[trace_stack_depth].func; \
    code = trace_stack[trace_stack_depth].code; \
    assert(func == NULL || func->func_code == (PyObject *)code); \
    instr = trace_stack[trace_stack_depth].instr;

/* Returns the length of the trace on success,
 * 0 if it failed to produce a worthwhile trace,
 * and -1 on an error.
 */
static int
translate_bytecode_to_trace(
    _PyInterpreterFrame *frame,
    _Py_CODEUNIT *instr,
    _PyUOpInstruction *trace,
    int buffer_size,
    _PyBloomFilter *dependencies)
{
    bool progress_needed = true;
    PyCodeObject *code = _PyFrame_GetCode(frame);
    PyFunctionObject *func = (PyFunctionObject *)frame->f_funcobj;
    assert(PyFunction_Check(func));
    PyCodeObject *initial_code = code;
    _Py_BloomFilter_Add(dependencies, initial_code);
    _Py_CODEUNIT *initial_instr = instr;
    int trace_length = 0;
    // Leave space for possible trailing _EXIT_TRACE
    int max_length = buffer_size-2;
    struct {
        PyFunctionObject *func;
        PyCodeObject *code;
        _Py_CODEUNIT *instr;
    } trace_stack[TRACE_STACK_SIZE];
    int trace_stack_depth = 0;
    int confidence = CONFIDENCE_RANGE;  // Adjusted by branch instructions

#ifdef Py_DEBUG
    char *python_lltrace = Py_GETENV("PYTHON_LLTRACE");
    int lltrace = 0;
    if (python_lltrace != NULL && *python_lltrace >= '0') {
        lltrace = *python_lltrace - '0';  // TODO: Parse an int and all that
    }
#endif

    DPRINTF(2,
            "Optimizing %s (%s:%d) at byte offset %d\n",
            PyUnicode_AsUTF8(code->co_qualname),
            PyUnicode_AsUTF8(code->co_filename),
            code->co_firstlineno,
            2 * INSTR_IP(initial_instr, code));
    ADD_TO_TRACE(_START_EXECUTOR, 0, (uintptr_t)instr, INSTR_IP(instr, code));
    uint32_t target = 0;

top:  // Jump here after _PUSH_FRAME or likely branches
    for (;;) {
        target = INSTR_IP(instr, code);
        // Need space for _DEOPT
        max_length--;

        uint32_t opcode = instr->op.code;
        uint32_t oparg = instr->op.arg;

        DPRINTF(2, "%d: %s(%d)\n", target, _PyOpcode_OpName[opcode], oparg);

        if (opcode == ENTER_EXECUTOR) {
            assert(oparg < 256);
            _PyExecutorObject *executor = code->co_executors->executors[oparg];
            opcode = executor->vm_data.opcode;
            DPRINTF(2, "  * ENTER_EXECUTOR -> %s\n",  _PyOpcode_OpName[opcode]);
            oparg = executor->vm_data.oparg;
        }

        if (opcode == EXTENDED_ARG) {
            instr++;
            opcode = instr->op.code;
            oparg = (oparg << 8) | instr->op.arg;
            if (opcode == EXTENDED_ARG) {
                instr--;
                goto done;
            }
        }
        assert(opcode != ENTER_EXECUTOR && opcode != EXTENDED_ARG);
        RESERVE_RAW(2, "_CHECK_VALIDITY_AND_SET_IP");
        ADD_TO_TRACE(_CHECK_VALIDITY_AND_SET_IP, 0, (uintptr_t)instr, target);

        /* Special case the first instruction,
         * so that we can guarantee forward progress */
        if (progress_needed) {
            progress_needed = false;
            if (opcode == JUMP_BACKWARD || opcode == JUMP_BACKWARD_NO_INTERRUPT) {
                instr += 1 + _PyOpcode_Caches[opcode] - (int32_t)oparg;
                initial_instr = instr;
                if (opcode == JUMP_BACKWARD) {
                    ADD_TO_TRACE(_TIER2_RESUME_CHECK, 0, 0, target);
                }
                continue;
            }
            else {
                if (OPCODE_HAS_EXIT(opcode) || OPCODE_HAS_DEOPT(opcode)) {
                    opcode = _PyOpcode_Deopt[opcode];
                }
                assert(!OPCODE_HAS_EXIT(opcode));
                assert(!OPCODE_HAS_DEOPT(opcode));
            }
        }

        if (OPCODE_HAS_EXIT(opcode)) {
            // Make space for exit code
            max_length--;
        }
        if (OPCODE_HAS_ERROR(opcode)) {
            // Make space for error code
            max_length--;
        }
        switch (opcode) {
            case POP_JUMP_IF_NONE:
            case POP_JUMP_IF_NOT_NONE:
            case POP_JUMP_IF_FALSE:
            case POP_JUMP_IF_TRUE:
            {
                RESERVE(1);
                int counter = instr[1].cache;
                int bitcount = _Py_popcount32(counter);
                int jump_likely = bitcount > 8;
                /* If bitcount is 8 (half the jumps were taken), adjust confidence by 50%.
                   If it's 16 or 0 (all or none were taken), adjust by 10%
                   (since the future is still somewhat uncertain).
                   For values in between, adjust proportionally. */
                if (jump_likely) {
                    confidence = confidence * (bitcount + 2) / 20;
                }
                else {
                    confidence = confidence * (18 - bitcount) / 20;
                }
                uint32_t uopcode = BRANCH_TO_GUARD[opcode - POP_JUMP_IF_FALSE][jump_likely];
                DPRINTF(2, "%d: %s(%d): counter=%04x, bitcount=%d, likely=%d, confidence=%d, uopcode=%s\n",
                        target, _PyOpcode_OpName[opcode], oparg,
                        counter, bitcount, jump_likely, confidence, _PyUOpName(uopcode));
                if (confidence < CONFIDENCE_CUTOFF) {
                    DPRINTF(2, "Confidence too low (%d < %d)\n", confidence, CONFIDENCE_CUTOFF);
                    OPT_STAT_INC(low_confidence);
                    goto done;
                }
                _Py_CODEUNIT *next_instr = instr + 1 + _PyOpcode_Caches[_PyOpcode_Deopt[opcode]];
                _Py_CODEUNIT *target_instr = next_instr + oparg;
                if (jump_likely) {
                    DPRINTF(2, "Jump likely (%04x = %d bits), continue at byte offset %d\n",
                            instr[1].cache, bitcount, 2 * INSTR_IP(target_instr, code));
                    instr = target_instr;
                    ADD_TO_TRACE(uopcode, 0, 0, INSTR_IP(next_instr, code));
                    goto top;
                }
                ADD_TO_TRACE(uopcode, 0, 0, INSTR_IP(target_instr, code));
                break;
            }

            case JUMP_BACKWARD:
            case JUMP_BACKWARD_NO_INTERRUPT:
            {
                _Py_CODEUNIT *target = instr + 1 + _PyOpcode_Caches[opcode] - (int)oparg;
                if (target == initial_instr) {
                    /* We have looped round to the start */
                    RESERVE(1);
                    ADD_TO_TRACE(_JUMP_TO_TOP, 0, 0, 0);
                }
                else {
                    OPT_STAT_INC(inner_loop);
                    DPRINTF(2, "JUMP_BACKWARD not to top ends trace\n");
                }
                goto done;
            }

            case JUMP_FORWARD:
            {
                RESERVE(0);
                // This will emit two _SET_IP instructions; leave it to the optimizer
                instr += oparg;
                break;
            }

            case RESUME:
                /* Use a special tier 2 version of RESUME_CHECK to allow traces to
                 *  start with RESUME_CHECK */
                ADD_TO_TRACE(_TIER2_RESUME_CHECK, 0, 0, target);
                break;

            default:
            {
                const struct opcode_macro_expansion *expansion = &_PyOpcode_macro_expansion[opcode];
                if (expansion->nuops > 0) {
                    // Reserve space for nuops (+ _SET_IP + _EXIT_TRACE)
                    int nuops = expansion->nuops;
                    RESERVE(nuops + 1); /* One extra for exit */
                    int16_t last_op = expansion->uops[nuops-1].uop;
                    if (last_op == _POP_FRAME || last_op == _RETURN_GENERATOR || last_op == _YIELD_VALUE) {
                        // Check for trace stack underflow now:
                        // We can't bail e.g. in the middle of
                        // LOAD_CONST + _POP_FRAME.
                        if (trace_stack_depth == 0) {
                            DPRINTF(2, "Trace stack underflow\n");
                            OPT_STAT_INC(trace_stack_underflow);
                            goto done;
                        }
                    }
                    uint32_t orig_oparg = oparg;  // For OPARG_TOP/BOTTOM
                    for (int i = 0; i < nuops; i++) {
                        oparg = orig_oparg;
                        uint32_t uop = expansion->uops[i].uop;
                        uint64_t operand = 0;
                        // Add one to account for the actual opcode/oparg pair:
                        int offset = expansion->uops[i].offset + 1;
                        switch (expansion->uops[i].size) {
                            case OPARG_FULL:
                                assert(opcode != JUMP_BACKWARD_NO_INTERRUPT && opcode != JUMP_BACKWARD);
                                break;
                            case OPARG_CACHE_1:
                                operand = read_u16(&instr[offset].cache);
                                break;
                            case OPARG_CACHE_2:
                                operand = read_u32(&instr[offset].cache);
                                break;
                            case OPARG_CACHE_4:
                                operand = read_u64(&instr[offset].cache);
                                break;
                            case OPARG_TOP:  // First half of super-instr
                                oparg = orig_oparg >> 4;
                                break;
                            case OPARG_BOTTOM:  // Second half of super-instr
                                oparg = orig_oparg & 0xF;
                                break;
                            case OPARG_SAVE_RETURN_OFFSET:  // op=_SAVE_RETURN_OFFSET; oparg=return_offset
                                oparg = offset;
                                assert(uop == _SAVE_RETURN_OFFSET);
                                break;
                            case OPARG_REPLACED:
                                uop = _PyUOp_Replacements[uop];
                                assert(uop != 0);
#ifdef Py_DEBUG
                                {
                                    uint32_t next_inst = target + 1 + INLINE_CACHE_ENTRIES_FOR_ITER + (oparg > 255);
                                    uint32_t jump_target = next_inst + oparg;
                                    assert(base_opcode(code, jump_target) == END_FOR ||
                                        base_opcode(code, jump_target) == INSTRUMENTED_END_FOR);
                                    assert(base_opcode(code, jump_target+1) == POP_TOP);
                                }
#endif
                                break;
                            default:
                                fprintf(stderr,
                                        "opcode=%d, oparg=%d; nuops=%d, i=%d; size=%d, offset=%d\n",
                                        opcode, oparg, nuops, i,
                                        expansion->uops[i].size,
                                        expansion->uops[i].offset);
                                Py_FatalError("garbled expansion");
                        }

                        if (uop == _POP_FRAME || uop == _RETURN_GENERATOR || uop == _YIELD_VALUE) {
                            TRACE_STACK_POP();
                            /* Set the operand to the function or code object returned to,
                             * to assist optimization passes. (See _PUSH_FRAME below.)
                             */
                            if (func != NULL) {
                                operand = (uintptr_t)func;
                            }
                            else if (code != NULL) {
                                operand = (uintptr_t)code | 1;
                            }
                            else {
                                operand = 0;
                            }
                            ADD_TO_TRACE(uop, oparg, operand, target);
                            DPRINTF(2,
                                "Returning to %s (%s:%d) at byte offset %d\n",
                                PyUnicode_AsUTF8(code->co_qualname),
                                PyUnicode_AsUTF8(code->co_filename),
                                code->co_firstlineno,
                                2 * INSTR_IP(instr, code));
                            goto top;
                        }

                        if (uop == _PUSH_FRAME) {
                            assert(i + 1 == nuops);
                            int func_version_offset =
                                offsetof(_PyCallCache, func_version)/sizeof(_Py_CODEUNIT)
                                // Add one to account for the actual opcode/oparg pair:
                                + 1;
                            uint32_t func_version = read_u32(&instr[func_version_offset].cache);
                            PyCodeObject *new_code = NULL;
                            PyFunctionObject *new_func =
                                _PyFunction_LookupByVersion(func_version, (PyObject **) &new_code);
                            DPRINTF(2, "Function: version=%#x; new_func=%p, new_code=%p\n",
                                    (int)func_version, new_func, new_code);
                            if (new_code != NULL) {
                                if (new_code == code) {
                                    // Recursive call, bail (we could be here forever).
                                    DPRINTF(2, "Bailing on recursive call to %s (%s:%d)\n",
                                            PyUnicode_AsUTF8(new_code->co_qualname),
                                            PyUnicode_AsUTF8(new_code->co_filename),
                                            new_code->co_firstlineno);
                                    OPT_STAT_INC(recursive_call);
                                    ADD_TO_TRACE(uop, oparg, 0, target);
                                    ADD_TO_TRACE(_EXIT_TRACE, 0, 0, 0);
                                    goto done;
                                }
                                if (new_code->co_version != func_version) {
                                    // func.__code__ was updated.
                                    // Perhaps it may happen again, so don't bother tracing.
                                    // TODO: Reason about this -- is it better to bail or not?
                                    DPRINTF(2, "Bailing because co_version != func_version\n");
                                    ADD_TO_TRACE(uop, oparg, 0, target);
                                    ADD_TO_TRACE(_EXIT_TRACE, 0, 0, 0);
                                    goto done;
                                }
                                if (opcode == FOR_ITER_GEN) {
                                    DPRINTF(2, "Bailing due to dynamic target\n");
                                    ADD_TO_TRACE(uop, oparg, 0, target);
                                    ADD_TO_TRACE(_DYNAMIC_EXIT, 0, 0, 0);
                                    goto done;
                                }
                                // Increment IP to the return address
                                instr += _PyOpcode_Caches[_PyOpcode_Deopt[opcode]] + 1;
                                TRACE_STACK_PUSH();
                                _Py_BloomFilter_Add(dependencies, new_code);
                                /* Set the operand to the callee's function or code object,
                                 * to assist optimization passes.
                                 * We prefer setting it to the function (for remove_globals())
                                 * but if that's not available but the code is available,
                                 * use the code, setting the low bit so the optimizer knows.
                                 */
                                if (new_func != NULL) {
                                    operand = (uintptr_t)new_func;
                                }
                                else if (new_code != NULL) {
                                    operand = (uintptr_t)new_code | 1;
                                }
                                else {
                                    operand = 0;
                                }
                                ADD_TO_TRACE(uop, oparg, operand, target);
                                code = new_code;
                                func = new_func;
                                instr = _PyCode_CODE(code);
                                DPRINTF(2,
                                    "Continuing in %s (%s:%d) at byte offset %d\n",
                                    PyUnicode_AsUTF8(code->co_qualname),
                                    PyUnicode_AsUTF8(code->co_filename),
                                    code->co_firstlineno,
                                    2 * INSTR_IP(instr, code));
                                goto top;
                            }
                            DPRINTF(2, "Bail, new_code == NULL\n");
                            ADD_TO_TRACE(uop, oparg, 0, target);
                            ADD_TO_TRACE(_DYNAMIC_EXIT, 0, 0, 0);
                            goto done;
                        }

                        // All other instructions
                        ADD_TO_TRACE(uop, oparg, operand, target);
                    }
                    break;
                }
                DPRINTF(2, "Unsupported opcode %s\n", _PyOpcode_OpName[opcode]);
                OPT_UNSUPPORTED_OPCODE(opcode);
                goto done;  // Break out of loop
            }  // End default

        }  // End switch (opcode)

        instr++;
        // Add cache size for opcode
        instr += _PyOpcode_Caches[_PyOpcode_Deopt[opcode]];
    }  // End for (;;)

done:
    while (trace_stack_depth > 0) {
        TRACE_STACK_POP();
    }
    assert(code == initial_code);
    // Skip short traces like _SET_IP, LOAD_FAST, _SET_IP, _EXIT_TRACE
    if (progress_needed || trace_length < 5) {
        OPT_STAT_INC(trace_too_short);
        DPRINTF(2,
                "No trace for %s (%s:%d) at byte offset %d (%s)\n",
                PyUnicode_AsUTF8(code->co_qualname),
                PyUnicode_AsUTF8(code->co_filename),
                code->co_firstlineno,
                2 * INSTR_IP(initial_instr, code),
                progress_needed ? "no progress" : "too short");
        return 0;
    }
    if (trace[trace_length-1].opcode != _JUMP_TO_TOP) {
        ADD_TO_TRACE(_EXIT_TRACE, 0, 0, target);
    }
    DPRINTF(1,
            "Created a proto-trace for %s (%s:%d) at byte offset %d -- length %d\n",
            PyUnicode_AsUTF8(code->co_qualname),
            PyUnicode_AsUTF8(code->co_filename),
            code->co_firstlineno,
            2 * INSTR_IP(initial_instr, code),
            trace_length);
    OPT_HIST(trace_length, trace_length_hist);
    return trace_length;
}

#undef RESERVE
#undef RESERVE_RAW
#undef INSTR_IP
#undef ADD_TO_TRACE
#undef DPRINTF

#define UNSET_BIT(array, bit) (array[(bit)>>5] &= ~(1<<((bit)&31)))
#define SET_BIT(array, bit) (array[(bit)>>5] |= (1<<((bit)&31)))
#define BIT_IS_SET(array, bit) (array[(bit)>>5] & (1<<((bit)&31)))

/* Count the number of unused uops and exits
*/
static int
count_exits(_PyUOpInstruction *buffer, int length)
{
    int exit_count = 0;
    for (int i = 0; i < length; i++) {
        int opcode = buffer[i].opcode;
        if (opcode == _EXIT_TRACE || opcode == _DYNAMIC_EXIT) {
            exit_count++;
        }
    }
    return exit_count;
}

static void make_exit(_PyUOpInstruction *inst, int opcode, int target)
{
    inst->opcode = opcode;
    inst->oparg = 0;
    inst->operand = 0;
    inst->format = UOP_FORMAT_TARGET;
    inst->target = target;
}

/* Convert implicit exits, errors and deopts
 * into explicit ones. */
static int
prepare_for_execution(_PyUOpInstruction *buffer, int length)
{
    int32_t current_jump = -1;
    int32_t current_jump_target = -1;
    int32_t current_error = -1;
    int32_t current_error_target = -1;
    int32_t current_popped = -1;
    int32_t current_exit_op = -1;
    /* Leaving in NOPs slows down the interpreter and messes up the stats */
    _PyUOpInstruction *copy_to = &buffer[0];
    for (int i = 0; i < length; i++) {
        _PyUOpInstruction *inst = &buffer[i];
        if (inst->opcode != _NOP) {
            if (copy_to != inst) {
                *copy_to = *inst;
            }
            copy_to++;
        }
    }
    length = (int)(copy_to - buffer);
    int next_spare = length;
    for (int i = 0; i < length; i++) {
        _PyUOpInstruction *inst = &buffer[i];
        int opcode = inst->opcode;
        int32_t target = (int32_t)uop_get_target(inst);
        if (_PyUop_Flags[opcode] & (HAS_EXIT_FLAG | HAS_DEOPT_FLAG)) {
            uint16_t exit_op = (_PyUop_Flags[opcode] & HAS_EXIT_FLAG) ?
                _EXIT_TRACE : _DEOPT;
            int32_t jump_target = target;
            if (is_for_iter_test[opcode]) {
                /* Target the POP_TOP immediately after the END_FOR,
                 * leaving only the iterator on the stack. */
                int extended_arg = inst->oparg > 255;
                int32_t next_inst = target + 1 + INLINE_CACHE_ENTRIES_FOR_ITER + extended_arg;
                jump_target = next_inst + inst->oparg + 1;
            }
            if (jump_target != current_jump_target || current_exit_op != exit_op) {
                make_exit(&buffer[next_spare], exit_op, jump_target);
                current_exit_op = exit_op;
                current_jump_target = jump_target;
                current_jump = next_spare;
                next_spare++;
            }
            buffer[i].jump_target = current_jump;
            buffer[i].format = UOP_FORMAT_JUMP;
        }
        if (_PyUop_Flags[opcode] & HAS_ERROR_FLAG) {
            int popped = (_PyUop_Flags[opcode] & HAS_ERROR_NO_POP_FLAG) ?
                0 : _PyUop_num_popped(opcode, inst->oparg);
            if (target != current_error_target || popped != current_popped) {
                current_popped = popped;
                current_error = next_spare;
                current_error_target = target;
                make_exit(&buffer[next_spare], _ERROR_POP_N, 0);
                buffer[next_spare].oparg = popped;
                buffer[next_spare].operand = target;
                next_spare++;
            }
            buffer[i].error_target = current_error;
            if (buffer[i].format == UOP_FORMAT_TARGET) {
                buffer[i].format = UOP_FORMAT_JUMP;
                buffer[i].jump_target = 0;
            }
        }
    }
    return next_spare;
}

/* Executor side exits */

static _PyExecutorObject *
allocate_executor(int exit_count, int length)
{
    int size = exit_count*sizeof(_PyExitData) + length*sizeof(_PyUOpInstruction);
    _PyExecutorObject *res = PyObject_GC_NewVar(_PyExecutorObject, &_PyUOpExecutor_Type, size);
    if (res == NULL) {
        return NULL;
    }
    res->trace = (_PyUOpInstruction *)(res->exits + exit_count);
    res->code_size = length;
    res->exit_count = exit_count;
    return res;
}

#ifdef Py_DEBUG

#define CHECK(PRED) \
if (!(PRED)) { \
    printf(#PRED " at %d\n", i); \
    assert(0); \
}

static int
target_unused(int opcode)
{
    return (_PyUop_Flags[opcode] & (HAS_ERROR_FLAG | HAS_EXIT_FLAG | HAS_DEOPT_FLAG)) == 0;
}

static void
sanity_check(_PyExecutorObject *executor)
{
    for (uint32_t i = 0; i < executor->exit_count; i++) {
        _PyExitData *exit = &executor->exits[i];
        CHECK(exit->target < (1 << 25));
    }
    bool ended = false;
    uint32_t i = 0;
    CHECK(executor->trace[0].opcode == _START_EXECUTOR || executor->trace[0].opcode == _COLD_EXIT);
    for (; i < executor->code_size; i++) {
        const _PyUOpInstruction *inst = &executor->trace[i];
        uint16_t opcode = inst->opcode;
        CHECK(opcode <= MAX_UOP_ID);
        CHECK(_PyOpcode_uop_name[opcode] != NULL);
        switch(inst->format) {
            case UOP_FORMAT_TARGET:
                CHECK(target_unused(opcode));
                break;
            case UOP_FORMAT_EXIT:
                CHECK(opcode == _EXIT_TRACE);
                CHECK(inst->exit_index < executor->exit_count);
                break;
            case UOP_FORMAT_JUMP:
                CHECK(inst->jump_target < executor->code_size);
                break;
            case UOP_FORMAT_UNUSED:
                CHECK(0);
                break;
        }
        if (_PyUop_Flags[opcode] & HAS_ERROR_FLAG) {
            CHECK(inst->format == UOP_FORMAT_JUMP);
            CHECK(inst->error_target < executor->code_size);
        }
        if (opcode == _JUMP_TO_TOP || opcode == _EXIT_TRACE || opcode == _COLD_EXIT) {
            ended = true;
            i++;
            break;
        }
    }
    CHECK(ended);
    for (; i < executor->code_size; i++) {
        const _PyUOpInstruction *inst = &executor->trace[i];
        uint16_t opcode = inst->opcode;
        CHECK(
            opcode == _DEOPT ||
            opcode == _EXIT_TRACE ||
            opcode == _ERROR_POP_N);
        if (opcode == _EXIT_TRACE) {
            CHECK(inst->format == UOP_FORMAT_EXIT);
        }
    }
}

#undef CHECK
#endif

/* Makes an executor from a buffer of uops.
 * Account for the buffer having gaps and NOPs by computing a "used"
 * bit vector and only copying the used uops. Here "used" means reachable
 * and not a NOP.
 */
static _PyExecutorObject *
make_executor_from_uops(_PyUOpInstruction *buffer, int length, const _PyBloomFilter *dependencies)
{
    int exit_count = count_exits(buffer, length);
    _PyExecutorObject *executor = allocate_executor(exit_count, length);
    if (executor == NULL) {
        return NULL;
    }

    /* Initialize exits */
    assert(exit_count < COLD_EXIT_COUNT);
    for (int i = 0; i < exit_count; i++) {
        executor->exits[i].executor = &COLD_EXITS[i];
        executor->exits[i].temperature = initial_temperature_backoff_counter();
    }
    int next_exit = exit_count-1;
    _PyUOpInstruction *dest = (_PyUOpInstruction *)&executor->trace[length];
    assert(buffer[0].opcode == _START_EXECUTOR);
    buffer[0].operand = (uint64_t)executor;
    for (int i = length-1; i >= 0; i--) {
        int opcode = buffer[i].opcode;
        dest--;
        *dest = buffer[i];
        assert(opcode != _POP_JUMP_IF_FALSE && opcode != _POP_JUMP_IF_TRUE);
        if (opcode == _EXIT_TRACE) {
            executor->exits[next_exit].target = buffer[i].target;
            dest->exit_index = next_exit;
            dest->format = UOP_FORMAT_EXIT;
            next_exit--;
        }
        if (opcode == _DYNAMIC_EXIT) {
            executor->exits[next_exit].target = 0;
            dest->oparg = next_exit;
            next_exit--;
        }
    }
    assert(next_exit == -1);
    assert(dest == executor->trace);
    assert(dest->opcode == _START_EXECUTOR);
    _Py_ExecutorInit(executor, dependencies);
#ifdef Py_DEBUG
    char *python_lltrace = Py_GETENV("PYTHON_LLTRACE");
    int lltrace = 0;
    if (python_lltrace != NULL && *python_lltrace >= '0') {
        lltrace = *python_lltrace - '0';  // TODO: Parse an int and all that
    }
    if (lltrace >= 2) {
        printf("Optimized trace (length %d):\n", length);
        for (int i = 0; i < length; i++) {
            printf("%4d OPTIMIZED: ", i);
            _PyUOpPrint(&executor->trace[i]);
            printf("\n");
        }
    }
    sanity_check(executor);
#endif
#ifdef _Py_JIT
    executor->jit_code = NULL;
    executor->jit_side_entry = NULL;
    executor->jit_size = 0;
    if (_PyJIT_Compile(executor, executor->trace, length)) {
        Py_DECREF(executor);
        return NULL;
    }
#endif
    _PyObject_GC_TRACK(executor);
    return executor;
}

static int
init_cold_exit_executor(_PyExecutorObject *executor, int oparg)
{
    _Py_SetImmortalUntracked((PyObject *)executor);
    Py_SET_TYPE(executor, &_PyUOpExecutor_Type);
    executor->trace = (_PyUOpInstruction *)executor->exits;
    executor->code_size = 1;
    executor->exit_count = 0;
    _PyUOpInstruction *inst = (_PyUOpInstruction *)&executor->trace[0];
    inst->opcode = _COLD_EXIT;
    inst->oparg = oparg;
    executor->vm_data.valid = true;
    executor->vm_data.linked = false;
    for (int i = 0; i < BLOOM_FILTER_WORDS; i++) {
        assert(executor->vm_data.bloom.bits[i] == 0);
    }
#ifdef Py_DEBUG
    sanity_check(executor);
#endif
#ifdef _Py_JIT
    executor->jit_code = NULL;
    executor->jit_side_entry = NULL;
    executor->jit_size = 0;
    if (_PyJIT_Compile(executor, executor->trace, 1)) {
        return -1;
    }
#endif
    return 0;
}

#ifdef Py_STATS
/* Returns the effective trace length.
 * Ignores NOPs and trailing exit and error handling.*/
int effective_trace_length(_PyUOpInstruction *buffer, int length)
{
    int nop_count = 0;
    for (int i = 0; i < length; i++) {
        int opcode = buffer[i].opcode;
        if (opcode == _NOP) {
            nop_count++;
        }
        if (opcode == _EXIT_TRACE ||
            opcode == _JUMP_TO_TOP ||
            opcode == _COLD_EXIT) {
            return i+1-nop_count;
        }
    }
    Py_FatalError("No terminating instruction");
    Py_UNREACHABLE();
}
#endif

static int
uop_optimize(
    _PyOptimizerObject *self,
    _PyInterpreterFrame *frame,
    _Py_CODEUNIT *instr,
    _PyExecutorObject **exec_ptr,
    int curr_stackentries)
{
    _PyBloomFilter dependencies;
    _Py_BloomFilter_Init(&dependencies);
    _PyUOpInstruction buffer[UOP_MAX_TRACE_LENGTH];
    OPT_STAT_INC(attempts);
    int length = translate_bytecode_to_trace(frame, instr, buffer, UOP_MAX_TRACE_LENGTH, &dependencies);
    if (length <= 0) {
        // Error or nothing translated
        return length;
    }
    assert(length < UOP_MAX_TRACE_LENGTH);
    OPT_STAT_INC(traces_created);
    char *env_var = Py_GETENV("PYTHON_UOPS_OPTIMIZE");
    if (env_var == NULL || *env_var == '\0' || *env_var > '0') {
        length = _Py_uop_analyze_and_optimize(frame, buffer,
                                           length,
                                           curr_stackentries, &dependencies);
        if (length <= 0) {
            return length;
        }
    }
    assert(length < UOP_MAX_TRACE_LENGTH);
    assert(length >= 1);
    /* Fix up */
    for (int pc = 0; pc < length; pc++) {
        int opcode = buffer[pc].opcode;
        int oparg = buffer[pc].oparg;
        if (_PyUop_Flags[opcode] & HAS_OPARG_AND_1_FLAG) {
            buffer[pc].opcode = opcode + 1 + (oparg & 1);
        }
        else if (oparg < _PyUop_Replication[opcode]) {
            buffer[pc].opcode = opcode + oparg + 1;
        }
        else if (opcode == _JUMP_TO_TOP || opcode == _EXIT_TRACE) {
            break;
        }
        assert(_PyOpcode_uop_name[buffer[pc].opcode]);
        assert(strncmp(_PyOpcode_uop_name[buffer[pc].opcode], _PyOpcode_uop_name[opcode], strlen(_PyOpcode_uop_name[opcode])) == 0);
    }
    OPT_HIST(effective_trace_length(buffer, length), optimized_trace_length_hist);
    length = prepare_for_execution(buffer, length);
    assert(length <= UOP_MAX_TRACE_LENGTH);
    _PyExecutorObject *executor = make_executor_from_uops(buffer, length,  &dependencies);
    if (executor == NULL) {
        return -1;
    }
    assert(length <= UOP_MAX_TRACE_LENGTH);
    *exec_ptr = executor;
    return 1;
}

static void
uop_opt_dealloc(PyObject *self) {
    PyObject_Free(self);
}

PyTypeObject _PyUOpOptimizer_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "uop_optimizer",
    .tp_basicsize = sizeof(_PyOptimizerObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
    .tp_dealloc = uop_opt_dealloc,
};

PyObject *
_PyOptimizer_NewUOpOptimizer(void)
{
    _PyOptimizerObject *opt = PyObject_New(_PyOptimizerObject, &_PyUOpOptimizer_Type);
    if (opt == NULL) {
        return NULL;
    }
    opt->optimize = uop_optimize;
    return (PyObject *)opt;
}

static void
counter_dealloc(_PyExecutorObject *self) {
    /* The optimizer is the operand of the second uop. */
    PyObject *opt = (PyObject *)self->trace[1].operand;
    Py_DECREF(opt);
    uop_dealloc(self);
}

PyTypeObject _PyCounterExecutor_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "counting_executor",
    .tp_basicsize = offsetof(_PyExecutorObject, exits),
    .tp_itemsize = 1,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION | Py_TPFLAGS_HAVE_GC,
    .tp_dealloc = (destructor)counter_dealloc,
    .tp_methods = executor_methods,
    .tp_traverse = executor_traverse,
    .tp_clear = (inquiry)executor_clear,
};

static int
counter_optimize(
    _PyOptimizerObject* self,
    _PyInterpreterFrame *frame,
    _Py_CODEUNIT *instr,
    _PyExecutorObject **exec_ptr,
    int Py_UNUSED(curr_stackentries)
)
{
    PyCodeObject *code = _PyFrame_GetCode(frame);
    int oparg = instr->op.arg;
    while (instr->op.code == EXTENDED_ARG) {
        instr++;
        oparg = (oparg << 8) | instr->op.arg;
    }
    if (instr->op.code != JUMP_BACKWARD) {
        /* Counter optimizer can only handle backward edges */
        return 0;
    }
    _Py_CODEUNIT *target = instr + 1 + _PyOpcode_Caches[JUMP_BACKWARD] - oparg;
    _PyUOpInstruction buffer[4] = {
        { .opcode = _START_EXECUTOR, .jump_target = 3, .format=UOP_FORMAT_JUMP },
        { .opcode = _LOAD_CONST_INLINE_BORROW, .operand = (uintptr_t)self },
        { .opcode = _INTERNAL_INCREMENT_OPT_COUNTER },
        { .opcode = _EXIT_TRACE, .target = (uint32_t)(target - _PyCode_CODE(code)), .format=UOP_FORMAT_TARGET }
    };
    _PyExecutorObject *executor = make_executor_from_uops(buffer, 4, &EMPTY_FILTER);
    if (executor == NULL) {
        return -1;
    }
    Py_INCREF(self);
    Py_SET_TYPE(executor, &_PyCounterExecutor_Type);
    *exec_ptr = executor;
    return 1;
}

static PyObject *
counter_get_counter(PyObject *self, PyObject *args)
{
    return PyLong_FromLongLong(((_PyCounterOptimizerObject *)self)->count);
}

static PyMethodDef counter_optimizer_methods[] = {
    { "get_count", counter_get_counter, METH_NOARGS, NULL },
    { NULL, NULL },
};

PyTypeObject _PyCounterOptimizer_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "Counter optimizer",
    .tp_basicsize = sizeof(_PyCounterOptimizerObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
    .tp_methods = counter_optimizer_methods,
    .tp_dealloc = (destructor)PyObject_Del,
};

PyObject *
_PyOptimizer_NewCounter(void)
{
    _PyCounterOptimizerObject *opt = (_PyCounterOptimizerObject *)_PyObject_New(&_PyCounterOptimizer_Type);
    if (opt == NULL) {
        return NULL;
    }
    opt->base.optimize = counter_optimize;
    opt->count = 0;
    return (PyObject *)opt;
}


/*****************************************
 *        Executor management
 ****************************************/

/* We use a bloomfilter with k = 6, m = 256
 * The choice of k and the following constants
 * could do with a more rigourous analysis,
 * but here is a simple analysis:
 *
 * We want to keep the false positive rate low.
 * For n = 5 (a trace depends on 5 objects),
 * we expect 30 bits set, giving a false positive
 * rate of (30/256)**6 == 2.5e-6 which is plenty
 * good enough.
 *
 * However with n = 10 we expect 60 bits set (worst case),
 * giving a false positive of (60/256)**6 == 0.0001
 *
 * We choose k = 6, rather than a higher number as
 * it means the false positive rate grows slower for high n.
 *
 * n = 5, k = 6 => fp = 2.6e-6
 * n = 5, k = 8 => fp = 3.5e-7
 * n = 10, k = 6 => fp = 1.6e-4
 * n = 10, k = 8 => fp = 0.9e-4
 * n = 15, k = 6 => fp = 0.18%
 * n = 15, k = 8 => fp = 0.23%
 * n = 20, k = 6 => fp = 1.1%
 * n = 20, k = 8 => fp = 2.3%
 *
 * The above analysis assumes perfect hash functions,
 * but those don't exist, so the real false positive
 * rates may be worse.
 */

#define K 6

#define SEED 20221211

/* TO DO -- Use more modern hash functions with better distribution of bits */
static uint64_t
address_to_hash(void *ptr) {
    assert(ptr != NULL);
    uint64_t uhash = SEED;
    uintptr_t addr = (uintptr_t)ptr;
    for (int i = 0; i < SIZEOF_VOID_P; i++) {
        uhash ^= addr & 255;
        uhash *= (uint64_t)PyHASH_MULTIPLIER;
        addr >>= 8;
    }
    return uhash;
}

void
_Py_BloomFilter_Init(_PyBloomFilter *bloom)
{
    for (int i = 0; i < BLOOM_FILTER_WORDS; i++) {
        bloom->bits[i] = 0;
    }
}

/* We want K hash functions that each set 1 bit.
 * A hash function that sets 1 bit in M bits can be trivially
 * derived from a log2(M) bit hash function.
 * So we extract 8 (log2(256)) bits at a time from
 * the 64bit hash. */
void
_Py_BloomFilter_Add(_PyBloomFilter *bloom, void *ptr)
{
    uint64_t hash = address_to_hash(ptr);
    assert(K <= 8);
    for (int i = 0; i < K; i++) {
        uint8_t bits = hash & 255;
        bloom->bits[bits >> 5] |= (1 << (bits&31));
        hash >>= 8;
    }
}

static bool
bloom_filter_may_contain(_PyBloomFilter *bloom, _PyBloomFilter *hashes)
{
    for (int i = 0; i < BLOOM_FILTER_WORDS; i++) {
        if ((bloom->bits[i] & hashes->bits[i]) != hashes->bits[i]) {
            return false;
        }
    }
    return true;
}

static void
link_executor(_PyExecutorObject *executor)
{
    PyInterpreterState *interp = _PyInterpreterState_GET();
    _PyExecutorLinkListNode *links = &executor->vm_data.links;
    _PyExecutorObject *head = interp->executor_list_head;
    if (head == NULL) {
        interp->executor_list_head = executor;
        links->previous = NULL;
        links->next = NULL;
    }
    else {
        assert(head->vm_data.links.previous == NULL);
        links->previous = NULL;
        links->next = head;
        head->vm_data.links.previous = executor;
        interp->executor_list_head = executor;
    }
    executor->vm_data.linked = true;
    /* executor_list_head must be first in list */
    assert(interp->executor_list_head->vm_data.links.previous == NULL);
}

static void
unlink_executor(_PyExecutorObject *executor)
{
    if (!executor->vm_data.linked) {
        return;
    }
    _PyExecutorLinkListNode *links = &executor->vm_data.links;
    assert(executor->vm_data.valid);
    _PyExecutorObject *next = links->next;
    _PyExecutorObject *prev = links->previous;
    if (next != NULL) {
        next->vm_data.links.previous = prev;
    }
    if (prev != NULL) {
        prev->vm_data.links.next = next;
    }
    else {
        // prev == NULL implies that executor is the list head
        PyInterpreterState *interp = PyInterpreterState_Get();
        assert(interp->executor_list_head == executor);
        interp->executor_list_head = next;
    }
    executor->vm_data.linked = false;
}

/* This must be called by optimizers before using the executor */
void
_Py_ExecutorInit(_PyExecutorObject *executor, const _PyBloomFilter *dependency_set)
{
    executor->vm_data.valid = true;
    for (int i = 0; i < BLOOM_FILTER_WORDS; i++) {
        executor->vm_data.bloom.bits[i] = dependency_set->bits[i];
    }
    link_executor(executor);
}

/* Detaches the executor from the code object (if any) that
 * holds a reference to it */
void
_Py_ExecutorDetach(_PyExecutorObject *executor)
{
    PyCodeObject *code = executor->vm_data.code;
    if (code == NULL) {
        return;
    }
    _Py_CODEUNIT *instruction = &_PyCode_CODE(code)[executor->vm_data.index];
    assert(instruction->op.code == ENTER_EXECUTOR);
    int index = instruction->op.arg;
    assert(code->co_executors->executors[index] == executor);
    instruction->op.code = executor->vm_data.opcode;
    instruction->op.arg = executor->vm_data.oparg;
    executor->vm_data.code = NULL;
    code->co_executors->executors[index] = NULL;
    Py_DECREF(executor);
}

static int
executor_clear(_PyExecutorObject *executor)
{
    if (!executor->vm_data.valid) {
        return 0;
    }
    assert(executor->vm_data.valid == 1);
    unlink_executor(executor);
    executor->vm_data.valid = 0;
    /* It is possible for an executor to form a reference
     * cycle with itself, so decref'ing a side exit could
     * free the executor unless we hold a strong reference to it
     */
    Py_INCREF(executor);
    for (uint32_t i = 0; i < executor->exit_count; i++) {
        const _PyExecutorObject *cold = &COLD_EXITS[i];
        const _PyExecutorObject *side = executor->exits[i].executor;
        executor->exits[i].temperature = initial_unreachable_backoff_counter();
        if (side != cold) {
            executor->exits[i].executor = cold;
            Py_DECREF(side);
        }
    }
    _Py_ExecutorDetach(executor);
    Py_DECREF(executor);
    return 0;
}

void
_Py_Executor_DependsOn(_PyExecutorObject *executor, void *obj)
{
    assert(executor->vm_data.valid);
    _Py_BloomFilter_Add(&executor->vm_data.bloom, obj);
}

/* Invalidate all executors that depend on `obj`
 * May cause other executors to be invalidated as well
 */
void
_Py_Executors_InvalidateDependency(PyInterpreterState *interp, void *obj, int is_invalidation)
{
    _PyBloomFilter obj_filter;
    _Py_BloomFilter_Init(&obj_filter);
    _Py_BloomFilter_Add(&obj_filter, obj);
    /* Walk the list of executors */
    /* TO DO -- Use a tree to avoid traversing as many objects */
    bool no_memory = false;
    PyObject *invalidate = PyList_New(0);
    if (invalidate == NULL) {
        PyErr_Clear();
        no_memory = true;
    }
    /* Clearing an executor can deallocate others, so we need to make a list of
     * executors to invalidate first */
    for (_PyExecutorObject *exec = interp->executor_list_head; exec != NULL;) {
        assert(exec->vm_data.valid);
        _PyExecutorObject *next = exec->vm_data.links.next;
        if (bloom_filter_may_contain(&exec->vm_data.bloom, &obj_filter)) {
            unlink_executor(exec);
            if (no_memory) {
                exec->vm_data.valid = 0;
            } else {
                if (PyList_Append(invalidate, (PyObject *)exec) < 0) {
                    PyErr_Clear();
                    no_memory = true;
                    exec->vm_data.valid = 0;
                }
            }
            if (is_invalidation) {
                OPT_STAT_INC(executors_invalidated);
            }
        }
        exec = next;
    }
    if (invalidate != NULL) {
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(invalidate); i++) {
            _PyExecutorObject *exec = (_PyExecutorObject *)PyList_GET_ITEM(invalidate, i);
            executor_clear(exec);
        }
        Py_DECREF(invalidate);
    }
    return;
}

/* Invalidate all executors */
void
_Py_Executors_InvalidateAll(PyInterpreterState *interp, int is_invalidation)
{
    while (interp->executor_list_head) {
        _PyExecutorObject *executor = interp->executor_list_head;
        assert(executor->vm_data.valid == 1 && executor->vm_data.linked == 1);
        if (executor->vm_data.code) {
            // Clear the entire code object so its co_executors array be freed:
            _PyCode_Clear_Executors(executor->vm_data.code);
        }
        else {
            executor_clear(executor);
        }
        if (is_invalidation) {
            OPT_STAT_INC(executors_invalidated);
        }
    }
}

#endif /* _Py_TIER2 */

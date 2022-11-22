#include "Python.h"

#include "callgrind.h"

static PyObject* callgrind_dump_stats(PyObject* self, PyObject* args) {
  const char* description = NULL;
  if (!PyArg_ParseTuple(args, "|s", &description)) {
    return NULL;
  }

  if (description == NULL) {
    CALLGRIND_DUMP_STATS;
  } else {
    CALLGRIND_DUMP_STATS_AT(description);
  }
  Py_RETURN_NONE;
}

static PyObject* callgrind_start_instrumentation(PyObject* self) {
  CALLGRIND_START_INSTRUMENTATION;
  Py_RETURN_NONE;
}

static PyObject* callgrind_stop_instrumentation(PyObject* self) {
  CALLGRIND_STOP_INSTRUMENTATION;
  Py_RETURN_NONE;
}

static PyObject* callgrind_zero_stats(PyObject* self) {
  CALLGRIND_ZERO_STATS;
  Py_RETURN_NONE;
}

static PyMethodDef _valgrind_methods[] = {
    {"callgrind_dump_stats", (PyCFunction)callgrind_dump_stats, METH_VARARGS,
     NULL},
    {"callgrind_start_instrumentation",
     (PyCFunction)callgrind_start_instrumentation, METH_NOARGS, NULL},
    {"callgrind_stop_instrumentation",
     (PyCFunction)callgrind_stop_instrumentation, METH_NOARGS, NULL},
    {"callgrind_zero_stats", (PyCFunction)callgrind_zero_stats, METH_NOARGS,
     NULL},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef _valgrindmodule = {PyModuleDef_HEAD_INIT, "_valgrind",
                                             NULL, -1, _valgrind_methods};

PyMODINIT_FUNC PyInit__valgrind(void) {
  PyObject* m = PyState_FindModule(&_valgrindmodule);
  if (m != NULL) {
    Py_INCREF(m);
    return m;
  }
  m = PyModule_Create(&_valgrindmodule);
  if (m == NULL) return NULL;

  PyState_AddModule(m, &_valgrindmodule);
  return m;
}

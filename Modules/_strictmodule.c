/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */
#include "Python.h"
#include "StrictModules/pystrictmodule.h"

#ifndef Py_LIMITED_API
#ifdef __cplusplus
extern "C" {
#endif


PyDoc_STRVAR(strictmodule_doc, "Strict Module related types and methods");

static int strictmodule_exec(PyObject* m) {
  if (PyType_Ready(&StrictModuleLoader_Type) < 0)
    goto fail;

  PyModule_AddObject(
      m, "StrictModuleLoader", (PyObject*)&StrictModuleLoader_Type);
  return 0;
fail:
  Py_XDECREF(m);
  return -1;
}

static struct PyModuleDef_Slot strictmodule_slots[] = {
    {Py_mod_exec, strictmodule_exec},
    {0, NULL},
};

static struct PyModuleDef strictmodulemodule = {PyModuleDef_HEAD_INIT,
                                                "_strictmodule",
                                                strictmodule_doc,
                                                0,
                                                NULL,
                                                strictmodule_slots,
                                                NULL,
                                                NULL,
                                                NULL};

/* Export function for the module (*must* be called PyInit_strictmodule) */

PyMODINIT_FUNC PyInit__strictmodule(void) {
  return PyModuleDef_Init(&strictmodulemodule);
}

#ifdef __cplusplus
}
#endif
#endif /* Py_LIMITED_API */

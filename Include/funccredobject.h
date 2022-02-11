#ifndef Py_FUNCCREDOBJECT_H
#define Py_FUNCCREDOBJECT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  PyObject_HEAD
  PyObject *module_name;
  PyObject *class_name;
  PyObject *function_name;
} PyFunctionCredentialObject;

PyAPI_DATA(PyTypeObject) PyFunctionCredential_Type;

#define PyFunctionCredential_CheckExact(op) (Py_TYPE(op) == &PyFunctionCredential_Type)

PyAPI_FUNC(PyObject *) PyFunctionCredential_New(void);
PyAPI_FUNC(void) PyFunctionCredential_Fini(void);

PyAPI_FUNC(PyObject *)func_cred_new(PyObject *tuple);

#ifdef __cplusplus
}
#endif

#endif

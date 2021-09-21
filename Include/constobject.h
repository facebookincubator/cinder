#ifndef Py_CONSTOBJECT_H
#define Py_CONSTOBJECT_H

typedef struct {
  PyObject_HEAD

  PyObject *ob_item; /* wrapped object */
} PyConstObject;

PyAPI_DATA(PyTypeObject) PyConst_Type;

#define PyConst_CheckExact(op) (Py_TYPE(op) == &PyConst_Type)

PyAPI_FUNC(PyObject *) PyConst_New(void);
PyAPI_FUNC(PyObject *) PyConst_GetItem(PyObject *);
PyAPI_FUNC(int) PyConst_SetItem(PyObject *, PyObject *);
PyAPI_FUNC(void) PyConst_Fini(void);

#endif

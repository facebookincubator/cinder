/* Lazy object interface */

#ifndef Py_LAZYIMPORTOBJECT_H
#define Py_LAZYIMPORTOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

CiAPI_DATA(PyTypeObject) PyLazyImport_Type;

#define PyLazyImport_CheckExact(op) Py_IS_TYPE((op), &PyLazyImport_Type)

#ifdef __cplusplus
}
#endif
#endif /* !Py_LAZYIMPORTOBJECT_H */

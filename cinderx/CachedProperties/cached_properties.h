#ifndef Py_CACHED_PROPERTIES_H
#define Py_CACHED_PROPERTIES_H

#include "Python.h"

/* fb t46346203 */
typedef struct {
    PyObject_HEAD
    PyObject *func;             /* function object */
    PyObject *name_or_descr;    /* str or member descriptor object */
} PyCachedPropertyDescrObject;
/* end fb t46346203 */

 /* fb T82701047 */
typedef struct {
    PyObject_HEAD
    PyObject *func;             /* function object */
    PyObject *name_or_descr;    /* str or member descriptor object */
} PyAsyncCachedPropertyDescrObject;
 /* end fb T82701047 */

/* fb T82701047 */
typedef struct {
    PyObject_HEAD
    PyObject *func;             /* function object */
    PyObject *name;             /* str or member descriptor object */
    PyObject *value;            /* value or NULL when uninitialized */
} PyAsyncCachedClassPropertyDescrObject;
/* end fb T82701047 */

CiAPI_DATA(PyTypeObject) PyAsyncCachedPropertyWithDescr_Type;
CiAPI_DATA(PyType_Spec) _PyCachedClassProperty_TypeSpec;     /* fb t46346203 */
CiAPI_DATA(PyTypeObject) PyCachedProperty_Type;     /* fb T46346203 */
CiAPI_DATA(PyTypeObject) PyCachedPropertyWithDescr_Type;     /* fb T46346203 */
CiAPI_DATA(PyTypeObject) PyAsyncCachedProperty_Type;     /* fb T82701047 */
CiAPI_DATA(PyTypeObject) PyAsyncCachedClassProperty_Type;     /* fb T82701047 */
#endif /* !Py_CACHED_PROPERTIES_H */


/* Module object interface */

#ifndef Py_MODULEOBJECT_H
#define Py_MODULEOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

PyAPI_DATA(PyTypeObject) PyLazyImport_Type;
PyAPI_DATA(PyTypeObject) PyModule_Type;
PyAPI_DATA(PyTypeObject) PyStrictModule_Type;

#define PyLazyImport_CheckExact(op) (Py_TYPE(op) == &PyLazyImport_Type)
#define PyModule_Check(op) PyObject_TypeCheck(op, &PyModule_Type)
#define PyModule_CheckExact(op) Py_IS_TYPE(op, &PyModule_Type)
#define PyStrictModule_Check(op) PyObject_TypeCheck(op, &PyStrictModule_Type)
#define PyStrictModule_CheckExact(op) (Py_TYPE(op) == &PyStrictModule_Type)

PyAPI_FUNC(PyObject *) PyLazyImportModule_NewObject(
    PyObject *name, PyObject *globals, PyObject *locals, PyObject *fromlist, PyObject *level);
PyAPI_FUNC(PyObject *) PyLazyImportObject_NewObject(PyObject *deferred, PyObject *name);

PyAPI_FUNC(PyObject *) PyStrictModule_New(PyTypeObject*, PyObject*, PyObject*);

#if !defined(Py_LIMITED_API) || Py_LIMITED_API+0 >= 0x03030000
PyAPI_FUNC(PyObject *) PyModule_NewObject(
    PyObject *name
    );
#endif
PyAPI_FUNC(PyObject *) PyModule_New(
    const char *name            /* UTF-8 encoded string */
    );
PyAPI_FUNC(PyObject *) PyModule_GetDict(PyObject *);
#if !defined(Py_LIMITED_API) || Py_LIMITED_API+0 >= 0x03030000
PyAPI_FUNC(PyObject *) PyModule_GetNameObject(PyObject *);
#endif
PyAPI_FUNC(const char *) PyModule_GetName(PyObject *);
Py_DEPRECATED(3.2) PyAPI_FUNC(const char *) PyModule_GetFilename(PyObject *);
PyAPI_FUNC(PyObject *) PyModule_GetFilenameObject(PyObject *);
#ifndef Py_LIMITED_API
PyAPI_FUNC(void) _PyModule_Clear(PyObject *);
PyAPI_FUNC(void) _PyModule_ClearDict(PyObject *);
PyAPI_FUNC(int) _PyModuleSpec_IsInitializing(PyObject *);
#endif
PyAPI_FUNC(struct PyModuleDef*) PyModule_GetDef(PyObject*);
PyAPI_FUNC(void*) PyModule_GetState(PyObject*);

#if !defined(Py_LIMITED_API) || Py_LIMITED_API+0 >= 0x03050000
/* New in 3.5 */
PyAPI_FUNC(PyObject *) PyModuleDef_Init(struct PyModuleDef*);
PyAPI_DATA(PyTypeObject) PyModuleDef_Type;
#endif

typedef struct PyModuleDef_Base {
  PyObject_HEAD
  PyObject* (*m_init)(void);
  Py_ssize_t m_index;
  PyObject* m_copy;
} PyModuleDef_Base;

#define PyModuleDef_HEAD_INIT { \
    PyObject_HEAD_INIT(NULL)    \
    NULL, /* m_init */          \
    0,    /* m_index */         \
    NULL, /* m_copy */          \
  }

struct PyModuleDef_Slot;
#if !defined(Py_LIMITED_API) || Py_LIMITED_API+0 >= 0x03050000
/* New in 3.5 */
typedef struct PyModuleDef_Slot{
    int slot;
    void *value;
} PyModuleDef_Slot;

#define Py_mod_create 1
#define Py_mod_exec 2

#ifndef Py_LIMITED_API
#define _Py_mod_LAST_SLOT 2
#endif

#endif /* New in 3.5 */

typedef struct PyModuleDef{
  PyModuleDef_Base m_base;
  const char* m_name;
  const char* m_doc;
  Py_ssize_t m_size;
  PyMethodDef *m_methods;
  struct PyModuleDef_Slot* m_slots;
  traverseproc m_traverse;
  inquiry m_clear;
  freefunc m_free;
} PyModuleDef;


// Internal C API
#ifdef Py_BUILD_CORE
extern int _PyModule_IsExtension(PyObject *obj);
#endif

typedef struct {
    PyObject_HEAD
    PyObject *lz_lazy_import;
    PyObject *lz_name;
    PyObject *lz_globals;
    PyObject *lz_locals;
    PyObject *lz_fromlist;
    PyObject *lz_level;
    PyObject *lz_obj;
    PyObject *lz_next;
    int lz_resolving;
    int lz_skip_warmup;
} PyLazyImport;

int PyLazyImport_Match(PyLazyImport *deferred, PyObject *mod_dict, PyObject *name);

#if !defined(Py_LIMITED_API)

extern Py_ssize_t strictmodule_dictoffset;

int strictmodule_is_unassigned(PyObject *dict, PyObject *name);
PyObject * PyStrictModule_GetOriginal(PyObject *obj, PyObject *name);
PyAPI_FUNC(int) _Py_do_strictmodule_patch(PyObject *self, PyObject *name, PyObject *value);
PyAPI_FUNC(PyObject *) PyStrictModule_GetDictSetter(PyObject *);
PyAPI_FUNC(PyObject *) PyStrictModule_GetDict(PyObject *);
#endif

#ifdef __cplusplus
}
#endif
#endif /* !Py_MODULEOBJECT_H */

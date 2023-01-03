
/* Module object interface */

#ifndef Py_MODULEOBJECT_H
#define Py_MODULEOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

PyAPI_DATA(PyTypeObject) PyDeferred_Type;
PyAPI_DATA(PyTypeObject) PyModule_Type;
PyAPI_DATA(PyTypeObject) PyStrictModule_Type;

#define PyDeferred_CheckExact(op) (Py_TYPE(op) == &PyDeferred_Type)

#define PyModule_Check(op) PyObject_TypeCheck(op, &PyModule_Type)
#define PyModule_CheckExact(op) (Py_TYPE(op) == &PyModule_Type)
#define PyStrictModule_Check(op) PyObject_TypeCheck(op, &PyStrictModule_Type)
#define PyStrictModule_CheckExact(op) (Py_TYPE(op) == &PyStrictModule_Type)

PyAPI_FUNC(PyObject *) PyDeferredModule_NewObject(PyObject *name, PyObject *globals, PyObject *locals, PyObject *fromlist, PyObject *level);
PyAPI_FUNC(PyObject *) PyDeferred_NewObject(PyObject *deferred, PyObject *name);

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
PyAPI_FUNC(PyObject *) _PyModule_LookupAttr(PyObject *mod, PyObject *name);
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
    PyObject_HEAD_IMMORTAL_INIT(NULL)    \
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

typedef struct {
    PyObject_HEAD
    PyObject *df_deferred;
    PyObject *df_name;
    PyObject *df_globals;
    PyObject *df_locals;
    PyObject *df_fromlist;
    PyObject *df_level;
    PyObject *df_obj;
    PyObject *df_next;
    int df_resolving;
    int df_skip_warmup;
} PyDeferredObject;

int PyDeferred_Match(PyDeferredObject *deferred, PyObject *mod_dict, PyObject *name);

typedef struct {
    PyObject_HEAD
    PyObject *md_dict;
    struct PyModuleDef *md_def;
    void *md_state;
    PyObject *md_weaklist;
    PyObject *md_name;  /* for logging purposes after md_dict is cleared */
} PyModuleObject;

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

#ifndef Py_LIMITED_API
typedef struct {
    PyModuleObject base;
    PyObject *globals;
    PyObject *global_setter;
    PyObject *originals;
    PyObject *static_thunks;
    PyObject *imported_from;
} PyStrictModuleObject;

#define PyModule_Dict(op) (PyStrictModule_Check(op) ? ((PyStrictModuleObject *)op)->globals : ((PyModuleObject *)op)->md_dict)
extern Py_ssize_t strictmodule_dictoffset;

int strictmodule_is_unassigned(PyObject *dict, PyObject *name);
PyObject * PyStrictModule_GetOriginal(PyStrictModuleObject *self, PyObject *name);
int _Py_do_strictmodule_patch(PyObject *self, PyObject *name, PyObject *value);
#endif
#ifdef __cplusplus
}
#endif
#endif /* !Py_MODULEOBJECT_H */

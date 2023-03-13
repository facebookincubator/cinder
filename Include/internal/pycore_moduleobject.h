#ifndef Py_INTERNAL_MODULEOBJECT_H
#define Py_INTERNAL_MODULEOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_BUILD_CORE
#  error "this header requires Py_BUILD_CORE define"
#endif

typedef struct {
    PyObject_HEAD
    PyObject *md_dict;
    struct PyModuleDef *md_def;
    void *md_state;
    PyObject *md_weaklist;
    // for logging purposes after md_dict is cleared
    PyObject *md_name;
} PyModuleObject;

static inline PyModuleDef* _PyModule_GetDef(PyObject *mod) {
    assert(PyModule_Check(mod));
    return ((PyModuleObject *)mod)->md_def;
}

static inline void* _PyModule_GetState(PyObject* mod) {
    assert(PyModule_Check(mod));
    return ((PyModuleObject *)mod)->md_state;
}

static inline PyObject* _PyModule_GetDict(PyObject *mod) {
    assert(PyModule_Check(mod));
    PyObject *dict = ((PyModuleObject *)mod) -> md_dict;
    // _PyModule_GetDict(mod) must not be used after calling module_clear(mod)
    assert(dict != NULL);
    return dict;
}

typedef struct {
    PyModuleObject base;
    PyObject *globals;
    PyObject *global_setter;
    PyObject *originals;
    PyObject *static_thunks;
    PyObject *imported_from;
} PyStrictModuleObject;

#define PyModule_Dict(op) (PyStrictModule_Check(op) ? ((PyStrictModuleObject *)op)->globals : ((PyModuleObject *)op)->md_dict)

CiAPI_STATIC_INLINE_FUNC(PyObject*) _PyStrictModuleGetDict(PyObject *mod);
CiAPI_STATIC_INLINE_FUNC(PyObject*) _PyStrictModuleGetDictSetter(PyObject *mod);

static inline PyObject* _PyStrictModuleGetDict(PyObject *mod) {
    assert(PyStrictModule_Check(mod));
    return ((PyStrictModuleObject*) mod) -> globals;
}

static inline PyObject* _PyStrictModuleGetDictSetter(PyObject *mod) {
    assert(PyStrictModule_Check(mod));
    return ((PyStrictModuleObject*) mod) -> global_setter;
}

#ifdef __cplusplus
}
#endif
#endif /* !Py_INTERNAL_MODULEOBJECT_H */

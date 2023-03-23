/*[clinic input]
preserve
[clinic start generated code]*/

PyDoc_STRVAR(_imp_lock_held__doc__,
"lock_held($module, /)\n"
"--\n"
"\n"
"Return True if the import lock is currently held, else False.\n"
"\n"
"On platforms without threads, return False.");

#define _IMP_LOCK_HELD_METHODDEF    \
    {"lock_held", (PyCFunction)_imp_lock_held, METH_NOARGS, _imp_lock_held__doc__},

static PyObject *
_imp_lock_held_impl(PyObject *module);

static PyObject *
_imp_lock_held(PyObject *module, PyObject *Py_UNUSED(ignored))
{
    return _imp_lock_held_impl(module);
}

PyDoc_STRVAR(_imp_acquire_lock__doc__,
"acquire_lock($module, /)\n"
"--\n"
"\n"
"Acquires the interpreter\'s import lock for the current thread.\n"
"\n"
"This lock should be used by import hooks to ensure thread-safety when importing\n"
"modules. On platforms without threads, this function does nothing.");

#define _IMP_ACQUIRE_LOCK_METHODDEF    \
    {"acquire_lock", (PyCFunction)_imp_acquire_lock, METH_NOARGS, _imp_acquire_lock__doc__},

static PyObject *
_imp_acquire_lock_impl(PyObject *module);

static PyObject *
_imp_acquire_lock(PyObject *module, PyObject *Py_UNUSED(ignored))
{
    return _imp_acquire_lock_impl(module);
}

PyDoc_STRVAR(_imp_release_lock__doc__,
"release_lock($module, /)\n"
"--\n"
"\n"
"Release the interpreter\'s import lock.\n"
"\n"
"On platforms without threads, this function does nothing.");

#define _IMP_RELEASE_LOCK_METHODDEF    \
    {"release_lock", (PyCFunction)_imp_release_lock, METH_NOARGS, _imp_release_lock__doc__},

static PyObject *
_imp_release_lock_impl(PyObject *module);

static PyObject *
_imp_release_lock(PyObject *module, PyObject *Py_UNUSED(ignored))
{
    return _imp_release_lock_impl(module);
}

PyDoc_STRVAR(_imp__fix_co_filename__doc__,
"_fix_co_filename($module, code, path, /)\n"
"--\n"
"\n"
"Changes code.co_filename to specify the passed-in file path.\n"
"\n"
"  code\n"
"    Code object to change.\n"
"  path\n"
"    File path to use.");

#define _IMP__FIX_CO_FILENAME_METHODDEF    \
    {"_fix_co_filename", (PyCFunction)(void(*)(void))_imp__fix_co_filename, METH_FASTCALL, _imp__fix_co_filename__doc__},

static PyObject *
_imp__fix_co_filename_impl(PyObject *module, PyCodeObject *code,
                           PyObject *path);

static PyObject *
_imp__fix_co_filename(PyObject *module, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *return_value = NULL;
    PyCodeObject *code;
    PyObject *path;

    if (!_PyArg_CheckPositional("_fix_co_filename", nargs, 2, 2)) {
        goto exit;
    }
    if (!PyObject_TypeCheck(args[0], &PyCode_Type)) {
        _PyArg_BadArgument("_fix_co_filename", "argument 1", (&PyCode_Type)->tp_name, args[0]);
        goto exit;
    }
    code = (PyCodeObject *)args[0];
    if (!PyUnicode_Check(args[1])) {
        _PyArg_BadArgument("_fix_co_filename", "argument 2", "str", args[1]);
        goto exit;
    }
    if (PyUnicode_READY(args[1]) == -1) {
        goto exit;
    }
    path = args[1];
    return_value = _imp__fix_co_filename_impl(module, code, path);

exit:
    return return_value;
}

PyDoc_STRVAR(_imp_create_builtin__doc__,
"create_builtin($module, spec, /)\n"
"--\n"
"\n"
"Create an extension module.");

#define _IMP_CREATE_BUILTIN_METHODDEF    \
    {"create_builtin", (PyCFunction)_imp_create_builtin, METH_O, _imp_create_builtin__doc__},

PyDoc_STRVAR(_imp_extension_suffixes__doc__,
"extension_suffixes($module, /)\n"
"--\n"
"\n"
"Returns the list of file suffixes used to identify extension modules.");

#define _IMP_EXTENSION_SUFFIXES_METHODDEF    \
    {"extension_suffixes", (PyCFunction)_imp_extension_suffixes, METH_NOARGS, _imp_extension_suffixes__doc__},

static PyObject *
_imp_extension_suffixes_impl(PyObject *module);

static PyObject *
_imp_extension_suffixes(PyObject *module, PyObject *Py_UNUSED(ignored))
{
    return _imp_extension_suffixes_impl(module);
}

PyDoc_STRVAR(_imp_init_frozen__doc__,
"init_frozen($module, name, /)\n"
"--\n"
"\n"
"Initializes a frozen module.");

#define _IMP_INIT_FROZEN_METHODDEF    \
    {"init_frozen", (PyCFunction)_imp_init_frozen, METH_O, _imp_init_frozen__doc__},

static PyObject *
_imp_init_frozen_impl(PyObject *module, PyObject *name);

static PyObject *
_imp_init_frozen(PyObject *module, PyObject *arg)
{
    PyObject *return_value = NULL;
    PyObject *name;

    if (!PyUnicode_Check(arg)) {
        _PyArg_BadArgument("init_frozen", "argument", "str", arg);
        goto exit;
    }
    if (PyUnicode_READY(arg) == -1) {
        goto exit;
    }
    name = arg;
    return_value = _imp_init_frozen_impl(module, name);

exit:
    return return_value;
}

PyDoc_STRVAR(_imp_get_frozen_object__doc__,
"get_frozen_object($module, name, /)\n"
"--\n"
"\n"
"Create a code object for a frozen module.");

#define _IMP_GET_FROZEN_OBJECT_METHODDEF    \
    {"get_frozen_object", (PyCFunction)_imp_get_frozen_object, METH_O, _imp_get_frozen_object__doc__},

static PyObject *
_imp_get_frozen_object_impl(PyObject *module, PyObject *name);

static PyObject *
_imp_get_frozen_object(PyObject *module, PyObject *arg)
{
    PyObject *return_value = NULL;
    PyObject *name;

    if (!PyUnicode_Check(arg)) {
        _PyArg_BadArgument("get_frozen_object", "argument", "str", arg);
        goto exit;
    }
    if (PyUnicode_READY(arg) == -1) {
        goto exit;
    }
    name = arg;
    return_value = _imp_get_frozen_object_impl(module, name);

exit:
    return return_value;
}

PyDoc_STRVAR(_imp_is_frozen_package__doc__,
"is_frozen_package($module, name, /)\n"
"--\n"
"\n"
"Returns True if the module name is of a frozen package.");

#define _IMP_IS_FROZEN_PACKAGE_METHODDEF    \
    {"is_frozen_package", (PyCFunction)_imp_is_frozen_package, METH_O, _imp_is_frozen_package__doc__},

static PyObject *
_imp_is_frozen_package_impl(PyObject *module, PyObject *name);

static PyObject *
_imp_is_frozen_package(PyObject *module, PyObject *arg)
{
    PyObject *return_value = NULL;
    PyObject *name;

    if (!PyUnicode_Check(arg)) {
        _PyArg_BadArgument("is_frozen_package", "argument", "str", arg);
        goto exit;
    }
    if (PyUnicode_READY(arg) == -1) {
        goto exit;
    }
    name = arg;
    return_value = _imp_is_frozen_package_impl(module, name);

exit:
    return return_value;
}

PyDoc_STRVAR(_imp_is_builtin__doc__,
"is_builtin($module, name, /)\n"
"--\n"
"\n"
"Returns True if the module name corresponds to a built-in module.");

#define _IMP_IS_BUILTIN_METHODDEF    \
    {"is_builtin", (PyCFunction)_imp_is_builtin, METH_O, _imp_is_builtin__doc__},

static PyObject *
_imp_is_builtin_impl(PyObject *module, PyObject *name);

static PyObject *
_imp_is_builtin(PyObject *module, PyObject *arg)
{
    PyObject *return_value = NULL;
    PyObject *name;

    if (!PyUnicode_Check(arg)) {
        _PyArg_BadArgument("is_builtin", "argument", "str", arg);
        goto exit;
    }
    if (PyUnicode_READY(arg) == -1) {
        goto exit;
    }
    name = arg;
    return_value = _imp_is_builtin_impl(module, name);

exit:
    return return_value;
}

PyDoc_STRVAR(_imp_is_frozen__doc__,
"is_frozen($module, name, /)\n"
"--\n"
"\n"
"Returns True if the module name corresponds to a frozen module.");

#define _IMP_IS_FROZEN_METHODDEF    \
    {"is_frozen", (PyCFunction)_imp_is_frozen, METH_O, _imp_is_frozen__doc__},

static PyObject *
_imp_is_frozen_impl(PyObject *module, PyObject *name);

static PyObject *
_imp_is_frozen(PyObject *module, PyObject *arg)
{
    PyObject *return_value = NULL;
    PyObject *name;

    if (!PyUnicode_Check(arg)) {
        _PyArg_BadArgument("is_frozen", "argument", "str", arg);
        goto exit;
    }
    if (PyUnicode_READY(arg) == -1) {
        goto exit;
    }
    name = arg;
    return_value = _imp_is_frozen_impl(module, name);

exit:
    return return_value;
}

#if defined(HAVE_DYNAMIC_LOADING)

PyDoc_STRVAR(_imp_create_dynamic__doc__,
"create_dynamic($module, spec, file=<unrepresentable>, /)\n"
"--\n"
"\n"
"Create an extension module.");

#define _IMP_CREATE_DYNAMIC_METHODDEF    \
    {"create_dynamic", (PyCFunction)(void(*)(void))_imp_create_dynamic, METH_FASTCALL, _imp_create_dynamic__doc__},

static PyObject *
_imp_create_dynamic_impl(PyObject *module, PyObject *spec, PyObject *file);

static PyObject *
_imp_create_dynamic(PyObject *module, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *return_value = NULL;
    PyObject *spec;
    PyObject *file = NULL;

    if (!_PyArg_CheckPositional("create_dynamic", nargs, 1, 2)) {
        goto exit;
    }
    spec = args[0];
    if (nargs < 2) {
        goto skip_optional;
    }
    file = args[1];
skip_optional:
    return_value = _imp_create_dynamic_impl(module, spec, file);

exit:
    return return_value;
}

#endif /* defined(HAVE_DYNAMIC_LOADING) */

#if defined(HAVE_DYNAMIC_LOADING)

PyDoc_STRVAR(_imp_exec_dynamic__doc__,
"exec_dynamic($module, mod, /)\n"
"--\n"
"\n"
"Initialize an extension module.");

#define _IMP_EXEC_DYNAMIC_METHODDEF    \
    {"exec_dynamic", (PyCFunction)_imp_exec_dynamic, METH_O, _imp_exec_dynamic__doc__},

static int
_imp_exec_dynamic_impl(PyObject *module, PyObject *mod);

static PyObject *
_imp_exec_dynamic(PyObject *module, PyObject *mod)
{
    PyObject *return_value = NULL;
    int _return_value;

    _return_value = _imp_exec_dynamic_impl(module, mod);
    if ((_return_value == -1) && PyErr_Occurred()) {
        goto exit;
    }
    return_value = PyLong_FromLong((long)_return_value);

exit:
    return return_value;
}

#endif /* defined(HAVE_DYNAMIC_LOADING) */

PyDoc_STRVAR(_imp_exec_builtin__doc__,
"exec_builtin($module, mod, /)\n"
"--\n"
"\n"
"Initialize a built-in module.");

#define _IMP_EXEC_BUILTIN_METHODDEF    \
    {"exec_builtin", (PyCFunction)_imp_exec_builtin, METH_O, _imp_exec_builtin__doc__},

static int
_imp_exec_builtin_impl(PyObject *module, PyObject *mod);

static PyObject *
_imp_exec_builtin(PyObject *module, PyObject *mod)
{
    PyObject *return_value = NULL;
    int _return_value;

    _return_value = _imp_exec_builtin_impl(module, mod);
    if ((_return_value == -1) && PyErr_Occurred()) {
        goto exit;
    }
    return_value = PyLong_FromLong((long)_return_value);

exit:
    return return_value;
}

PyDoc_STRVAR(_imp_source_hash__doc__,
"source_hash($module, /, key, source)\n"
"--\n"
"\n");

#define _IMP_SOURCE_HASH_METHODDEF    \
    {"source_hash", (PyCFunction)(void(*)(void))_imp_source_hash, METH_FASTCALL|METH_KEYWORDS, _imp_source_hash__doc__},

static PyObject *
_imp_source_hash_impl(PyObject *module, long key, Py_buffer *source);

static PyObject *
_imp_source_hash(PyObject *module, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"key", "source", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "source_hash", 0};
    PyObject *argsbuf[2];
    long key;
    Py_buffer source = {NULL, NULL};

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 2, 2, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    key = PyLong_AsLong(args[0]);
    if (key == -1 && PyErr_Occurred()) {
        goto exit;
    }
    if (PyObject_GetBuffer(args[1], &source, PyBUF_SIMPLE) != 0) {
        goto exit;
    }
    if (!PyBuffer_IsContiguous(&source, 'C')) {
        _PyArg_BadArgument("source_hash", "argument 'source'", "contiguous buffer", args[1]);
        goto exit;
    }
    return_value = _imp_source_hash_impl(module, key, &source);

exit:
    /* Cleanup for source */
    if (source.obj) {
       PyBuffer_Release(&source);
    }

    return return_value;
}

PyDoc_STRVAR(_imp_is_lazy_import__doc__,
"is_lazy_import($module, dict, name, /)\n"
"--\n"
"\n"
"Check if `name` is a lazy import object in `dict`.\n"
"\n"
"Returns 1 if `name` in `dict` contains a lazy import object.\n"
"Returns 0 if `name` in `dict` is not a lazy import object.\n"
"Returns -1 if `name` doesn\'t exist in `dict`, or an error occurred.");

#define _IMP_IS_LAZY_IMPORT_METHODDEF    \
    {"is_lazy_import", (PyCFunction)(void(*)(void))_imp_is_lazy_import, METH_FASTCALL, _imp_is_lazy_import__doc__},

static PyObject *
_imp_is_lazy_import_impl(PyObject *module, PyObject *dict, PyObject *name);

static PyObject *
_imp_is_lazy_import(PyObject *module, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *return_value = NULL;
    PyObject *dict;
    PyObject *name;

    if (!_PyArg_CheckPositional("is_lazy_import", nargs, 2, 2)) {
        goto exit;
    }
    if (!PyDict_Check(args[0])) {
        _PyArg_BadArgument("is_lazy_import", "argument 1", "dict", args[0]);
        goto exit;
    }
    dict = args[0];
    if (!PyUnicode_Check(args[1])) {
        _PyArg_BadArgument("is_lazy_import", "argument 2", "str", args[1]);
        goto exit;
    }
    if (PyUnicode_READY(args[1]) == -1) {
        goto exit;
    }
    name = args[1];
    return_value = _imp_is_lazy_import_impl(module, dict, name);

exit:
    return return_value;
}

PyDoc_STRVAR(_imp__set_lazy_imports__doc__,
"_set_lazy_imports($module, enabled=True, /, excluding=<unrepresentable>)\n"
"--\n"
"\n"
"Programmatic API for enabling lazy imports at runtime.\n"
"\n"
"`excluding` is an optional container of module names\n"
"within which all imports will remain eager.");

#define _IMP__SET_LAZY_IMPORTS_METHODDEF    \
    {"_set_lazy_imports", (PyCFunction)(void(*)(void))_imp__set_lazy_imports, METH_FASTCALL|METH_KEYWORDS, _imp__set_lazy_imports__doc__},

static PyObject *
_imp__set_lazy_imports_impl(PyObject *module, PyObject *enabled,
                            PyObject *excluding);

static PyObject *
_imp__set_lazy_imports(PyObject *module, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"", "excluding", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "_set_lazy_imports", 0};
    PyObject *argsbuf[2];
    Py_ssize_t noptargs = nargs + (kwnames ? PyTuple_GET_SIZE(kwnames) : 0) - 0;
    PyObject *enabled = Py_True;
    PyObject *excluding = NULL;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 0, 2, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    if (nargs < 1) {
        goto skip_optional_posonly;
    }
    noptargs--;
    enabled = args[0];
skip_optional_posonly:
    if (!noptargs) {
        goto skip_optional_pos;
    }
    excluding = args[1];
skip_optional_pos:
    return_value = _imp__set_lazy_imports_impl(module, enabled, excluding);

exit:
    return return_value;
}

PyDoc_STRVAR(_imp__set_lazy_imports_in_module__doc__,
"_set_lazy_imports_in_module($module, enabled=True, /)\n"
"--\n"
"\n"
"Enables or disables.");

#define _IMP__SET_LAZY_IMPORTS_IN_MODULE_METHODDEF    \
    {"_set_lazy_imports_in_module", (PyCFunction)(void(*)(void))_imp__set_lazy_imports_in_module, METH_FASTCALL, _imp__set_lazy_imports_in_module__doc__},

static PyObject *
_imp__set_lazy_imports_in_module_impl(PyObject *module, PyObject *enabled);

static PyObject *
_imp__set_lazy_imports_in_module(PyObject *module, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *return_value = NULL;
    PyObject *enabled = Py_True;

    if (!_PyArg_CheckPositional("_set_lazy_imports_in_module", nargs, 0, 1)) {
        goto exit;
    }
    if (nargs < 1) {
        goto skip_optional;
    }
    enabled = args[0];
skip_optional:
    return_value = _imp__set_lazy_imports_in_module_impl(module, enabled);

exit:
    return return_value;
}

PyDoc_STRVAR(_imp_is_lazy_imports_enabled__doc__,
"is_lazy_imports_enabled($module, /)\n"
"--\n"
"\n"
"Return True is lazy imports is currently enabled.");

#define _IMP_IS_LAZY_IMPORTS_ENABLED_METHODDEF    \
    {"is_lazy_imports_enabled", (PyCFunction)_imp_is_lazy_imports_enabled, METH_NOARGS, _imp_is_lazy_imports_enabled__doc__},

static PyObject *
_imp_is_lazy_imports_enabled_impl(PyObject *module);

static PyObject *
_imp_is_lazy_imports_enabled(PyObject *module, PyObject *Py_UNUSED(ignored))
{
    return _imp_is_lazy_imports_enabled_impl(module);
}

PyDoc_STRVAR(_imp__maybe_set_submodule_attribute__doc__,
"_maybe_set_submodule_attribute($module, parent, child, child_module,\n"
"                               name, /)\n"
"--\n"
"\n"
"Sets the module as an attribute on its parent, if the side effect is neded.");

#define _IMP__MAYBE_SET_SUBMODULE_ATTRIBUTE_METHODDEF    \
    {"_maybe_set_submodule_attribute", (PyCFunction)(void(*)(void))_imp__maybe_set_submodule_attribute, METH_FASTCALL, _imp__maybe_set_submodule_attribute__doc__},

static PyObject *
_imp__maybe_set_submodule_attribute_impl(PyObject *module, PyObject *parent,
                                         PyObject *child,
                                         PyObject *child_module,
                                         PyObject *name);

static PyObject *
_imp__maybe_set_submodule_attribute(PyObject *module, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *return_value = NULL;
    PyObject *parent;
    PyObject *child;
    PyObject *child_module;
    PyObject *name;

    if (!_PyArg_CheckPositional("_maybe_set_submodule_attribute", nargs, 4, 4)) {
        goto exit;
    }
    if (!PyUnicode_Check(args[0])) {
        _PyArg_BadArgument("_maybe_set_submodule_attribute", "argument 1", "str", args[0]);
        goto exit;
    }
    if (PyUnicode_READY(args[0]) == -1) {
        goto exit;
    }
    parent = args[0];
    if (!PyUnicode_Check(args[1])) {
        _PyArg_BadArgument("_maybe_set_submodule_attribute", "argument 2", "str", args[1]);
        goto exit;
    }
    if (PyUnicode_READY(args[1]) == -1) {
        goto exit;
    }
    child = args[1];
    child_module = args[2];
    if (!PyUnicode_Check(args[3])) {
        _PyArg_BadArgument("_maybe_set_submodule_attribute", "argument 4", "str", args[3]);
        goto exit;
    }
    if (PyUnicode_READY(args[3]) == -1) {
        goto exit;
    }
    name = args[3];
    return_value = _imp__maybe_set_submodule_attribute_impl(module, parent, child, child_module, name);

exit:
    return return_value;
}

#ifndef _IMP_CREATE_DYNAMIC_METHODDEF
    #define _IMP_CREATE_DYNAMIC_METHODDEF
#endif /* !defined(_IMP_CREATE_DYNAMIC_METHODDEF) */

#ifndef _IMP_EXEC_DYNAMIC_METHODDEF
    #define _IMP_EXEC_DYNAMIC_METHODDEF
#endif /* !defined(_IMP_EXEC_DYNAMIC_METHODDEF) */
/*[clinic end generated code: output=b4dd949667edf292 input=a9049054013a1b77]*/

PyDoc_STRVAR(is_lazy_import__doc__,
"is_lazy_import(module, dict, key)\n"
"It will check if *key*'s value in dict is loaded or not.\n"
"It will return 1 if the value is not loaded (a lazy key).\n"
"It will return 0 if the value is loaded.\n"
"It will return -1 if existing an error.\n"
);

#define IS_LAZY_IMPORT_METHODDEF    \
    {"is_lazy_import", _PyCFunction_CAST(is_lazy_import), METH_FASTCALL, is_lazy_import__doc__},

static PyObject *
is_lazy_import_impl(PyObject *module, PyObject *dict, PyObject *key);

static PyObject *
is_lazy_import(PyObject *module, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *return_value = NULL;

    if (!_PyArg_CheckPositional("is_lazy_import", nargs, 2, 2)) {
        goto exit;
    }

    if (!PyDict_Check(args[0])) {
        _PyArg_BadArgument("is_lazy_import", "argument 1", "dict", args[0]);
        goto exit;
    }

    if (!PyUnicode_Check(args[1])) {
        _PyArg_BadArgument("is_lazy_import", "argument 2", "str", args[1]);
        goto exit;
    }

    return_value = is_lazy_import_impl(module, args[0], args[1]);

exit:
    return return_value;
}

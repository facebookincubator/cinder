PyDoc_STRVAR(is_lazy_key__doc__,
"is_lazy_key__doc__ content\n"
);

#define IS_LAZY_KEY_METHODDEF    \
    {"is_lazy_key", _PyCFunction_CAST(is_lazy_key), METH_FASTCALL, is_lazy_key__doc__},

static PyObject *
is_lazy_key_impl(PyObject *module, PyObject *dict, PyObject *key);

static PyObject *
is_lazy_key(PyObject *module, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *return_value = NULL;

    if (!_PyArg_CheckPositional("is_lazy_key", nargs, 2, 2)) {
        goto exit;
    }

    if (!PyDict_Check(args[0])) {
        _PyArg_BadArgument("is_lazy_key", "argument 1", "dict", args[0]);
        goto exit;
    }

    if (!PyUnicode_Check(args[1])) {
        _PyArg_BadArgument("is_lazy_key", "argument 2", "str", args[1]);
        goto exit;
    }

    return_value = is_lazy_key_impl(module, args[0], args[1]);

exit:
    return return_value;
}

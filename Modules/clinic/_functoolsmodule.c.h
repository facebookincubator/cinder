/* Copyright (c) Meta Platforms, Inc. and affiliates. */
/*[clinic input]
preserve
[clinic start generated code]*/

static PyObject *
_functools__lru_cache_wrapper_impl(PyTypeObject *type, PyObject *func,
                                   PyObject *maxsize_O, int typed,
                                   PyObject *cache_info_type);

static PyObject *
_functools__lru_cache_wrapper(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"user_function", "maxsize", "typed", "cache_info_type", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "_lru_cache_wrapper", 0};
    PyObject *argsbuf[4];
    PyObject * const *fastargs;
    Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    PyObject *func;
    PyObject *maxsize_O;
    int typed;
    PyObject *cache_info_type;

    fastargs = _PyArg_UnpackKeywords(_PyTuple_CAST(args)->ob_item, nargs, kwargs, NULL, &_parser, 4, 4, 0, argsbuf);
    if (!fastargs) {
        goto exit;
    }
    func = fastargs[0];
    maxsize_O = fastargs[1];
    typed = PyObject_IsTrue(fastargs[2]);
    if (typed < 0) {
        goto exit;
    }
    cache_info_type = fastargs[3];
    return_value = _functools__lru_cache_wrapper_impl(type, func, maxsize_O, typed, cache_info_type);

exit:
    return return_value;
}
/*[clinic end generated code: output=b7ff47f215ea4a74 input=a9049054013a1b77]*/

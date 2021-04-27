/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */
/*[clinic input]
preserve
[clinic start generated code]*/

static int
memoize_memoize_func_wrapper___init___impl(memoize_func_wrapper_object *self,
                                           PyObject *func,
                                           PyObject *cache_fetcher);

static int
memoize_memoize_func_wrapper___init__(PyObject *self, PyObject *args, PyObject *kwargs)
{
    int return_value = -1;
    static const char * const _keywords[] = {"user_function", "cache_fetcher", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "memoize_func_wrapper", 0};
    PyObject *argsbuf[2];
    PyObject * const *fastargs;
    Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    PyObject *func;
    PyObject *cache_fetcher;

    fastargs = _PyArg_UnpackKeywords(_PyTuple_CAST(args)->ob_item, nargs, kwargs, NULL, &_parser, 2, 2, 0, argsbuf);
    if (!fastargs) {
        goto exit;
    }
    func = fastargs[0];
    cache_fetcher = fastargs[1];
    return_value = memoize_memoize_func_wrapper___init___impl((memoize_func_wrapper_object *)self, func, cache_fetcher);

exit:
    return return_value;
}
/*[clinic end generated code: output=22747f33bff39f08 input=a9049054013a1b77]*/

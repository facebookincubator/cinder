/*[clinic input]
preserve
[clinic start generated code]*/

PyDoc_STRVAR(async_cached_property_init__doc__,
"async_cached_property(func, name_or_descr=None)\n"
"--\n"
"\n"
"init a async_cached_property.\n"
"\n"
"Creates a new async cached property where function will be called to produce\n"
"the async lazy value on the first access.\n"
"\n"
"If slot descriptor is provided it will be used for storing the value.\"");

static int
async_cached_property_init_impl(PyAsyncCachedPropertyDescrObject *self,
                                PyObject *func, PyObject *name_or_descr);

static int
async_cached_property_init(PyObject *self, PyObject *args, PyObject *kwargs)
{
    int return_value = -1;
    static const char * const _keywords[] = {"func", "name_or_descr", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "async_cached_property", 0};
    PyObject *argsbuf[2];
    PyObject * const *fastargs;
    Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    Py_ssize_t noptargs = nargs + (kwargs ? PyDict_GET_SIZE(kwargs) : 0) - 1;
    PyObject *func;
    PyObject *name_or_descr = NULL;

    fastargs = _PyArg_UnpackKeywords(_PyTuple_CAST(args)->ob_item, nargs, kwargs, NULL, &_parser, 1, 2, 0, argsbuf);
    if (!fastargs) {
        goto exit;
    }
    func = fastargs[0];
    if (!noptargs) {
        goto skip_optional_pos;
    }
    if (!PyObject_TypeCheck(fastargs[1], &PyMemberDescr_Type)) {
        _PyArg_BadArgument("async_cached_property", "argument 'name_or_descr'", (&PyMemberDescr_Type)->tp_name, fastargs[1]);
        goto exit;
    }
    name_or_descr = fastargs[1];
skip_optional_pos:
    return_value = async_cached_property_init_impl((PyAsyncCachedPropertyDescrObject *)self, func, name_or_descr);

exit:
    return return_value;
}

PyDoc_STRVAR(async_cached_classproperty_new__doc__,
"async_cached_classproperty(func)\n"
"--\n"
"\n"
"Provides an async cached class property.\n"
"\n"
"Works with normal types and frozen types to create values on demand\n"
"and cache them in the class.");

static PyObject *
async_cached_classproperty_new_impl(PyTypeObject *type, PyObject *func);

static PyObject *
async_cached_classproperty_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"func", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "async_cached_classproperty", 0};
    PyObject *argsbuf[1];
    PyObject * const *fastargs;
    Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    PyObject *func;

    fastargs = _PyArg_UnpackKeywords(_PyTuple_CAST(args)->ob_item, nargs, kwargs, NULL, &_parser, 1, 1, 0, argsbuf);
    if (!fastargs) {
        goto exit;
    }
    if (!PyObject_TypeCheck(fastargs[0], &PyFunction_Type)) {
        _PyArg_BadArgument("async_cached_classproperty", "argument 'func'", (&PyFunction_Type)->tp_name, fastargs[0]);
        goto exit;
    }
    func = fastargs[0];
    return_value = async_cached_classproperty_new_impl(type, func);

exit:
    return return_value;
}
/*[clinic end generated code: output=aaf271c9aed7e9ec input=a9049054013a1b77]*/

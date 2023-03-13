/*[clinic input]
preserve
[clinic start generated code]*/

static PyObject *
mappingproxy_new_impl(PyTypeObject *type, PyObject *mapping);

static PyObject *
mappingproxy_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"mapping", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "mappingproxy", 0};
    PyObject *argsbuf[1];
    PyObject * const *fastargs;
    Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    PyObject *mapping;

    fastargs = _PyArg_UnpackKeywords(_PyTuple_CAST(args)->ob_item, nargs, kwargs, NULL, &_parser, 1, 1, 0, argsbuf);
    if (!fastargs) {
        goto exit;
    }
    mapping = fastargs[0];
    return_value = mappingproxy_new_impl(type, mapping);

exit:
    return return_value;
}

PyDoc_STRVAR(property_init__doc__,
"property(fget=None, fset=None, fdel=None, doc=None)\n"
"--\n"
"\n"
"Property attribute.\n"
"\n"
"  fget\n"
"    function to be used for getting an attribute value\n"
"  fset\n"
"    function to be used for setting an attribute value\n"
"  fdel\n"
"    function to be used for del\'ing an attribute\n"
"  doc\n"
"    docstring\n"
"\n"
"Typical use is to define a managed attribute x:\n"
"\n"
"class C(object):\n"
"    def getx(self): return self._x\n"
"    def setx(self, value): self._x = value\n"
"    def delx(self): del self._x\n"
"    x = property(getx, setx, delx, \"I\'m the \'x\' property.\")\n"
"\n"
"Decorators make defining new properties or modifying existing ones easy:\n"
"\n"
"class C(object):\n"
"    @property\n"
"    def x(self):\n"
"        \"I am the \'x\' property.\"\n"
"        return self._x\n"
"    @x.setter\n"
"    def x(self, value):\n"
"        self._x = value\n"
"    @x.deleter\n"
"    def x(self):\n"
"        del self._x");

static int
property_init_impl(propertyobject *self, PyObject *fget, PyObject *fset,
                   PyObject *fdel, PyObject *doc);

static int
property_init(PyObject *self, PyObject *args, PyObject *kwargs)
{
    int return_value = -1;
    static const char * const _keywords[] = {"fget", "fset", "fdel", "doc", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "property", 0};
    PyObject *argsbuf[4];
    PyObject * const *fastargs;
    Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    Py_ssize_t noptargs = nargs + (kwargs ? PyDict_GET_SIZE(kwargs) : 0) - 0;
    PyObject *fget = NULL;
    PyObject *fset = NULL;
    PyObject *fdel = NULL;
    PyObject *doc = NULL;

    fastargs = _PyArg_UnpackKeywords(_PyTuple_CAST(args)->ob_item, nargs, kwargs, NULL, &_parser, 0, 4, 0, argsbuf);
    if (!fastargs) {
        goto exit;
    }
    if (!noptargs) {
        goto skip_optional_pos;
    }
    if (fastargs[0]) {
        fget = fastargs[0];
        if (!--noptargs) {
            goto skip_optional_pos;
        }
    }
    if (fastargs[1]) {
        fset = fastargs[1];
        if (!--noptargs) {
            goto skip_optional_pos;
        }
    }
    if (fastargs[2]) {
        fdel = fastargs[2];
        if (!--noptargs) {
            goto skip_optional_pos;
        }
    }
    doc = fastargs[3];
skip_optional_pos:
    return_value = property_init_impl((propertyobject *)self, fget, fset, fdel, doc);

exit:
    return return_value;
}

#if defined(ENABLE_CINDERVM)

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

#endif /* defined(ENABLE_CINDERVM) */

#if defined(ENABLE_CINDERVM)

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

#endif /* defined(ENABLE_CINDERVM) */
/*[clinic end generated code: output=1e3401d10f761978 input=a9049054013a1b77]*/

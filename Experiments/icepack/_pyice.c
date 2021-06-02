
/* C accelerator implementation for PyIce */

#include "Python.h"
#include "bytes_methods.h"
#include <stdint.h>

/* Structures for the in-memory/on-disk format */

typedef struct {
    int64_t marker;          /* ICEPACK<version byte> */
    uint32_t timestamp;      /* timestamp of latest file */
    unsigned int modules;    /* offset to module table */
    unsigned int codes;      /* offset to code object table */
    unsigned int strings;    /* offset to string table */
    unsigned int bytes;      /* offset to byte table */
    unsigned int ints;       /* offset to int table */
    unsigned int bigints;    /* offset to big int table */
    unsigned int floats;     /* offset to float table */
    unsigned int complexes;  /* offset to complex table */
    unsigned int tuples;     /* offset to tuple table */
    unsigned int frozensets; /* offset to frozen set table */
} IcePackHeader;

typedef struct {
    unsigned int count;
    unsigned int offsets[1];
} OffsetSection;

typedef struct {
    unsigned int count;
    unsigned int padding;
    double values[1];
} FloatSection;

typedef struct {
    unsigned int count;
    unsigned int padding;
    Py_complex values[1];
} ComplexSection;

typedef struct {
    unsigned int count;
    int values[1];
} IntSection;

typedef struct {
    unsigned int name;
    unsigned int code;
    unsigned int is_package;
    unsigned int filename;
    unsigned int children;
} ModuleInfo;

typedef struct {
    unsigned int count;
    ModuleInfo modules[1];
} ModuleSection;

typedef struct {
    unsigned int bytes;
    unsigned int argcount;
    unsigned int kwonlyargcount;
    unsigned int nlocals;
    unsigned int stacksize;
    unsigned int flags;
    unsigned int firstlineno;
    unsigned int name;
    unsigned int filename;
    unsigned int lnotab;
    unsigned int cellvars;
    unsigned int freevars;
    unsigned int names;
    unsigned int varnames;
    unsigned int consts;
} CodeObject;

typedef struct {
    unsigned int len;
    const char data[1];
} IcePackStr;

typedef struct {
    unsigned int len;
    const unsigned char data[1];
} IcePackBytes;

typedef struct {
    unsigned int count;
    unsigned int values[1];
} IcePackArray;

/* Python objects */

typedef struct {
    PyObject_HEAD
    PyObject *obj;
    PyObject* base_dir;
    PyObject* filename_map;
    // Pointers into our memory mapped buffer that represent different sections
    ModuleSection* section_modules;
    OffsetSection* section_code, *section_frozenset,
                 * section_str, *section_bytes, *section_bigint, *section_tuple;
    FloatSection* section_float;
    ComplexSection* section_complex;
    IntSection *section_int;

    // Caches of Python objects produced from the sections
    PyObject** str_cache;
    PyObject** bytes_cache;
    PyObject** float_cache;
    PyObject** complex_cache;
    PyObject** int_cache;
    PyObject** bigint_cache;
    PyObject** tuple_cache;
    PyObject** frozenset_cache;
    Py_buffer codebuffer;
} CIceBreaker;

typedef struct {
    PyObject_HEAD
    const char* data;
    size_t size;
    Py_ssize_t hash;
    CIceBreaker *breaker;
    size_t exports;
    PyObject* code_obj; // borrowed reference, the code object keeps us alive
} CIceBreakerCode;

typedef struct {
    PyObject_HEAD
    PyObject* value;
    Py_hash_t hash;
} CObjectValue;

static PyTypeObject CIceBreakerType;
static PyTypeObject CIceBreakerCodeType;
static PyTypeObject CObjectValueType;

static PyObject *IcePackError;

#define CIceBreakerObject_Check(v)      (Py_TYPE(v) == &CIceBreakerType)
#define CIceBreakerCodeObject_Check(v)  (Py_TYPE(v) == &CIceBreakerCodeType)

#define CLEANUP_IF_NULL(x) if (x == NULL) goto cleanup

/* CIceBreaker methods */

static void clear_cache(PyObject** cache, unsigned int count) {
    if (cache != NULL) {
        for (unsigned int i = 0; i<count; i++) {
            Py_XDECREF(cache[i]);
        }
    }
}

static inline int
validate_index(CIceBreaker *self, const void* pointer, const char* source) {
    if (pointer < self->codebuffer.buf ||
        pointer >= (void*)(((char*)self->codebuffer.buf) + self->codebuffer.len)) {
        char buf[81];
        if (pointer < self->codebuffer.buf) {
            snprintf(buf, sizeof(buf), "Invalid icepack: reading %s negative offset %ld",
              source,
              (char*)self->codebuffer.buf - (char*)pointer);
        } else {
            snprintf(buf, sizeof(buf), "Invalid icepack: reading %s offset %ld is out of bounds %ld",
                source,
                (char*)self->codebuffer.buf - (char*)pointer,
                self->codebuffer.len);
        }
        PyErr_SetString(PyExc_MemoryError, "Invalid Icepack");
        return 0;
    }
    return 1;
}

static void
CIceBreaker_dealloc(CIceBreaker *self)
{
    if (self->section_str != NULL) {
        clear_cache(self->str_cache, self->section_str->count);
    }
    if (self->section_bytes != NULL) {
        clear_cache(self->bytes_cache, self->section_bytes->count);
    }
    if (self->section_float != NULL) {
        clear_cache(self->float_cache, self->section_float->count);
    }
    if (self->section_complex != NULL) {
        clear_cache(self->complex_cache, self->section_complex->count);
    }
    if (self->section_int != NULL) {
        clear_cache(self->int_cache, self->section_int->count);
    }
    if (self->section_bigint != NULL) {
        clear_cache(self->bigint_cache, self->section_bigint->count);
    }
    if (self->section_tuple != NULL) {
        clear_cache(self->tuple_cache, self->section_tuple->count);
    }
    if (self->section_frozenset != NULL) {
        clear_cache(self->frozenset_cache, self->section_frozenset->count);
    }
    Py_XDECREF(self->base_dir);
    Py_XDECREF(self->filename_map);
    PyBuffer_Release(&self->codebuffer);
    if (self->obj != NULL) {
        Py_DECREF(self->obj);
    }
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject *
read_const(CIceBreaker *self, unsigned int const_val, int* can_cache);

static PyObject *
CIceBreaker__exit__(CIceBreaker *self, PyObject *args)
{
    // Release our buffer when we're used as a context manager
    PyBuffer_Release(&self->codebuffer);
    self->codebuffer = (Py_buffer) { 0 };

    // Call __exit__ on our super class if it's defined, so it can release
    // the mmap as well.
    PyObject* super_args = NULL, *super = NULL, *func = NULL;
    PyObject* res = NULL;
    super_args = PyTuple_New(2);
    if (super_args == NULL) {
        goto cleanup;
    }

    PyTuple_SET_ITEM(super_args, 0, (PyObject*)&CIceBreakerType);
    Py_INCREF(&CIceBreakerType);
    PyTuple_SET_ITEM(super_args, 1, (PyObject*)self);
    Py_INCREF(self);

    super = PyType_GenericNew(&PySuper_Type, super_args, NULL);
    if (super == NULL || super->ob_type->tp_init(super, super_args, NULL)) {
        goto cleanup;
    }

    func = PyObject_GetAttrString(super, "__exit__");
    if (func != NULL) {
        res = PyObject_Call(func, args, NULL);
        if (res == NULL) {
           goto cleanup;
        }
    } else {
        PyErr_Clear();
        res = Py_None;
        Py_INCREF(res);
    }

cleanup:
    Py_XDECREF(super);
    Py_XDECREF(super_args);
    Py_XDECREF(func);
    return res;
}

#define CHECK_CACHED(type)                                             \
    if (index >= self->section_##type->count) {                        \
        PyErr_SetString(PyExc_ValueError, "Invalid " #type " index");  \
        return NULL;                                                   \
    } else if (self->type##_cache[index] != NULL) {                    \
        PyObject* res = self->type##_cache[index];                     \
        Py_INCREF(res);                                                \
        return res;                                                    \
    }

#define VALIDATE_TABLE_INDEX(type)                                     \
    if (index >= self->section_##type->count) {                        \
        PyErr_SetString(PyExc_ValueError, "Invalid " #type " index");  \
        return NULL;                                                   \
    }

#define DEFINE_CACHED_READER(type)                                   \
static PyObject *                                                      \
read_##type(CIceBreaker* self, unsigned int index) {                   \
    VALIDATE_TABLE_INDEX(type)                                         \
                                                                       \
    PyObject* res = self->type##_cache[index];                         \
    if (res == NULL) {                                                 \
        res = read_##type##_uncached(self, index);                     \
        self->type##_cache[index] = res;                               \
    }                                                                  \
    Py_XINCREF(res);                                                   \
    return res;                                                        \
}

static PyObject *
read_float_uncached(CIceBreaker* self, unsigned int index) {
    return PyFloat_FromDouble(self->section_float->values[index]);
}
DEFINE_CACHED_READER(float)


static PyObject *
read_complex_uncached(CIceBreaker* self, unsigned int index) {
    return PyComplex_FromCComplex(self->section_complex->values[index]);
}
DEFINE_CACHED_READER(complex)


static PyObject *
read_int_uncached(CIceBreaker* self, unsigned int index) {
    return PyLong_FromLong(self->section_int->values[index]);
}
DEFINE_CACHED_READER(int)

static PyObject *
read_str_uncached(CIceBreaker* self, unsigned int index) {
    unsigned int location = self->section_str->offsets[index];
    IcePackStr *str  = (IcePackStr*)((char*)self->codebuffer.buf + location);
    if (str->len != 0 && !validate_index(self, &str->data[str->len - 1], "str")) {
        return NULL;
    }
    PyObject* res = PyUnicode_DecodeUTF8(str->data, str->len, "surrogatepass");
    if (res != NULL) {
        // When we construct a code object all of the strings got interned, so
        // we intern from the cache
        PyUnicode_InternInPlace(&res);
    }
    return res;
}

DEFINE_CACHED_READER(str)

static PyObject *
read_bytes_uncached(CIceBreaker* self, unsigned int index) {
    unsigned int location = self->section_bytes->offsets[index];
    IcePackStr *str  = (IcePackStr*)((char*)self->codebuffer.buf + location);
    if (str->len != 0 &&
        !validate_index(self, &str->data[str->len - 1], "bytes")) {
        return NULL;
    }

    return PyBytes_FromStringAndSize(str->data, str->len);
}

DEFINE_CACHED_READER(bytes)

static PyObject*
read_bigint_uncached(CIceBreaker* self, unsigned int index) {
    unsigned int location = self->section_bigint->offsets[index];
    IcePackBytes *bytes  = (IcePackBytes*)((char*)self->codebuffer.buf + location);
    if (bytes->len != 0 &&
        !validate_index(self, &bytes->data[bytes->len - 1], "bigint")) {
        return NULL;
    }

    return _PyLong_FromByteArray(bytes->data, bytes->len, 1, 1);
}

DEFINE_CACHED_READER(bigint)

static PyObject*
read_tuple(CIceBreaker* self, unsigned int index, int* can_cache_outer) {
    VALIDATE_TABLE_INDEX(tuple)

    if (self->tuple_cache[index] != NULL) {
        PyObject* res = self->tuple_cache[index];
        Py_INCREF(res);
        return res;
    }

    unsigned int location = self->section_tuple->offsets[index];

    IcePackArray *items  = (IcePackArray*)((char*)self->codebuffer.buf + location);
    if (items->count != 0 &&
        !validate_index(self, &items->values[items->count - 1], "tuple")) {
        return NULL;
    }

    PyObject* tuple = PyTuple_New(items->count);
    if (tuple == NULL) {
        return NULL;
    }

    int can_cache = 1;
    for (unsigned int i = 0; i<items->count; i++) {
        PyObject* str = read_const(self, items->values[i], &can_cache);
        if (str == NULL) {
            Py_DECREF(tuple);
            return NULL;
        }
        PyTuple_SET_ITEM(tuple, i, str);
    }

    if (can_cache) {
        self->tuple_cache[index] = tuple;
        Py_INCREF(tuple);  // saved in the cache
    } else if (can_cache_outer) {
        can_cache_outer = 0;
    }
    return tuple;
}

static PyObject*
read_frozenset_uncached(CIceBreaker* self, unsigned int index) {
    unsigned int location = self->section_frozenset->offsets[index];

    IcePackArray *items  = (IcePackArray*)((char*)self->codebuffer.buf + location);
    if (items->count != 0 &&
        !validate_index(self, &items->values[items->count - 1], "frozenset")) {
        return NULL;
    }

    PyObject* set = PyFrozenSet_New(NULL);
    if (set == NULL) {
        return NULL;
    }

    int can_cache = 1;
    for (unsigned int i = 0; i<items->count; i++) {
        PyObject* val = read_const(self, items->values[i], &can_cache);
        if (val == NULL || PySet_Add(set, val) == -1) {
            Py_DECREF(set);
            return NULL;
        }
    }

    return set;
}

DEFINE_CACHED_READER(frozenset)

static PyObject*
read_code(CIceBreaker* self, unsigned int index) {
    if (index >= self->section_code->count) {
        PyErr_SetString(PyExc_ValueError, "Invalid code index");
        return NULL;
    }

    unsigned int location = self->section_code->offsets[index];
    CodeObject *header  = (CodeObject*)((char*)self->codebuffer.buf + location);
    if (!validate_index(self, ((char*)header) + sizeof(CodeObject) - 1, "code")) {
        return NULL;
    }

    PyObject* res = NULL, *name = NULL, *filename = NULL,
           *lnotab = NULL, *cellvars = NULL, *freevars = NULL, *names = NULL,
           *varnames = NULL, *consts = NULL, *fixed_fn = NULL;

    CIceBreakerCode* code = PyObject_New(CIceBreakerCode, &CIceBreakerCodeType);
    CLEANUP_IF_NULL(code);

    unsigned int bytes_loc = self->section_bytes->offsets[header->bytes];
    IcePackStr *bytes  = (IcePackStr*)((char*)self->codebuffer.buf + bytes_loc);
    if (bytes->len != 0 &&
        !validate_index(self, &bytes->data[bytes->len - 1], "code bytes")) {
        goto cleanup;
    }

    code->data = &bytes->data[0];
    code->size = bytes->len;
    code->hash = -1;
    code->breaker = self;
    Py_INCREF(self);

    name = read_str(self, header->name);
    CLEANUP_IF_NULL(name);
    filename = read_str(self, header->filename);
    CLEANUP_IF_NULL(filename);
    lnotab = read_bytes(self, header->lnotab);
    CLEANUP_IF_NULL(lnotab);
    cellvars = read_tuple(self, header->cellvars, NULL);
    CLEANUP_IF_NULL(cellvars);
    freevars = read_tuple(self, header->freevars, NULL);
    CLEANUP_IF_NULL(freevars);
    names = read_tuple(self, header->names, NULL);
    CLEANUP_IF_NULL(names);
    varnames = read_tuple(self, header->varnames, NULL);
    CLEANUP_IF_NULL(varnames);
    consts = read_tuple(self, header->consts, NULL);
    CLEANUP_IF_NULL(consts);

    fixed_fn = PyDict_GetItem(self->filename_map, filename);
    if (fixed_fn == NULL) {
        fixed_fn = PyUnicode_Concat(self->base_dir, filename);
        CLEANUP_IF_NULL(fixed_fn);

        int set = PyDict_SetItem(self->filename_map, filename, fixed_fn);
        // fixed_fn will be borrowed from dictionary to hand off to PyCode_new
        // or freed if we failed to insert it into the dictionary
        Py_DECREF(fixed_fn);

        if (set == -1) {
            goto cleanup;
        }
    }
    res = (PyObject*)PyCode_New(header->argcount, header->kwonlyargcount,
               header->nlocals, header->stacksize, header->flags,
               (PyObject*)code, consts, names, varnames, freevars, cellvars,
               fixed_fn, name, header->firstlineno, lnotab);
    code->code_obj = res;

 cleanup:
     Py_XDECREF(code);
     Py_XDECREF(name);
     Py_XDECREF(filename);
     Py_XDECREF(lnotab);
     Py_XDECREF(cellvars);
     Py_XDECREF(freevars);
     Py_XDECREF(names);
     Py_XDECREF(varnames);
     Py_XDECREF(consts);
     return res;
}

static PyObject *
CIceBreaker_find_module(CIceBreaker *self, PyObject *o)
{
    if (!PyUnicode_Check(o)) {
        PyErr_SetString(PyExc_TypeError, "expected module name as str");
        return NULL;
    }

    const ModuleSection* cur = self->section_modules;

    const char* name = PyUnicode_AsUTF8(o);
    int last_segment;
    const char* buf = self->codebuffer.buf;
    do {
        unsigned int len;
        const char* end;
        if ((end = strchr(name, '.')) == NULL) {
            len = strlen(name);
            last_segment = 1;
        } else {
            len = end - name;
            last_segment = 0;

        }

        // Binary search the module tree
        int low = 0, high = cur->count - 1;
        while (low <= high) {
            int i = (low + high) / 2;

            if (!validate_index(self, &cur->modules[i], "module table")) {
                return NULL;
            }
            unsigned int str_index = cur->modules[i].name;
            unsigned int location = self->section_str->offsets[str_index];
            char* str_mem = (char*)self->codebuffer.buf + location;
            IcePackStr *str  = (IcePackStr*)str_mem;
            if (str->len != 0 &&
                !validate_index(self, &str->data[str->len - 1], "module name")) {
                return NULL;
            }

            int cmp = strncmp(name, &str->data[0], len < str->len ? len : str->len);
            if (cmp == 0 && str->len == len) {
                if (last_segment) {
                    PyObject *code = read_code(self, cur->modules[i].code);
                    if (code == NULL) {
                        return NULL;
                    }

                    PyObject *filename = read_str(self, cur->modules[i].filename);
                    if (filename == NULL) {
                        Py_DECREF(code);
                        return NULL;
                    }

                    PyObject *res = PyTuple_New(3);
                    if (res == NULL) {
                        Py_DECREF(filename);
                        Py_DECREF(code);
                        return NULL;
                    }

                    PyObject* is_pkg = cur->modules[i].is_package ?
                                        Py_True : Py_False;
                    Py_INCREF(is_pkg);
                    PyTuple_SET_ITEM(res, 0, code);
                    PyTuple_SET_ITEM(res, 1, is_pkg);
                    PyTuple_SET_ITEM(res, 2, filename);
                    return res;
                } else if (cur->modules[i].children == 0) {
                    goto not_found;
                }
                cur = (ModuleSection*)(buf + cur->modules[i].children);
                name = name + len + 1;
                break;
            } else if (cmp > 0 || (cmp == 0 && str->len < len)) {
                low = i + 1;
            } else {
                /* cmp < 0 */
                high = i - 1;
            }
        }
        if (low > high) {
            goto not_found;
        }
    } while(!last_segment);
not_found:
    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject *
CIceBreaker_read_code(CIceBreaker *self, PyObject *o)
{
    unsigned long index = PyLong_AsUnsignedLong(o);
    if (index == (unsigned long)-1 && PyErr_Occurred()) {
        return NULL;
    }

    return read_code(self, index);
}


static PyObject *
CIceBreaker_read_str(CIceBreaker *self, PyObject *o)
{
    unsigned long index = PyLong_AsUnsignedLong(o);
    if (index == (unsigned long)-1 && PyErr_Occurred()) {
        return NULL;
    }

    return read_str(self, index);
}

static PyObject *
CIceBreaker_read_bytes(CIceBreaker *self, PyObject *o)
{
    unsigned long index = PyLong_AsUnsignedLong(o);
    if (index == (unsigned long)-1 && PyErr_Occurred()) {
        return NULL;
    }

    return read_bytes(self, index);
}

static PyObject *
read_const(CIceBreaker *self, unsigned int const_val, int* can_cache) {
    unsigned int data = const_val >> 8;
    switch (const_val & 0xff) {
        case 0x00:
            if (data == 0) {
                Py_INCREF(Py_None);
                return Py_None;
            }
            break;
        case 0x01:
            switch (data) {
                case 0: Py_INCREF(Py_False); return Py_False;
                case 1: Py_INCREF(Py_True); return Py_True;
                case 2: Py_INCREF(Py_Ellipsis); return Py_Ellipsis;
            }
            break;
        case 0x03: return read_int(self, data);
        case 0x04: return read_bigint(self, data);
        case 0x05: return read_bytes(self, data);
        case 0x06: return read_str(self, data);
        case 0x07: return read_float(self, data);
        case 0x08: return read_complex(self, data);
        case 0x09: return read_tuple(self, data, can_cache);
        case 0x0A:
            if (can_cache != NULL) {
                // caching anything containing a code object sets up a circular
                // reference leading to a memory leak.
                *can_cache = 0;
            }
            return read_code(self, data);
        case 0x0B: return read_frozenset(self, data);
    }

    PyErr_SetString(PyExc_ValueError, "Unknown constant");
    return NULL;
}

static PyObject *
CIceBreaker_read_const(CIceBreaker *self, PyObject *o)
{
    unsigned long const_val = PyLong_AsUnsignedLong(o);
    if (const_val == (unsigned long)-1 && PyErr_Occurred()) {
        return NULL;
    }

    return read_const(self, const_val, NULL);
}

static PyMethodDef CIceBreaker_methods[] = {
    {"__exit__",            (PyCFunction)CIceBreaker__exit__,  METH_VARARGS},
    {"find_module",         (PyCFunction)CIceBreaker_find_module, METH_O},
    {"read_bytes",          (PyCFunction)CIceBreaker_read_bytes, METH_O},
    {"read_code",           (PyCFunction)CIceBreaker_read_code,  METH_O},
    {"read_const",          (PyCFunction)CIceBreaker_read_const, METH_O},
    {"read_str",            (PyCFunction)CIceBreaker_read_str,  METH_O},
    {NULL,              NULL}           /* sentinel */
};

static PyObject *
CIceBreaker_New(PyTypeObject *subtype, PyObject *args, PyObject *kwds) {
    if (!PyTuple_Check(args)) {
        PyErr_SetString(PyExc_TypeError, "Expected tuple for args");
        return NULL;
    } else if (PyTuple_Size(args) != 2) {
        PyErr_SetString(PyExc_TypeError,
                        "CIceBreaker: expected buffer, base filename");
        return NULL;
    }

    PyObject* res = subtype->tp_alloc(subtype, 0);
    if (res == NULL) {
        return NULL;
    }

    CIceBreaker* breaker = (CIceBreaker*)res;
    Py_buffer codebuffer = {};
    PyObject* base_dir = PyTuple_GET_ITEM(args, 1);
    Py_INCREF(base_dir);

    if (!PyUnicode_CheckExact(base_dir)) {
        PyErr_SetString(PyExc_TypeError,
                        "CIceBreaker: expected base_dir to be a str");
        goto cleanup;
    } else if (PyObject_GetBuffer(PyTuple_GET_ITEM(args, 0), &codebuffer,
                           PyBUF_SIMPLE) != 0) {
        PyErr_SetString(PyExc_TypeError,
                        "CIceBreaker: expected bufferable argument");
        goto cleanup;
    }

    breaker->codebuffer = codebuffer;
    breaker->obj = PyTuple_GET_ITEM(args, 0);
    Py_INCREF(breaker->obj);
    breaker->base_dir = base_dir;
    Py_INCREF(breaker->base_dir);
    breaker->filename_map = PyDict_New();
    CLEANUP_IF_NULL(breaker->filename_map);

    IcePackHeader* header = (IcePackHeader*)codebuffer.buf;
    if (header->marker != 0x004b434150454349) { // ICEPACK\0
        PyErr_SetString(IcePackError,
                        "CIceBreaker: expected IcePack file, bad header");
        goto cleanup;
    } else if ((size_t)codebuffer.len < sizeof(IcePackHeader)) {
        PyErr_SetString(IcePackError,
                        "CIceBreaker: expected IcePack file, too short");
        goto cleanup;
    }

    char *mem = (char*)codebuffer.buf;

#define CLEANUP_IF_INVALID(x) if(!validate_index(breaker, x, "section")) goto cleanup;

    breaker->section_modules = (ModuleSection*)(mem + header->modules);
    CLEANUP_IF_INVALID(breaker->section_modules);
    breaker->section_code = (OffsetSection*)(mem + header->codes);
    CLEANUP_IF_INVALID(breaker->section_code);
    breaker->section_str = (OffsetSection*)(mem + header->strings);
    CLEANUP_IF_INVALID(breaker->section_str);
    breaker->section_bytes = (OffsetSection*)(mem + header->bytes);
    CLEANUP_IF_INVALID(breaker->section_bytes);
    breaker->section_int = (IntSection*)(mem + header->ints);
    CLEANUP_IF_INVALID(breaker->section_int);
    breaker->section_bigint = (OffsetSection*)(mem + header->bigints);
    CLEANUP_IF_INVALID(breaker->section_bigint);
    breaker->section_float = (FloatSection*)(mem + header->floats);
    CLEANUP_IF_INVALID(breaker->section_float);
    breaker->section_complex = (ComplexSection*)(mem + header->complexes);
    CLEANUP_IF_INVALID(breaker->section_complex);
    breaker->section_tuple = (OffsetSection*)(mem + header->tuples);
    CLEANUP_IF_INVALID(breaker->section_tuple);
    breaker->section_frozenset = (OffsetSection*)(mem + header->frozensets);
    CLEANUP_IF_INVALID(breaker->section_frozenset);

    breaker->str_cache = (PyObject**)PyMem_RawCalloc(
        breaker->section_str->count,
        sizeof(PyObject*));
    CLEANUP_IF_NULL(breaker->str_cache);

    breaker->bytes_cache = (PyObject**)PyMem_RawCalloc(
        breaker->section_bytes->count,
        sizeof(PyObject*));
    CLEANUP_IF_NULL(breaker->bytes_cache);

    breaker->int_cache = (PyObject**)PyMem_RawCalloc(
        breaker->section_int->count,
        sizeof(PyObject*));
    CLEANUP_IF_NULL(breaker->int_cache);

    breaker->bigint_cache = (PyObject**)PyMem_RawCalloc(
        breaker->section_bigint->count,
        sizeof(PyObject*));
    CLEANUP_IF_NULL(breaker->bigint_cache);

    breaker->float_cache = (PyObject**)PyMem_RawCalloc(
        breaker->section_float->count,
        sizeof(PyObject*));
    CLEANUP_IF_NULL(breaker->float_cache);

    breaker->complex_cache = (PyObject**)PyMem_RawCalloc(
        breaker->section_complex->count,
        sizeof(PyObject*));
    CLEANUP_IF_NULL(breaker->complex_cache);

    breaker->tuple_cache = (PyObject**)PyMem_RawCalloc(
        breaker->section_tuple->count,
        sizeof(PyObject*));
    CLEANUP_IF_NULL(breaker->tuple_cache);

    breaker->frozenset_cache = (PyObject**)PyMem_RawCalloc(
        breaker->section_frozenset->count,
        sizeof(PyObject*));
    CLEANUP_IF_NULL(breaker->frozenset_cache);

    return res;
cleanup:
    Py_DECREF(res);
    return NULL;

}

static PyObject *
CIceBreakerType_timestamp(CIceBreaker *self) {
    IcePackHeader* header = (IcePackHeader*)self->codebuffer.buf;
    unsigned int timestamp = header->timestamp;
    return PyLong_FromLong(timestamp);
}

static PyGetSetDef CIceBreaker_getset[] = {
    {
        "timestamp",
        (getter)CIceBreakerType_timestamp,
        (setter)NULL,
        "the timestamp of the latest file in the icepack",
        NULL
    },
 };

static PyTypeObject CIceBreakerType = {
    /* The ob_type field must be initialized in the module init function
     * to be portable to Windows without using C++. */
    PyVarObject_HEAD_INIT(NULL, 0)
    "_pyice.CIceBreaker",             /*tp_name*/
    sizeof(CIceBreaker),        /*tp_basicsize*/
    0,                          /*tp_itemsize*/
    /* methods */
    (destructor)CIceBreaker_dealloc,    /*tp_dealloc*/
    0,                          /*tp_print*/
    0,                          /*tp_getattr*/
    0,                          /*tp_setattr*/
    0,                          /*tp_reserved*/
    0,                          /*tp_repr*/
    0,                          /*tp_as_number*/
    0,                          /*tp_as_sequence*/
    0,                          /*tp_as_mapping*/
    0,                          /*tp_hash*/
    0,                          /*tp_call*/
    0,                          /*tp_str*/
    0,                          /*tp_getattro*/
    0,                          /*tp_setattro*/
    0,                          /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,         /*tp_flags*/
    0,                          /*tp_doc*/
    0,                          /*tp_traverse*/
    0,                          /*tp_clear*/
    0,                          /*tp_richcompare*/
    0,                          /*tp_weaklistoffset*/
    0,                          /*tp_iter*/
    0,                          /*tp_iternext*/
    CIceBreaker_methods,        /*tp_methods*/
    0,                          /*tp_members*/
    CIceBreaker_getset,         /*tp_getset*/
    0,                          /*tp_base*/
    0,                          /*tp_dict*/
    0,                          /*tp_descr_get*/
    0,                          /*tp_descr_set*/
    0,                          /*tp_dictoffset*/
    0,                          /*tp_init*/
    0,                          /*tp_alloc*/
    CIceBreaker_New,            /*tp_new*/
    0,                          /*tp_free*/
    0,                          /*tp_is_gc*/
};

/* --------------------------------------------------------------------- */

static void
CIceBreakerCode_dealloc(CIceBreakerCode *self)
{
    Py_DECREF(self->breaker);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyMethodDef CIceBreakerCode_methods[] = {
    {NULL,              NULL}           /* sentinel */
};

// This is similar to a memoryview, but is designed to be more compatible w/
/* Buffer methods */

static Py_ssize_t code_strides[1] = {1};

static int
codetype_getbuf(CIceBreakerCode *self, Py_buffer *view, int flags) {
  if (flags & PyBUF_WRITABLE) {
      PyErr_SetString(PyExc_BufferError,
          "CIceBreakerCode: read-only buffer");
      return -1;
  }

  view->obj = (PyObject*)self;
  Py_INCREF(self);
  view->buf = (void*)self->data;
  view->len = self->size;
  view->readonly = 1;
  view->itemsize = 1;
  view->ndim = 1;
  view->format = "B";
  view->suboffsets = NULL;
  view->shape = (Py_ssize_t*)&self->size;
  view->strides = &code_strides[0];
  view->internal = NULL;
  self->exports++;
  return 0;
}

static void
codetype_releasebuf(CIceBreakerCode *self, Py_buffer *view) {
    self->exports--;
    return;
}

static PyBufferProcs codetype_as_buffer = {
    (getbufferproc)codetype_getbuf,         /* bf_getbuffer */
    (releasebufferproc)codetype_releasebuf, /* bf_releasebuffer */
};

static Py_ssize_t
codetype_length(CIceBreakerCode *self) {
    return self->size;
}

static PyObject *
codetype_item(CIceBreakerCode *self, Py_ssize_t i) {
    if (i < 0 || (size_t)i >= self->size) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }
    return PyLong_FromLong((unsigned char)self->data[i]);
}

static int
codetype_contains(CIceBreakerCode *self, PyObject *arg) {
    return _Py_bytes_contains(self->data, self->size, arg);
}

PyObject *codetype_richcompare(CIceBreakerCode *a, PyObject *b, int op) {
    const char *bytes;
    size_t len;
    PyObject* result = NULL;
    Py_buffer codebuffer = {};

    if ((PyObject*)a == b) {
        switch (op) {
        case Py_EQ:
        case Py_LE:
        case Py_GE:
            /* a string is equal to itself */
            result = Py_True;
            break;
        case Py_NE:
        case Py_LT:
        case Py_GT:
            result = Py_False;
            break;
        default:
            PyErr_BadArgument();
            return NULL;
        }
    }

    if (PyBytes_Check(b)) {
        bytes = PyBytes_AS_STRING(b);
        len = PyBytes_GET_SIZE(b);
    } else if(CIceBreakerCodeObject_Check(b)) {
        bytes = ((CIceBreakerCode*)b)->data;
        len = ((CIceBreakerCode*)b)->size;
    } else if(PyObject_GetBuffer(b, &codebuffer, PyBUF_SIMPLE) != 0) {
        PyErr_Clear();
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    } else {
        bytes = codebuffer.buf;
        len = codebuffer.len;
    }

    if (op == Py_EQ || op == Py_NE) {
        if (len != a->size) {
            result = Py_False;
        } else if(len == 0) {
            result = Py_True;
        } else {
            int eq = memcmp(a->data, bytes, len);
            eq ^= (op == Py_EQ);
            result = eq ? Py_True : Py_False;
        }
    } else {
        Py_ssize_t min_len = Py_MIN(len, a->size);
        int c;
        if (min_len > 0) {
            c = memcmp(a->data, bytes, min_len);
        } else {
            c = 0;
        }
        if (c == 0) {
            c = (a->size < len) ? -1 : (a->size > len) ? 1 : 0;
        }
        switch (op) {
        case Py_LT: c = c <  0; break;
        case Py_LE: c = c <= 0; break;
        case Py_GT: c = c >  0; break;
        case Py_GE: c = c >= 0; break;
        default:
            PyErr_BadArgument();
            goto error;
        }
        result = c ? Py_True : Py_False;
    }

    Py_INCREF(result);
error:
    PyBuffer_Release(&codebuffer);
    return result;
}


static PyObject*
codetype_subscript(CIceBreakerCode* self, PyObject* item) {
    if (PyIndex_Check(item)) {
        Py_ssize_t i = PyNumber_AsSsize_t(item, PyExc_IndexError);
        if (i == -1 && PyErr_Occurred())
            return NULL;
        return codetype_item(self, i);
    } else if (PySlice_Check(item)) {
        Py_ssize_t start, stop, step, slicelength, cur, i;
        char* result_buf;
        PyObject* result;

        if (PySlice_Unpack(item, &start, &stop, &step) < 0) {
            return NULL;
        }
        slicelength = PySlice_AdjustIndices(self->size, &start,
                                            &stop, step);

        if (slicelength <= 0) {
            return PyBytes_FromStringAndSize("", 0);
        } else if (step == 1) {
            return PyBytes_FromStringAndSize(
                self->data + start,
                slicelength);
        } else {
            result = PyBytes_FromStringAndSize(NULL, slicelength);
            if (result == NULL) {
                return NULL;
            }

            result_buf = PyBytes_AS_STRING(result);
            for (cur = start, i = 0; i < slicelength; cur += step, i++) {
                result_buf[i] = self->data[cur];
            }

            return result;
        }
    } else {
        PyErr_Format(PyExc_TypeError,
                     "byte indices must be integers or slices, not %.200s",
                     Py_TYPE(item)->tp_name);
        return NULL;
    }
}

static Py_hash_t
codetype_hash(CIceBreakerCode *a) {
    if (a->hash == -1) {
        /* Can't fail */
        a->hash = _Py_HashBytes(a->data, a->size);
    }
    return a->hash;
}


static PySequenceMethods codetype_as_sequence = {
    (lenfunc)codetype_length, /*sq_length*/
    0, /*sq_concat*/
    0, /*sq_repeat*/
    (ssizeargfunc)codetype_item, /*sq_item*/
    0,                  /*sq_slice*/
    0,                  /*sq_ass_item*/
    0,                  /*sq_ass_slice*/
    (objobjproc)codetype_contains /*sq_contains*/
};

static PyMappingMethods codetype_as_mapping = {
    (lenfunc)codetype_length,
    (binaryfunc)codetype_subscript,
    0,
};

static PyTypeObject CIceBreakerCodeType = {
    /* The ob_type field must be initialized in the module init function
     * to be portable to Windows without using C++. */
    PyVarObject_HEAD_INIT(NULL, 0)
    "_pyice.CIceBreakerCode",             /*tp_name*/
    sizeof(CIceBreakerCode),        /*tp_basicsize*/
    0,                          /*tp_itemsize*/
    /* methods */
    (destructor)CIceBreakerCode_dealloc,    /*tp_dealloc*/
    0,                          /*tp_print*/
    0,                          /*tp_getattr*/
    0,                          /*tp_setattr*/
    0,                          /*tp_reserved*/
    0,                          /*tp_repr*/
    0,                          /*tp_as_number*/
    &codetype_as_sequence,      /*tp_as_sequence*/
    &codetype_as_mapping,       /*tp_as_mapping*/
    (hashfunc)codetype_hash,    /*tp_hash*/
    0,                          /*tp_call*/
    0,                          /*tp_str*/
    0,                          /*tp_getattro*/
    0,                          /*tp_setattro*/
    &codetype_as_buffer,        /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,         /*tp_flags*/
    0,                          /*tp_doc*/
    0,                          /*tp_traverse*/
    0,                          /*tp_clear*/
    (richcmpfunc)&codetype_richcompare,                          /*tp_richcompare*/
    0,                          /*tp_weaklistoffset*/
    0,                          /*tp_iter*/
    0,                          /*tp_iternext*/
    CIceBreakerCode_methods,    /*tp_methods*/
    0,                          /*tp_members*/
    0,                          /*tp_getset*/
    0,                          /*tp_base*/
    0,                          /*tp_dict*/
    0,                          /*tp_descr_get*/
    0,                          /*tp_descr_set*/
    0,                          /*tp_dictoffset*/
    0,                          /*tp_init*/
    0,                          /*tp_alloc*/
    0,                          /*tp_new*/
    0,                          /*tp_free*/
    0,                          /*tp_is_gc*/
};


static void
CObjectValue_dealloc(CObjectValue *self) {
    Py_DECREF(self->value);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyMethodDef CObjectValue_methods[] = {
    {NULL,              NULL}           /* sentinel */
};

Py_hash_t CObjectValue_hash(CObjectValue *o) {
    return o->hash;
}

int float_equals(double a, double b) {
    if (Py_IS_NAN(a) && Py_IS_NAN(b)) {
        return 1;
    } else if (a == 0.0 && b == 0.0) {
        if (copysign(1, a) != copysign(1, b)) {
            return 0;
        }
    }
    return a == b;
}

PyObject *CObjectValue_richcompare(CObjectValue *a, PyObject *b, int op) {
    if (op != Py_EQ) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    } else if (b->ob_type != &CObjectValueType) {
        Py_INCREF(Py_False);
        return Py_False;
    }

    CObjectValue* obval = (CObjectValue*)b;
    if (a->hash != obval->hash || a->value->ob_type != obval->value->ob_type) {
        Py_INCREF(Py_False);
        return Py_False;
    } else if (PyFloat_Check(a->value)) {
        double af = PyFloat_AsDouble(a->value);
        double bf = PyFloat_AsDouble(obval->value);
        if (float_equals(af, bf)) {
            Py_INCREF(Py_True);
            return Py_True;
        } else {
            Py_INCREF(Py_False);
            return Py_False;
        }
    } else if (PyComplex_Check(a->value)) {
        Py_complex ac = PyComplex_AsCComplex(a->value);
        Py_complex bc = PyComplex_AsCComplex(obval->value);
        if (float_equals(ac.real, bc.real) && float_equals(ac.imag, bc.imag)) {
            Py_INCREF(Py_True);
            return Py_True;
        } else {
            Py_INCREF(Py_False);
            return Py_False;
        }
    }
    return PyObject_RichCompare(a->value, obval->value, op);
}

static PyObject* CObjectValue_wrap(PyObject* from) {
  PyObject* value = from;
  CObjectValue* self = PyObject_New(CObjectValue, &CObjectValueType);
  if (self == NULL) {
      return NULL;
  }

  if (PyTuple_Check(value)) {
      PyObject* tuple = PyTuple_New(PyTuple_Size(value));
      if (tuple == NULL) {
          Py_DECREF(self);
          return NULL;
      }
      for (int i = 0; i<PyTuple_Size(value); i++) {
          PyObject* wrapped = CObjectValue_wrap(PyTuple_GET_ITEM(value, i));
          if (wrapped == NULL) {
              Py_DECREF(self);
              Py_DECREF(tuple);
              return NULL;
          }
          PyTuple_SET_ITEM(tuple, i, wrapped);
      }
      self->value = tuple;
  } else if (PyFrozenSet_CheckExact(value)) {
      PyObject* set = PyFrozenSet_New(NULL);
      if (set == NULL) {
          Py_DECREF(self);
          return NULL;
      }
      Py_ssize_t pos = 0;
      PyObject *key;
      Py_hash_t hash;
      while (_PySet_NextEntry(value, &pos, &key, &hash)) {
          PyObject* wrapped = CObjectValue_wrap(key);
          if (wrapped == NULL || PySet_Add(set, wrapped) == -1) {
              Py_DECREF(self);
              Py_DECREF(set);
              return NULL;
          }
      }
      self->value = set;
  } else {
      self->value = value;
      Py_INCREF(self->value);
  }

  self->hash = PyObject_Hash(self->value);
  return (PyObject*)self;
}

static PyObject *
CObjectValue_New(PyTypeObject *subtype, PyObject *args, PyObject *kwds) {
    if (!PyTuple_CheckExact(args)) {
        return NULL;
    } else if(PyTuple_Size(args) != 1) {
        PyErr_SetString(PyExc_ValueError, "Expected a single argument");
        return NULL;
    }

    return CObjectValue_wrap(PyTuple_GET_ITEM(args, 0));
}

static PyObject *
CObjectValue_value(CObjectValue *self) {
    Py_INCREF(self->value);
    return self->value;
}

static PyGetSetDef CObjectValue_getset[] = {
    {"value",
     (getter)CObjectValue_value, (setter)NULL,
     "the wrapped value",
     NULL},
 };

static PyTypeObject CObjectValueType = {
    /* The ob_type field must be initialized in the module init function
     * to be portable to Windows without using C++. */
    PyVarObject_HEAD_INIT(NULL, 0)
    "_pyice.CObjectValue",             /*tp_name*/
    sizeof(CObjectValue),        /*tp_basicsize*/
    0,                          /*tp_itemsize*/
    /* methods */
    (destructor)CObjectValue_dealloc,    /*tp_dealloc*/
    0,                          /*tp_print*/
    0,                          /*tp_getattr*/
    0,                          /*tp_setattr*/
    0,                          /*tp_reserved*/
    0,                          /*tp_repr*/
    0,                          /*tp_as_number*/
    0,                          /*tp_as_sequence*/
    0,                          /*tp_as_mapping*/
    (hashfunc)CObjectValue_hash, /*tp_hash*/
    0,                          /*tp_call*/
    0,                          /*tp_str*/
    0,                          /*tp_getattro*/
    0,                          /*tp_setattro*/
    0,                          /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,         /*tp_flags*/
    0,                          /*tp_doc*/
    0,                          /*tp_traverse*/
    0,                          /*tp_clear*/
    (richcmpfunc)CObjectValue_richcompare,   /*tp_richcompare*/
    0,                          /*tp_weaklistoffset*/
    0,                          /*tp_iter*/
    0,                          /*tp_iternext*/
    CObjectValue_methods,       /*tp_methods*/
    0,                          /*tp_members*/
    CObjectValue_getset,                          /*tp_getset*/
    0,                          /*tp_base*/
    0,                          /*tp_dict*/
    0,                          /*tp_descr_get*/
    0,                          /*tp_descr_set*/
    0,                          /*tp_dictoffset*/
    0,                          /*tp_init*/
    0,                          /*tp_alloc*/
    CObjectValue_New,                          /*tp_new*/
    0,                          /*tp_free*/
    0,                          /*tp_is_gc*/
};

/* List of functions defined in the module */

static PyMethodDef pyice_methods[] = {
    {NULL,              NULL}           /* sentinel */
};

PyDoc_STRVAR(module_doc, "Provides C accelerators for the PyIce package.");

static int
pyice_exec(PyObject *m) {
     /* object; doing it here is required for portability, too. */
    if (PyType_Ready(&CIceBreakerType) < 0)
        goto fail;
    if (PyType_Ready(&CIceBreakerCodeType) < 0)
        goto fail;
    if (PyType_Ready(&CObjectValueType) < 0)
        goto fail;

    IcePackError = PyErr_NewException("pyice.IcepackError", NULL, NULL);
    if (IcePackError == NULL) {
        goto fail;
    }

    /* Add exception object to your module */
    PyModule_AddObject(m, "IcePackError", IcePackError);
    PyModule_AddObject(m, "CIceBreaker", (PyObject *)&CIceBreakerType);
    PyModule_AddObject(m, "CIceBreakerCode", (PyObject *)&CIceBreakerCodeType);
    PyModule_AddObject(m, "CObjectValue", (PyObject *)&CObjectValueType);

    return 0;
 fail:
    Py_XDECREF(m);
    return -1;
}

static struct PyModuleDef_Slot pyice_slots[] = {
    {Py_mod_exec, pyice_exec},
    {0, NULL},
};

static struct PyModuleDef pyicemodule = {
    PyModuleDef_HEAD_INIT,
    "_pyice",
    module_doc,
    0,
    pyice_methods,
    pyice_slots,
    NULL,
    NULL,
    NULL
};

/* Export function for the module */

PyMODINIT_FUNC
PyInit__pyice(void)
{
    return PyModuleDef_Init(&pyicemodule);
}

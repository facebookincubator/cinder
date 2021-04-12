#include <stddef.h>

#include "switchboard.h"

/* A subscription for changes to an object */
typedef struct {
    PyObject_HEAD

    Switchboard_Callback callback;

    /* An argument to callback, may be NULL */
    PyObject *arg;

    /* A weak reference to the object we've subscribed to */
    PyObject *watched;
} ObjSubscr;

static int obj_subscr_traverse(ObjSubscr *self, visitproc visit, void *arg);
static void obj_subscr_dealloc(ObjSubscr *subscr);

PyTypeObject ObjSubscr_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "ObjSubscr",
    .tp_basicsize = sizeof(ObjSubscr),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc) obj_subscr_traverse,
    .tp_dealloc = (destructor) obj_subscr_dealloc,
};

static ObjSubscr *
obj_subscr_new(PyObject *obj, Switchboard_Callback callback, PyObject *arg)
{
    ObjSubscr *subscr = PyObject_GC_New(ObjSubscr, &ObjSubscr_Type);
    if (subscr == NULL) {
        return NULL;
    }

    /* This only creates a new weak reference if one did not already exist. Otherwise
     * the pre-existing weakref is returned.
     */
    subscr->watched = PyWeakref_NewRef(obj, NULL);
    if (subscr->watched == NULL) {
        Py_DECREF(subscr);
        return NULL;
    }
    subscr->callback = callback;
    Py_XINCREF(arg);
    subscr->arg = arg;

    PyObject_GC_Track((PyObject *) subscr);

    return subscr;
}

static int
obj_subscr_traverse(ObjSubscr *self, visitproc visit, void *arg)
{
    Py_VISIT(self->arg);
    Py_VISIT(self->watched);
    return 0;
}

static void
obj_subscr_dealloc(ObjSubscr *subscr)
{
    PyObject_GC_UnTrack((PyObject *) subscr);

    Py_XDECREF(subscr->arg);
    Py_XDECREF(subscr->watched);

    PyObject_GC_Del((PyObject *) subscr);
}

/*
 * These callbacks are used to notify the switchboard that an object it has
 * subscribers for has been reclaimed.
 *
 * It handles notifying any registered subscriptions and then removes them
 * from the switchboard.
 */
typedef struct {
    PyObject_HEAD

    /* A weak reference to the switchboard to notify */
    PyObject *switchboard_ref;
} ObjGoneCallback;

static PyObject *obj_gone_callback_call(PyObject *callable, PyObject *args, PyObject *kwargs);
static int obj_gone_callback_traverse(ObjGoneCallback *self, visitproc visit, void *arg);
static void obj_gone_callback_dealloc(ObjGoneCallback *self);

static void switchboard_notify_gone(Switchboard *switchboard, PyObject *ref);

PyTypeObject ObjGoneCallback_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "ObjGoneCallback",
    .tp_basicsize = sizeof(ObjGoneCallback),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_call = obj_gone_callback_call,
    .tp_traverse = (traverseproc) obj_gone_callback_traverse,
    .tp_dealloc = (destructor) obj_gone_callback_dealloc,
};

#define ObjGoneCallback_Check(obj) (Py_TYPE(obj) == &ObjGoneCallback_Type)

static ObjGoneCallback *
obj_gone_callback_new(Switchboard *switchboard)
{
    ObjGoneCallback *cb = PyObject_GC_New(ObjGoneCallback, &ObjGoneCallback_Type);
    if (cb == NULL) {
        return NULL;
    }

    cb->switchboard_ref = PyWeakref_NewRef((PyObject *) switchboard, NULL);
    if (cb->switchboard_ref == NULL) {
        Py_DECREF(cb);
        return NULL;
    }

    PyObject_GC_Track((PyObject *) cb);

    return cb;
}

static PyObject *
obj_gone_callback_call(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    assert(ObjGoneCallback_Check(callable));
    ObjGoneCallback *cb = (ObjGoneCallback *) callable;
    PyObject *ref = PyTuple_GetItem(args, 0);
    PyObject *switchboard = PyWeakref_GetObject(cb->switchboard_ref);
    if (switchboard != Py_None) {
        switchboard_notify_gone((Switchboard *) switchboard, ref);
    }
    Py_RETURN_NONE;
}

static int
obj_gone_callback_traverse(ObjGoneCallback *self, visitproc visit, void *arg)
{
    Py_VISIT(self->switchboard_ref);
    return 0;
}

static void
obj_gone_callback_dealloc(ObjGoneCallback *self)
{
    PyObject_GC_UnTrack((PyObject *) self);
    Py_XDECREF(self->switchboard_ref);
    PyObject_GC_Del(self);
}

static int switchboard_traverse(Switchboard *self, visitproc visit, void *arg);
static int switchboard_clear(Switchboard *switchboard);
static void switchboard_dealloc(Switchboard *switchboard);

PyTypeObject Switchboard_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "Switchboard",
    .tp_basicsize = sizeof(Switchboard),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc) switchboard_traverse,
    .tp_clear = (inquiry) switchboard_clear,
    .tp_dealloc = (destructor) switchboard_dealloc,
    .tp_weaklistoffset = offsetof(Switchboard, weaklist),
};

int
Switchboard_Init(void)
{
    if (PyType_Ready(&ObjSubscr_Type) < 0) {
        return -1;
    }
    if (PyType_Ready(&ObjGoneCallback_Type) < 0) {
        return -1;
    }
    return PyType_Ready(&Switchboard_Type);
}

Switchboard *
Switchboard_New(void)
{
    if (Switchboard_Init() < 0) {
        return NULL;
    }

    Switchboard *switchboard = PyObject_GC_New(Switchboard, &Switchboard_Type);
    if (switchboard == NULL) {
        return NULL;
    }

    switchboard->subscrs = PyDict_New();
    if (switchboard->subscrs == NULL) {
        Py_DECREF(switchboard);
        return NULL;
    }
    switchboard->weaklist = NULL;

    switchboard->obj_gone_callback = (PyObject *)obj_gone_callback_new(switchboard);
    if (switchboard->obj_gone_callback == NULL) {
        Py_DECREF(switchboard);
        return NULL;
    }

    PyObject_GC_Track((PyObject *) switchboard);

    return switchboard;
}

static int
switchboard_traverse(Switchboard *self, visitproc visit, void *arg)
{
    Py_VISIT(self->subscrs);
    return 0;
}

static int
switchboard_clear(Switchboard *switchboard)
{
    Py_CLEAR(switchboard->subscrs);
    return 0;
}

static void
switchboard_dealloc(Switchboard *switchboard)
{
    PyObject_GC_UnTrack((PyObject *) switchboard);

    Py_DECREF(switchboard->subscrs);
    Py_DECREF(switchboard->obj_gone_callback);

    PyObject_GC_Del((PyObject *) switchboard);
}

PyObject *
Switchboard_Subscribe(Switchboard *switchboard, PyObject *obj, Switchboard_Callback cb, PyObject *cb_arg) {
    ObjSubscr *subscr = obj_subscr_new(obj, cb, cb_arg);
    if (subscr == NULL) {
        return NULL;
    }

    PyObject *subscrs = PyDict_GetItem(switchboard->subscrs, subscr->watched);
    if (subscrs == NULL) {
        /* No subscriptions for obj yet */
        subscrs = PySet_New(NULL);
        if (subscrs == NULL) {
            Py_DECREF(subscr);
            return NULL;
        }

        PyObject *key = PyWeakref_NewRef(obj, switchboard->obj_gone_callback);
        if (key == NULL) {
            Py_DECREF(subscr);
            Py_DECREF(subscrs);
            return NULL;
        }

        if (PyDict_SetItem(switchboard->subscrs, key, subscrs) != 0) {
            Py_DECREF(subscrs);
            Py_DECREF(subscr);
            Py_DECREF(key);
            return NULL;
        }
        Py_DECREF(key);
    } else {
        Py_INCREF(subscrs);
    }

    if (PySet_Add(subscrs, (PyObject *) subscr) < 0) {
        Py_DECREF(subscr);
        subscr = NULL;
    }
    Py_DECREF(subscrs);

    return (PyObject *) subscr;
}

static PyObject *
duplicate(PyObject *sequence)
{
    Py_ssize_t size = PyObject_Size(sequence);
    if (size < 0) {
        return NULL;
    }

    PyObject *result = PyTuple_New(size);
    if (result == NULL) {
        return NULL;
    }

    PyObject *iter = PyObject_GetIter(sequence);
    if (iter == NULL) {
        Py_DECREF(result);
        return NULL;
    }

    PyObject *item = NULL;
    Py_ssize_t i = 0;
    while ((item = PyIter_Next(iter))) {
        /* PyTuple_SetItem steals a reference */
        Py_INCREF(item);
        PyTuple_SetItem(result, i, item);
        Py_DECREF(item);
        i++;
    }
    Py_DECREF(iter);

    if (PyErr_Occurred()) {
        Py_DECREF(result);
        result = NULL;
    }

    return result;
}

int
Switchboard_Notify(Switchboard *switchboard, PyObject *obj)
{
    PyObject *ref = PyWeakref_NewRef(obj, NULL);
    if (ref == NULL) {
        return -1;
    }

    PyObject *subscrs = PyDict_GetItem(switchboard->subscrs, ref);
    Py_DECREF(ref);
    if (subscrs == NULL) {
        /* No subscriptions for obj; nothing to do */
        return 0;
    }

    /* Copy subscriptions in case any of the callbacks modify the original set */
    PyObject *subscrs_copy = duplicate(subscrs);
    if (subscrs_copy == NULL) {
        return -1;
    }

    /* Invoke the callbacks */
    Py_ssize_t num_subscrs = PyTuple_Size(subscrs_copy);
    for (Py_ssize_t i = 0; i < num_subscrs; i++) {
        ObjSubscr *subscr = (ObjSubscr *) PyTuple_GetItem(subscrs_copy, i);
        subscr->callback((PyObject *) subscr, subscr->arg, subscr->watched);
    }

    Py_DECREF(subscrs_copy);

    return 0;
}

static void
switchboard_notify_gone(Switchboard *switchboard, PyObject *ref)
{
    assert(PyWeakref_Check(ref));

    PyObject *subscrs = PyDict_GetItem(switchboard->subscrs, ref);
    if (subscrs == NULL) {
        return;
    }

    Py_INCREF(subscrs);
    PyDict_DelItem(switchboard->subscrs, ref);

    PyObject *iter = PyObject_GetIter(subscrs);
    if (iter == NULL) {
        Py_DECREF(subscrs);
        return;
    }

    /* Notify all subscribers */
    PyObject *item = NULL;
    while ((item = PyIter_Next(iter))) {
        ObjSubscr *subscr = (ObjSubscr *) item;
        subscr->callback(item, subscr->arg, subscr->watched);
        Py_DECREF(item);
    }
    Py_DECREF(iter);
    Py_DECREF(subscrs);

    if (PyErr_Occurred()) {
        /* An error ocurred while iterating through the subscriptions. There
           isn't anything we can do at this point, since the subscribed object
           is gone.  Clear the error and move on.
        */
        PyErr_Clear();
    }
}

Py_ssize_t
Switchboard_GetNumSubscriptions(Switchboard *switchboard, PyObject *object)
{
    PyObject *ref = PyWeakref_NewRef(object, NULL);
    if (ref == NULL) {
        return -1;
    }

    PyObject *subscrs = PyDict_GetItem(switchboard->subscrs, ref);
    Py_DECREF(ref);
    if (subscrs == NULL) {
        /* No subscriptions for obj; nothing to do */
        return 0;
    }

    return PyObject_Size(subscrs);
}

int
Switchboard_Unsubscribe(Switchboard *switchboard, PyObject *subscr)
{
    PyObject *watched = ((ObjSubscr *) subscr)->watched;
    PyObject *subscrs = PyDict_GetItem(switchboard->subscrs, watched);
    if (subscrs == NULL) {
        return 0;
    }

    if (PySet_Discard(subscrs, subscr) < 0) {
        return -1;
    }

    if (PyObject_Size(subscrs) == 0) {
        PyDict_DelItem(switchboard->subscrs, watched);
    }

    return 1;
}

int
Switchboard_UnsubscribeAll(Switchboard *switchboard, PyObject *handles)
{
    PyObject *iter = PyObject_GetIter(handles);
    if (iter == NULL) {
        return -1;
    }

    PyObject *subscr = NULL;
    while ((subscr = PyIter_Next(iter))) {
        Switchboard_Unsubscribe(switchboard, subscr);
        Py_DECREF(subscr);
    }
    Py_DECREF(iter);

    if (PyErr_Occurred()) {
        return -1;
    }

    return 0;
}

#ifndef SWITCHBOARD_H
#define SWITCHBOARD_H

#include "Python.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A switchboard provides an abstraction for broadcasting changes to an object
 * to a set of subscribers.
 *
 * Switchboards are used to notify the JIT about changes to dependencies of JIT
 * compiled functions. The JIT makes assumptions about the state of the world
 * in order to generate more efficient code. As a result, it may need to
 * de-optimize JIT compiled functions when any of the assumptions are no longer
 * valid.
 *
 * For example, the JIT compiled version of a function depends on the code
 * object that is attached to the function. When the code object is
 * re-assigned, the compiled code is no longer valid and we'll need to fall
 * back to the interpreted version (or recompile).
 *
 * There is typically one switchboard per type being monitored (e.g. Function,
 * Type). When a Function is JIT compiled, the JIT subscribes to the
 * appropriate objects using Switchboard_Subscribe and the appropriate
 * switchboard. When a change occurs, the changed object is responsible for
 * using Switchboard_Notify to notify subscribers. If the object is gc-ed,
 * the Switchboard will handle notifying subscribers that the object has
 * gone away and will remove all subscribers.
 *
 * To avoid keeping subscribed objects alive, switchboards must not keep strong
 * references to them. These creates an unfortunate amount of complexity, as we
 * must store weak references to an object that is being watched.
 */

/*
 * A callback is invoked when the object that is monitored by a subscription
 * changes.
 *
 *   handle  - An opaque handle that represents the subscription. It may be used
 *             to unsubscribe.
 *   arg     - An arbitrary argument that was registered with the subscription.
 *   watched - A weak reference to the object being monitored.
 */
typedef void (*Switchboard_Callback)(PyObject *handle, PyObject *arg, PyObject *watched);

typedef struct {
    PyObject_HEAD

    /*
     * Dictionary mapping a weakref for an object to the set of subscriptions for
     * the object.
     */
    PyObject *subscrs;

    /* Head of the list of weak references to the switchboard */
    PyObject *weaklist;

    /* Callback object to notify subscribers when an object is destroyed. */
    PyObject *obj_gone_callback;
} Switchboard;

/*
 * This must be called before using any switchboard functionality.
 */
int Switchboard_Init(void);

/*
 * Create a new switchboard.
 *
 * Returns the switchboard on success, or NULL on error.
 */
Switchboard *Switchboard_New(void);

/*
 * Watch an object for changes.
 *
 * obj must be weak-referenceable.
 *
 * Callback will be called with arg when the watch is triggered; pass NULL to
 * supply no argument.
 *
 * Returns a handle to the subscription on success or NULL on error.
 */
PyObject *Switchboard_Subscribe(Switchboard *switchboard, PyObject *obj, Switchboard_Callback cb, PyObject *cb_arg);

/*
 * Notify subscribers that an object has changed.
 *
 * This will invoke any callbacks that were registered for the object that
 * changed. This does not clear any subscriptions.
 *
 * Returns 0 on success and -1 on error.
 */
int Switchboard_Notify(Switchboard *switchboard, PyObject *object);

/*
 * Return the number of subscriptions for object.
 *
 * Returns a value >= 0 on success or -1 on error.
 */
Py_ssize_t Switchboard_GetNumSubscriptions(Switchboard *switchboard, PyObject *object);

/*
 * Remove a subscription
 *
 * Returns
 *     1 if the subscription existed and was removed successfully
 *     0 if the subscription did not exist
 *    -1 if an error occurred
 */
int Switchboard_Unsubscribe(Switchboard *switchboard, PyObject *handle);

/*
 * Remove all supplied subscriptions
 *
 * handles is expected to be an iterable of subscription objects
 *
 * Returns
 *     0 on success
 *    -1 if an error occurred
 */
int Switchboard_UnsubscribeAll(Switchboard *switchboard, PyObject *handles);

#ifdef __cplusplus
}
#endif
#endif /* SWITCHBOARD_H */

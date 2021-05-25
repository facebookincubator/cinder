.. fbmeta::
    hide_page_title=true

Shadow Byte Code
################

What is Shadow Byte Code?
-------------------------

Shadow byte code is Cinder's implementation of inline caching which uses
a hidden copy of the byte code.  Python usually exposes the byte code to the
running program as an immutable byte array.  The hidden, "shadow byte code",
is mutable and can be updated in-place in order to provide more specialized
versions of opcodes.  The shadow byte code is only initialized once a function
is determined to have become "hot", which is when it has been executed a
minimal number of times.  Shadow byte code is not observable to the running
program except for the fact that it improves performance of the running code.

Patching byte code
------------------

Once a method has had shadow code created, patching it is a fairly simple process.
The eval loop is updated to use a new data structure, `_PyShadow_EvalState`,
that tracks the code object, the shadow code object, and the first instruction.
APIs in the shadow byte code receive this data structure along with the
current instruction.  From there a simple API is provided to replace the current
instruction:

.. code-block:: C

    int
    _PyShadow_PatchByteCode(_PyShadow_EvalState *shadow,
                            const _Py_CODEUNIT *next_instr,
                            int op,
                            int arg);

This will return 0 if the opcode can successfully be patched.

With this API it is simple to patch any method as long as the desired inputs
are encoded into the oparg.  For more complicated opcodes the oparg will need
to refer to some cache data which will need to be managed externally.  There's
no prescribed way to manage this data, and shadow byte code uses several different
strategies depending on what makes the most sense.


Opcodes targeted
-----------------

Shadow byte code replaces a number of Python opcodes.  The first two that were
implemented were `LOAD_ATTR` and `LOAD_GLOBAL`.  These are some of the hottest
opcodes in terms of a combination of hit count and runtime.  It has expanded
to cover other opcodes such as `LOAD_METHOD`, `STORE_ATTR`, and
specialized versions of `BINARY_SUBSCR`.  More opcodes can easily be added
based upon profiling data.

Each of these opcodes can have varying strategies for the caching information.
For more complicated opcodes such as `LOAD_ATTR` and `STORE_ATTR` a multi-level
caching strategy is employed.  For simpler optimized opcodes like
`BINARY_SUBSCR_TUPLE_CONST_INT`, which replaces a `LOAD_CONST`/`BINARY_SUBSCR`
pair, no extra data structures are used and the relevant information (the index)
is encoded directly into the oparg.  `LOAD_GLOBAL` represents something of
an in-between in complexity where it's easy to identify caches that can be
shared across the function.

Shadow Code Data Structure
--------------------------

.. code-block:: C

    typedef struct _PyShadowCode {
        _ShadowCache l1_cache;
        GlobalCacheEntry *globals;
        _PyShadow_InstanceAttrEntry ***polymorphic_caches;

        Py_ssize_t update_count;

        int globals_size;
        int polymorphic_caches_size;

        Py_ssize_t len;
        _Py_CODEUNIT code[];
    } _PyShadowCode;

The _PyShadowCode data structure includes the information necessary to provide
the optimized code and locations to store the caches and is associated with a
code object.  More caches can be added as necessary, and generally represent
a small amount of overhead compared to the code.

The current implementation has a few different cache lines.  The first is the
l1 cache used for attribute access (LOAD_ATTR, STORE_ATTR, and LOAD_METHOD).
There is one entry in this array for each name/type pair that is accessed in
the function.  The second is the globals cache, with one entry for each global
name accessed in the function.  And finally are a set of polymorphic caches -
one cache exists for each `LOAD_ATTR` site which has gone polymorphic.

We also track how much a function has been seeing updates to existing cache
lines via `update_count` and will stop updating it if it appears to be highly
polymorphic.  Existing settled opcodes will continue to use their populated
cache information.  This is a compromise on space to avoid having to track on a
per-opcode basis.

Finally the end of the data structure contains the byte code which will
be mutated in place.  This is initially populated with a copy of the
byte code from the initial code object.


Caching Strategies
------------------
Different opcodes require different caching strategies.  This is because the
opcodes are working in rather different domains, and have very different
complexity for what you want to optimize.  Caching strategies can therefore
range from having a complex set of data structures in the case of attributes
on instances to having no additional metadata necessary for opcodes which
are designed to specialize around a well-known data type.

Instance Attribute Caches
~~~~~~~~~~~~~~~~~~~~~~~~~
The instance caches represent the most complicated inline cache implementation
in shadow byte code, partially because attribute resolution in Python is
rather complicated.  Ultimately they cover attributes that are resolved from
simple objects (that use `PyObject_GenericGetAttr` and
`PyObject_GenericSetAttr` for their resolution), type objects (which have not
overridden attribute access), and module objects.

For these caches shadow byte code uses a 2-level cache.  An L2 cache exists
that lives logically off the type object or module object.  This L2 cache is
actually a weak reference subclass which can be quickly recovered
from the object.  This scheme was chosen to provide 100% binary compatibility
to not need to introduce modifications to type or module objects. The L2 cache
then contains a dictionary which maps from attribute name to a cache instance.
The cache instances are then shared amongst the code objects in the L1 caches,
so only one  cache entry exists for each cache object.

Beyond the sharing of the cache information there's an additional benefit -
this allows the caches to be invalidated when a type is modified.  This
reduces the cache-hit test down to loading the objects type and the type
from the cache instance (versus relying upon checking the type version
tag in the current Python 3.10 caches).  So there's no need to check if a
type has a valid version tag, or if the version tag is correct.


This is ultimately trading off reads from the type object for reads from
the shadow byte code.  Ultimately we push this all the way to not reading
from the type at all, instead pushing all of the necessary data into the
shared cache entry. This requires slightly chunkier cache entries, but
the sharing makes the tradeoff memory neutral.

.. code-block:: C

    typedef struct {
        PyObject head;
        PyObject *name;     /* name of the attribute we cache for */
        PyTypeObject *type; /* target type we're caching against, borrowed */
        PyObject *value;    /* descriptor if one is present, borrowed */

        size_t dictoffset;
        Py_ssize_t splitoffset;
        Py_ssize_t nentries;
        PyDictKeysObject *keys;
    } _PyShadow_InstanceAttrEntry;


Some of the additional data here makes the lookup into the split dictionary
case a little more efficient.  Having keys lets us validate that the dictionary
is good with a single comparison, and we also store the keys with a low-bit
set (`POISONED_DICT_KEYS`) to indicate when there is an explicit miss against
a split dictionary.  This gives us caching for not only when there is a hit
because the value is in the dictionary, but also gives us a fast hit for when
the value is not in the dictionary.  The latter case is important for the
performance of looking up methods which are typically not shadowed by instance
members.

The name can usually easy be recovered from the current opcode, but we store it
here as there's enough situations where we need need the name to do the dictionary
lookup.

The current implementation doesn't attempt to limit the size of these caches based
upon what data is necessary.  For example we could have smaller cache instances
for built-in types which have no dictionaries, as they'll never need the
second half of the data structure.  But it should be a relatively easy optimization
to add.  A smaller more specialized data structure is used for module attributes.

Polymorphic Caches
~~~~~~~~~~~~~~~~~~

For call sites which are hitting multiple types we extend the instance cache to
support a limited number of lookups.  In this case we simply re-use the instance
caches in an array of _PyShadow_InstanceAttrEntry which we can quickly run through,
do a simple type test, and then dispatch into the inline cache code for that
style of instance attribute.  This is another one of the advantages of having the
shared cache instances.

To enable fast dispatch to the correct implementation of the inline cache implementation
each of the _PyShadow_InstanceAttrEntry instances has one of several different types,
based upon what data they care about.  The types are actually slightly extended
PyType_Object's such that they can have a few additional fields after, one of which
is the opcode for handling the load.

Because polymorphic caches are likely to vary heavily based upon the call site there
are no attempts made to share caches between `LOAD_ATTR` opcodes within the same
method.  Instead each cache gets a unique array for the call site.  Therefore we
are limited to 256 polymorphic calls per-function.

Global Caches
~~~~~~~~~~~~~

Our global caches use yet another strategy for caching the global values.  Because
all globals will refer to the same value when the shadow cache is initiated we scan
the byte code for all uses of `LOAD_GLOBAL`.  We calculate how many global variables
are referred to, and then allocate the number of global caches needed for the entire
function.  This gives us a fixed size array which can be stored locally in the eval
loop which is slightly more efficient than the attribute l1 caches which can potentially
change.

We've explored two different ways of caching global lookups.  The current implementation
in Cinder uses dictionary watchers, which turn the global load into a single indirection.
The array of globals includes pointers to a location in memory which is updated based
upon the current value in the combination of the globals and builtins dictionary.  If
the value is in the globals, it holds that value.  If the value is not in the globals
dictionary it holds the value in the builtins or NULL.  When either the globals or
builtins module is written to the value in the indirect pointer is updated.  This allows
the global to be fetched with a single read.

Previous to this scheme we had an array that includes the name, the value, and a single
version.  This version is `max(globals, builtins)` when the value was cached.  This
requires checking the version of the dictionaries and making sure
`max(globals, builtins) == cache_version`.

Cache Free Opcodes
~~~~~~~~~~~~~~~~~~
The optimized versions of `BINARY_SUBSCR` demonstrate a fourth strategy for the caches.
In these no external data is necessary.  Most variations of these don't require any
oparg at all - they just do fast checks on the receiver, and then go to the most
efficient implementation.

One variation deserves some additional commentary.  `BINARY_SUBSCR_TUPLE_CONST_INT` uses
the oparg to encode the index which is being loaded.  This opcode actually replaces
both a `LOAD_CONST` and `BINARY_SUBSCR` pair.  Both opcodes end up getting replaced, with
the `LOAD_CONST` being replaced with `BINARY_SUBSCR_TUPLE_CONST_INT` and the `BINARY_SUBSCR`
being patched with `BINARY_SUBSCR_TUPLE`.  The latter case is merely to make sure that
if there were any jumps to the `BINARY_SUBSCR` that they continue to behave normally.

The `BINARY_SUBSCR_TUPLE_CONST_INT` can then do a quick check on the type of the container
and can then load the value directly from the tuples `ob_item` array.


Specialized Opcode list
-----------------------

The current opcodes that are supported and optimized are:

LOAD_ATTR
~~~~~~~~~

Loading attributes is currently one of the most highly specialized opcodes.
There are variations which support objects without dictionaries, objects
with split dictionaries, objects with normal dictionaries, attributes stored
in slots, type objects, as well as module objects.  There is also the ability
to combine multiple caches together for polymorphic call sites.


LOAD_ATTR_NO_DICT_DESCR
^^^^^^^^^^^^^^^^^^^^^^^
This is commonly used on built-in types which don't have dictionaries
associated with them.  As such the attribute always resolves to a descriptor.
This can also be used when the descriptor is a data descriptor (e.g. a
property) in which case we can completely avoid any dictionary related
operations.


LOAD_ATTR_SPLIT_DICT_DESCR, LOAD_ATTR_SPLIT_DICT
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Split dictionaries are an important optimization in CPython which reduces
the memory overhead of dictionaries for instances of objects.  We specialize
these for both the case where the value would be expected to be in the
dictionary (`LOAD_ATTR_SPLIT_DICT`) as well as the case where the value exists
as a descriptor and likely is not shadowed by the dictionary but could be
(`LOAD_ATTR_SPLIT_DICT_DESCR`).

LOAD_ATTR_DICT_NO_DESCR
^^^^^^^^^^^^^^^^^^^^^^^
Many times objects fall out of the split dictionary sweet case and we
specialize the case where we know there's no descriptor that needs to
be looked up.  This avoids the overhead of various _PyType_Lookup calls.

LOAD_ATTR_TYPE
^^^^^^^^^^^^^^
Accessing attributes from type objects is also very common, whether
that's accessing a class method or accessing some generic data.

LOAD_ATTR_SLOT
^^^^^^^^^^^^^^
We push code to use slots pretty heavily in our code base, and even
have various frameworks which perform auto-slotification.  This is not
only a significant memory savings by not relying upon dictionaries for
objects, but we get significantly improved attribute lookup time with
support for inline caching for the slots.

LOAD_ATTR_MODULE
^^^^^^^^^^^^^^^^
Modules are a little trickier to optimize because their dictionaries can
be mutated externally.  Therefore we need to track the dictionary version
for the module in addition to the value, and do a version check on the
dictionary when performing the lookup.  This is very similar to how the
existing instance attributes work in Python 3.10.

LOAD_ATTR_POLYMORPHIC
^^^^^^^^^^^^^^^^^^^^^
While certainly not the common case there are plenty of call sites which
will be polymorphic over a small number of types.  When we see a polymorphic
call site we'll replace it with one which can track multiple types, and
expand the caches over time.

LOAD_ATTR_UNCACHABLE
^^^^^^^^^^^^^^^^^^^^
If a function is frequently updating the inline caches then we're better
off not using inline caching at all.  In this case we patch the byte code
with `LOAD_ATTR_UNCACHABLE` which is the normal implementation of `LOAD_ATTR`
which does a simple `PyObject_GetAttr`.

Another use case for this is when the caching system doesn't understand the
nature of the object - this happens with any object which has a custom
`tp_getattro` that's not the type or module objects `tp_getattro`.

STORE_ATTR
~~~~~~~~~~
STORE_ATTR isn't hit quite as hard as `LOAD_ATTR` is hit, and there's also
a lot less need to support so many variations of it as there aren't the same
set of combinations of dictionaries and descriptors due to the nature of
descriptors and data descriptors.

STORE_ATTR_SPLIT_DICT
^^^^^^^^^^^^^^^^^^^^^
This is used to optimize the ideal case where we're storing into an object
with a split dictionary.  These need to handle both replacing existing
values in the split dictionary, as well as being the first case to add a
value.

STORE_ATTR_DICT
^^^^^^^^^^^^^^^
If there's no split dictionary, and no data descriptor, we know we can do
a store directly to the dictionary and avoid the overhead of doing the
`PyType_Lookup`.

STORE_ATTR_DESCR
^^^^^^^^^^^^^^^^
If there's a data descriptor like a property we know that the assignment
should just follow the normal descriptor protocol, and we can cache the
descriptor and avoid looking it up on each call.

STORE_ATTR_SLOT
^^^^^^^^^^^^^^^
Matching the load case we also support direct stores into slots as well.

STORE_ATTR_UNCACHABLE
^^^^^^^^^^^^^^^^^^^^^
This again just mirrors the behavior for loads where if we have a highly
polymorphic call site that is thrashing or an object which doesn't support
simple assignment semantics we can give up on caching.

LOAD_METHOD
~~~~~~~~~~~
The handling of `LOAD_METHOD` is very similar to the handling of `LOAD_ATTR`,
but the big difference is we want specialized versions which handle the
case when we know we have a method.  These variations don't need to check
if we have a method like object and can instead just immediately
return the method.

LOAD_METHOD_NO_DICT_METHOD
^^^^^^^^^^^^^^^^^^^^^^^^^^
This is the ideal version for methods on built-in types.  The cache hit
merely verifies that the type is correct and can immediately return the
value.

LOAD_METHOD_SPLIT_DICT_METHOD
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
For instances which implement split dictionaries we can consult the split
dictionary and with a few quick checks verify that the value does
not exist in the dictionary, which then makes returning the method easy.

LOAD_METHOD_DICT_METHOD
^^^^^^^^^^^^^^^^^^^^^^^
When we fall off the split dictionary fast-path we are forced into
performing a dictionary lookup to verify the value isn't present.  But
this still allows us to avoid the typical overhead of doing a
`_PyType_Lookup`.


LOAD_METHOD_DICT_DESCR, LOAD_METHOD_SPLIT_DICT_DESCR
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
The descriptor variations are less common, but easy to support as we
already have all the information in our caches.  This variation needs
to check the value isn't in the dictionary and can then follow
normal descriptor protocol.

LOAD_METHOD_TYPE
^^^^^^^^^^^^^^^^
This will commonly end up handling cases such as invoking a class
method through the type instance.  There's no specialization for the
underlying descriptor, and the generic descriptor protocol is invoked
if the object is a descriptor.  This results in something that isn't
a bound method as far as the `LOAD_METHOD` opcode is concerned.

LOAD_METHOD_MODULE
^^^^^^^^^^^^^^^^^^
This is similar to `LOAD_METHOD_TYPE` but handles cases where a function
or a type object is being called from an imported module.  Again a
method will not be loaded as far `LOAD_METHOD` is concerned.

LOAD_METHOD_NO_DICT_DESCR
^^^^^^^^^^^^^^^^^^^^^^^^^
For attributes on built-in objects which aren't types this will just
return the value or invoke the descriptor protocol as necessary.

LOAD_METHOD_UNCACHABLE
^^^^^^^^^^^^^^^^^^^^^^
Again we have an `UNCACHABLE` variation for types which don't follow the
typical attribute access resolution behavior.

BINARY_SUBSCR
~~~~~~~~~~~~~
We've specialized indexing into common objects such as lists, tuples, and
strings.  While these aren't super expensive operations when going through
the generic code path there is still a small savings to be had here and there
is also still plenty of opcode space for such small wins.

BINARY_SUBSCR_DICT, BINARY_SUBSCR_DICT_STR
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
This merely gets rid of the indirect dispatch used to lookup in a dictionary
and replaces it with an exact type match.

BINARY_SUBSCR_TUPLE_CONST_INT
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
This is the one example of a "super instruction" that we've implemented so far.
It not only allows getting rid of some dynamic dispatch to index into the tuple,
but it also allows to inline the load directly, and skip an extra loop around
the interpreter loop.

BINARY_SUBSCR_TUPLE, BINARY_SUBSCR_LIST
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Similar to the dictionary case these simply replace the dynamic dispatch with
a type check that can quickly go to the dedicated function.

Other opcodes
~~~~~~~~~~~~~
We've also implemented shadow byte codes for main static Python opcodes
as well.

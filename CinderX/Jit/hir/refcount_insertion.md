# HIR Refcount Insertion

The HIR reference count insertion pass (in `Jit/hir/refcount_insertion.cpp`) is
responsible for inserting reference count operations on a nearly-finalized HIR
function. It uses information about the inputs, outputs, and side-effects of
each instruction to attempt to insert as few increfs and decrefs as necessary
to ensure correctness.

At a high level, the pass is divided into two phases: analysis and
mutation. The analysis phase computes the in-state and out-state of every
block, appropriately merging states at control flow joins. The mutation phase
uses the results of the analysis phase to insert refcount operations, both
along edges between blocks and in the middle of blocks as values die and values
become unborrowed.

## Value States

The state computed by the analysis phase consists of a `RegState` for every
live value. This state has three main parts:

### Reference kind

1. **Uncounted:** A value is Uncounted if its type does not intersect with
   `Object` or if it is an immortal value not subject to reference
   counting. For now, the only value known to be immortal by the compiler is
   the sentinel value that can be returned by `InvokeIterNext`. We will
   probably extend this to other singletons like `None`, `True`, `False`, and
   others in the future.
2. **Borrowed:** A borrowed value is a value that the JITted code does not own
   a reference to, but is being kept alive by another reference that we can
   track the lifetime of. Borrowed values are associated with *borrow
   support*, which is discussed further down.
3. **Owned:** An owned value is either the result of an opcode that produces a
   new reference (e.g., `MakeList`) or a borrowed value that was promoted to
   owned because its borrow support disappeared.

### Borrow support

Every borrowed value is associated with *borrow support*. Conceptually, this is
a set of memory locations and/or other owned values, and it is implemented as a
bit vector. As an optimization, values that are borrowed from locations that
live at least as long as the execution of the current function (e.g.,
arguments, elements of `co_consts`, etc.) have an empty set as their borrow
support and are left out of some bookkeeping data structures. Values that are
borrowed from a transient location (e.g., a tuple item, a list item, or a
global variable) must be promoted to owned before any instruction that might
modify their borrow support.

When a value’s borrow support set has more than one member, it means the value
is borrowed from one of the members of the set, but we don’t know which
one. This is usually from phi nodes where each incoming value is borrowed with
different support. Consequently, it is never wrong to add more borrow support
to a borrowed value; the resulting program will run correctly but may perform
suboptimally (e.g., there may be unnecessary refcounting operations, or
refcounting operations may happen earlier than necessary).

### Value copies

HIR instructions like `CheckExc` and `CheckVar` produce an output that is a
copy of their input with a refined type. All copies of a value are treated as
one, with one state shared between them. `RegState` tracks the root value,
referred to as the “model”, along with a list of all live copies, in definition
order. Most values only have one copy live at any given time (e.g., `CheckExc`
is usually the last use of its input), but the pass can correctly handle any
number of live copies at once.

## Dying Values

When the last copy of an owned value dies, either after its last use or at a
control flow split point where it isn’t live on all outgoing edges, the
mutation phase will insert a `Decref` (or `XDecref`, for possibly-null values)
to destroy the owned reference. Python types can have destructors that run
arbitrary code, so the analysis code must detect these program points and apply
the side-effects that the future `Decref` instruction will have. Additionally,
the `Decref` instruction could destroy the last reference to a container that
is providing memory support for a borrowed value. For both of these reasons, we
invalidate all memory-backed borrow support at program points where an owned
reference dies, which may promote some borrowed values to owned.

Once we start type specializing code and can prove that destroying certain
values won’t run an arbitrary destructor, it will be worth adding more
complexity to improve both of these issues: avoiding the memory effects of a
potential destructor, and tracking which container a value is borrowed from so
we can detect which containers will survive past the `Decref`.

Alternately, artificially extending the lifetime of an owned value past the
lifetime of other, borrowed values may result in fewer reference count
instructions by avoiding these promotions. The current version of the pass does
not attempt to do this, but it’s worth exploring in the future.

## Merging States

Before the analysis phase, all [critical
edges](https://en.wikipedia.org/wiki/Control-flow_graph#Special_edges) in the
CFG are split, ensuring that every edge is from a block with a single successor
and/or to a block with a single predecessor. This ensures that every edge has a
safe location to insert any necessary reconciliation refcounting operations:
during the mutation phase, we insert these instructions when entering a block
with one predecessor, or exiting a block with one successor.

During the analysis phase, the incoming states from each visited predecessor
are merged at control flow join points to form the in-state for the successor
block.

First, Phi nodes are processed:

1. If a phi input is owned out of the predecessor and not separately live into
   the successor block, the phi output is owned, since the owned reference must
   go somewhere. If the phi input appears as borrow support to any values live
   into the block, it is replaced with the phi output, since the owned
   reference being borrowed from now lives in the phi output.

2. Otherwise, the phi output is borrowed. The output’s borrow support is the
   union of:
    1. For owned inputs, the input value itself.
    2. For borrowed inputs, the input’s borrow support.

Then, for each value that is live into the successor block:

1. If the value is owned on any incoming edge, it is owned into the block.
2. If the value is borrowed or uncounted along all incoming edges, it is
   borrowed into the block. Its borrow support is the union of all incoming borrow
   support.
3. If the value is uncounted along all incoming edges, it is uncounted into the
   block.

As the analysis is iterated to a fixed point and newly-visited predecessors
become available, or previously-visited predecessors have different out-states,
value states may transition from uncounted to borrowed, from borrowed to
borrowed with wider support, or from finally from borrowed to owned. Note that
this state ordering only applies to `{program location, value, state}` tuples
when the first two elements remain constant. Value states may make other
transitions when going between program locations (if an instruction tells us
that a previously-Owned value is now Uncounted, for example).

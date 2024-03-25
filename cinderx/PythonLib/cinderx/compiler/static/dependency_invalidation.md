# Static Python bytecode cache invalidation

The compilation of a Static Python module can depend on the contents of other modules that it imports from. E.g. we may generate `LOAD_FIELD` and `STORE_FIELD` against types imported from other static modules, and the correctness of these opcodes depends on the shape of that type. Similarly we may emit `INVOKE_FUNCTION` and `INVOKE_METHOD` to call functions in other modules, and the correctness of these depends on the signature of the called function. Many other changes (eg changes to classes we inherit from, changes to inheritance hierarchies, removal of unsupported decorators, or module transitions from nonstatic to static) can result in changes to the bytecode we would emit on a fresh compile (perhaps we would now emit more static opcodes and/or casts) or to whether or not we would issue static type errors in compilation.

The usual Python bytecode cache invalidation strategy is that each module’s bytecode is cached in a `.pyc` file which is invalidated based only on the timestamp or hash of the corresponding source file. This is not sufficient for Static Python; it can result in cached bytecode being used for an unchanged module even though the bytecode has been rendered obsolete by changes to dependency modules. This can result in various runtime errors whose cause can be difficult to identify, or in phantom errors that don't show up immediately but only after a pyc is invalidated by other edits.

When importing a Static Python module from cached bytecode, we must check whether it or any of its dependency modules have changed since the cached bytecode was compiled. If so, we must invalidate the cached bytecode and recompile.

## Checking dependencies when loading from pyc

Assuming we can identify the correct dependencies for a given static module, how can we record these and check them at loading-from-pyc time?

### Constraints

* We cannot assume that the dependency modules have been imported yet when we are making the judgment whether to invalidate a module or not, and we can’t import them as part of making the judgment (this leads to deadlock.)
* We cannot execute the module body as part of making this judgment, since (both for safety and efficiency) we don’t want to execute possibly-obsolete bytecode.
* Therefore, all the information we need to make this decision must be available to the import loader in the `pyc` files themselves, or on the filesystem, without the need to import or execute any modules.

### Existing Python pyc file format and read algorithm

A `pyc` file as read/written by the normal Python import loader consists of a header of four 32-bit words, followed by the module’s code object as serialized by `marshal`. The header format is as follows:

* The first word is a magic number to indicate compatibility with a given interpreter (the magic number set in `importlib._bootstrap_external.py`).
* The second word is a bit field in which only the two lowest bits currently have a defined meaning: if the low bit is set, the `pyc` file uses hash-based invalidation instead of the default timestamp-based invalidation, and the second-lowest bit determines whether hashes are actually verified at import time if hash-based validation is used (see [PEP 552](https://www.python.org/dev/peps/pep-0552/).)
* The third and fourth words’ meaning depends on whether hash-based or timestamp-based invalidation is used. With timestamp-based invalidation, the third word is the mtime of the source file and the fourth word is its size in bytes. With hash-based invalidation, the third and fourth words are an 8-byte hash of the source file contents.

A `pyc` file as read/written by the strict/static loader prepends one additional 32-bit word to this header, which is a strict-specific magic number (defined in `compiler.strict.common`) to indicate compatibility with a given version of strict/static compiler.

The process for reading a `pyc` file (in the strict/static loader) is currently as follows:

1. Check the strict prefix word against the strict/static magic number and fail if they don’t match. If they do match, pass the remainder of the data on to Python’s built-in import loader, which will:
2. Check the first word against the Python importlib magic number and fail if they don’t match.
3. Classify the pyc as hash-based or timestamp-based based on the bit flags set in the second word.
4. Either stat or hash the module’s source file and compare the mtime/size or hash against the third and fourth word, failing if they don’t match.
5. Unmarshal the code object from the remainder of the pyc data.

### Changes to pyc files

We will insert information on dependency modules after the existing five-word header and before the marshaled code object. This keeps the `pyc` file data in the order we want to process it for best efficiency: first check magic number compatibility, then check staleness of the `pyc` relative to its own source, then check dependency staleness, then unmarshal the code object.

The process for reading a `pyc` will remain the same as before, except that we will insert a new step in between (4) and (5) above, to check for staleness of any dependency. This means the strict/static loader will have to reimplement more of the built-in loader’s behavior than it currently does; we can no longer take care of the strict/static-specific parts up front and then hand off the rest of the data to the built-in loader, because dependency checking should come after checking built-in magic number and self-staleness, but before loading the code object.

The dependency module information is a tuple of tuples dumped via `marshal`. Each dependency tuple is `("module.name", mtime_and_size_or_hash)` for one dependency module.

The `mtime_and_size_or_hash` is 64 bits (representing the mtime and size in bytes of the dependency module, each as a 32-bit integer, or its 8-byte hash) marshaled as a Python bytes object. This will be interpreted as mtime-and-size or as a hash depending on the classification of the `pyc` file containing this dependency data; i.e. a given `pyc` will always invalidate based either on all-timestamps or all-hashes, for its own source and the sources of all its dependencies.

We also track dependencies on non-static modules; these dependencies will be recorded with an empty (zero-length) `mtime_and_size_or_hash`.

The algorithm for dependency staleness check is as follows, for each listed dependency in order:

1. Find the source file for the given module name. If none is found, invalidate if the dependency was static, otherwise don't invalidate (unknown/not-found/not-static are all equivalent.)
2. Stat or hash the source file, depending on the invalidation mode.
3. For a was-static dependency, compare the mtime/size or hash to that recorded in the dependency tuple, and fail if they don’t match.
4. For a wasn't-static dependency, grep the module source (if any) for `import __static__` . If it is found, invalidate.

For non-static modules (or for static modules with no dependencies), the recorded dependency information will simply be an empty tuple.

The dependencies recorded in the pyc file are the full list of concrete dependencies; transitive dependencies have already been resolved in the collection of dependencies at compile time. So the invalidation check does not require any graph traversal or transitive checks.

### Performance

The search for a given source file against `sys.path` will be slow for large numbers of dependencies, and this has to be done every time we load from a static `pyc`. To mitigate this, the loader may maintain a global cache mapping module name to mtime/size/hash, so that future dependency checks against it don’t require filesystem access at all.

## Acquiring dependency information at compile time

Determining the set of dependencies to record for a static module is a tradeoff between correctness / consistency (if we specify too few dependencies, we may continue to use stale bytecode from a `pyc` that would have now been compiled differently in the absence of the `pyc` cache) and avoiding the dev speed hit of over-recompilation due to over-approximation of dependencies.

Python’s current `pyc` system offers full consistency: the presence of a `pyc` will never result in the use of different bytecode (or in observably different behavior, aside from time not spent compiling source files) than if the `pyc` file were absent.

There are three different basic types of inconsistencies we could cause through failing to record a dependency, ordered from less to more acceptable:

1. We could use stale bytecode containing invokes or field loads that are now simply wrong for the target type, and will result in classloader errors or possibly segfaults or undefined behavior.
2. If a dependency has changed such that a previously opaque (and thus treated as dynamic) type is now visible to us (because of a nonstatic→static module conversion, or the removal of a non-understood decorator, etc), or if the type lattice has changed due to a dependency change, or if methods we don’t call, but do override have changed, then a recompile might issue static type errors that were not previously visible to our compiler. If we fail to recompile we may not issue those type errors until some unpredictable future point when the dependency is invalidated for some other reason.
3. If there are no static type errors revealed by the availability of new type information, we may still fail to use better-optimized bytecode until an unpredictable future unrelated change triggers a recompile, so a change to the performance of the code which ought to be immediate is instead unpredictably delayed and separated from its cause.

I think that although (2) and (3) are somewhat better than (1), they are still an unacceptable user experience for a non-expert trying to adopt Static Python. Even the most “harmless” case (delayed pickup of available optimizations) can cause a lot of extra confusion and churn for someone trying to benchmark the effect of a conversion to Static Python. So our design should aim for full consistency. (At least initially, unless we conclusively determine in practice that it’s impossible to achieve full consistency with acceptable levels of recompilation.)

Determining dependencies solely from the type descrs we emit in codegen can cover only case (1); it can’t cover cases (2) or (3), where newly-available type information also requires that we recompile. So we need to record as dependencies all external types looked up during compilation, whether they are from non-static or static modules, and whether or not they ultimately result in a type descr in codegen.

### Implementation

We modify `ModuleTable` such that its `children` dictionary is private, requiring all accesses of a module’s children to go through an explicit method call. We also modify `ModuleTable` so that when children are added to it via an `import from`, it tracks the source module for each child.

The API for resolving a name to a type in `ModuleTable` requires providing a requesting type or function name. Thus, each `ModuleTable` is able to build a mapping of which of its types or functions requested which types from which other modules (including non-static ones) during their compilation. We separate types requested during decl visit from types requested during full type binding; only the former are relevant when we are tracking transitive dependencies through this module, but both are relevant when we are considering the compilation of this module itself.

When a static module has finished compilation, we traverse the stored dependency information from its `ModuleTable` through only the specific objects used from other `ModuleTable` to build up the transitive closure of dependencies that must trigger recompilation of this module. We get the relative file path and size-and-mtime-or-hash for each of these dependencies (this information can also be cached in the `ModuleTable` the first time it is computed) and store them in the `pyc` file. We store `was_static` as `False` for dependencies imported from non-static modules, which means they won’t trigger recompilation unless they actually appear to have become static (see above.)

## Comparison to other dependency invalidation algorithms

Previous dependency invalidation algorithms tended to take one of two approaches:

* Module-level coarse-grained invalidation.
    * This was tried in practice by both mypy and Pyre, but ended up being untenable due to dramatic over-approximation of dependencies in large codebases.
* Finer-grained invalidation where we keep track of and invalidate classes, globals and functions rather than entire modules.
    * This is the approach now used by mypy, Flow, Hack and Pyre, but comes with having to store (dependency key, value) pairs for each value that’s depended on. This approach works well when a type checking server is present, but presents challenges in our system where we store all dependency data in .pyc’s and can’t easily get a hash of each value without doing extra work or storing extra information which is readily available in the aforementioned systems.

The method proposed here is a hybrid approach between the two: When computing dependencies, we follow the finer-grained invalidation approach, and build a list of (dependency type/function, type) tuples. However, when we actually store the values in the pycs, we lose fine-grained information and only keep the information of which version of a module was depended on. This means that when a dependency changes, we invalidate the current module, even if the key which changed in the dependency wasn’t used in our module.

Our hypothesis is that the real issue with coarse-grained invalidation has to do with the transitive invalidation - that is, if you don’t stop invalidating dependencies at the point where your dependencies didn’t affect the compilation of your module. For Static Python, the fact that we list each transitive dependency in the .pyc file eliminates the requirement for the graph traversal and makes invalidation relatively cheap, but comes at the cost of invalidating your entire module when a potentially unrelated key in the dependency gets changed, and can have size concerns since we list the closure of dependencies in each pyc file, which can get pretty large.

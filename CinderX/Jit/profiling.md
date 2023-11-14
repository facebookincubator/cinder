# Interpreter type profiling

Cinder adds type profiling to CPython's interpreter, and it has a few different pieces that work together.

## Overview

At a high level, this is a sampling profiler with an adjustable period. The adjustable period can be used to profile a normal production workload with minimal impact on performance, to avoid affecting the behavior of the application and getting a non-representative profile. It can be enabled and disabled freely at runtime, as long as the JIT was not enabled at process startup.

With a period `n`, every `n`th bytecode will be profiled. For each code object, a simple count of profiled bytecodes is kept. Additionally, the input types are recorded for bytecodes that the JIT may be interested in type-specializing (like `LOAD_ATTR` and `LOAD_METHOD`).

## Interface

### Command-line options

* `-X jit-profile-interp`: Start with profiling enabled for all threads, and start new threads with profiling enabled. This is intended to profile small scripts; large applications will probably want to use the Python interface to enable profiling after their startup phase is complete.
* `-X jit-disable`: Disable the JIT, overriding `-X jit` or `-X jit-list-file`. This should be roughly equivalent to not passing either of the options it overrides; it is provided as a convenience for deployment environments that make it difficult to remove options in the default configuration.

### Python-visible API

A few members are added to the `cinder` module:

* `set_profile_interp(bool enabled) -> bool`: Enable or disable interpreter profiling for the current thread. Returns whether or not profiling was enabled for this thread before the call.
* `set_profile_interp_all(bool enabled)`: Enable or disable interpreter profiling for all threads. Newly-created threads will use the last value passed to this function as well.
* `set_profile_interp_period(int period)`: Set the period for interpreter profiling. This does not enable or disable profiling on any threads.
* `get_and_clear_type_profiles() -> list`: Build a list containing the type profiling information. Its format may change over time but will always be suitable to directly pass to Scuba.
* `clear_type_profiles()`: Clear type profiles without returning them.

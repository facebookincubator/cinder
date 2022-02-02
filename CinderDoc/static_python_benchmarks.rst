Static Python Benchmarks
========================

We ran a comprehensive benchmarking matrix, across three benchmarks,
covering multiple combinations of Cinder features and code changes.

Each benchmark has several versions of its code that accomplish the same work:
the Original untyped version, a Typed version which just adds type annotations
without otherwise changing the code, and a TypedOpt version converted to take
advantage of Static Python features for maximum performance under Static Python
and the JIT. Fannkuch and DeltaBlue also have TypedMinOpt, which uses just a
few Static Python specific features where it can be done in a targeted way
without significant changes to the code.

The other axes of our test matrix are SP (whether the Static Python compiler is
used), JIT (whether the Cinder JIT is enabled), and SF (whether the JIT
shadow-frame mode is enabled, which reduces Python frame allocation costs.)
The matrix is not full, since using the Static Python compiler on untyped code
has no noticeable effect, and shadow-frame mode is only relevant under the JIT.

This is a preview of the detailed benchmarks results,
which are pending publication.
We present below a normalized graph of "speedup" compared to "Original ()"
(e.g. "1" is original untyped version with no JIT enabled).
Values greater than 1 represent speedup in that benchmark configuration,
and values smaller than 1 represent slowdown in that benchmark configuration.
The graph is in log scale.

.. image:: images/static_python_normalized_speedups.png
   :alt: Cinder build status on GitHub Actions

Strict Modules
##############

  `Python is more dynamic than the language I *wanted* to design. -- Guido van Rossum  <https://mail.python.org/pipermail/python-list/2001-August/073435.html>`_


What are strict modules?
------------------------

Strict modules are an opt-in mechanism for restricting the dynamism of
top-level (module-level) code in a Python module. These stricter
semantics are designed to have multiple benefits:

* Eliminate common classes of developer errors
* Improve the developer experience
* Unlock new opportunities for optimizing Python code and simplify other classes of optimizations

While strict modules alter what you can do at the top-level of your code
they don't limit what you can do inside of your function definitions. They
also are designed to not limit the expressiveness of what you can do even
at the top-level. The limits on strict modules at the top-level all have a single
goal: Make sure that module definitions can reliably be statically analyzed.

In order to support this there are also some runtime changes for strict
modules, mostly around immutability. In order to be able to make guarantees
about the analysis we must be certain that it won't later be invalidated by
runtime changes. To this end strict modules themselves are immutable and
types defined within strict modules are immutable as well.

.. toctree::
    :caption: Overview
    :maxdepth: 2

    guide/quickstart

.. toctree::
    :caption: Users Guide
    :maxdepth: 3

    guide/index

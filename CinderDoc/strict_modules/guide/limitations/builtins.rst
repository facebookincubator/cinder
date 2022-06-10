Supported Builtins
##################

Strict modules support static verification for a subset of the standard Python
builtin types and functions.  Usage of builtins not in this
list at the top-level of a module will result in an error message
and will prevent the module from being able to be marked as strict.

If you need support for an additional built-in function or type outside of the
supported list please report a bug!

Supported types and values:

* AttributeError
* bool
* bytes
* classmethod
* complex
* dict
* object
* Ellipsis
* Exception
* float
* int
* list
* None
* NotImplemented
* property
* range
* set
* staticmethod
* str
* super
* type
* TypeError
* tuple
* ValueError

Supported functions:

* callable
* chr
* getattr
* hasattr
* len
* max
* min
* ord
* print
* isinstance
* setattr

Unsupported functions:

Currently exec and eval are disallowed at the top-level.  In the future we may
allow their usage if they are used with deterministic strings.

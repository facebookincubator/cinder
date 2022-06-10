ProhibitedBuiltinException
##########################

  Call to built-in '<name>' is prohibited at module level.

This error indicates that you are attempting to call a known built-in function
which strict modules do not support.

Currently this error applies to calling exec and eval.


.. code-block:: python

    exec('x = 42')


Currently strict modules do not support exec or eval at the top level.  If you
must use them you can move them into a function which lazily computes the value
and stores it in a global variable.   See :doc:`../conversion/singletons` as
one possible solution to this.

In the future strict modules may support exec/eval as long as the values being
passed to them are deterministic.  This would enable more complex library code
to be defined within strict modules.  One example of this in real-world code
is Python's namedtuple class which uses exec to define tuple instances.

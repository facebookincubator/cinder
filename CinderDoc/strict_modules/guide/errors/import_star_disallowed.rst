ImportStarDisallowedException
#############################

  Strict modules may not import ``*``.

This error indicates that you are attempting to do a ``from module import *``.

.. code-block:: python

    from foo import *


Strict modules simply outright prohibit this construct.  Import stars are not
only generally considered bad style but they also make it impossible to
understand what attributes are defined within a module.  Because import * can
bring in any name it has the possibility of overwriting existing names that
have been previously imported.

To work around this explicitly import the values that you intend to use from
the module.

For additional information see the section on
:doc:`/strict_modules/guide/limitations/imports`.

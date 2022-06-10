Global Singletons
#################

Sometimes it might be useful to encapsulate a set of functionality into
a class and then have a global singleton of that class. And sometimes
that global singleton might have dependencies on non-strict code which
makes it impossible to construct at the top-level in a strict module.

.. code-block:: python

    from non_strict import get_counter_start

    class Counter:
        def __init__(self) -> None:
            self.value: int = get_counter_start()

        def next(self) -> int:
            res = self.value
            self.value += 1
            return res

    COUNTER = Counter()

One way to address this is to refactor the Counter class so that it
does less when constructed, delaying some work until first use. For
example:

.. code-block:: python

    from non_strict import get_counter_start

    class Counter:
        def __init__(self) -> None:
            self.value: int = -1

        def next(self) -> int:
            if self.value == -1:
                self.value = get_counter_start()
            res = self.value
            self.value += 1
            return res
    COUNTER = Counter()

Another approach is that instead of constructing the singleton at the
top of the file you can push this into a function so it gets defined
the first time it'll need to be used:

.. code-block:: python

    _COUNTER = None

    def get_counter() -> Counter:
        global _COUNTER
        if _COUNTER is None:
            _COUNTER = Counter()
        return _COUNTER

You can also use an lru_cache instead of a global variable:

.. code-block:: python

    from functools import lru_cache

    @lru_cache(maxsize=1)
    def get_counter() -> Counter:
        return Counter()

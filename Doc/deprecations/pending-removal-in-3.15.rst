Pending Removal in Python 3.15
------------------------------

* :mod:`ctypes`:

  * The undocumented :func:`!ctypes.SetPointerType` function
    has been deprecated since Python 3.13.

* :mod:`http.server`:

  * The obsolete and rarely used :class:`~http.server.CGIHTTPRequestHandler`
    has been deprecated since Python 3.13.
    No direct replacement exists.
    *Anything* is better than CGI to interface
    a web server with a request handler.

  * The :option:`!--cgi` flag to the :program:`python -m http.server`
    command-line interface has been deprecated since Python 3.13.

* :mod:`importlib`: ``__package__`` and ``__cached__`` will cease to be set or
  taken into consideration by the import system (:gh:`97879`).

* :class:`locale`:

  * The :func:`~locale.getdefaultlocale` function
    has been deprecated since Python 3.11.
    Its removal was originally planned for Python 3.13 (:gh:`90817`),
    but has been postponed to Python 3.15.
    Use :func:`~locale.getlocale`, :func:`~locale.setlocale`,
    and :func:`~locale.getencoding` instead.
    (Contributed by Hugo van Kemenade in :gh:`111187`.)

* :mod:`pathlib`:

  * :meth:`.PurePath.is_reserved`
    has been deprecated since Python 3.13.
    Use :func:`os.path.isreserved` to detect reserved paths on Windows.

* :mod:`platform`:

  * :func:`~platform.java_ver` has been deprecated since Python 3.13.
    This function is only useful for Jython support, has a confusing API,
    and is largely untested.

* :mod:`threading`:

  * :func:`~threading.RLock` will take no arguments in Python 3.15.
    Passing any arguments has been deprecated since Python 3.14,
    as the  Python version does not permit any arguments,
    but the C version allows any number of positional or keyword arguments,
    ignoring every argument.

* :mod:`typing`:

  * The undocumented keyword argument syntax for creating
    :class:`~typing.NamedTuple` classes
    (e.g. ``Point = NamedTuple("Point", x=int, y=int)``)
    has been deprecated since Python 3.13.
    Use the class-based syntax or the functional syntax instead.

  * The :func:`typing.no_type_check_decorator` decorator function
    has been deprecated since Python 3.13.
    After eight years in the :mod:`typing` module,
    it has yet to be supported by any major type checker.

* :mod:`wave`:

  * The :meth:`~wave.Wave_read.getmark`, :meth:`!setmark`,
    and :meth:`~wave.Wave_read.getmarkers` methods of
    the :class:`~wave.Wave_read` and :class:`~wave.Wave_write` classes
    have been deprecated since Python 3.13.

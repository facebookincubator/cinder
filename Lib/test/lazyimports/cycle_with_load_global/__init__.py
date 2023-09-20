try:
    from . import a
    a
# we expect a circular import ImportError; anything else is wrong
except ImportError as e:
    if "circular import" not in str(e):
        raise

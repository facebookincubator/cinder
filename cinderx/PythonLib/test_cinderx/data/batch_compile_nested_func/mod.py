def outer():
    def inner():
        return 42

    return inner

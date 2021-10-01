import cinder


def get_await_stack(coro):
    """Return the chain of coroutines reachable from coro via its awaiter"""
    stack = []
    awaiter = cinder._get_coro_awaiter(coro)
    while awaiter is not None:
        stack.append(awaiter)
        awaiter = cinder._get_coro_awaiter(awaiter)
    return stack

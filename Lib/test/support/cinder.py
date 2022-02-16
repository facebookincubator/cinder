import cinder


def get_await_stack(coro):
    """Return the chain of coroutines reachable from coro via its awaiter"""
    stack = []
    awaiter = cinder._get_coro_awaiter(coro)
    while awaiter is not None:
        stack.append(awaiter)
        awaiter = cinder._get_coro_awaiter(awaiter)
    return stack

def verify_stack(testcase, stack, expected):
    n = len(expected)
    frames = stack[-n:]
    testcase.assertEqual(len(frames), n, "Callstack had less frames than expected")

    for actual, expected in zip(frames, expected):
        testcase.assertTrue(
            actual.endswith(expected),
            f"The actual frame {actual} doesn't refer to the expected function {expected}",
        )

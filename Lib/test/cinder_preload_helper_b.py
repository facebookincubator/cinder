#!/usr/bin/env python3

print("loading helper_b")

import cinder_preload_helper_a

# The particular bug we're exercising can be hidden by the freelist in
# funcobject.c. Fill it up before freeing the critical function.
funcs = []
for i in range(400):
    def f():
        pass
    funcs.append(f)
del funcs

del cinder_preload_helper_a.a_func

def b_func() -> str:
    return "hello from b_func!"

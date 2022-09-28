#!/usr/bin/env python3

import cinderjit
import cinder_preload_helper_a
import cinder_preload_helper_b
import weakref

print("resolving a_func")
ref = weakref.ref(cinder_preload_helper_a.a_func)

print("defining main_func()")
def main_func() -> str:
    return cinder_preload_helper_b.b_func()

print("disabling jit")
cinderjit.disable()
print("jit disabled")

print(type(ref()))
print(main_func())

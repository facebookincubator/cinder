import sys
old_meta_path = sys.meta_path.copy()
try:
    from .extern.packaging import markers
finally:
    sys.meta_path[:] = old_meta_path

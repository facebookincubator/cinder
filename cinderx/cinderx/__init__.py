"""High-performance Python runtime extensions."""

import os
import sys

try:
    # We need to make the symbols from _cinderx available process wide as they
    # are used in other CinderX modules like _static, etc.
    old_dlopen_flags: int = sys.getdlopenflags()
    sys.setdlopenflags(old_dlopen_flags | os.RTLD_GLOBAL)
    try:
        from _cinderx import (
            _compile_perf_trampoline_pre_fork,
            _get_entire_call_stack_as_qualnames_with_lineno,
            _get_entire_call_stack_as_qualnames_with_lineno_and_frame,
            _is_compile_perf_trampoline_pre_fork_enabled,
            async_cached_classproperty,
            async_cached_property,
            cached_classproperty,
            cached_property,
            clear_all_shadow_caches,
            clear_caches,
            clear_classloader_caches,
            clear_type_profiles,
            disable_parallel_gc,
            enable_parallel_gc,
            get_and_clear_type_profiles,
            get_and_clear_type_profiles_with_metadata,
            get_parallel_gc_settings,
            init as cinderx_init,
            set_profile_interp,
            set_profile_interp_all,
            set_profile_interp_period,
            strict_module_patch,
            strict_module_patch_delete,
            strict_module_patch_enabled,
            StrictModule,
            watch_sys_modules,
        )
    finally:
        sys.setdlopenflags(old_dlopen_flags)
except ImportError:
    cinderx_init = None


def init() -> None:
    """Initialize CinderX."""
    if cinderx_init is None:
        # I don't like letting this pass silently, but currently the build
        # breaks unless we do, due to the build bootstrapping itself with the
        # built binary when CinderX isn't built yet. This won't be an issue
        # after buckification and removing the `site.py` call to cinderx init.
        return
    cinderx_init()

    # Turn _static into a StrictModule so we can do direct invokes against it
    import _static

    # if it has a __file__ attribute, libregrtest will try to write to it
    del _static.__file__
    sys.modules["_static"] = StrictModule(_static.__dict__, False)

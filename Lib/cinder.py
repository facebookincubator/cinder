from _cinder import (
    # Methods
    _get_arg0_from_pyframe,
    _get_awaiter_frame,
    _get_call_stack,
    _get_coro_awaiter,
    _get_entire_call_stack_as_qualnames,
    _get_frame_gen,
    _get_qualname,
    _has_no_shadowing_instances,
    _set_qualname,
    cinder_set_warn_handler,
    debug_break,
    freeze_type,
    get_warn_handler,
    getknobs,
    set_warn_handler,
    setknobs,
    toggle_dump_ref_changes,
    warn_on_inst_dict,

    # Other attributes
    STRUCTURED_DATA_VERSION,
    _built_with_asan,
)

try:
    from cinderx import (
        # Methods
        _compile_perf_trampoline_pre_fork,
        _get_entire_call_stack_as_qualnames_with_lineno_and_frame,
        _get_entire_call_stack_as_qualnames_with_lineno,
        _is_compile_perf_trampoline_pre_fork_enabled,
        clear_all_shadow_caches,
        clear_caches,
        clear_classloader_caches,
        disable_parallel_gc,
        enable_parallel_gc,
        get_parallel_gc_settings,
        strict_module_patch_delete,
        strict_module_patch_enabled,
        strict_module_patch,
        watch_sys_modules,

        # Other attributes
        async_cached_classproperty,
        async_cached_property,
        cached_classproperty,
        cached_property,
        StrictModule,
    )
except ImportError:
    pass

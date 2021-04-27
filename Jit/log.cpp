// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/log.h"

namespace jit {

int g_debug = 0;
int g_debug_refcount = 0;
int g_debug_verbose = 0;
int g_dump_hir = 0;
int g_dump_hir_passes = 0;
int g_dump_final_hir = 0;
int g_dump_lir = 0;
int g_dump_lir_no_origin = 0;
int g_disas_funcs = 0;
FILE* g_log_file = stderr;

} // namespace jit

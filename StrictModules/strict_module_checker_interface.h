// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef STRICTM_CHECKER_INTERFACE_H
#define STRICTM_CHECKER_INTERFACE_H

#include "Python.h"

/*
 * This file defines the C interface to the strict module loader.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _ErrorInfo {
  PyObject* msg;
  PyObject* filename;
  int lineno;
  int col;
} ErrorInfo;

void ErrorInfo_Clean(ErrorInfo* info);

typedef struct StrictModuleChecker StrictModuleChecker;
typedef struct StrictAnalyzedModule StrictAnalyzedModule;

/*
 * Create a new strict module checker
 *
 * Returns a pointer to a new StrictModuleChecker on success or NULL on error.
 */
StrictModuleChecker* StrictModuleChecker_New(void);

/** Set import paths
 * return 0 for success and -1 for failure
 */
int StrictModuleChecker_SetImportPaths(
    StrictModuleChecker* checker,
    const char* import_paths[],
    int length);

/** Set stub import path
 * return 0 for success and -1 for failure
 */
int StrictModuleChecker_SetStubImportPath(
    StrictModuleChecker* checker,
    const char* stub_import_path);

int StrictModuleChecker_SetAllowListPrefix(
    StrictModuleChecker* checker,
    const char* allowList[],
    int length);

int StrictModuleChecker_SetAllowListExact(
    StrictModuleChecker* checker,
    const char* allowList[],
    int length);

void StrictModuleChecker_Free(StrictModuleChecker* checker);

/** Return the analyzed module
 *  return NULL for internal error cases
 *  out parameter: `error_count` how many errors in error sink
 *  is reported
 */
StrictAnalyzedModule* StrictModuleChecker_Check(
    StrictModuleChecker* checker,
    PyObject* module_name,
    int* out_error_count,
    int* is_strict_out);

/** Fill in errors_out (of size `length`) with ErrorInfo
 *  Of the given module. The size is obtained in `StrictModuleChecker_Check`
 *  Return 0 for success and -1 for failure
 */
int StrictModuleChecker_GetErrors(
    StrictAnalyzedModule* mod,
    ErrorInfo errors_out[],
    size_t length);

/** Set whether the loader should force a module to be strict
 *  reutrn 0 if no error and -1 for internal error
 */
int StrictModuleChecker_SetForceStrict(
    StrictModuleChecker* checker,
    PyObject* force_strict);

#ifdef __cplusplus
}
#endif
#endif // STRICTM_LOADER_INTERFACE_H

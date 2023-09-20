// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Python.h"

/*
 * This file defines the C interface to the strict module loader.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _arena PyArena;

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

int StrictModuleChecker_SetAllowListRegex(
    StrictModuleChecker* checker,
    const char* allowList[],
    int length);

int StrictModuleChecker_LoadStrictModuleBuiltins(StrictModuleChecker* checker);

int StrictModuleChecker_EnableVerboseLogging(StrictModuleChecker* checker);

int StrictModuleChecker_DisableAnalysis(StrictModuleChecker* checker);

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

/** Return the analyzed module
 *  return NULL for internal error cases
 *  in parameter: `source` need to be parsed by python ast parser
 *  out parameter: `error_count` how many errors in error sink
 *  is reported
 */
StrictAnalyzedModule* StrictModuleChecker_CheckSource(
    StrictModuleChecker* checker,
    const char* source,
    PyObject* module_name,
    PyObject* file_name,
    const char* submodule_search_locations[],
    int search_locations_size,
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

/** Return how many modules have been analyzed*/
int StrictModuleChecker_GetAnalyzedModuleCount(StrictModuleChecker* checker);

/** Set whether the loader should force a module to be strict
 *  reutrn 0 if no error and -1 for internal error
 */
int StrictModuleChecker_SetForceStrict(
    StrictModuleChecker* checker,
    PyObject* force_strict);
int StrictModuleChecker_SetForceStrictByName(
    StrictModuleChecker* checker,
    const char* forced_module_name);

// Delete the module named `mod` from the analyzed modules
int StrictModuleChecker_DeleteModule(
    StrictModuleChecker* checker,
    const char* module_name);

PyArena* StrictModuleChecker_GetArena(StrictModuleChecker* checker);

/** Retrieve the AST for a given module name
 *  If the module with given name is not yet checked,
 *  This will *not* trigger a check and NULL will be returned.
 */
PyObject* StrictAnalyzedModule_GetAST(
    StrictAnalyzedModule* mod,
    PyArena* arena);

/** Retrieve the symtable for a given module name
 *  If the module with given name is not yet checked,
 *  This will *not* trigger a check and NULL will be returned.
 */
PyObject* StrictAnalyzedModule_GetSymtable(StrictAnalyzedModule* mod);

// retrieve filename
PyObject* StrictAnalyzedModule_GetFilename(StrictAnalyzedModule* mod);

// retrieve modulekind as int
int StrictAnalyzedModule_GetModuleKind(StrictAnalyzedModule* mod);

// retrieve stubkind as int
int StrictAnalyzedModule_GetStubKind(StrictAnalyzedModule* mod);

#ifdef __cplusplus
}
#endif

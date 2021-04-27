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

typedef struct StrictModuleChecker StrictModuleChecker;

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

void StrictModuleChecker_Free(StrictModuleChecker* checker);

/** Whether `module_name` is a strict module (0) or not (1)
 *  return -1 for internal error cases
 */
int StrictModuleChecker_Check(
    StrictModuleChecker* checker,
    PyObject* module_name);

#ifdef __cplusplus
}
#endif
#endif // STRICTM_LOADER_INTERFACE_H

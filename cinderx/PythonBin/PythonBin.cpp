/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under both the MIT license found in the
 * LICENSE-MIT file in the root directory of this source tree and the Apache
 * License, Version 2.0 found in the LICENSE-APACHE file in the root directory
 * of this source tree.
 */

 /*
  This is an alternative Native Python entry-point that starts a Python
  environment similar to what people might expect when running a stock 'python'
  binary. The idea is to leverage the cinder_binary() Buck macro to ease
  building a Python distribution with CinderX from fbcode.

  The primary reason we need this is to support tests that fork and execute
  using 'sys.executable', particularly with options like -I and -S.

  Firstly, this wrapper undoes the mangling of the executable name the PAR
  wrapper does so it points to the real binary. Secondly, it works around the
  problem that today we've packed Native Python and CinderX initialization
  into a potentially optional part of start-up (i.e. site-customize) but which
  are really not optional. This wrapper ensures we *always* have these setup,
  regardless of how this binary is invoked.

  Ideally we wouldn't need this wrapper and the normal Buck/PAR machinery would
  be able to handle this. However, they do not today and I don't have the time
  right now to figure out all the cases which need to be supported for this.
  Particularly, this implementation has a hard assumption that we're using
  Native Python and CinderX. There may also be issuese with how the environment
  initially setup by the PAR startup scripts are propagated (or not) to further
  forked processes.

  This is only expected to be good enough for Cinder/Python Runtime developers
  to test their changes. Not production applications.
*/

#include <Python.h>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

PyMODINIT_FUNC PyInit__static_extension_utils();

static int load_and_init_module(
    const char* module_name, const char* init_func) {
  PyObject* mod = PyImport_ImportModule(module_name);
  if (mod == nullptr) {
    PyErr_Print();
    std::cerr
      << "Error: could not import module '" << module_name << "'" << std::endl;
    return -1;
  }

  PyObject* init_func_obj = PyObject_GetAttrString(mod, init_func);
  Py_DECREF(mod);
  if (init_func_obj == nullptr || !PyCallable_Check(init_func_obj)) {
    Py_CLEAR(init_func_obj);
    PyErr_Print();
    std::cerr << "Error: could not find callable '" << init_func
      << "' in module '" << module_name << "'" << std::endl;
    return -1;
  }

  PyObject* result = PyObject_CallObject(init_func_obj, nullptr);
  Py_DECREF(init_func_obj);
  if (result == nullptr) {
    PyErr_Print();
    std::cerr << "Error: failed calling '" << init_func << "' in module '"
      << module_name << "'" << std::endl;
    return -1;
  }

  Py_DECREF(result);
  return 0;
}

static std::optional<int> maybe_get_exit_code(
    PyStatus* status, PyConfig* config) {
  if (PyStatus_IsExit(*status)) {
    return status->exitcode;
  }
  PyConfig_Clear(config);
  Py_ExitStatusException(*status);
  return std::nullopt;
}

int main(int argc, char* argv[]) {
  // If we've been started by the PAR wrapper: re-write argv[0] to be the real
  // path to this binary, and remove args 1 and maybe 2 which will be ["-tt"]
  // "<PAR Python Wrapper>".
  fs::path real_exe = fs::read_symlink("/proc/self/exe");
  std::string real_path_str = real_exe.string();

  std::vector<char*> fake_argv;
  if (std::string_view(argv[0]).starts_with("[xarexec] ")) {
    fake_argv.push_back(real_path_str.data());
    int arg = std::string_view(argv[1]) == "-tt" ? 3 : 2;
    for (; arg < argc; arg++) {
      fake_argv.push_back(argv[arg]);
    }
    argc = fake_argv.size();
    argv = fake_argv.data();
  }

  PyConfig config;
  PyConfig_InitPythonConfig(&config);

  PyStatus status = PyConfig_SetBytesArgv(&config, argc, argv);
  if (PyStatus_Exception(status)) {
    return maybe_get_exit_code(&status, &config).value_or(1);
  }

  status = PyConfig_Read(&config);
  if (PyStatus_Exception(status)) {
    return maybe_get_exit_code(&status, &config).value_or(1);
  }

  if (PyImport_AppendInittab(
        "_static_extension_utils", PyInit__static_extension_utils) != 0) {
    PyErr_Print();
    fprintf(stderr, "Error: could not update inittab\n");
    return 1;
  }

  // Ensure the PAR root is in the module search path as this is where
  // static_extension_finder.py is.
  if (real_exe.parent_path().filename() != "bin"
      || real_exe.parent_path().parent_path().filename() != "runtime") {
    std::cerr << "Expeted executable to be in a .../runtime/bin directory. "
        << "Actual path: " << real_exe << std::endl;
    return 1;
  }
  fs::path par_root = real_exe.parent_path().parent_path().parent_path();
  status = PyWideStringList_Append(
    &config.module_search_paths, par_root.wstring().c_str());
  if (PyStatus_Exception(status)) {
    return maybe_get_exit_code(&status, &config).value_or(1);
  }

  // Potential execution of site-customize etc. happens here.
  status = Py_InitializeFromConfig(&config);
  if (PyStatus_Exception(status)) {
    PyErr_Print();
    return maybe_get_exit_code(&status, &config).value_or(1);
  }

  // If site-customize ran then these two initializations are redundant.
  // However, if it did not then these are needed for a functional Native
  // Python build with CinderX.
  if (load_and_init_module("static_extension_finder", "_initialize") == -1) {
    return 1;
  }
  if (load_and_init_module("cinderx", "init") == -1) {
    return 1;
  }

  PyConfig_Clear(&config);
  return Py_RunMain();
}

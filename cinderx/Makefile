#
# Python-based test rules
#

# Required external environment variables:
# TESTPYTHON - Python binary/command to use to run cinder_test_runner.py

# Optional external environment variables:
# JIT_TEST_RUNNER_ARGS - Arbitrary args to pass to cinder_test_runner.py
# USE_RR - Setting this enables rr recording of test execution
# RECORDING_METADATA_PATH - Use with above to set path to record metadata.
# ASAN_TEST_ENV - Command prefix for environment variables when built with ASAN


ifeq ($(TESTPYTHON),)
TESTPYTHON=$(error TESTPYTHON must be set to a python binary)
endif

ifneq ($(strip $(USE_RR)),)
	JIT_TEST_RR_ARGS=--use-rr
endif
ifneq ($(strip $(RECORDING_METADATA_PATH)),)
	JIT_TEST_RR_ARGS += --recording-metadata-path=$(strip $(RECORDING_METADATA_PATH))
endif

# Arg 1 is args to the Python runtime e.g. -X args
# Arg 2 is args for the Cinder test runner script
# Arg 3 is args for libregrtest (python -m test --help)
define RUN_CINDER_TEST_RUNNER
	$(ASAN_TEST_ENV) $(TESTPYTHON) $(1) TestScripts/cinder_test_runner.py dispatcher $(JIT_TEST_RUNNER_ARGS) $(JIT_TEST_RR_ARGS) $(2) -- -w $(3)
endef

define RUN_TESTCINDERJIT_PROFILE
	TEST_CINDERJIT_PROFILE_MAKE_PROFILE=1 $(ASAN_TEST_ENV) $(TESTPYTHON) \
		-X jit-profile-interp -X jit-profile-interp-period=1 -X jit-write-profile="$(abs_builddir)/test_cinderjit_profile.types" \
		$(1) -munittest -v test_cinderx.test_cinderjit_profile
	TEST_CINDERJIT_PROFILE_TEST_PROFILE=1 $(ASAN_TEST_ENV) $(TESTPYTHON) \
		-X jit-read-profile="$(abs_builddir)/test_cinderjit_profile.types" -X jit \
		$(1) -munittest -v test_cinderx.test_cinderjit_profile
	rm "$(abs_builddir)/test_cinderjit_profile.types"
endef

define RUN_TESTCINDERJIT
	$(call RUN_CINDER_TEST_RUNNER, -X usepycompiler -X jit -X jit-enable-inline-cache-stats-collection $(1))
	$(ASAN_TEST_ENV) $(TESTPYTHON) -X jit $(1) -X jit-multithreaded-compile-test -X jit-batch-compile-workers=10 -m test_cinderx.multithreaded_compile_test
endef

define RUN_TESTCINDERJITAUTO
	$(call RUN_CINDER_TEST_RUNNER, -X usepycompiler -X jit-auto=200 -X jit-enable-inline-cache-stats-collection $(1))
endef

define RUN_TESTCINDERJIT_AUTOPROFILE
	$(call RUN_CINDER_TEST_RUNNER, -X usepycompiler -X jit-auto=200 -X jit-auto-profile=2 -X jit-enable-inline-cache-stats-collection $(1))
endef


testcinder:
	$(call RUN_CINDER_TEST_RUNNER)
.PHONY: testcinder

testcinder_refleak:
	$(call RUN_CINDER_TEST_RUNNER, , --worker-respawn-interval 1, -R :)
.PHONY: testcinder_refleak

testcinder_jit:
	$(call RUN_TESTCINDERJIT,)
.PHONY: testcinder_jit

testcinder_jit_auto:
	$(call RUN_TESTCINDERJITAUTO,)
.PHONY: testcinder_jit_auto

testcinder_jit_profile:
	$(call RUN_TESTCINDERJIT_PROFILE,)
.PHONY: testcinder_jit_profile

testcinder_jit_auto_profile:
	$(call RUN_TESTCINDERJIT_AUTOPROFILE,)
.PHONY: testcinder_jit_auto_profile

testcinder_jit_shadowframe:
	$(call RUN_TESTCINDERJIT,-X jit-shadow-frame)
.PHONY: testcinder_jit_shadowframe

testcinder_jit_shadowframe_profile:
	$(call RUN_TESTCINDERJIT_PROFILE,-X jit-shadow-frame)
.PHONY: testcinder_jit_shadowframe_profile

testcinder_jit_inliner:
	$(call RUN_TESTCINDERJIT,-X jit-enable-hir-inliner)
.PHONY: testcinder_jit_inliner

testcinder_jit_inliner_profile:
	$(call RUN_TESTCINDERJIT_PROFILE,-X jit-enable-hir-inliner)
.PHONY: testcinder_jit_inliner_profile

testcinder_jit_shadowframe_inliner:
	$(call RUN_TESTCINDERJIT,-X jit-shadow-frame -X jit-enable-hir-inliner)
.PHONY: testcinder_jit_shadowframe_inliner

testcinder_jit_shadowframe_inliner_profile:
	$(call RUN_TESTCINDERJIT_PROFILE,-X jit-shadow-frame -X jit-enable-hir-inliner)
.PHONY: testcinder_jit_shadowframe_inliner_profile


#
# C++ Runtime/Strict Module rules
#

ULIMIT_VMEM=$$(( 1024 * 1024 ))
GTEST_WORKERS=10
ifeq ($(strip $(ASAN_TEST_ENV)),)
	ULIMIT_PARALLEL= && ulimit -v $$(($(ULIMIT_VMEM) * $(GTEST_WORKERS)))
	ULIMIT= && ulimit -v $(ULIMIT_VMEM)
else
	ULIMIT_PARALLEL=
	ULIMIT=
endif

# Required environment variables:
# abs_builddir - Absolute path of the CPython build (may be different from
#     source for out-of-tree builds)
# abs_srcdir - Absolute path of the CPython source.
# CXX - C++ compiler
# PYTHON_CONFIG_CMD - Shell snippet to run the generated python-config.py.
# PY_CORE_CXXFLAGS - C++ build flags.
# ASAN_TEST_ENV - ASAN env variables.
# ASAN_TEST_ENV_WITH_LSAN - ASAN env variables.
# PYTHONPATH - Python extension/stdlib search path.
# LIBPYTHON_A - absolute libpython.a path
# TEST_LINK_FLAGS - Linker args used when linking
# TESTPYTHON - Python binary. This is primarily used to run gtest-parallel as
#    using the system Python gets confused when run out of the Cinder root.
# CINDERX_SO - The _cinderx.SOABI.so file.

GTEST_DIR=ThirdParty/googletest-1.8.1/googletest
GTEST_BUILD_DIR=$(abs_builddir)/cinderx/$(GTEST_DIR)
GTEST_SRCDIR=$(CURDIR)/$(GTEST_DIR)
RUNTIME_TESTS_DIR=RuntimeTests
RUNTIME_TESTS_BUILD_DIR=$(abs_builddir)/cinderx/$(RUNTIME_TESTS_DIR)
RUNTIME_TESTS_SRCDIR=$(CURDIR)/$(RUNTIME_TESTS_DIR)

STRICTM_TESTS_DIR=StrictModules/Tests
STRICTM_TESTS_BUILD_DIR=$(abs_builddir)/cinderx/$(STRICTM_TESTS_DIR)
STRICTM_TESTS_SRCDIR=$(CURDIR)/$(STRICTM_TESTS_DIR)


${GTEST_BUILD_DIR}/libgtest.a: ${GTEST_SRCDIR}/src/gtest-all.cc
	$(CXX) -std=c++11 -isystem ${GTEST_SRCDIR}/include -I${GTEST_SRCDIR} -pthread -c $< -o ${GTEST_BUILD_DIR}/gtest-all.o
	ar -rv $@ ${GTEST_BUILD_DIR}/gtest-all.o

RUNTIME_TEST_HEADERS = \
       ${RUNTIME_TESTS_SRCDIR}/fixtures.h \
       ${RUNTIME_TESTS_SRCDIR}/testutil.h

RUNTIME_TESTS_OBJS= \
	${RUNTIME_TESTS_BUILD_DIR}/alias_class_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/backend_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/bitvector_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/block_canonicalizer_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/bytecode_offsets_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/bytecode_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/cmdline_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/copy_graph_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/dataflow_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/deopt_patcher_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/deopt_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/elf_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/fixtures.o \
	${RUNTIME_TESTS_BUILD_DIR}/gen_asm_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/hir_analysis_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/hir_copy_propagation_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/hir_frame_state_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/hir_guard_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/hir_operand_type_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/hir_parser_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/hir_ssa_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/hir_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/hir_type_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/inline_cache_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/intrusive_list_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/jit_context_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/jit_flag_processor_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/jit_list_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/jit_time_log_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/lir_dce_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/lir_inliner_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/lir_postalloc_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/lir_postgen_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/lir_verify_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/lir_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/live_type_map_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/main.o \
	${RUNTIME_TESTS_BUILD_DIR}/profile_runtime_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/pyjit_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/ref_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/regalloc_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/sanity_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/slab_arena_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/testutil.o \
	${RUNTIME_TESTS_BUILD_DIR}/type_profiler_test.o \
	${RUNTIME_TESTS_BUILD_DIR}/util_test.o

STRICTM_TEST_HEADERS = \
    ${STRICTM_TESTS_SRCDIR}/test.h \
	${STRICTM_TESTS_SRCDIR}/test_util.h

STRICTM_TESTS_OBJS= \
	${STRICTM_TESTS_BUILD_DIR}/parse_test.o \
	${STRICTM_TESTS_BUILD_DIR}/analyzer_test.o \
	${STRICTM_TESTS_BUILD_DIR}/module_loader_test.o \
	${STRICTM_TESTS_BUILD_DIR}/scope_test.o \
	${STRICTM_TESTS_BUILD_DIR}/test_util.o \
	${STRICTM_TESTS_BUILD_DIR}/main.o

pyembed_includes:
	$(PYTHON_CONFIG_CMD) --includes > pyembed_includes

$(RUNTIME_TESTS_OBJS): $(RUNTIME_TESTS_BUILD_DIR)/%.o: pyembed_includes $(RUNTIME_TEST_HEADERS) $(RUNTIME_TESTS_SRCDIR)/%.cpp
	$(CXX) $(PY_CORE_CXXFLAGS) $(shell cat pyembed_includes) \
		-DBAKED_IN_PYTHONPATH=$(PYTHONPATH) \
		-isystem $(CURDIR)/ThirdParty -isystem $(GTEST_SRCDIR)/include -c $(filter %.cpp,$^) -o $@

$(STRICTM_TESTS_OBJS): $(STRICTM_TESTS_BUILD_DIR)/%.o: pyembed_includes $(STRICTM_TEST_HEADERS) $(STRICTM_TESTS_SRCDIR)/%.cpp
	$(CXX) $(PY_CORE_CXXFLAGS) $(shell cat pyembed_includes) \
		-DBAKED_IN_PYTHONPATH=$(PYTHONPATH) \
		-isystem $(CURDIR)/ThirdParty -isystem $(GTEST_SRCDIR)/include -c $(filter %.cpp,$^) -o $@

$(RUNTIME_TESTS_BUILD_DIR)/runtime_tests: ${GTEST_BUILD_DIR}/libgtest.a $(RUNTIME_TESTS_OBJS) $(LIBPYTHON_A)
	$(eval PYEMBED_LIBS := $$(shell $(PYTHON_CONFIG_CMD) --libs | sed -e "s/-lpython[^ ]*//"))
	cd $(abs_builddir) && \
		$(CXX) -std=c++20 -I. -isystem ${GTEST_SRCDIR}/include -pthread \
		$(RUNTIME_TESTS_OBJS) \
		${GTEST_BUILD_DIR}/libgtest.a \
		$(TEST_LINK_FLAGS) $(PYEMBED_LIBS) \
		$(CINDERX_SO) \
		-o $(RUNTIME_TESTS_BUILD_DIR)/runtime_tests -ggdb -rdynamic

runtime_tests: $(RUNTIME_TESTS_BUILD_DIR)/runtime_tests
.PHONY: runtime_tests

testruntime: $(RUNTIME_TESTS_BUILD_DIR)/runtime_tests
	cd $(abs_srcdir) $(ULIMIT_PARALLEL) && \
		$(ASAN_TEST_ENV_WITH_LSAN) \
		$(TESTPYTHON) $(CURDIR)/ThirdParty/gtest-parallel/gtest-parallel -w $(GTEST_WORKERS) \
		$(RUNTIME_TESTS_BUILD_DIR)/runtime_tests
.PHONY: testruntime

testruntime_serial: $(RUNTIME_TESTS_BUILD_DIR)/runtime_tests
	cd $(abs_srcdir) $(ULIMIT) && \
		LD_LIBRARY_PATH=$(abs_builddir) \
		$(ASAN_TEST_ENV_WITH_LSAN) \
		$(RUNTIME_TESTS_BUILD_DIR)/runtime_tests
.PHONY: testruntime_serial

$(STRICTM_TESTS_BUILD_DIR)/strict_module_tests: ${GTEST_BUILD_DIR}/libgtest.a $(STRICTM_TESTS_OBJS) $(STRICTM_HEADERS) $(LIBPYTHON_A)
	$(eval PYEMBED_LIBS := $$(shell $(PYTHON_CONFIG_CMD) --libs | sed -e "s/-lpython[^ ]*//"))
	cd $(abs_builddir) && \
		$(CXX) -std=c++20 -I. -isystem ${GTEST_SRCDIR}/include -pthread \
		$(STRICTM_TESTS_OBJS) \
		${GTEST_BUILD_DIR}/libgtest.a \
		$(TEST_LINK_FLAGS) $(PYEMBED_LIBS) \
		$(CINDERX_SO) \
		-o $(STRICTM_TESTS_BUILD_DIR)/strict_module_tests -ggdb -rdynamic

strict_module_tests: $(STRICTM_TESTS_BUILD_DIR)/strict_module_tests
.PHONY: strict_module_tests

STRICTM_ASAN_SKIP_TESTS=
ifneq ($(strip $(ASAN_TEST_ENV)),)
	STRICTM_ASAN_SKIP_TESTS = asan_skip_tests.txt
endif

test_strict_module: $(STRICTM_TESTS_BUILD_DIR)/strict_module_tests
	cd $(abs_srcdir) $(ULIMIT_PARALLEL) && \
		LD_LIBRARY_PATH=$(abs_builddir) \
		$(ASAN_TEST_ENV_WITH_LSAN) \
		$(TESTPYTHON) $(CURDIR)/ThirdParty/gtest-parallel/gtest-parallel -w $(GTEST_WORKERS) \
		$(STRICTM_TESTS_BUILD_DIR)/strict_module_tests -- $(STRICTM_ASAN_SKIP_TESTS)
.PHONY: test_strict_module


#
# Regen rules
#

UPDATE_FILE=python3 $(abs_srcdir)/Tools/scripts/update_file.py

.PHONY: regen-all
regen-all: regen-opcode regen-opcode-targets regen-jit

.PHONY: regen-opcode
regen-opcode:
	PYTHONPATH=$(CURDIR)/PythonLib python3 $(abs_srcdir)/Tools/scripts/generate_opcode_h.py \
		$(abs_srcdir)/Lib/opcode.py \
		Interpreter/opcode.h.new
	$(UPDATE_FILE) Interpreter/opcode.h Interpreter/opcode.h.new

.PHONY: regen-opcode-targets
regen-opcode-targets:
	PYTHONPATH=$(CURDIR)/PythonLib python3 $(abs_srcdir)/Python/makeopcodetargets.py Interpreter/cinderx_opcode_targets.h.new
	$(UPDATE_FILE) Interpreter/cinderx_opcode_targets.h Interpreter/cinderx_opcode_targets.h.new

.PHONY: regen-jit
regen-jit:
	python3 Jit/hir/generate_jit_type_h.py Jit/hir/type_generated.h.new
	$(UPDATE_FILE) Jit/hir/type_generated.h Jit/hir/type_generated.h.new

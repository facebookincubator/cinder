# Possible external environment variables:
# JIT_TEST_RUNNER_ARGS - Arbitrary args to pass to cinder_test_runner.py
# USE_RR - Setting this enables rr recording of test execution
# RECORDING_METADATA_PATH - Use with above to set path to record metadata.
# ASAN_TEST_ENV - Command prefix for environment variables when built with ASAN
# TESTPYTHON - Python binary/command to use to run cinder_test_runner.py


ifeq ($(TESTPYTHON),)
$(error TESTPYTHON must be set to a python binary)
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
	CINDER_JIT_PROFILE_TEST_PROFILING=1 $(ASAN_TEST_ENV) $(TESTPYTHON) -X jit-profile-interp -X jit-profile-interp-period=1 -X jit-write-profile=cinderjit_profile_test.types $(1) -munittest -v test_cinderx.cinderjit_profile_test
	$(ASAN_TEST_ENV) $(TESTPYTHON) -X jit-read-profile=cinderjit_profile_test.types -X jit $(1) -munittest -v test_cinderx.cinderjit_profile_test
	rm cinderjit_profile_test.types
endef

define RUN_TESTCINDERJIT
	$(call RUN_CINDER_TEST_RUNNER, -X usepycompiler -X jit -X jit-enable-inline-cache-stats-collection $(1))
	# TODO re-enable when it doesn't crash on exit
	# $(ASAN_TEST_ENV) $(TESTPYTHON) -X jit $(1) -X jit-multithreaded-compile-test -X jit-batch-compile-workers=10 Tests/multithreaded_compile_test.py
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

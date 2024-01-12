CXX=CL -EHsc -DNDEBUG -Fo$@ -O2
#CXX=CL -EHsc -g -debug -Zi  -Fo$@
#CXX=g++ -ggdb -O2 -lm -std=c++11 -DNDEBUG 

ABSEIL_LIBS=absl_bad_optional_access.lib absl_bad_variant_access.lib absl_base.lib absl_demangle_internal.lib absl_hash.lib absl_int128.lib absl_internal_bad_any_cast_impl.lib absl_internal_city.lib absl_internal_civil_time.lib absl_internal_debugging_internal.lib absl_internal_graphcycles_internal.lib absl_internal_hashtablez_sampler.lib absl_internal_malloc_internal.lib absl_internal_spinlock_wait.lib absl_internal_strings_internal.lib absl_internal_throw_delegate.lib absl_internal_time_zone.lib absl_optional.lib absl_raw_hash_set.lib absl_stacktrace.lib absl_strings.lib absl_symbolize.lib absl_synchronization.lib absl_time.lib

PROGS       = stl_unordered_map sparsepp phmap abseil_flat abseil_parallel_flat phmap_flat
BUILD_PROGS = $(addprefix build/,$(PROGS))
SIZE        = 100000000
ABSEIL      = ../../abseil-cpp
PHMAP_SRC   = ../parallel_hashmap

all: test

builddir: 
	@if [ ! -d build ]; then mkdir build; fi

build/stl_unordered_map: bench.cc Makefile
	$(CXX) -DSTL_UNORDERED -I.. bench.cc -o $@

build/sparsepp: bench.cc Makefile
	$(CXX) -DSPARSEPP -I.. -I../../sparsepp bench.cc -o $@

build/phmap: bench.cc Makefile $(PHMAP_SRC)/phmap.h
	$(CXX) -DPHMAP -I.. -I$(ABSEIL) bench.cc /MD -o $@ /link /LIBPATH:$(ABSEIL)/build/lib ${ABSEIL_LIBS}

build/phmap_flat: bench.cc Makefile $(PHMAP_SRC)/phmap.h
	$(CXX) -DPHMAP_FLAT -I.. bench.cc /MD -o $@ 

build/abseil_flat: bench.cc Makefile
	$(CXX) -DABSEIL_FLAT -I.. -I$(ABSEIL) bench.cc /MD -o $@ /link /LIBPATH:$(ABSEIL)/build/lib ${ABSEIL_LIBS}

build/abseil_parallel_flat: bench.cc Makefile
	$(CXX) -DABSEIL_PARALLEL_FLAT -I.. -I $(ABSEIL) bench.cc /MD -o $@ /link /LIBPATH:$(ABSEIL)/build/lib ${ABSEIL_LIBS}

build/emplace: emplace.cc Makefile $(PHMAP_SRC)/phmap.h
	$(CXX) -DABSEIL_FLAT -I.. -I$(ABSEIL) emplace.cc /MD -o $@ /link /LIBPATH:$(ABSEIL)/build/lib ${ABSEIL_LIBS}

progs:	$(BUILD_PROGS)

test: builddir progs
	-rm -f output
	#./build/stl_unordered_map $(SIZE) random >> output
	#./build/sparsepp    $(SIZE) random >> output
	./build/abseil_flat  $(SIZE) random >> output
	#./build/phmap_flat   $(SIZE) random >> output
	./build/phmap        $(SIZE) random >> output
	./build/abseil_parallel_flat $(SIZE) random >> output
	python make_chart_data.py < output

test_cust: 
	-rm -f output
	#./build/abseil_flat $(SIZE) random >> output
	#./build/sparsepp    $(SIZE) random >> output
	./build/abseil_parallel_flat_5 $(SIZE) random >> output
	./build/abseil_parallel_flat $(SIZE) random >> output
	python make_chart_data.py < output

chart:
	python make_chart_data.py < output

clean:
	-rm -fr output build

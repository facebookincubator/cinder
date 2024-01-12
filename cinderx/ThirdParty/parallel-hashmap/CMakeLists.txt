cmake_minimum_required(VERSION 3.2)

list (APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(DetectVersion)

cmake_policy(SET CMP0048 NEW)                    ## set VERSION as documented by the project() command.
set(CMAKE_CXX_STANDARD 11)                       ## compile with C++11 support
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(phmap VERSION ${DETECTED_PHMAP_VERSION} LANGUAGES CXX)

set(PHMAP_HEADERS phmap.h phmap_base.h phmap_bits.h phmap_utils.h phmap_config.h)
set(PHMAP_DIR parallel_hashmap)
set(CMAKE_SUPPRESS_REGENERATION true)            ## suppress ZERO_CHECK project

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(helpers)
include_directories("${CMAKE_CURRENT_SOURCE_DIR}")

add_library(${PROJECT_NAME} INTERFACE)

target_sources(${PROJECT_NAME} INTERFACE ${PHMAP_HEADERS})

target_include_directories(
     ${PROJECT_NAME} INTERFACE
     $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
     $<INSTALL_INTERFACE:include>)

install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/${PHMAP_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PHMAP_DIR})

install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}-targets)

export(EXPORT ${PROJECT_NAME}-targets
       FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Targets.cmake")

## ------------------------- building tests and examples -------------
option(PHMAP_BUILD_TESTS    "Whether or not to build the tests"    OFF)
option(PHMAP_BUILD_EXAMPLES "Whether or not to build the examples" OFF)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj")
else()
    if(CXXFLAGS)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXXFLAGS}")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O3 -march=native")
    endif()
endif()

if (PHMAP_BUILD_TESTS)

    include(cmake/DownloadGTest.cmake)
    include_directories(${PROJECT_SOURCE_DIR})

    check_target(gtest)
    check_target(gtest_main)
    check_target(gmock)

    enable_testing()

    ## ---------------- regular hash maps ----------------------------
    phmap_cc_test(NAME compressed_tuple SRCS "tests/compressed_tuple_test.cc"
                  DEPS gmock_main)

    phmap_cc_test(NAME container_memory SRCS "tests/container_memory_test.cc"
                  DEPS gmock_main)

    phmap_cc_test(NAME hash_policy_testing SRCS "tests/hash_policy_testing_test.cc"
                  DEPS gmock_main)

    phmap_cc_test(NAME node_hash_policy SRCS "tests/node_hash_policy_test.cc"
                  DEPS gmock_main)

    phmap_cc_test(NAME raw_hash_set SRCS "tests/raw_hash_set_test.cc"
                  DEPS gmock_main)

    phmap_cc_test(NAME raw_hash_set_allocator SRCS "tests/raw_hash_set_allocator_test.cc"
                  DEPS gmock_main)

    ## ---------------- regular hash maps ----------------------------
    phmap_cc_test(NAME flat_hash_set SRCS "tests/flat_hash_set_test.cc"
                  COPTS "-DUNORDERED_SET_CXX17" DEPS gmock_main)

    phmap_cc_test(NAME flat_hash_map SRCS "tests/flat_hash_map_test.cc"
                  DEPS gmock_main)

    phmap_cc_test(NAME node_hash_map SRCS "tests/node_hash_map_test.cc"
                  DEPS gmock_main)

    phmap_cc_test(NAME node_hash_set SRCS "tests/node_hash_set_test.cc"
                  COPTS "-DUNORDERED_SET_CXX17" DEPS  gmock_main)

    ## --------------- parallel hash maps -----------------------------------------------
    phmap_cc_test(NAME parallel_flat_hash_map SRCS "tests/parallel_flat_hash_map_test.cc"
                  COPTS "-DUNORDERED_MAP_CXX17" DEPS gmock_main)

    phmap_cc_test(NAME parallel_flat_hash_set SRCS "tests/parallel_flat_hash_set_test.cc"
                  COPTS "-DUNORDERED_SET_CXX17" DEPS gmock_main)

    phmap_cc_test(NAME parallel_node_hash_map SRCS "tests/parallel_node_hash_map_test.cc"
                  DEPS gmock_main)

    phmap_cc_test(NAME parallel_node_hash_set SRCS "tests/parallel_node_hash_set_test.cc"
                  COPTS "-DUNORDERED_SET_CXX17" DEPS  gmock_main)

    phmap_cc_test(NAME parallel_flat_hash_map_mutex SRCS "tests/parallel_flat_hash_map_mutex_test.cc"
                  COPTS "-DUNORDERED_MAP_CXX17" DEPS gmock_main)

    phmap_cc_test(NAME dump_load SRCS "tests/dump_load_test.cc"
                  COPTS "-DUNORDERED_MAP_CXX17" DEPS gmock_main)

    ## --------------- btree -----------------------------------------------
    phmap_cc_test(NAME btree SRCS "tests/btree_test.cc"
                  CLOPTS "-w" DEPS gmock_main)


endif()

if (PHMAP_BUILD_EXAMPLES)
    if(NOT MSVC)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic -Wall -Wextra -Wcast-align -Wcast-qual -Wdisabled-optimization -Winit-self -Wlogical-op -Wmissing-include-dirs -Woverloaded-virtual -Wredundant-decls -Wshadow -Wstrict-null-sentinel  -Wswitch-default -Wno-unused -Wno-unknown-warning-option -Wno-gnu-zero-variadic-macro-arguments")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /Zc:__cplusplus /std:c++latest")
    endif()

    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)

    add_executable(ex_allmaps examples/allmaps.cc phmap.natvis)
    add_executable(ex_basic examples/basic.cc phmap.natvis)
    add_executable(ex_bench examples/bench.cc phmap.natvis)
    add_executable(ex_emplace examples/emplace.cc phmap.natvis)
    add_executable(ex_serialize examples/serialize.cc phmap.natvis)
    add_executable(ex_hash_std examples/hash_std.cc phmap.natvis)
    add_executable(ex_hash_value examples/hash_value.cc phmap.natvis)
    add_executable(ex_two_files examples/f1.cc examples/f2.cc phmap.natvis)
    add_executable(ex_insert_bench examples/insert_bench.cc phmap.natvis)
    add_executable(ex_knucleotide examples/knucleotide.cc phmap.natvis)
    add_executable(ex_dump_load examples/dump_load.cc phmap.natvis)
    add_executable(ex_btree examples/btree.cc phmap.natvis)
    add_executable(ex_matt examples/matt.cc phmap.natvis)

    target_link_libraries(ex_knucleotide Threads::Threads)
    target_link_libraries(ex_bench Threads::Threads)
endif()


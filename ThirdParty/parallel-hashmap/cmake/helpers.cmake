#set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(PHMAP_IDE_FOLDER phmap)

# -------------------------------------------------------------
# phmap_cc_test(NAME awesome_test
#               SRCS "awesome_test.cc"
#               DEPS phmap::awesome gmock gtest_main)
# -------------------------------------------------------------
function(phmap_cc_test)
  cmake_parse_arguments(PHMAP_CC_TEST
    ""
    "NAME"
    "SRCS;COPTS;CWOPTS;CLOPTS;DEFINES;LINKOPTS;DEPS"
    ${ARGN}
  )

  set(_NAME "test_${PHMAP_CC_TEST_NAME}")
  add_executable(${_NAME} "")
  target_sources(${_NAME} PRIVATE ${PHMAP_CC_TEST_SRCS})
  target_include_directories(${_NAME}
    PUBLIC ${PHMAP_COMMON_INCLUDE_DIRS}
    PRIVATE ${GMOCK_INCLUDE_DIRS} ${GTEST_INCLUDE_DIRS}
  )
  target_compile_definitions(${_NAME}
    PUBLIC ${PHMAP_CC_TEST_DEFINES}
  )
if(MSVC)
  target_compile_options(${_NAME}
    PRIVATE ${PHMAP_CC_TEST_CWOPTS} /W4 /Zc:__cplusplus /std:c++latest
  )
else()
  target_compile_options(${_NAME}
    PRIVATE ${PHMAP_CC_TEST_CLOPTS}
  )
endif()
  target_compile_options(${_NAME}
    PRIVATE ${PHMAP_CC_TEST_COPTS}
  )
  target_link_libraries(${_NAME}
    PUBLIC ${PHMAP_CC_TEST_DEPS}
    PRIVATE ${PHMAP_CC_TEST_LINKOPTS}
  )
  # Add all Abseil targets to a a folder in the IDE for organization.
  set_property(TARGET ${_NAME} PROPERTY FOLDER ${PHMAP_IDE_FOLDER}/test)

  set_property(TARGET ${_NAME} PROPERTY CXX_STANDARD ${PHMAP_CXX_STANDARD})
  set_property(TARGET ${_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)

  add_test(NAME ${_NAME} COMMAND ${_NAME})
endfunction()

# -------------------------------------------------------------
function(check_target my_target)
  if(NOT TARGET ${my_target})
    message(FATAL_ERROR " PHMAP: compiling phmap tests requires a ${my_target} CMake target in your project,
                   see CMake/README.md for more details")
  endif(NOT TARGET ${my_target})
endfunction()



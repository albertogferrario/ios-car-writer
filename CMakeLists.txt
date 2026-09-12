#
# ios-car-writer
#
# A minimal, Linux-native Assets.car (compiled iOS asset catalog) reader/writer.
# Forked from facebookarchive/xcbuild (BSD License) at commit
# dbaee552d2f13640773eb1ad3c79c0d2aca7229c (the last commit to touch
# Libraries/libcar, 2019-11-20). Vendors only Libraries/libcar,
# Libraries/libbom, Libraries/ext, and a trimmed Libraries/libutil
# (the LIBUTIL_PACKED_STRUCT_BEGIN/END macro header only) -- the rest of the
# upstream xcbuild tree (acdriver, graphics, pbxbuild, xcassets, etc.) is not
# needed and is not vendored.
#

cmake_minimum_required(VERSION 3.14)
project(ios-car-writer C CXX)

cmake_policy(SET CMP0054 NEW)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  add_compile_options(-Wall)
endif ()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

include(CTest)

if (BUILD_TESTING)
  include(FetchContent)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
  )
  FetchContent_MakeAvailable(googletest)

  function (ADD_UNIT_GTEST LIBRARY NAME SOURCES)
    set(TARGET_NAME "test_${LIBRARY}_${NAME}")
    add_executable("${TARGET_NAME}" ${SOURCES})
    target_link_libraries("${TARGET_NAME}" PRIVATE "${LIBRARY}" gtest gtest_main)
    add_test(NAME "${TARGET_NAME}" COMMAND "${TARGET_NAME}")
  endfunction ()
endif ()

add_subdirectory(Libraries)

#
# Copyright (c) 2015-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree.
#
# Trimmed fork: the upstream Tools/ PNG-dump executable and its image-export
# link target are not vendored -- see this repo's README for the rationale
# (raw-byte rendition copy only, no PNG decode/encode needed).
#

add_library(car
            Sources/Reader.cpp
            Sources/AttributeList.cpp
            Sources/Facet.cpp
            Sources/Rendition.cpp
            Sources/car_format.c
            Sources/Writer.cpp
            Sources/VariablePassthrough.cpp
            )

find_package(ZLIB REQUIRED)
target_include_directories(car PRIVATE "${ZLIB_INCLUDE_DIR}")
target_link_libraries(car PRIVATE ${ZLIB_LIBRARIES})

find_library(COMPRESSION compression)
if ("${COMPRESSION}" STREQUAL "COMPRESSION-NOTFOUND")
  set(COMPRESSION "")
endif ()

target_link_libraries(car PUBLIC ext bom ${COMPRESSION})
target_include_directories(car PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Headers")
install(TARGETS car DESTINATION usr/lib)

# car_roundtrip: the roundtrip/verify CLI (Phase 243 Plan 02), wiring
# Reader -> Writer end to end. See Tools/car_roundtrip.cpp.
add_executable(car_roundtrip Tools/car_roundtrip.cpp)
target_link_libraries(car_roundtrip PRIVATE car)
install(TARGETS car_roundtrip DESTINATION usr/bin)

if (BUILD_TESTING)
  ADD_UNIT_GTEST(car Facet Tests/test_Facet.cpp)
  ADD_UNIT_GTEST(car Rendition Tests/test_Rendition.cpp)
  ADD_UNIT_GTEST(car AttributeList Tests/test_AttributeList.cpp)
  ADD_UNIT_GTEST(car Writer Tests/test_Writer.cpp)

  # Lets the real-shell-catalog round-trip test (test_Writer.cpp) locate the
  # checked-in fixture regardless of ctest's working directory.
  target_compile_definitions(test_car_Writer PRIVATE CAR_ROUNDTRIP_REPO_ROOT="${CMAKE_SOURCE_DIR}")
endif ()

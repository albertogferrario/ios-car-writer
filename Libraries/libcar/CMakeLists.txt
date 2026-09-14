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

target_link_libraries(car PUBLIC ext bom lzfse ${COMPRESSION})
target_include_directories(car PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Headers")
install(TARGETS car DESTINATION usr/lib)

# car_roundtrip: the roundtrip/verify/patch CLI (Phase 243 Plan 02, Phase 244
# Plan 02), wiring Reader -> Writer end to end. See Tools/car_roundtrip.cpp.
# The `patch` verb's AppIcon/SplashScreenLogo master-PNG decode (D-03) is a
# dependency of THIS tool only -- the reusable `car` library above stays
# raw-byte-copy-only, no image codec (see this file's original trimmed-fork
# comment). The Alpine static-musl release build (.github/workflows/release.yml)
# installs libpng-dev/libpng-static and links libpng statically.
find_package(PNG REQUIRED)

add_executable(car_roundtrip Tools/car_roundtrip.cpp)
target_include_directories(car_roundtrip PRIVATE ${PNG_INCLUDE_DIRS})
target_link_libraries(car_roundtrip PRIVATE car ${PNG_LIBRARIES})
install(TARGETS car_roundtrip DESTINATION usr/bin)

if (BUILD_TESTING)
  ADD_UNIT_GTEST(car Facet Tests/test_Facet.cpp)
  ADD_UNIT_GTEST(car Rendition Tests/test_Rendition.cpp)
  ADD_UNIT_GTEST(car AttributeList Tests/test_AttributeList.cpp)
  ADD_UNIT_GTEST(car Writer Tests/test_Writer.cpp)
  ADD_UNIT_GTEST(car Patch Tests/test_Patch.cpp)

  # Lets the real-shell-catalog tests (test_Writer.cpp, test_Patch.cpp)
  # locate the checked-in fixture regardless of ctest's working directory.
  target_compile_definitions(test_car_Writer PRIVATE CAR_ROUNDTRIP_REPO_ROOT="${CMAKE_SOURCE_DIR}")
  target_compile_definitions(test_car_Patch PRIVATE CAR_ROUNDTRIP_REPO_ROOT="${CMAKE_SOURCE_DIR}")
endif ()

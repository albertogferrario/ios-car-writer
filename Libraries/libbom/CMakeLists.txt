#
# Copyright (c) 2015-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree.
#
# Trimmed fork: the upstream lsbom/dump_bom Tools/ executables are not
# vendored (lsbom needs libutil/Options.h, which this fork intentionally
# does not carry -- see Libraries/libutil/CMakeLists.txt).
#

add_library(bom
            Sources/bom.c
            Sources/bom_memory.c
            Sources/bom_tree.c
            )

target_link_libraries(bom PUBLIC util)
target_include_directories(bom PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Headers")
install(TARGETS bom DESTINATION usr/lib)

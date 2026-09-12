#
# Trimmed fork: upstream Libraries/libutil is a full filesystem/options/md5
# utility library. This fork vendors only Headers/libutil/CompilerSupport.h --
# the LIBUTIL_PACKED_STRUCT_BEGIN/END macros car_format.h needs for its
# packed wire-format structs -- so it is declared as a header-only INTERFACE
# library rather than vendoring the full upstream Sources/ tree.
#

add_library(util INTERFACE)
target_include_directories(util INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/Headers")

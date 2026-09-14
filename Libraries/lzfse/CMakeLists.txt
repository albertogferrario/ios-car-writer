#
# Vendored lzfse (Apple, BSD-3-Clause). See PROVENANCE.md for the pinned
# upstream commit and the exact vendoring scope. Only the library translation
# units are built here -- the upstream CLI (lzfse_main.c) is not vendored, and
# no upstream install/test target is imported.
#
# Static-linked into libcar so Rendition::Encode() can emit the App Store
# marketing icon as lzfse+KCBC (compression=4), the codec actool produces.
#

add_library(lzfse STATIC
            src/lzfse_decode.c
            src/lzfse_decode_base.c
            src/lzfse_encode.c
            src/lzfse_encode_base.c
            src/lzfse_fse.c
            src/lzvn_decode_base.c
            src/lzvn_encode_base.c
            )

# src/ holds both the public header (lzfse.h) and the internal headers the
# translation units include from their own directory.
target_include_directories(lzfse PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

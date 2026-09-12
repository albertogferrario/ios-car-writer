# ios-car-writer

A minimal, Linux-native reader/writer for compiled iOS asset catalogs
(`Assets.car`, the CoreUI/BOM binary format Xcode's `actool` produces).

## Origin

Forked from [`facebookarchive/xcbuild`](https://github.com/facebookarchive/xcbuild)
(BSD-licensed, archived 2021-01-02) at commit
`dbaee552d2f13640773eb1ad3c79c0d2aca7229c` (2019-11-20, the last commit to
touch `Libraries/libcar`). Vendors `Libraries/libcar`, `Libraries/libbom`,
and `Libraries/ext` in full, plus a trimmed `Libraries/libutil` (the
`LIBUTIL_PACKED_STRUCT_BEGIN`/`END` compiler-support macros `car_format.h`
needs for its packed wire-format structs -- the rest of upstream's
filesystem/options/md5 utility library is not vendored). The rest of the
upstream `xcbuild` tree (`acdriver`, `graphics`, `pbxbuild`, `xcassets`, and
so on) is not needed and is not vendored.

## Scope

This fork adds a minimal set of targeted edits on top of the vendored
`libcar`/`libbom`:

- A `Writer::header()` full-struct override (mirroring the existing
  `Writer::keyfmt()` optional setter) so a caller can carry a source
  `CARHEADER` through `write()` verbatim, instead of `libcar`'s stale
  compiled-in constants (`ui_version = 0x131`, `storage_version = 0xC`,
  `schema_version = 4`).
- A matching `Reader::header()` getter.
- A one-line fix to `Writer::write()`'s hardcoded `rendition_count = 0` bug
  (the correct value was already computed and discarded two lines above).

See the upstream project's own `LICENSE` file (BSD) for licensing terms,
preserved verbatim in this fork.

## Building

```
cmake -B build -DBUILD_TESTING=ON .
cmake --build build
ctest --test-dir build --output-on-failure
```

Requires a C++11 compiler and zlib (`find_package(ZLIB REQUIRED)`). Tests
are built with GoogleTest, fetched via CMake `FetchContent` when
`BUILD_TESTING=ON`.

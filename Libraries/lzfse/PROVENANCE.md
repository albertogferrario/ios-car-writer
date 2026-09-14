# Vendored lzfse

Reference LZFSE compression library, Apple Inc., BSD-3-Clause (see `LICENSE`).

- Upstream: https://github.com/lzfse/lzfse
- Pinned commit: `e634ca58b4821d9f3d560cdc6df5dec02ffc93fd`
- Vendored: `src/` verbatim, excluding `lzfse_main.c` (the standalone CLI, which
  carries its own `main()` and is not part of the library).

Static-linked into `libcar` to emit the App Store marketing-icon rendition as
lzfse compressed data wrapped in Apple's KCBC block framing (compression=4),
the codec `actool` produces. See `Libraries/libcar/Sources/Rendition.cpp`
`Encode()` and `246-KCBC-FRAMING-SPEC.md` in the apps-web-app planning tree.

Only the library translation units and headers are vendored; no upstream build,
install, or test target is imported. The build is declared in `CMakeLists.txt`
here.

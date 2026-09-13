/*
 * car-writer: roundtrip/verify CLI for Assets.car (compiled iOS asset
 * catalog), wiring car::Reader -> car::Writer end to end.
 *
 * Mechanism (see 243-RESEARCH.md "Round-Trip Tool Skeleton", D-04/D-15):
 *   - header()/keyfmt() are carried through verbatim from the source
 *     catalog (never libcar's stale synthesis constants).
 *   - renditions are copied raw-KV byte-verbatim (renditionFastIterate ->
 *     addRendition(void*,size_t,void*,size_t)) -- no decode/re-encode, so
 *     codec support is irrelevant to this tool (D-15).
 *   - facets are copied via the structured facetIterate -> addFacet(Facet
 *     const&) path; losslessness is proven (not assumed) by the round-trip
 *     test in Tests/test_Writer.cpp via an independent raw FACETKEYS diff.
 *   - every OTHER top-level BOM variable the source catalog carries --
 *     anything libcar's structured Writer does not itself emit (observed on
 *     real CoreUI-972 catalogs: APPEARANCEKEYS, BITMAPKEYS,
 *     EXTENDED_METADATA; potentially others libcar/libbom, frozen since
 *     2019, has never heard of) -- is passed through verbatim at the raw
 *     libbom level (see passthroughVariable() below). Their absence is what
 *     made Apple's own assetutil reject the previous ("four variables
 *     only") output with "no header information" / "BOMStreamGetDataPointer
 *     buffer overflow".
 *
 * CLI contract (243-02-PLAN.md <interfaces>):
 *   car-writer roundtrip --source <in.car> --out <out.car>
 *     -> exit 0 on success; writes a freshly re-serialized catalog.
 *   car-writer verify --source <original.car> --out <roundtripped.car>
 *     -> exit 0 iff header/KEYFORMAT/per-rendition-SHA-256/FACETKEYS/full
 *        top-level-variable-name-set are all consistent between the two
 *        files; non-zero + a JSON summary on stdout otherwise.
 *   car-writer patch --source <in.car> --icon-1024 <master.png>
 *                     --splash-logo <logo.png> --splash-bg-hex '#RRGGBB'
 *                     --out <out.car>
 *     -> exit 0 on success; reproduces roundtrip's full BOM re-serialization
 *        but substitutes the AppIcon and SplashScreenLogo renditions with
 *        zlib-encoded content resampled from the supplied PNG masters, and
 *        re-colors SplashScreenBackground's own 260-byte value via a raw
 *        byte-template patch of its trailing four RGBA doubles (offset
 *        228-259) derived from --splash-bg-hex (244-03-PLAN.md, D-07), while
 *        copying every other rendition byte-verbatim (244-02-PLAN.md,
 *        D-01/D-02/D-03).
 *
 *        Rendition selection uses the same identifier-partition technique
 *        Reader::Load() already uses internally (Sources/Reader.cpp): the
 *        three branded facets' identifiers are looked up via
 *        lookupFacet(name)->attributes().get(car_attribute_identifier_
 *        identifier), and identifier_index is derived from the live
 *        KEYFORMAT every time -- never hardcoded. Only a source rendition's
 *        plain header-struct fields (width/height/attributes/layout/flags)
 *        are ever read; `.data()`/Decode() is never called on a source
 *        rendition -- the Linux build has no LZFSE/deepmap2 decode path at
 *        all (compile-time __APPLE__ guard in Rendition.cpp), and this tool
 *        only ever needs the header facts, never the pixel bytes, to
 *        reproduce the shell's own bucket set (D-03).
 */

#include <car/Reader.h>
#include <car/Writer.h>
#include <car/Facet.h>
#include <car/Rendition.h>
#include <car/car_format.h>
#include <car/sha256.h>
#include <car/VariablePassthrough.h>
#include <bom/bom.h>

#include <png.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

using UniqueBom = std::unique_ptr<struct bom_context, decltype(&bom_free)>;

/*
 * Open a catalog read-only. Malformed/truncated input fails cleanly here
 * (bom_alloc_load's own size/magic checks, and car::Reader::Load's own
 * CARHEADER/KEYFORMAT checks) rather than crashing or reading out of bounds
 * (243-RESEARCH.md Security Domain, ASVS V5).
 */
ext::optional<car::Reader> openReader(std::string const &path)
{
    struct bom_context_memory memory = bom_context_memory_file(path.c_str(), /* writeable */ false, 0);
    if (memory.data == NULL || memory.size == 0) {
        return ext::nullopt;
    }

    UniqueBom bom = UniqueBom(bom_alloc_load(memory), bom_free);
    if (bom == nullptr) {
        return ext::nullopt;
    }

    return car::Reader::Load(std::move(bom));
}

/* Per-rendition SHA-256 map: hex(key) -> sha256(value). This shape seeds
 * Phase 244's CatalogVerifier (D-13). */
std::map<std::string, std::string> renditionHashes(car::Reader const &reader)
{
    std::map<std::string, std::string> hashes;
    reader.renditionFastIterate([&hashes](void *key, size_t keyLen, void *value, size_t valueLen) {
        hashes[car::bytesToHex(key, keyLen)] = car::sha256Hex(value, valueLen);
    });
    return hashes;
}

/* Raw FACETKEYS bytes: hex(key) -> hex(value). A byte-diff, not a content
 * hash -- this is the assertion that answers Open Question 1 / A4
 * empirically (the structured Facet round-trip's losslessness is not
 * assumed safe). */
std::map<std::string, std::string> facetKeyBytes(car::Reader const &reader)
{
    std::map<std::string, std::string> facets;
    reader.facetFastIterate([&facets](void *key, size_t keyLen, void *value, size_t valueLen) {
        facets[car::bytesToHex(key, keyLen)] = car::bytesToHex(value, valueLen);
    });
    return facets;
}

/* hex(key)/hex(value) pairs contain only [0-9a-f] -- no JSON escaping
 * needed for this specific map shape. */
std::string mapToJson(std::map<std::string, std::string> const &values)
{
    std::string json = "{";
    bool first = true;
    for (auto const &pair : values) {
        if (!first) {
            json += ", ";
        }
        first = false;
        json += "\"" + pair.first + "\": \"" + pair.second + "\"";
    }
    json += "}";
    return json;
}

std::string joinNames(std::set<std::string> const &names)
{
    std::string joined;
    bool first = true;
    for (auto const &name : names) {
        if (!first) {
            joined += ", ";
        }
        first = false;
        joined += name;
    }
    return joined;
}

void printUsage()
{
    fprintf(stderr, "usage: car-writer roundtrip --source <in.car> --out <out.car>\n");
    fprintf(stderr, "       car-writer verify --source <original.car> --out <roundtripped.car>\n");
    fprintf(stderr, "       car-writer patch --source <in.car> --icon-1024 <master.png> --splash-logo <logo.png> --splash-bg-hex '#RRGGBB' --out <out.car>\n");
}

int cmdRoundtrip(std::string const &sourcePath, std::string const &outPath)
{
    ext::optional<car::Reader> reader = openReader(sourcePath);
    if (reader == ext::nullopt) {
        fprintf(stderr, "car-writer: failed to open or parse source catalog: %s\n", sourcePath.c_str());
        return 1;
    }

    /* Never in-place patch: always start from a fresh, empty output file so
     * every block offset is recomputed (D-11/Pitfall 3), not carried over
     * from a stale prior run at the same path. */
    std::remove(outPath.c_str());

    struct bom_context_memory outMemory = bom_context_memory_file(outPath.c_str(), /* writeable */ true, 0);
    if (outMemory.data == NULL) {
        fprintf(stderr, "car-writer: failed to open output path for writing: %s\n", outPath.c_str());
        return 1;
    }

    UniqueBom outBom = UniqueBom(bom_alloc_empty(outMemory), bom_free);
    if (outBom == nullptr) {
        fprintf(stderr, "car-writer: failed to allocate output BOM\n");
        return 1;
    }

    ext::optional<car::Writer> writer = car::Writer::Create(std::move(outBom));
    if (writer == ext::nullopt) {
        fprintf(stderr, "car-writer: failed to create writer\n");
        return 1;
    }

    /* Carry the source CARHEADER and KEYFORMAT through verbatim (D-04 edits
     * 1+2) -- never libcar's stale hardcoded synthesis constants. */
    writer->header() = reader->header();
    writer->keyfmt() = reader->keyfmt();

    /* Copy every rendition's already-compressed payload byte-verbatim: no
     * decode, no re-encode (D-15). */
    reader->renditionFastIterate([&writer](void *key, size_t keyLen, void *value, size_t valueLen) {
        writer->addRendition(key, keyLen, value, valueLen);
    });

    /* Copy every facet via the structured path (the only one Writer
     * exposes; see the round-trip test for the raw-byte losslessness
     * proof). */
    reader->facetIterate([&writer](car::Facet const &facet) {
        writer->addFacet(facet);
    });

    writer->write();

    /* Carry every OTHER top-level BOM variable through verbatim -- the
     * variables libcar's structured Writer does not itself model (D-04's
     * "everything else stays upstream" framing did not anticipate that
     * upstream itself silently drops unmodeled variables entirely; see the
     * file header comment above and 243-RESEARCH.md). Must run AFTER
     * writer->write() so the four writer-managed variables already exist
     * in the destination bom and are correctly skipped. */
    car::passthroughUnmanagedVariables(reader->bom(), writer->bom());

    /* Relocate the trailer (index+freelist+variables) from immediately
     * after the header -- this library's own bom_alloc_empty() layout --
     * to the end of the file, matching the layout every real Apple BOM
     * file actually uses (variables, then a 16-byte-aligned index+
     * freelist, ending exactly at EOF). This library's own Reader follows
     * whatever offsets the header declares regardless of physical
     * ordering, so the mismatch was invisible to every existing test; a
     * real Apple BOM reader (CoreUI/assetutil) requires the end-of-file
     * convention and otherwise fails with "no header information" /
     * "BOMStreamGetDataPointer buffer overflow" on ANY output this writer
     * produces, independent of which variables are present. Must run last,
     * after every block/variable has been added. */
    bom_relocate_trailer(writer->bom());

    return 0;
}

int cmdVerify(std::string const &sourcePath, std::string const &outPath)
{
    ext::optional<car::Reader> sourceReader = openReader(sourcePath);
    if (sourceReader == ext::nullopt) {
        printf(
            "{\"pass\": false, \"category\": \"open_failure\", \"detail\": \"failed to open or parse source: %s\"}\n",
            sourcePath.c_str()
        );
        return 1;
    }

    ext::optional<car::Reader> outReader = openReader(outPath);
    if (outReader == ext::nullopt) {
        printf(
            "{\"pass\": false, \"category\": \"open_failure\", \"detail\": \"failed to open or parse output: %s\"}\n",
            outPath.c_str()
        );
        return 1;
    }

    struct car_header *sourceHeader = sourceReader->header();
    struct car_header *outHeader = outReader->header();

    /* Absolute correctness (D-11 leg 2): defense-in-depth against a
     * stale/wrong shell -- a fresh re-serialize of the wrong source could
     * otherwise pass the relative checks below trivially. */
    if (sourceHeader->ui_version != 972 || sourceHeader->storage_version != 17 || sourceHeader->schema_version != 2) {
        printf(
            "{\"pass\": false, \"category\": \"absolute_header_mismatch\", "
            "\"detail\": \"source header is not CoreUI 972/StorageVersion 17/SchemaVersion 2 (got %u/%u/%u)\"}\n",
            sourceHeader->ui_version, sourceHeader->storage_version, sourceHeader->schema_version
        );
        return 1;
    }

    /* Relative fidelity leg 0: the FULL top-level BOM variable NAME set is
     * equal (set equality, not the four variables libcar's Writer happens
     * to model). This is the check that closes the prior false-positive:
     * before this fix, a round-trip that silently dropped APPEARANCEKEYS/
     * BITMAPKEYS/EXTENDED_METADATA still passed verify, because verify only
     * ever compared the four variables the SAME writer produced on both
     * sides -- a circular check against libcar's own reader, never against
     * the source's actual variable set. Must run before the per-variable
     * checks below so a missing variable is reported as exactly that, not
     * as an incidental header/keyformat mismatch. */
    std::set<std::string> sourceVariables = car::variableNames(sourceReader->bom());
    std::set<std::string> outVariables = car::variableNames(outReader->bom());
    if (sourceVariables != outVariables) {
        std::vector<std::string> missing;
        for (auto const &name : sourceVariables) {
            if (outVariables.find(name) == outVariables.end()) {
                missing.push_back(name);
            }
        }
        std::vector<std::string> unexpected;
        for (auto const &name : outVariables) {
            if (sourceVariables.find(name) == sourceVariables.end()) {
                unexpected.push_back(name);
            }
        }
        std::set<std::string> missingSet(missing.begin(), missing.end());
        std::set<std::string> unexpectedSet(unexpected.begin(), unexpected.end());
        printf(
            "{\"pass\": false, \"category\": \"variable_set_mismatch\", "
            "\"detail\": \"top-level BOM variable set differs from source\", "
            "\"source_variables\": \"%s\", \"output_variables\": \"%s\", "
            "\"missing\": \"%s\", \"unexpected\": \"%s\"}\n",
            joinNames(sourceVariables).c_str(), joinNames(outVariables).c_str(),
            joinNames(missingSet).c_str(), joinNames(unexpectedSet).c_str()
        );
        return 1;
    }

    /* Relative fidelity leg 1: full CARHEADER struct byte-identical
     * (covers all 12 fields, including uuid/storage_timestamp/
     * associated_checksum/rendition_count -- not just the 3 version
     * fields; Pitfall A). */
    if (memcmp(sourceHeader, outHeader, sizeof(struct car_header)) != 0) {
        printf(
            "{\"pass\": false, \"category\": \"header_mismatch\", "
            "\"detail\": \"CARHEADER differs between source and round-tripped output\"}\n"
        );
        return 1;
    }

    /* Relative fidelity leg 2: KEYFORMAT token list byte-identical. */
    struct car_key_format *sourceKeyfmt = sourceReader->keyfmt();
    struct car_key_format *outKeyfmt = outReader->keyfmt();
    if (sourceKeyfmt->num_identifiers != outKeyfmt->num_identifiers ||
        memcmp(sourceKeyfmt->identifier_list, outKeyfmt->identifier_list, sourceKeyfmt->num_identifiers * sizeof(uint32_t)) != 0) {
        printf(
            "{\"pass\": false, \"category\": \"keyformat_mismatch\", "
            "\"detail\": \"KEYFORMAT token list differs between source and round-tripped output\"}\n"
        );
        return 1;
    }

    /* Relative fidelity leg 3: per-rendition SHA-256 map identical (D-13
     * shape). */
    std::map<std::string, std::string> sourceHashes = renditionHashes(*sourceReader);
    std::map<std::string, std::string> outHashes = renditionHashes(*outReader);
    if (sourceHashes != outHashes) {
        printf(
            "{\"pass\": false, \"category\": \"rendition_hash_mismatch\", "
            "\"detail\": \"per-rendition SHA-256 map differs (source has %zu renditions, output has %zu)\"}\n",
            sourceHashes.size(), outHashes.size()
        );
        return 1;
    }

    /* Relative fidelity leg 4: FACETKEYS raw bytes identical (Open Question
     * 1 / A4, empirically settled here). */
    std::map<std::string, std::string> sourceFacets = facetKeyBytes(*sourceReader);
    std::map<std::string, std::string> outFacets = facetKeyBytes(*outReader);
    if (sourceFacets != outFacets) {
        printf(
            "{\"pass\": false, \"category\": \"facetkeys_mismatch\", "
            "\"detail\": \"FACETKEYS raw bytes differ between source and round-tripped output\"}\n"
        );
        return 1;
    }

    /* FACETKEYS/RENDITIONS consistency: same facet-name set, each resolving
     * to the same rendition-key set, before and after. */
    if (sourceReader->facetCount() != outReader->facetCount() || sourceReader->renditionCount() != outReader->renditionCount()) {
        printf(
            "{\"pass\": false, \"category\": \"count_mismatch\", "
            "\"detail\": \"facet/rendition counts differ (source: %d facets/%d renditions, output: %d facets/%d renditions)\"}\n",
            sourceReader->facetCount(), sourceReader->renditionCount(), outReader->facetCount(), outReader->renditionCount()
        );
        return 1;
    }

    printf(
        "{\"pass\": true, \"header\": {\"ui_version\": %u, \"storage_version\": %u, \"schema_version\": %u}, "
        "\"rendition_count\": %zu, \"facet_count\": %d, \"variables\": \"%s\", \"rendition_hashes\": %s}\n",
        sourceHeader->ui_version, sourceHeader->storage_version, sourceHeader->schema_version,
        sourceHashes.size(), sourceReader->facetCount(), joinNames(sourceVariables).c_str(), mapToJson(sourceHashes).c_str()
    );
    return 0;
}

/*
 * A decoded PNG: straight (non-premultiplied) alpha RGBA8, row-major, one
 * byte per channel. Intentionally the only pixel representation this file
 * needs -- it is fed directly to resizeRgba()/toPremultipliedBGRA8() below.
 */
struct RgbaImage {
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
};

/*
 * Decode a PNG file into a straight-alpha RGBA8 buffer, normalizing every
 * PNG color type (palette/gray/gray+alpha/RGB/RGBA, any bit depth) to 8-bit
 * RGBA via libpng's own transform pipeline. Returns false on any failure
 * (missing file, bad signature, decode error) -- callers must fail closed
 * (fall back to a byte-verbatim copy of the rendition) rather than author a
 * structured rendition from garbage pixel data.
 */
bool decodePng(std::string const &path, RgbaImage *out)
{
    FILE *fp = fopen(path.c_str(), "rb");
    if (fp == NULL) {
        return false;
    }

    png_byte signature[8];
    if (fread(signature, 1, 8, fp) != 8 || png_sig_cmp(signature, 0, 8) != 0) {
        fclose(fp);
        return false;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png == NULL) {
        fclose(fp);
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (info == NULL) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return false;
    }

    png_init_io(png, fp);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    png_uint_32 width = png_get_image_width(png, info);
    png_uint_32 height = png_get_image_height(png, info);
    png_byte colorType = png_get_color_type(png, info);
    png_byte bitDepth = png_get_bit_depth(png, info);

    if (bitDepth == 16) {
        png_set_strip_16(png);
    }
    if (colorType == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png);
    }
    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }
    if (colorType == PNG_COLOR_TYPE_RGB || colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }

    png_read_update_info(png, info);

    out->width = width;
    out->height = height;
    out->pixels.assign(static_cast<size_t>(width) * height * 4, 0);

    std::vector<png_bytep> rows(height);
    for (png_uint_32 y = 0; y < height; y++) {
        rows[y] = out->pixels.data() + static_cast<size_t>(y) * width * 4;
    }
    png_read_image(png, rows.data());

    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    return width > 0 && height > 0;
}

/*
 * Bilinear-resample an RGBA8 buffer to an exact target size. D-03: the
 * writer -- not PHP -- derives every AppIcon/SplashScreenLogo bucket from
 * the single supplied master, matching each existing shell rendition's own
 * width/height exactly (read from the SOURCE rendition's header fields by
 * the caller, never from a hardcoded size list).
 */
RgbaImage resizeRgba(RgbaImage const &source, uint32_t targetWidth, uint32_t targetHeight)
{
    RgbaImage output;
    output.width = targetWidth;
    output.height = targetHeight;
    output.pixels.assign(static_cast<size_t>(targetWidth) * targetHeight * 4, 0);

    if (source.width == 0 || source.height == 0 || targetWidth == 0 || targetHeight == 0) {
        return output;
    }

    if (source.width == targetWidth && source.height == targetHeight) {
        output.pixels = source.pixels;
        return output;
    }

    for (uint32_t y = 0; y < targetHeight; y++) {
        double srcY = (targetHeight > 1) ? (static_cast<double>(y) * (source.height - 1) / (targetHeight - 1)) : 0.0;
        uint32_t y0 = static_cast<uint32_t>(srcY);
        uint32_t y1 = std::min(y0 + 1, source.height - 1);
        double fy = srcY - y0;

        for (uint32_t x = 0; x < targetWidth; x++) {
            double srcX = (targetWidth > 1) ? (static_cast<double>(x) * (source.width - 1) / (targetWidth - 1)) : 0.0;
            uint32_t x0 = static_cast<uint32_t>(srcX);
            uint32_t x1 = std::min(x0 + 1, source.width - 1);
            double fx = srcX - x0;

            for (int channel = 0; channel < 4; channel++) {
                double p00 = source.pixels[(static_cast<size_t>(y0) * source.width + x0) * 4 + channel];
                double p10 = source.pixels[(static_cast<size_t>(y0) * source.width + x1) * 4 + channel];
                double p01 = source.pixels[(static_cast<size_t>(y1) * source.width + x0) * 4 + channel];
                double p11 = source.pixels[(static_cast<size_t>(y1) * source.width + x1) * 4 + channel];
                double top = p00 + (p10 - p00) * fx;
                double bottom = p01 + (p11 - p01) * fx;
                double value = top + (bottom - top) * fy;
                output.pixels[(static_cast<size_t>(y) * targetWidth + x) * 4 + channel] = static_cast<uint8_t>(value + 0.5);
            }
        }
    }

    return output;
}

/*
 * Convert straight-alpha RGBA8 to premultiplied BGRA8 -- the interleaved
 * byte order car::Rendition::Data::Format::PremultipliedBGRA8 expects
 * (RESEARCH.md Pitfall 4).
 */
std::vector<uint8_t> toPremultipliedBGRA8(RgbaImage const &image)
{
    std::vector<uint8_t> output(image.pixels.size());
    for (size_t i = 0; i + 3 < image.pixels.size(); i += 4) {
        uint8_t r = image.pixels[i + 0];
        uint8_t g = image.pixels[i + 1];
        uint8_t b = image.pixels[i + 2];
        uint8_t a = image.pixels[i + 3];

        output[i + 0] = static_cast<uint8_t>((static_cast<uint32_t>(b) * a + 127) / 255);
        output[i + 1] = static_cast<uint8_t>((static_cast<uint32_t>(g) * a + 127) / 255);
        output[i + 2] = static_cast<uint8_t>((static_cast<uint32_t>(r) * a + 127) / 255);
        output[i + 3] = a;
    }
    return output;
}

/* True iff every pixel's alpha channel is fully opaque (255). */
bool isFullyOpaque(RgbaImage const &image)
{
    for (size_t i = 3; i < image.pixels.size(); i += 4) {
        if (image.pixels[i] != 255) {
            return false;
        }
    }
    return true;
}

/*
 * The index into a raw rendition key (an array of car_rendition_key, i.e.
 * uint16_t, one per KEYFORMAT-declared identifier, in KEYFORMAT order)
 * where the FACET identifier lives. Derived from the live KEYFORMAT every
 * time, exactly as Reader::Load() does internally (Sources/Reader.cpp) --
 * never hardcoded, since a future shell rebuild could reorder KEYFORMAT
 * tokens.
 */
size_t identifierIndexFor(struct car_key_format *keyfmt)
{
    for (size_t i = 0; i < keyfmt->num_identifiers; i++) {
        if (keyfmt->identifier_list[i] == car_attribute_identifier_identifier) {
            return i;
        }
    }
    return 0;
}

/*
 * Parse a `#RRGGBB` hex color string into three bytes. Fails closed (returns
 * false) on anything other than exactly 7 characters, a leading '#', and six
 * hex digits -- T-244-03-01. car-writer's --splash-bg-hex reaches this
 * function as plain process argv (never shell-interpolated), but a
 * malformed value must still never reach the buffer-write step below.
 */
bool parseHexColor(std::string const &hex, uint8_t *outR, uint8_t *outG, uint8_t *outB)
{
    if (hex.size() != 7 || hex[0] != '#') {
        return false;
    }
    for (size_t i = 1; i < 7; i++) {
        if (!isxdigit(static_cast<unsigned char>(hex[i]))) {
            return false;
        }
    }

    *outR = static_cast<uint8_t>(std::stoul(hex.substr(1, 2), nullptr, 16));
    *outG = static_cast<uint8_t>(std::stoul(hex.substr(3, 2), nullptr, 16));
    *outB = static_cast<uint8_t>(std::stoul(hex.substr(5, 2), nullptr, 16));
    return true;
}

/*
 * SplashScreenBackground's fixed 260-byte value layout (byte-verified on the
 * shell fixture and re-confirmed against the real build-180/181 branded
 * oracles, 244-03-PLAN.md Task 1 / <interfaces>): bytes 0-227 are the CTSI
 * header + info-list + "COLR" payload header, unchanged across every color
 * variant observed; bytes 228-259 are four IEEE-754 little-endian doubles
 * (R, G, B, A).
 */
constexpr size_t kSplashBgValueLength = 260;
constexpr size_t kSplashBgDoublesOffset = 228;

/*
 * Copy the source SplashScreenBackground value verbatim and overwrite only
 * the trailing four RGBA doubles. Returns false (fail closed, T-244-03-02)
 * if the source value is not exactly kSplashBgValueLength bytes -- callers
 * must fall back to a byte-verbatim copy of the untouched source value in
 * that case, matching the AppIcon/SplashScreenLogo missing-master fallback
 * below, rather than reading or writing past a differently-shaped buffer.
 */
bool patchSplashScreenBackgroundValue(
    void const *sourceValue,
    size_t sourceValueLen,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    std::vector<uint8_t> *outValue)
{
    if (sourceValueLen != kSplashBgValueLength) {
        return false;
    }

    uint8_t const *sourceBytes = static_cast<uint8_t const *>(sourceValue);
    outValue->assign(sourceBytes, sourceBytes + sourceValueLen);

    double components[4] = {
        static_cast<double>(r) / 255.0,
        static_cast<double>(g) / 255.0,
        static_cast<double>(b) / 255.0,
        1.0,
    };
    /* This fork targets little-endian platforms only (x86_64/ARM64); a
     * double's in-memory representation there already matches the
     * little-endian layout car_format.h documents, so a direct memcpy
     * reproduces it without a manual byte-swap. */
    memcpy(outValue->data() + kSplashBgDoublesOffset, components, sizeof(components));
    return true;
}

/*
 * car-writer patch: reproduces cmdRoundtrip()'s full BOM re-serialization
 * but substitutes the AppIcon and SplashScreenLogo renditions with fresh
 * zlib-encoded content resampled from the supplied PNG masters (Phase 244
 * Plan 02, D-01/D-02/D-03), and re-colors SplashScreenBackground's own
 * 260-byte value via a raw byte-template patch of its trailing four RGBA
 * doubles (Phase 244 Plan 03, D-07) -- SplashScreenBackground has no
 * Rendition::Create()/Decode() branch in this fork (pixel_format=0,
 * layout=1009), so it goes through the same raw fast-edit
 * addRendition(void*,size_t,void*,size_t) overload used for byte-verbatim
 * copies, never the structured authoring path. Every other rendition stays
 * byte-verbatim.
 */
int cmdPatch(
    std::string const &sourcePath,
    std::string const &iconPath,
    std::string const &splashLogoPath,
    std::string const &splashBgHex,
    std::string const &outPath)
{
    /* T-244-03-01: validate before any file I/O -- a malformed hex must
     * never reach the buffer-write step below. */
    uint8_t splashR = 0;
    uint8_t splashG = 0;
    uint8_t splashB = 0;
    if (!parseHexColor(splashBgHex, &splashR, &splashG, &splashB)) {
        fprintf(stderr, "car-writer: --splash-bg-hex must be a well-formed #RRGGBB hex color (got: \"%s\")\n", splashBgHex.c_str());
        return 1;
    }

    ext::optional<car::Reader> reader = openReader(sourcePath);
    if (reader == ext::nullopt) {
        fprintf(stderr, "car-writer: failed to open or parse source catalog: %s\n", sourcePath.c_str());
        return 1;
    }

    std::remove(outPath.c_str());

    struct bom_context_memory outMemory = bom_context_memory_file(outPath.c_str(), /* writeable */ true, 0);
    if (outMemory.data == NULL) {
        fprintf(stderr, "car-writer: failed to open output path for writing: %s\n", outPath.c_str());
        return 1;
    }

    UniqueBom outBom = UniqueBom(bom_alloc_empty(outMemory), bom_free);
    if (outBom == nullptr) {
        fprintf(stderr, "car-writer: failed to allocate output BOM\n");
        return 1;
    }

    ext::optional<car::Writer> writer = car::Writer::Create(std::move(outBom));
    if (writer == ext::nullopt) {
        fprintf(stderr, "car-writer: failed to create writer\n");
        return 1;
    }

    /* Carry the source CARHEADER and KEYFORMAT through verbatim (Phase 243,
     * unchanged here). */
    writer->header() = reader->header();
    writer->keyfmt() = reader->keyfmt();

    size_t identifierIndex = identifierIndexFor(reader->keyfmt());

    /* Look up all three branded facets by name (Phase 244 Plan 02 established
     * the identifier-partition technique for AppIcon/SplashScreenLogo; Plan 03
     * adds SplashScreenBackground's own branch below via the raw
     * byte-template patch path, since it has no Rendition::Create()/
     * Decode() support in this fork -- Pattern: "SplashScreenBackground is
     * authored via raw-template patch", 244-RESEARCH.md). If a branded
     * facet is missing on a future shell, its id stays ext::nullopt and the
     * corresponding branch below never matches, falling through to the
     * byte-verbatim default -- visible during development, never a crash. */
    ext::optional<car::Facet> appIconFacet = reader->lookupFacet("AppIcon");
    ext::optional<car::Facet> splashLogoFacet = reader->lookupFacet("SplashScreenLogo");
    ext::optional<car::Facet> splashBgFacet = reader->lookupFacet("SplashScreenBackground");

    ext::optional<uint16_t> appIconId = appIconFacet ? appIconFacet->attributes().get(car_attribute_identifier_identifier) : ext::nullopt;
    ext::optional<uint16_t> splashLogoId = splashLogoFacet ? splashLogoFacet->attributes().get(car_attribute_identifier_identifier) : ext::nullopt;
    ext::optional<uint16_t> splashBgId = splashBgFacet ? splashBgFacet->attributes().get(car_attribute_identifier_identifier) : ext::nullopt;

    /* Decode each branded master once, up front. Never touch a SOURCE
     * rendition's pixel data below (Pitfall A: no LZFSE/deepmap2 decode path
     * exists on Linux) -- a decode failure here fails closed to a
     * byte-verbatim copy of the affected renditions, never a corrupt
     * structured write. */
    RgbaImage iconMaster;
    bool haveIconMaster = decodePng(iconPath, &iconMaster);
    RgbaImage splashLogoMaster;
    bool haveSplashLogoMaster = decodePng(splashLogoPath, &splashLogoMaster);

    /* Writer::addRendition(void*,size_t,void*,size_t) (the raw fast-edit
     * overload) stores the given pointers verbatim and only dereferences
     * them later, inside write() below -- it does not copy the bytes
     * eagerly (Writer.cpp). Byte-verbatim copies pass pointers into the
     * SOURCE reader's own mmap, which stays valid for this whole function's
     * lifetime, so that is safe. A patched SplashScreenBackground value is a
     * freshly allocated buffer, so it MUST be kept alive in a scope that
     * outlives the renditionFastIterate lambda (until writer->write() runs
     * below) -- never a buffer local to a single lambda invocation, which
     * would already be destroyed by then (dangling pointer). */
    std::vector<std::vector<uint8_t>> splashBgOwnedValues;

    reader->renditionFastIterate([&](void *key, size_t keyLen, void *value, size_t valueLen) {
        car_rendition_key *renditionKey = (car_rendition_key *)key;
        uint16_t identifier = renditionKey[identifierIndex];

        bool isAppIcon = appIconId && identifier == *appIconId;
        bool isSplashLogo = splashLogoId && identifier == *splashLogoId;
        bool isSplashBg = splashBgId && identifier == *splashBgId;

        if (isSplashBg) {
            splashBgOwnedValues.emplace_back();
            std::vector<uint8_t> &patchedValue = splashBgOwnedValues.back();
            if (patchSplashScreenBackgroundValue(value, valueLen, splashR, splashG, splashB, &patchedValue)) {
                writer->addRendition(key, keyLen, patchedValue.data(), patchedValue.size());
                return;
            }
            /* Fail closed (T-244-03-02): unexpected value size means this
             * is not the fixed-260-byte template this research
             * reverse-engineered -- copy byte-verbatim rather than guess at
             * a different layout, matching the missing-master fallback
             * below. */
            writer->addRendition(key, keyLen, value, valueLen);
            return;
        }

        if (!isAppIcon && !isSplashLogo) {
            /* Byte-verbatim: every other rendition. */
            writer->addRendition(key, keyLen, value, valueLen);
            return;
        }

        /* Structured authoring path. Only header-struct fields are read
         * from the SOURCE rendition (width/height/scale/flags/layout/
         * metadata.name) -- never .data()/Decode() (Pitfall A). */
        struct car_rendition_value *sourceValue = (struct car_rendition_value *)value;
        uint32_t targetWidth = sourceValue->width;
        uint32_t targetHeight = sourceValue->height;

        RgbaImage const &master = isAppIcon ? iconMaster : splashLogoMaster;
        bool haveMaster = isAppIcon ? haveIconMaster : haveSplashLogoMaster;

        if (!haveMaster || targetWidth == 0 || targetHeight == 0) {
            /* Fail closed: a "MultiSized Image" container entry (metadata
             * only, no pixel payload -- width==height==0) or a missing/
             * undecodable master both fall back to the byte-verbatim copy
             * rather than author a corrupt structured rendition. */
            writer->addRendition(key, keyLen, value, valueLen);
            return;
        }

        car::AttributeList attributes = car::AttributeList::Load(
            reader->keyfmt()->num_identifiers, reader->keyfmt()->identifier_list, renditionKey);

        RgbaImage resized = resizeRgba(master, targetWidth, targetHeight);
        std::vector<uint8_t> bgra = toPremultipliedBGRA8(resized);

        car::Rendition::Data data(bgra, car::Rendition::Data::Format::PremultipliedBGRA8);
        car::Rendition rendition = car::Rendition::Create(attributes, ext::optional<car::Rendition::Data>(data));

        /* Preserve every other structural fact of the source rendition
         * verbatim -- only its pixel content changes (D-03's "bucket set
         * matches the shell exactly" framing). Rendition::Create() leaves
         * these plain-enum/bool fields uninitialized; they must be set
         * explicitly before write(). */
        rendition.width() = static_cast<int>(targetWidth);
        rendition.height() = static_cast<int>(targetHeight);
        rendition.scale() = static_cast<double>(sourceValue->scale_factor) / 100.0;
        rendition.isVector() = static_cast<bool>(sourceValue->flags.is_vector);
        rendition.isOpaque() = isFullyOpaque(resized);
        rendition.layout() = static_cast<enum car_rendition_value_layout>(sourceValue->metadata.layout);
        rendition.fileName() = std::string(sourceValue->metadata.name, strnlen(sourceValue->metadata.name, sizeof(sourceValue->metadata.name)));

        /* Encode()/write() emits zlib only (car_rendition_data_compression_
         * magic_zlib) -- Rendition.cpp's Encode() has no other encode
         * branch, so LZFSE/deepmap2 are structurally unreachable here
         * (D-02/WRITER-03). */
        writer->addRendition(rendition);
    });

    /* Facets are unchanged -- only the branded facets' RENDITIONS values
     * moved, never their own attributes/identifier. */
    reader->facetIterate([&writer](car::Facet const &facet) {
        writer->addFacet(facet);
    });

    writer->write();
    car::passthroughUnmanagedVariables(reader->bom(), writer->bom());
    bom_relocate_trailer(writer->bom());

    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string verb = argv[1];
    std::string sourcePath;
    std::string outPath;
    std::string iconPath;
    std::string splashLogoPath;
    std::string splashBgHex;

    for (int i = 2; i + 1 < argc; i += 2) {
        std::string flag = argv[i];
        std::string value = argv[i + 1];
        if (flag == "--source") {
            sourcePath = value;
        } else if (flag == "--out") {
            outPath = value;
        } else if (flag == "--icon-1024") {
            iconPath = value;
        } else if (flag == "--splash-logo") {
            splashLogoPath = value;
        } else if (flag == "--splash-bg-hex") {
            splashBgHex = value;
        }
    }

    if (sourcePath.empty() || outPath.empty()) {
        printUsage();
        return 1;
    }

    if (verb == "roundtrip") {
        return cmdRoundtrip(sourcePath, outPath);
    } else if (verb == "verify") {
        return cmdVerify(sourcePath, outPath);
    } else if (verb == "patch") {
        return cmdPatch(sourcePath, iconPath, splashLogoPath, splashBgHex, outPath);
    }

    printUsage();
    return 1;
}

/**
 Copyright (c) 2015-present, Facebook, Inc.
 All rights reserved.

 This source code is licensed under the BSD-style license found in the
 LICENSE file in the root directory of this source tree.
 */

/*
 * GoogleTest coverage for car-writer's `patch` verb (Phase 244 Plan 02).
 *
 * cmdPatch() lives in Tools/car_roundtrip.cpp, a separate executable target
 * (car_roundtrip) that this test binary does not link -- test_Writer.cpp
 * establishes the convention this file follows: the patch logic is
 * duplicated inline (runPatch(), below), operating on the real shell
 * fixture exactly like car_roundtrip.cpp's cmdPatch(). The PNG-decode step
 * (D-03's "libpng" input-preparation) is Tools/car_roundtrip.cpp-only and
 * orthogonal to what this test exercises -- runPatch() takes an in-memory
 * RgbaImage master directly, sidestepping file-based PNG I/O entirely.
 */

#include <car/Reader.h>
#include <car/Writer.h>
#include <car/Facet.h>
#include <car/Rendition.h>
#include <car/car_format.h>
#include <car/sha256.h>
#include <car/VariablePassthrough.h>
#include <bom/bom.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using car::AttributeList;
using car::Facet;
using car::Reader;
using car::Rendition;
using car::Writer;

namespace {

typedef std::unique_ptr<struct bom_context, decltype(&bom_free)> UniqueBom;

/*
 * SplashScreenBackground raw value layout (Phase 244 Plan 03 Task 1):
 * re-verified against the real build-180/181 branded oracles --
 * `assetutil -I` "Color components" plus a direct byte inspection of the
 * value at the offsets below -- and confirmed to match the shell-fixture-
 * derived layout in 244-RESEARCH.md exactly (no actool point-release drift,
 * Open Question 2 / Assumption A2, RESOLVED):
 *   - value length is 260 bytes on shell-stock.car and both oracles.
 *   - the "COLR" payload magic (stored reversed as "RLOC", matching the
 *     "CTSI"->"ISTC" convention) sits at value-relative offset 212.
 *   - the four RGBA channels are IEEE-754 little-endian doubles at
 *     value-relative offset 228-259; bytes 0-227 are byte-identical across
 *     shell-stock/build-180-navy/build-181-orange.
 * The navy/orange oracles are a recolored shell-stock.car (only these
 * trailing 32 bytes differ) -- see tests/fixtures/car-oracles/README.md.
 * Expected doubles below are derived as #RRGGBB -> [R/255.0, G/255.0,
 * B/255.0, 1.0] and cross-checked against assetutil's own "Color
 * components" output for each oracle.
 */
constexpr size_t kSplashBgValueLength = 260;
constexpr size_t kSplashBgDoublesOffset = 228;

std::string const kSplashBgNavyHex = "#001F3F";
double const kSplashBgNavyRgba[4] = {0.0 / 255.0, 31.0 / 255.0, 63.0 / 255.0, 1.0};

std::string const kSplashBgOrangeHex = "#FF851B";
double const kSplashBgOrangeRgba[4] = {255.0 / 255.0, 133.0 / 255.0, 27.0 / 255.0, 1.0};

/* A synthetic master image: straight (non-premultiplied) alpha RGBA8. */
struct RgbaImage {
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
};

RgbaImage makeSolidImage(uint32_t width, uint32_t height, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    RgbaImage image;
    image.width = width;
    image.height = height;
    image.pixels.assign(static_cast<size_t>(width) * height * 4, 0);
    for (size_t i = 0; i + 3 < image.pixels.size(); i += 4) {
        image.pixels[i + 0] = r;
        image.pixels[i + 1] = g;
        image.pixels[i + 2] = b;
        image.pixels[i + 3] = a;
    }
    return image;
}

/* [Duplicated from car_roundtrip.cpp's cmdPatch() helpers -- see this
 * file's header comment.] */
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

bool isFullyOpaque(RgbaImage const &image)
{
    for (size_t i = 3; i < image.pixels.size(); i += 4) {
        if (image.pixels[i] != 255) {
            return false;
        }
    }
    return true;
}

size_t identifierIndexFor(struct car_key_format *keyfmt)
{
    for (size_t i = 0; i < keyfmt->num_identifiers; i++) {
        if (keyfmt->identifier_list[i] == car_attribute_identifier_identifier) {
            return i;
        }
    }
    return 0;
}

/* [Duplicated from car_roundtrip.cpp's cmdPatch() helpers -- see this
 * file's header comment.] Fails closed (returns false) on anything other
 * than exactly 7 characters, a leading '#', and six hex digits. */
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

/* [Duplicated from car_roundtrip.cpp's cmdPatch() helpers -- see this
 * file's header comment.] Copies the source SplashScreenBackground value
 * verbatim and overwrites only the trailing four RGBA doubles at
 * kSplashBgDoublesOffset. Returns false (fail closed, T-244-03-02) if the
 * source value is not exactly kSplashBgValueLength bytes. */
bool patchSplashScreenBackgroundValue(void const *sourceValue, size_t sourceValueLen, uint8_t r, uint8_t g, uint8_t b, std::vector<uint8_t> *outValue)
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
    /* Little-endian target platforms only (x86_64/ARM64): a double's own
     * in-memory representation there already matches the little-endian
     * layout car_format.h documents, so a direct memcpy reproduces it. */
    memcpy(outValue->data() + kSplashBgDoublesOffset, components, sizeof(components));
    return true;
}

/*
 * Mirrors car_roundtrip.cpp's cmdPatch() exactly (identifier-partition,
 * byte-verbatim default arm, structured zlib authoring for AppIcon/
 * SplashScreenLogo, raw byte-template patch for SplashScreenBackground).
 * Collects the hex-key of every AppIcon/SplashScreenLogo rendition (the ones
 * that went through the structured zlib-authoring branch) into
 * *zlibBrandedKeys, and every branded key including SplashScreenBackground
 * into *allBrandedKeys -- kept as two separate sets because
 * SplashScreenBackground's raw byte-template patch does not produce an MLEC
 * zlib header the way the other two do (BrandedRenditionsAreZlibCompressed...
 * below only checks zlibBrandedKeys; the byte-identity test below checks
 * allBrandedKeys).
 */
void runPatch(
    Reader const &reader,
    RgbaImage const &iconMaster,
    RgbaImage const &splashLogoMaster,
    std::string const &splashBgHex,
    std::string const &outputPath,
    std::set<std::string> *zlibBrandedKeys,
    std::set<std::string> *allBrandedKeys,
    std::set<std::string> *appIconKeys,
    std::set<std::string> *splashLogoKeys)
{
    uint8_t splashR = 0;
    uint8_t splashG = 0;
    uint8_t splashB = 0;
    ASSERT_TRUE(parseHexColor(splashBgHex, &splashR, &splashG, &splashB)) << "malformed --splash-bg-hex: " << splashBgHex;

    std::remove(outputPath.c_str());
    struct bom_context_memory outMemory = bom_context_memory_file(outputPath.c_str(), /* writeable */ true, 0);
    ASSERT_NE(outMemory.data, nullptr);

    UniqueBom outBom = UniqueBom(bom_alloc_empty(outMemory), bom_free);
    ASSERT_NE(outBom, nullptr);

    ext::optional<Writer> writer = Writer::Create(std::move(outBom));
    ASSERT_NE(writer, ext::nullopt);

    writer->header() = reader.header();
    writer->keyfmt() = reader.keyfmt();

    size_t identifierIndex = identifierIndexFor(reader.keyfmt());

    ext::optional<Facet> appIconFacet = reader.lookupFacet("AppIcon");
    ext::optional<Facet> splashLogoFacet = reader.lookupFacet("SplashScreenLogo");
    ext::optional<Facet> splashBgFacet = reader.lookupFacet("SplashScreenBackground");
    ASSERT_NE(appIconFacet, ext::nullopt) << "shell fixture has no AppIcon facet";
    ASSERT_NE(splashLogoFacet, ext::nullopt) << "shell fixture has no SplashScreenLogo facet";
    ASSERT_NE(splashBgFacet, ext::nullopt) << "shell fixture has no SplashScreenBackground facet";

    ext::optional<uint16_t> appIconId = appIconFacet->attributes().get(car_attribute_identifier_identifier);
    ext::optional<uint16_t> splashLogoId = splashLogoFacet->attributes().get(car_attribute_identifier_identifier);
    ext::optional<uint16_t> splashBgId = splashBgFacet->attributes().get(car_attribute_identifier_identifier);
    ASSERT_NE(appIconId, ext::nullopt);
    ASSERT_NE(splashLogoId, ext::nullopt);
    ASSERT_NE(splashBgId, ext::nullopt);

    /*
     * Writer::addRendition(void*,size_t,void*,size_t) (the raw fast-edit
     * overload) stores the given pointers verbatim in _rawRenditions and
     * only dereferences them later, inside write() -- it does not copy the
     * bytes eagerly (Writer.cpp). Byte-verbatim copies pass pointers into
     * the SOURCE reader's own mmap, which stays valid for this whole
     * function's lifetime, so that is safe. A patched SplashScreenBackground
     * value, however, is a freshly allocated buffer -- it MUST be kept alive
     * in a scope that outlives the renditionFastIterate lambda (until
     * writer->write() runs below), never a vector local to a single lambda
     * invocation, which would already be destroyed by then (dangling
     * pointer / heap-use-after-free, exactly what this comment prevents).
     */
    std::vector<std::vector<uint8_t>> splashBgOwnedValues;

    reader.renditionFastIterate([&](void *key, size_t keyLen, void *value, size_t valueLen) {
        car_rendition_key *renditionKey = (car_rendition_key *)key;
        uint16_t identifier = renditionKey[identifierIndex];

        bool isAppIcon = (identifier == *appIconId);
        bool isSplashLogo = (identifier == *splashLogoId);
        bool isSplashBg = (identifier == *splashBgId);

        if (isSplashBg) {
            splashBgOwnedValues.emplace_back();
            std::vector<uint8_t> &patchedValue = splashBgOwnedValues.back();
            if (patchSplashScreenBackgroundValue(value, valueLen, splashR, splashG, splashB, &patchedValue)) {
                writer->addRendition(key, keyLen, patchedValue.data(), patchedValue.size());
                allBrandedKeys->insert(car::bytesToHex(key, keyLen));
                return;
            }
            /* Fail closed (T-244-03-02): unexpected value size -- copy
             * byte-verbatim rather than guess at a different layout. */
            writer->addRendition(key, keyLen, value, valueLen);
            return;
        }

        if (!isAppIcon && !isSplashLogo) {
            writer->addRendition(key, keyLen, value, valueLen);
            return;
        }

        struct car_rendition_value *sourceValue = (struct car_rendition_value *)value;
        uint32_t targetWidth = sourceValue->width;
        uint32_t targetHeight = sourceValue->height;

        if (targetWidth == 0 || targetHeight == 0) {
            /* "MultiSized Image" container entry -- no pixel payload. */
            writer->addRendition(key, keyLen, value, valueLen);
            return;
        }

        RgbaImage const &master = isAppIcon ? iconMaster : splashLogoMaster;
        AttributeList attributes = AttributeList::Load(reader.keyfmt()->num_identifiers, reader.keyfmt()->identifier_list, renditionKey);

        RgbaImage resized = resizeRgba(master, targetWidth, targetHeight);
        std::vector<uint8_t> bgra = toPremultipliedBGRA8(resized);

        Rendition::Data data(bgra, Rendition::Data::Format::PremultipliedBGRA8);
        Rendition rendition = Rendition::Create(attributes, ext::optional<Rendition::Data>(data));
        rendition.width() = static_cast<int>(targetWidth);
        rendition.height() = static_cast<int>(targetHeight);
        rendition.scale() = static_cast<double>(sourceValue->scale_factor) / 100.0;
        rendition.isVector() = static_cast<bool>(sourceValue->flags.is_vector);
        rendition.isOpaque() = isFullyOpaque(resized);
        if (isAppIcon) {
            /* Mirrors car_roundtrip.cpp's cmdPatch() AppIcon-only override
             * (Phase 246 Plan 01, D-01/D-02/D-03) -- see this file's header
             * comment on why this duplication exists. */
            rendition.bitmapDataFlags() = 0x3;
            rendition.isOpaque() = false;
        }
        rendition.layout() = static_cast<enum car_rendition_value_layout>(sourceValue->metadata.layout);
        rendition.fileName() = std::string(sourceValue->metadata.name, strnlen(sourceValue->metadata.name, sizeof(sourceValue->metadata.name)));

        writer->addRendition(rendition);
        std::string hexKey = car::bytesToHex(key, keyLen);
        zlibBrandedKeys->insert(hexKey);
        allBrandedKeys->insert(hexKey);
        if (isAppIcon) {
            appIconKeys->insert(hexKey);
        } else {
            splashLogoKeys->insert(hexKey);
        }
    });

    reader.facetIterate([&writer](Facet const &facet) {
        writer->addFacet(facet);
    });

    writer->write();
    car::passthroughUnmanagedVariables(reader.bom(), writer->bom());
    bom_relocate_trailer(writer->bom());
}

struct PatchResult {
    ext::optional<Reader> sourceReader;
    ext::optional<Reader> outReader;
    std::set<std::string> zlibBrandedKeys; /* AppIcon + SplashScreenLogo only. */
    std::set<std::string> allBrandedKeys;  /* zlibBrandedKeys + SplashScreenBackground. */
    std::set<std::string> appIconKeys;     /* Structured-authoring AppIcon buckets only. */
    std::set<std::string> splashLogoKeys;  /* Structured-authoring SplashScreenLogo buckets only. */
};

/*
 * Runs the patch once against the real shell fixture (checked into
 * Tests/fixtures/shell/, not synthetic) and reopens both files fresh for
 * diffing -- never reuse a prior reader's mmap (test_Writer.cpp's one hard
 * rule). Returned by value; Reader/ext::optional<Reader> are move-only,
 * matching this shim's usage elsewhere in this file.
 */
PatchResult patchRealShellFixture(std::string const &outputPath, std::string const &splashBgHex)
{
    PatchResult result;

    std::string fixturePath = std::string(CAR_ROUNDTRIP_REPO_ROOT) + "/Tests/fixtures/shell/Assets.car";
    std::remove(outputPath.c_str());

    struct bom_context_memory sourceMemory = bom_context_memory_file(fixturePath.c_str(), /* writeable */ false, 0);
    if (sourceMemory.data == nullptr || sourceMemory.size == 0) {
        return result;
    }
    UniqueBom sourceBom = UniqueBom(bom_alloc_load(sourceMemory), bom_free);
    if (sourceBom == nullptr) {
        return result;
    }
    ext::optional<Reader> sourceReader = Reader::Load(std::move(sourceBom));
    if (sourceReader == ext::nullopt) {
        return result;
    }

    RgbaImage iconMaster = makeSolidImage(1024, 1024, 10, 20, 30, 255);
    RgbaImage splashLogoMaster = makeSolidImage(64, 64, 200, 100, 50, 128);
    runPatch(*sourceReader, iconMaster, splashLogoMaster, splashBgHex, outputPath, &result.zlibBrandedKeys, &result.allBrandedKeys, &result.appIconKeys, &result.splashLogoKeys);

    struct bom_context_memory sourceMemory2 = bom_context_memory_file(fixturePath.c_str(), false, 0);
    if (sourceMemory2.data == nullptr) {
        return result;
    }
    UniqueBom sourceBom2 = UniqueBom(bom_alloc_load(sourceMemory2), bom_free);
    if (sourceBom2 == nullptr) {
        return result;
    }
    result.sourceReader = Reader::Load(std::move(sourceBom2));

    struct bom_context_memory outMemory = bom_context_memory_file(outputPath.c_str(), false, 0);
    if (outMemory.data == nullptr) {
        return result;
    }
    UniqueBom outBom = UniqueBom(bom_alloc_load(outMemory), bom_free);
    if (outBom == nullptr) {
        return result;
    }
    result.outReader = Reader::Load(std::move(outBom));

    return result;
}

/*
 * Locate the raw rendition value for a facet by name, using the same
 * identifier-partition technique as runPatch() above. Returns false if the
 * facet or a matching rendition is not found -- used by the
 * SplashScreenBackground test below to independently re-decode the patched
 * value straight from a re-opened Reader, rather than trusting runPatch()'s
 * own bookkeeping.
 */
bool findRenditionValueByFacetName(Reader const &reader, std::string const &facetName, std::vector<uint8_t> *outValue)
{
    ext::optional<Facet> facet = reader.lookupFacet(facetName);
    if (facet == ext::nullopt) {
        return false;
    }
    ext::optional<uint16_t> facetId = facet->attributes().get(car_attribute_identifier_identifier);
    if (facetId == ext::nullopt) {
        return false;
    }
    size_t identifierIndex = identifierIndexFor(reader.keyfmt());

    bool found = false;
    std::vector<uint8_t> value;
    reader.renditionFastIterate([&](void *key, size_t keyLen, void *rawValue, size_t valueLen) {
        (void)keyLen;
        if (found) {
            return;
        }
        car_rendition_key *renditionKey = (car_rendition_key *)key;
        if (renditionKey[identifierIndex] == *facetId) {
            uint8_t const *bytes = static_cast<uint8_t const *>(rawValue);
            value.assign(bytes, bytes + valueLen);
            found = true;
        }
    });

    if (found) {
        *outValue = value;
    }
    return found;
}

} // namespace

TEST(Patch, UntouchedRenditionsRemainByteIdenticalExceptBrandedKeys)
{
    PatchResult result = patchRealShellFixture("real_shell_patch_output_bytes.car", kSplashBgNavyHex);
    ASSERT_NE(result.sourceReader, ext::nullopt) << "shell fixture not found or unreadable, or patch setup failed";
    ASSERT_NE(result.outReader, ext::nullopt);
    /* Branded keys span 3 facets: AppIcon, SplashScreenLogo (structured
     * zlib authoring, one key per scale/idiom bucket), SplashScreenBackground
     * (raw byte-template patch, always exactly one "universal" key). Widened
     * from the 2-facet exclusion 244-02 established, now that
     * SplashScreenBackground is also branded (Phase 244 Plan 03) -- the
     * exact key COUNT depends on how many buckets the fixture's AppIcon/
     * SplashScreenLogo facets carry, so this asserts the superset
     * relationship rather than a specific magic number. */
    ASSERT_GT(result.allBrandedKeys.size(), result.zlibBrandedKeys.size()) << "SplashScreenBackground's own key must have been added on top of the zlib-branded keys";
    ASSERT_GT(result.zlibBrandedKeys.size(), static_cast<size_t>(0)) << "no rendition was branded -- fixture shape assumption wrong";

    std::map<std::string, std::string> sourceHashes;
    result.sourceReader->renditionFastIterate([&sourceHashes](void *key, size_t keyLen, void *value, size_t valueLen) {
        sourceHashes[car::bytesToHex(key, keyLen)] = car::sha256Hex(value, valueLen);
    });
    std::map<std::string, std::string> outHashes;
    result.outReader->renditionFastIterate([&outHashes](void *key, size_t keyLen, void *value, size_t valueLen) {
        outHashes[car::bytesToHex(key, keyLen)] = car::sha256Hex(value, valueLen);
    });

    ASSERT_EQ(sourceHashes.size(), outHashes.size());
    for (auto const &pair : sourceHashes) {
        auto outIt = outHashes.find(pair.first);
        ASSERT_NE(outIt, outHashes.end()) << "rendition key missing from output: " << pair.first;
        if (result.allBrandedKeys.count(pair.first) > 0) {
            EXPECT_NE(pair.second, outIt->second) << "branded rendition " << pair.first << " unexpectedly unchanged";
        } else {
            EXPECT_EQ(pair.second, outIt->second) << "untouched rendition " << pair.first << " unexpectedly changed";
        }
    }

    std::remove("real_shell_patch_output_bytes.car");
}

TEST(Patch, BrandedRenditionsAreZlibCompressedNotLzfseOrDeepmap2)
{
    PatchResult result = patchRealShellFixture("real_shell_patch_output_zlib.car", kSplashBgNavyHex);
    ASSERT_NE(result.outReader, ext::nullopt);
    /* AppIcon + SplashScreenLogo buckets only -- SplashScreenBackground's
     * raw byte-template patch has no MLEC/zlib header at all, so it must
     * not be included here (it is covered separately by
     * SplashScreenBackgroundIsRecoloredFromHexTemplate below). */
    ASSERT_GT(result.zlibBrandedKeys.size(), static_cast<size_t>(0));

    size_t checked = 0;
    result.outReader->renditionFastIterate([&](void *key, size_t keyLen, void *value, size_t valueLen) {
        (void)valueLen;
        std::string hexKey = car::bytesToHex(key, keyLen);
        if (result.zlibBrandedKeys.find(hexKey) == result.zlibBrandedKeys.end()) {
            return;
        }
        checked++;

        struct car_rendition_value *renditionValue = (struct car_rendition_value *)value;
        struct car_rendition_data_header1 *header1 = (struct car_rendition_data_header1 *)(
            (uintptr_t)renditionValue + sizeof(struct car_rendition_value) + renditionValue->info_len);

        EXPECT_EQ(std::string(header1->magic, 4), "MLEC") << "branded rendition " << hexKey << " has a malformed data header";
        EXPECT_EQ(header1->compression, static_cast<uint32_t>(car_rendition_data_compression_magic_zlib))
            << "branded rendition " << hexKey << " is not zlib-compressed";
        EXPECT_NE(header1->compression, static_cast<uint32_t>(car_rendition_data_compression_magic_lzvn))
            << "branded rendition " << hexKey << " unexpectedly uses LZVN";
        EXPECT_NE(header1->compression, static_cast<uint32_t>(car_rendition_data_compression_magic_jpeg_lzfse))
            << "branded rendition " << hexKey << " unexpectedly uses LZFSE";
    });

    EXPECT_EQ(checked, result.zlibBrandedKeys.size()) << "not every branded key was found during the output iteration";

    std::remove("real_shell_patch_output_zlib.car");
}

/*
 * ITMS-90717 fix anchor (Phase 246 Plan 01): Apple's actool marks the App
 * Store icon rendition's CELM data-header (car_rendition_data_header1) low
 * flags bits 0x3 (opaque compressed-data container), matching an on-disk
 * is_opaque bit of 0 -- this is the container-level discriminator that
 * clears ITMS-90717, independent of the ARGB pixel format underneath. The
 * override is scoped to AppIcon ONLY (D-03, safety-tested): SplashScreenLogo
 * must keep flags 0x0, or its legitimate transparency is destroyed.
 */
TEST(Patch, AppIconRenditionCarriesOpaqueCelmFlagsSplashLogoDoesNot)
{
    PatchResult result = patchRealShellFixture("real_shell_patch_output_celm_flags.car", kSplashBgNavyHex);
    ASSERT_NE(result.outReader, ext::nullopt);
    ASSERT_GT(result.appIconKeys.size(), static_cast<size_t>(0)) << "no AppIcon rendition went through structured authoring";
    ASSERT_GT(result.splashLogoKeys.size(), static_cast<size_t>(0)) << "no SplashScreenLogo rendition went through structured authoring";

    size_t checkedAppIcon = 0;
    size_t checkedSplashLogo = 0;
    result.outReader->renditionFastIterate([&](void *key, size_t keyLen, void *value, size_t valueLen) {
        (void)valueLen;
        std::string hexKey = car::bytesToHex(key, keyLen);
        bool isAppIconKey = result.appIconKeys.find(hexKey) != result.appIconKeys.end();
        bool isSplashLogoKey = result.splashLogoKeys.find(hexKey) != result.splashLogoKeys.end();
        if (!isAppIconKey && !isSplashLogoKey) {
            return;
        }

        struct car_rendition_value *renditionValue = (struct car_rendition_value *)value;
        struct car_rendition_data_header1 *header1 = (struct car_rendition_data_header1 *)(
            (uintptr_t)renditionValue + sizeof(struct car_rendition_value) + renditionValue->info_len);

        EXPECT_EQ(std::string(header1->magic, 4), "MLEC") << "rendition " << hexKey << " has a malformed data header";
        EXPECT_EQ(header1->compression, static_cast<uint32_t>(car_rendition_data_compression_magic_zlib))
            << "rendition " << hexKey << " is not zlib-compressed";

        uint32_t flags = header1->flags.unknown1 | (header1->flags.unknown2 << 1);
        if (isAppIconKey) {
            checkedAppIcon++;
            EXPECT_EQ(flags, static_cast<uint32_t>(0x3)) << "AppIcon rendition " << hexKey << " must carry the opaque CELM container flags 0x3";
        } else {
            checkedSplashLogo++;
            EXPECT_EQ(flags, static_cast<uint32_t>(0x0)) << "SplashScreenLogo rendition " << hexKey << " must keep flags 0x0 -- the AppIcon-only fix must not leak onto it";
        }
    });

    EXPECT_EQ(checkedAppIcon, result.appIconKeys.size()) << "not every AppIcon key was found during the output iteration";
    EXPECT_EQ(checkedSplashLogo, result.splashLogoKeys.size()) << "not every SplashScreenLogo key was found during the output iteration";

    std::remove("real_shell_patch_output_celm_flags.car");
}

TEST(Patch, OutputHeaderAndKeyformatMatchSourceExactly)
{
    PatchResult result = patchRealShellFixture("real_shell_patch_output_header.car", kSplashBgNavyHex);
    ASSERT_NE(result.sourceReader, ext::nullopt);
    ASSERT_NE(result.outReader, ext::nullopt);

    /* Absolute correctness: CoreUI 972 / StorageVersion 17 / SchemaVersion 2
     * (cross-verified via assetutil, Phase 243). */
    struct car_header *sourceHeader = result.sourceReader->header();
    EXPECT_EQ(sourceHeader->ui_version, static_cast<uint32_t>(972));
    EXPECT_EQ(sourceHeader->storage_version, static_cast<uint32_t>(17));
    EXPECT_EQ(sourceHeader->schema_version, static_cast<uint32_t>(2));

    struct car_header *outHeader = result.outReader->header();
    EXPECT_EQ(memcmp(sourceHeader, outHeader, sizeof(struct car_header)), 0);

    struct car_key_format *sourceKeyfmt = result.sourceReader->keyfmt();
    struct car_key_format *outKeyfmt = result.outReader->keyfmt();
    ASSERT_EQ(sourceKeyfmt->num_identifiers, outKeyfmt->num_identifiers);
    EXPECT_EQ(memcmp(sourceKeyfmt->identifier_list, outKeyfmt->identifier_list, sourceKeyfmt->num_identifiers * sizeof(uint32_t)), 0);

    std::remove("real_shell_patch_output_header.car");
}

TEST(Patch, FacetAndRenditionCountsArePreserved)
{
    PatchResult result = patchRealShellFixture("real_shell_patch_output_counts.car", kSplashBgNavyHex);
    ASSERT_NE(result.sourceReader, ext::nullopt);
    ASSERT_NE(result.outReader, ext::nullopt);

    /* FACETKEYS/RENDITIONS consistency: patch-in-place substitutes content,
     * never adds/removes a facet or rendition entry (D-01). */
    EXPECT_EQ(result.sourceReader->facetCount(), result.outReader->facetCount());
    EXPECT_EQ(result.sourceReader->renditionCount(), result.outReader->renditionCount());

    std::remove("real_shell_patch_output_counts.car");
}

/*
 * SplashScreenBackground has no Rendition::Create()/Decode() branch in this
 * fork (pixel_format=0, layout=1009) -- it is re-colored via the raw
 * byte-template patch (244-03-PLAN.md Task 2), never the structured
 * authoring path used for AppIcon/SplashScreenLogo.
 */
TEST(Patch, SplashScreenBackgroundIsRecoloredFromHexTemplate)
{
    PatchResult navyResult = patchRealShellFixture("real_shell_patch_output_splashbg_navy.car", kSplashBgNavyHex);
    ASSERT_NE(navyResult.sourceReader, ext::nullopt) << "shell fixture not found or unreadable, or patch setup failed";
    ASSERT_NE(navyResult.outReader, ext::nullopt);

    std::vector<uint8_t> sourceValue;
    ASSERT_TRUE(findRenditionValueByFacetName(*navyResult.sourceReader, "SplashScreenBackground", &sourceValue))
        << "shell fixture has no SplashScreenBackground rendition";
    ASSERT_EQ(sourceValue.size(), kSplashBgValueLength);

    std::vector<uint8_t> navyValue;
    ASSERT_TRUE(findRenditionValueByFacetName(*navyResult.outReader, "SplashScreenBackground", &navyValue));
    ASSERT_EQ(navyValue.size(), kSplashBgValueLength) << "SplashScreenBackground value length must stay fixed at 260 bytes";

    EXPECT_EQ(memcmp(sourceValue.data(), navyValue.data(), kSplashBgDoublesOffset), 0)
        << "bytes 0-227 of the patched SplashScreenBackground value must stay byte-identical to the source";

    double navyDoubles[4];
    memcpy(navyDoubles, navyValue.data() + kSplashBgDoublesOffset, sizeof(navyDoubles));
    for (int i = 0; i < 4; i++) {
        EXPECT_DOUBLE_EQ(navyDoubles[i], kSplashBgNavyRgba[i]) << "navy component " << i << " mismatch";
    }
    EXPECT_NE(memcmp(sourceValue.data(), navyValue.data(), kSplashBgValueLength), 0)
        << "SplashScreenBackground value unexpectedly unchanged for navy (not branded)";

    PatchResult orangeResult = patchRealShellFixture("real_shell_patch_output_splashbg_orange.car", kSplashBgOrangeHex);
    ASSERT_NE(orangeResult.outReader, ext::nullopt);

    std::vector<uint8_t> orangeValue;
    ASSERT_TRUE(findRenditionValueByFacetName(*orangeResult.outReader, "SplashScreenBackground", &orangeValue));
    ASSERT_EQ(orangeValue.size(), kSplashBgValueLength);

    EXPECT_EQ(memcmp(sourceValue.data(), orangeValue.data(), kSplashBgDoublesOffset), 0)
        << "bytes 0-227 of the patched SplashScreenBackground value must stay byte-identical to the source";

    double orangeDoubles[4];
    memcpy(orangeDoubles, orangeValue.data() + kSplashBgDoublesOffset, sizeof(orangeDoubles));
    for (int i = 0; i < 4; i++) {
        EXPECT_DOUBLE_EQ(orangeDoubles[i], kSplashBgOrangeRgba[i]) << "orange component " << i << " mismatch";
    }
    EXPECT_NE(memcmp(sourceValue.data(), orangeValue.data(), kSplashBgValueLength), 0)
        << "SplashScreenBackground value unexpectedly unchanged for orange (not branded)";

    std::remove("real_shell_patch_output_splashbg_navy.car");
    std::remove("real_shell_patch_output_splashbg_orange.car");
}

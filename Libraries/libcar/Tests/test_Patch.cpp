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

/*
 * Mirrors car_roundtrip.cpp's cmdPatch() exactly (identifier-partition,
 * byte-verbatim default arm, structured zlib authoring for AppIcon/
 * SplashScreenLogo). Collects the hex-key of every rendition that actually
 * went through the structured-authoring branch into *brandedKeys, so the
 * tests below can assert byte-identity everywhere else without guessing
 * which keys should differ.
 */
void runPatch(
    Reader const &reader,
    RgbaImage const &iconMaster,
    RgbaImage const &splashLogoMaster,
    std::string const &outputPath,
    std::set<std::string> *brandedKeys)
{
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
    ASSERT_NE(appIconFacet, ext::nullopt) << "shell fixture has no AppIcon facet";
    ASSERT_NE(splashLogoFacet, ext::nullopt) << "shell fixture has no SplashScreenLogo facet";

    ext::optional<uint16_t> appIconId = appIconFacet->attributes().get(car_attribute_identifier_identifier);
    ext::optional<uint16_t> splashLogoId = splashLogoFacet->attributes().get(car_attribute_identifier_identifier);
    ASSERT_NE(appIconId, ext::nullopt);
    ASSERT_NE(splashLogoId, ext::nullopt);

    reader.renditionFastIterate([&](void *key, size_t keyLen, void *value, size_t valueLen) {
        car_rendition_key *renditionKey = (car_rendition_key *)key;
        uint16_t identifier = renditionKey[identifierIndex];

        bool isAppIcon = (identifier == *appIconId);
        bool isSplashLogo = (identifier == *splashLogoId);

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
        rendition.layout() = static_cast<enum car_rendition_value_layout>(sourceValue->metadata.layout);
        rendition.fileName() = std::string(sourceValue->metadata.name, strnlen(sourceValue->metadata.name, sizeof(sourceValue->metadata.name)));

        writer->addRendition(rendition);
        brandedKeys->insert(car::bytesToHex(key, keyLen));
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
    std::set<std::string> brandedKeys;
};

/*
 * Runs the patch once against the real shell fixture (checked into
 * Tests/fixtures/shell/, not synthetic) and reopens both files fresh for
 * diffing -- never reuse a prior reader's mmap (test_Writer.cpp's one hard
 * rule). Returned by value; Reader/ext::optional<Reader> are move-only,
 * matching this shim's usage elsewhere in this file.
 */
PatchResult patchRealShellFixture(std::string const &outputPath)
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
    runPatch(*sourceReader, iconMaster, splashLogoMaster, outputPath, &result.brandedKeys);

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

} // namespace

TEST(Patch, UntouchedRenditionsRemainByteIdenticalExceptBrandedKeys)
{
    PatchResult result = patchRealShellFixture("real_shell_patch_output_bytes.car");
    ASSERT_NE(result.sourceReader, ext::nullopt) << "shell fixture not found or unreadable, or patch setup failed";
    ASSERT_NE(result.outReader, ext::nullopt);
    ASSERT_GT(result.brandedKeys.size(), static_cast<size_t>(0)) << "no rendition was branded -- fixture shape assumption wrong";

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
        if (result.brandedKeys.count(pair.first) > 0) {
            EXPECT_NE(pair.second, outIt->second) << "branded rendition " << pair.first << " unexpectedly unchanged";
        } else {
            EXPECT_EQ(pair.second, outIt->second) << "untouched rendition " << pair.first << " unexpectedly changed";
        }
    }

    std::remove("real_shell_patch_output_bytes.car");
}

TEST(Patch, BrandedRenditionsAreZlibCompressedNotLzfseOrDeepmap2)
{
    PatchResult result = patchRealShellFixture("real_shell_patch_output_zlib.car");
    ASSERT_NE(result.outReader, ext::nullopt);
    ASSERT_GT(result.brandedKeys.size(), static_cast<size_t>(0));

    size_t checked = 0;
    result.outReader->renditionFastIterate([&](void *key, size_t keyLen, void *value, size_t valueLen) {
        (void)valueLen;
        std::string hexKey = car::bytesToHex(key, keyLen);
        if (result.brandedKeys.find(hexKey) == result.brandedKeys.end()) {
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

    EXPECT_EQ(checked, result.brandedKeys.size()) << "not every branded key was found during the output iteration";

    std::remove("real_shell_patch_output_zlib.car");
}

TEST(Patch, OutputHeaderAndKeyformatMatchSourceExactly)
{
    PatchResult result = patchRealShellFixture("real_shell_patch_output_header.car");
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
    PatchResult result = patchRealShellFixture("real_shell_patch_output_counts.car");
    ASSERT_NE(result.sourceReader, ext::nullopt);
    ASSERT_NE(result.outReader, ext::nullopt);

    /* FACETKEYS/RENDITIONS consistency: patch-in-place substitutes content,
     * never adds/removes a facet or rendition entry (D-01). */
    EXPECT_EQ(result.sourceReader->facetCount(), result.outReader->facetCount());
    EXPECT_EQ(result.sourceReader->renditionCount(), result.outReader->renditionCount());

    std::remove("real_shell_patch_output_counts.car");
}

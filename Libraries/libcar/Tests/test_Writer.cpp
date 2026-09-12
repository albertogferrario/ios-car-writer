/**
 Copyright (c) 2015-present, Facebook, Inc.
 All rights reserved.

 This source code is licensed under the BSD-style license found in the
 LICENSE file in the root directory of this source tree.
 */

#include <gtest/gtest.h>
#include <bom/bom_format.h>
#include <bom/bom.h>
#include <car/car_format.h>
#include <car/Facet.h>
#include <car/AttributeList.h>
#include <car/Rendition.h>
#include <car/Facet.h>
#include <car/Writer.h>
#include <car/Reader.h>
#include <car/sha256.h>
#include <car/VariablePassthrough.h>

#include <cstdio>
#include <cstring>
#include <string>

#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <vector>

#ifndef CAR_ROUNDTRIP_REPO_ROOT
#define CAR_ROUNDTRIP_REPO_ROOT "."
#endif

// Test pattern as raw pixed data, in PremultipliedBGRA8 format
static std::vector<uint8_t> test_pixels = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x7f, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x7f, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

TEST(Writer, TestWriter)
{
    int width = 8;
    int height = 8;

    /* Write out. */
    auto writer_bom = car::Writer::unique_ptr_bom(bom_alloc_empty(bom_context_memory(NULL, 0)), bom_free);
    EXPECT_NE(writer_bom, nullptr);

    auto writer = car::Writer::Create(std::move(writer_bom));
    EXPECT_NE(writer, ext::nullopt);

    car::AttributeList attributes = car::AttributeList({
        { car_attribute_identifier_idiom, car_attribute_identifier_idiom_value_universal },
        { car_attribute_identifier_scale, 2 },
        { car_attribute_identifier_identifier, 1 },
    });

    car::Facet facet = car::Facet::Create("testpattern", attributes);
    writer->addFacet(facet);

    car::Rendition::Data::Format format = car::Rendition::Data::Format::PremultipliedBGRA8;
    auto data = car::Rendition::Data(test_pixels, format);
    car::Rendition rendition = car::Rendition::Create(attributes, data);
    rendition.width() = width;
    rendition.height() = height;
    rendition.scale() = 2;
    rendition.fileName() = "testpattern.png";
    rendition.layout() = car_rendition_value_layout_one_part_scale;
    writer->addRendition(rendition);

    writer->write();

    /* Read back. */
    struct bom_context_memory const *writer_memory = bom_memory(writer->bom());
    struct bom_context_memory reader_memory = bom_context_memory(writer_memory->data, writer_memory->size);
    auto reader_bom = std::unique_ptr<struct bom_context, decltype(&bom_free)>(bom_alloc_load(reader_memory), bom_free);
    EXPECT_NE(reader_bom, nullptr);

    ext::optional<car::Reader> reader = car::Reader::Load(std::move(reader_bom));
    EXPECT_NE(reader, ext::nullopt);

    int facet_count = 0;
    int rendition_count = 0;

    reader->facetIterate([&reader, &facet_count, &rendition_count](car::Facet const &facet) {
        facet_count++;

        EXPECT_EQ(facet.name(), "testpattern");

        auto renditions = reader->lookupRenditions(facet);
        for (auto const &rendition : renditions) {
            rendition_count++;

            auto data = rendition.data()->data();
            EXPECT_EQ(data, test_pixels);
        }
    });

    EXPECT_EQ(facet_count, 1);
    EXPECT_EQ(rendition_count, 1);
}


TEST(Writer, TestWriter100)
{
    int width = 8;
    int height = 8;
    int create_facet_count = 100;
    int create_scales_count = 3;
    int create_rendition_count = create_facet_count * create_scales_count;

    /* Write out. */
    /* Write with half the required number of pre-allocated indexes */
    int preallocated_index_count = 6 + create_facet_count * 2 + create_rendition_count * 2;
    auto writer_bom = car::Writer::unique_ptr_bom(bom_alloc_empty(bom_context_memory(NULL, 0)), bom_free);
    bom_index_reserve(writer_bom.get(), preallocated_index_count / 2);
    EXPECT_NE(writer_bom, nullptr);

    auto writer = car::Writer::Create(std::move(writer_bom));
    EXPECT_NE(writer, ext::nullopt);

    for (int facet_identifier = 1; facet_identifier <= create_facet_count; facet_identifier++) {
      car::AttributeList attributes = car::AttributeList({
          { car_attribute_identifier_idiom, car_attribute_identifier_idiom_value_universal },
          { car_attribute_identifier_scale, 2 },
          { car_attribute_identifier_identifier, facet_identifier },
      });

      car::Facet facet = car::Facet::Create("testpattern_" + std::to_string(facet_identifier), attributes);
      writer->addFacet(facet);

      for (int scale = 1; scale <= create_scales_count; scale++) {
        auto data = car::Rendition::Data(test_pixels, car::Rendition::Data::Format::PremultipliedBGRA8);
        car::Rendition rendition = car::Rendition::Create(attributes, data);
        rendition.width() = width;
        rendition.height() = height;
        rendition.scale() = static_cast<double>(scale);
        rendition.fileName() = "testpattern_" + std::to_string(facet_identifier) + "@" + std::to_string(scale) + "x.png";
        rendition.layout() = car_rendition_value_layout_one_part_scale;
        writer->addRendition(rendition);
      }
    }

    writer->write();

    /* Read back. */
    struct bom_context_memory const *writer_memory = bom_memory(writer->bom());
    struct bom_context_memory reader_memory = bom_context_memory(writer_memory->data, writer_memory->size);
    auto reader_bom = std::unique_ptr<struct bom_context, decltype(&bom_free)>(bom_alloc_load(reader_memory), bom_free);
    EXPECT_NE(reader_bom, nullptr);

    ext::optional<car::Reader> reader = car::Reader::Load(std::move(reader_bom));
    EXPECT_NE(reader, ext::nullopt);

    int facet_count = 0;
    int rendition_count = 0;

    reader->facetIterate([&reader, &facet_count, &rendition_count](car::Facet const &facet) {
        facet_count++;

        ext::optional<uint16_t> facet_identifier = facet.attributes().get(car_attribute_identifier_identifier);
        EXPECT_FALSE(facet_identifier == ext::nullopt);
        EXPECT_EQ(facet.name(), "testpattern_" + std::to_string(*facet_identifier));

        auto renditions = reader->lookupRenditions(facet);
        for (auto const &rendition : renditions) {
            rendition_count++;

            auto data = rendition.data()->data();
            EXPECT_EQ(data, test_pixels);
            std::string fileName = "testpattern_" + std::to_string(*facet_identifier) + "@" + std::to_string((int)rendition.scale()) + "x.png";
            EXPECT_TRUE(strcmp(rendition.fileName().c_str(), fileName.c_str()) == 0);
        }
    });

    EXPECT_EQ(facet_count, create_facet_count);
    EXPECT_EQ(rendition_count, create_rendition_count);
}

TEST(Writer, TestWriterHeaderOverrideRoundTrips)
{
    /*
     * A header supplied via Writer::header() must be re-emitted byte-identical
     * across all 12 CARHEADER fields, including the ones libcar would otherwise
     * regenerate/zero on every write() (uuid, storage_timestamp,
     * associated_checksum, rendition_count) -- not just the three stale version
     * constants (ui_version/storage_version/schema_version).
     */
    struct car_header source_header;
    memset(&source_header, 0, sizeof(source_header));
    memcpy(source_header.magic, "RATC", 4);
    source_header.ui_version = 972;
    source_header.storage_version = 17;
    source_header.storage_timestamp = 0x54458336;
    /* Deliberately different from the actual rendition count added below, to
     * prove the override path carries the source value through verbatim
     * rather than recomputing it (recomputation is the no-override path's job,
     * covered by TestWriterRenditionCountAndSynthesisFallback below). */
    source_header.rendition_count = 42;
    strncpy(source_header.file_creator, "test file creator\n", sizeof(source_header.file_creator));
    strncpy(source_header.other_creator, "test other creator", sizeof(source_header.other_creator));
    for (size_t i = 0; i < sizeof(source_header.uuid); i++) {
        source_header.uuid[i] = static_cast<uint8_t>(i + 1);
    }
    source_header.associated_checksum = 0xDEADBEEF;
    source_header.schema_version = 2;
    source_header.color_space_id = 1;
    source_header.key_semantics = 1;

    auto writer_bom = car::Writer::unique_ptr_bom(bom_alloc_empty(bom_context_memory(NULL, 0)), bom_free);
    EXPECT_NE(writer_bom, nullptr);

    auto writer = car::Writer::Create(std::move(writer_bom));
    EXPECT_NE(writer, ext::nullopt);

    writer->header() = &source_header;

    car::AttributeList attributes = car::AttributeList({
        { car_attribute_identifier_idiom, car_attribute_identifier_idiom_value_universal },
        { car_attribute_identifier_scale, 2 },
        { car_attribute_identifier_identifier, 1 },
    });

    car::Facet facet = car::Facet::Create("headeroverride", attributes);
    writer->addFacet(facet);

    auto data = car::Rendition::Data(test_pixels, car::Rendition::Data::Format::PremultipliedBGRA8);
    car::Rendition rendition = car::Rendition::Create(attributes, data);
    rendition.width() = 8;
    rendition.height() = 8;
    rendition.scale() = 2;
    rendition.fileName() = "headeroverride.png";
    rendition.layout() = car_rendition_value_layout_one_part_scale;
    writer->addRendition(rendition);

    writer->write();

    /* Read back. */
    struct bom_context_memory const *writer_memory = bom_memory(writer->bom());
    struct bom_context_memory reader_memory = bom_context_memory(writer_memory->data, writer_memory->size);
    auto reader_bom = std::unique_ptr<struct bom_context, decltype(&bom_free)>(bom_alloc_load(reader_memory), bom_free);
    EXPECT_NE(reader_bom, nullptr);

    ext::optional<car::Reader> reader = car::Reader::Load(std::move(reader_bom));
    EXPECT_NE(reader, ext::nullopt);

    struct car_header *round_tripped = reader->header();
    EXPECT_NE(round_tripped, nullptr);

    EXPECT_EQ(memcmp(round_tripped->magic, source_header.magic, sizeof(source_header.magic)), 0);
    EXPECT_EQ(round_tripped->ui_version, source_header.ui_version);
    EXPECT_EQ(round_tripped->storage_version, source_header.storage_version);
    EXPECT_EQ(round_tripped->storage_timestamp, source_header.storage_timestamp);
    EXPECT_EQ(round_tripped->rendition_count, source_header.rendition_count);
    EXPECT_EQ(memcmp(round_tripped->file_creator, source_header.file_creator, sizeof(source_header.file_creator)), 0);
    EXPECT_EQ(memcmp(round_tripped->other_creator, source_header.other_creator, sizeof(source_header.other_creator)), 0);
    EXPECT_EQ(memcmp(round_tripped->uuid, source_header.uuid, sizeof(source_header.uuid)), 0);
    EXPECT_EQ(round_tripped->associated_checksum, source_header.associated_checksum);
    EXPECT_EQ(round_tripped->schema_version, source_header.schema_version);
    EXPECT_EQ(round_tripped->color_space_id, source_header.color_space_id);
    EXPECT_EQ(round_tripped->key_semantics, source_header.key_semantics);

    /* Full-struct byte-identity, not just field-by-field -- catches any stray
     * padding/unaccounted byte the field-by-field checks above might miss. */
    EXPECT_EQ(memcmp(round_tripped, &source_header, sizeof(struct car_header)), 0);
}

TEST(Writer, TestWriterHeaderChainFromReader)
{
    /*
     * Proves the exact composition the round-trip driver (Plan 02) relies
     * on: writer.header() = reader.header() (paired with the pre-existing
     * writer.keyfmt() = reader.keyfmt()) carries a parsed source header and
     * key format through a second Writer/Reader hop byte-identical, with
     * renditions copied via the raw-KV fast-edit path (D-15) -- no field
     * ever touched individually by the driver.
     */
    int width = 8;
    int height = 8;

    struct car_header source_header;
    memset(&source_header, 0, sizeof(source_header));
    memcpy(source_header.magic, "RATC", 4);
    source_header.ui_version = 972;
    source_header.storage_version = 17;
    source_header.storage_timestamp = 0x11223344;
    source_header.rendition_count = 1;
    strncpy(source_header.file_creator, "chain test\n", sizeof(source_header.file_creator));
    strncpy(source_header.other_creator, "chain test other", sizeof(source_header.other_creator));
    for (size_t i = 0; i < sizeof(source_header.uuid); i++) {
        source_header.uuid[i] = static_cast<uint8_t>(0xA0 + i);
    }
    source_header.associated_checksum = 0xC0FFEE;
    source_header.schema_version = 2;
    source_header.color_space_id = 1;
    source_header.key_semantics = 1;

    /* First hop: write a synthetic catalog with an explicit header override. */
    auto writer1_bom = car::Writer::unique_ptr_bom(bom_alloc_empty(bom_context_memory(NULL, 0)), bom_free);
    auto writer1 = car::Writer::Create(std::move(writer1_bom));
    EXPECT_NE(writer1, ext::nullopt);
    writer1->header() = &source_header;

    car::AttributeList attributes = car::AttributeList({
        { car_attribute_identifier_idiom, car_attribute_identifier_idiom_value_universal },
        { car_attribute_identifier_scale, 2 },
        { car_attribute_identifier_identifier, 1 },
    });
    car::Facet facet = car::Facet::Create("chaintest", attributes);
    writer1->addFacet(facet);

    auto data = car::Rendition::Data(test_pixels, car::Rendition::Data::Format::PremultipliedBGRA8);
    car::Rendition rendition = car::Rendition::Create(attributes, data);
    rendition.width() = width;
    rendition.height() = height;
    rendition.scale() = 2;
    rendition.fileName() = "chaintest.png";
    rendition.layout() = car_rendition_value_layout_one_part_scale;
    writer1->addRendition(rendition);

    writer1->write();

    struct bom_context_memory const *writer1_memory = bom_memory(writer1->bom());
    struct bom_context_memory reader1_memory = bom_context_memory(writer1_memory->data, writer1_memory->size);
    auto reader1_bom = std::unique_ptr<struct bom_context, decltype(&bom_free)>(bom_alloc_load(reader1_memory), bom_free);
    ext::optional<car::Reader> reader1 = car::Reader::Load(std::move(reader1_bom));
    EXPECT_NE(reader1, ext::nullopt);

    /* Second hop: the exact composition pattern the round-trip driver uses. */
    auto writer2_bom = car::Writer::unique_ptr_bom(bom_alloc_empty(bom_context_memory(NULL, 0)), bom_free);
    auto writer2 = car::Writer::Create(std::move(writer2_bom));
    EXPECT_NE(writer2, ext::nullopt);

    writer2->header() = reader1->header();
    writer2->keyfmt() = reader1->keyfmt();

    reader1->renditionFastIterate([&writer2](void *key, size_t key_len, void *value, size_t value_len) {
        writer2->addRendition(key, key_len, value, value_len);
    });
    reader1->facetIterate([&writer2](car::Facet const &f) { writer2->addFacet(f); });

    writer2->write();

    struct bom_context_memory const *writer2_memory = bom_memory(writer2->bom());
    struct bom_context_memory reader2_memory = bom_context_memory(writer2_memory->data, writer2_memory->size);
    auto reader2_bom = std::unique_ptr<struct bom_context, decltype(&bom_free)>(bom_alloc_load(reader2_memory), bom_free);
    ext::optional<car::Reader> reader2 = car::Reader::Load(std::move(reader2_bom));
    EXPECT_NE(reader2, ext::nullopt);

    /* The second hop's header must be byte-identical to the first hop's
     * parsed header -- the chain carries the source header through two full
     * write/read cycles without drift. */
    EXPECT_EQ(memcmp(reader2->header(), reader1->header(), sizeof(struct car_header)), 0);

    /* The key format token list must also survive the chain unchanged. */
    struct car_key_format *keyfmt1 = reader1->keyfmt();
    struct car_key_format *keyfmt2 = reader2->keyfmt();
    ASSERT_EQ(keyfmt2->num_identifiers, keyfmt1->num_identifiers);
    EXPECT_EQ(memcmp(keyfmt2->identifier_list, keyfmt1->identifier_list, keyfmt1->num_identifiers * sizeof(uint32_t)), 0);
}

TEST(Writer, TestWriterRenditionCountAndSynthesisFallback)
{
    /*
     * With NO header() override supplied, write() must fall back to today's
     * synthesis behavior unchanged (no regression for other callers), while
     * fixing the rendition_count bug: the header's rendition_count must equal
     * the actual number of renditions written, not the hardcoded 0.
     */
    int width = 8;
    int height = 8;
    int create_facet_count = 3;

    auto writer_bom = car::Writer::unique_ptr_bom(bom_alloc_empty(bom_context_memory(NULL, 0)), bom_free);
    EXPECT_NE(writer_bom, nullptr);

    auto writer = car::Writer::Create(std::move(writer_bom));
    EXPECT_NE(writer, ext::nullopt);

    for (int facet_identifier = 1; facet_identifier <= create_facet_count; facet_identifier++) {
        car::AttributeList attributes = car::AttributeList({
            { car_attribute_identifier_idiom, car_attribute_identifier_idiom_value_universal },
            { car_attribute_identifier_scale, 2 },
            { car_attribute_identifier_identifier, facet_identifier },
        });

        car::Facet facet = car::Facet::Create("rendcount_" + std::to_string(facet_identifier), attributes);
        writer->addFacet(facet);

        auto data = car::Rendition::Data(test_pixels, car::Rendition::Data::Format::PremultipliedBGRA8);
        car::Rendition rendition = car::Rendition::Create(attributes, data);
        rendition.width() = width;
        rendition.height() = height;
        rendition.scale() = 2;
        rendition.fileName() = "rendcount_" + std::to_string(facet_identifier) + ".png";
        rendition.layout() = car_rendition_value_layout_one_part_scale;
        writer->addRendition(rendition);
    }

    writer->write();

    struct bom_context_memory const *writer_memory = bom_memory(writer->bom());
    struct bom_context_memory reader_memory = bom_context_memory(writer_memory->data, writer_memory->size);
    auto reader_bom = std::unique_ptr<struct bom_context, decltype(&bom_free)>(bom_alloc_load(reader_memory), bom_free);
    EXPECT_NE(reader_bom, nullptr);

    ext::optional<car::Reader> reader = car::Reader::Load(std::move(reader_bom));
    EXPECT_NE(reader, ext::nullopt);

    struct car_header *header = reader->header();
    EXPECT_NE(header, nullptr);

    /* Bug fix: rendition_count must equal the actual count, not 0. */
    EXPECT_EQ(header->rendition_count, static_cast<uint32_t>(create_facet_count));

    /* Unchanged-synthesis fallback: with no header() override, the pre-existing
     * (stale) synthesis constants are still emitted -- proves the override
     * branch does not regress other callers of the unpatched write path. */
    EXPECT_EQ(header->ui_version, static_cast<uint32_t>(0x131));
    EXPECT_EQ(header->storage_version, static_cast<uint32_t>(0xC));
    EXPECT_EQ(header->schema_version, static_cast<uint32_t>(4));
    EXPECT_EQ(header->color_space_id, static_cast<uint32_t>(1));
    EXPECT_EQ(header->key_semantics, static_cast<uint32_t>(1));
    EXPECT_EQ(header->associated_checksum, static_cast<uint32_t>(0));
}

TEST(Sha256, KnownVectors)
{
    /*
     * Sanity-check the vendored SHA-256 implementation (car/sha256.h)
     * against the two textbook FIPS 180-4 test vectors before trusting it
     * for the real-catalog round-trip's per-rendition identity check below.
     */
    EXPECT_EQ(car::sha256Hex("", 0), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    std::string abc = "abc";
    EXPECT_EQ(car::sha256Hex(abc.data(), abc.size()), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Writer, TestMalformedInputTruncatedFileFailsCleanly)
{
    /*
     * Security Domain V5 (243-RESEARCH.md): even in this pipeline's closed
     * trust model, a malformed/truncated BOM must fail cleanly (a caught
     * error / non-zero return), never crash or read out of bounds.
     */
    std::string path = "malformed_truncated.car";
    std::remove(path.c_str());

    /* Well below sizeof(struct bom_header) (32 bytes) -- must be rejected
     * by bom_alloc_load's own size check before any field is read. */
    std::vector<uint8_t> buffer(16, 0);
    memcpy(buffer.data(), "BOMStore", 8);

    std::ofstream out(path, std::ios::binary);
    ASSERT_TRUE(out.good());
    out.write(reinterpret_cast<char const *>(buffer.data()), buffer.size());
    out.close();

    struct bom_context_memory memory = bom_context_memory_file(path.c_str(), false, 0);
    ASSERT_NE(memory.data, nullptr);
    ASSERT_GT(memory.size, static_cast<size_t>(0));

    auto bom = car::Reader::unique_ptr_bom(bom_alloc_load(memory), bom_free);
    EXPECT_EQ(bom, nullptr) << "a truncated BOM must fail to load cleanly, never crash or read OOB";

    std::remove(path.c_str());
}

TEST(Writer, TestMalformedInputBadMagicFailsCleanly)
{
    std::string path = "malformed_bad_magic.car";
    std::remove(path.c_str());

    /* Full-size buffer (well above sizeof(struct bom_header)) but with
     * wrong magic bytes -- must be rejected on the magic check, not crash. */
    std::vector<uint8_t> buffer(512, 0);
    memcpy(buffer.data(), "NOTABOMX", 8);

    std::ofstream out(path, std::ios::binary);
    ASSERT_TRUE(out.good());
    out.write(reinterpret_cast<char const *>(buffer.data()), buffer.size());
    out.close();

    struct bom_context_memory memory = bom_context_memory_file(path.c_str(), false, 0);
    ASSERT_NE(memory.data, nullptr);
    ASSERT_GT(memory.size, static_cast<size_t>(0));

    auto bom = car::Reader::unique_ptr_bom(bom_alloc_load(memory), bom_free);
    EXPECT_EQ(bom, nullptr) << "a bad-magic BOM must fail to load cleanly, never crash or read OOB";

    std::remove(path.c_str());
}

TEST(Writer, TestRealShellCatalogZeroContentRoundTrip)
{
    /*
     * The load-bearing proof (243-CONTEXT.md D-11/D-12/D-13): round-tripping
     * the shell's REAL Assets.car (checked into Tests/fixtures/shell/, not a
     * synthetic fixture) through a fresh Writer/Reader hop must be
     * semantically byte-faithful -- header/KEYFORMAT byte-identical, every
     * rendition's payload SHA-256-identical, and FACETKEYS/RENDITIONS
     * consistent. This is the writer's first passing test against real
     * data, before any branded-content logic exists anywhere (Pitfall 3).
     */
    std::string fixturePath = std::string(CAR_ROUNDTRIP_REPO_ROOT) + "/Tests/fixtures/shell/Assets.car";
    std::string outputPath = "real_shell_roundtrip_output.car";
    std::remove(outputPath.c_str());

    /* 1. Load the shell's real catalog read-only. */
    struct bom_context_memory sourceMemory = bom_context_memory_file(fixturePath.c_str(), /* writeable */ false, 0);
    ASSERT_NE(sourceMemory.data, nullptr) << "shell fixture not found or unreadable: " << fixturePath;
    ASSERT_GT(sourceMemory.size, static_cast<size_t>(0));

    auto sourceBom = car::Reader::unique_ptr_bom(bom_alloc_load(sourceMemory), bom_free);
    ASSERT_NE(sourceBom, nullptr);

    ext::optional<car::Reader> sourceReader = car::Reader::Load(std::move(sourceBom));
    ASSERT_NE(sourceReader, ext::nullopt);

    /* 2. Round-trip: fresh output BOM (never in-place patched, Pitfall 3),
     * header+keyfmt carried through verbatim, renditions copied raw-KV
     * (D-15), facets copied via the structured path (checked below). */
    {
        struct bom_context_memory outMemory = bom_context_memory_file(outputPath.c_str(), /* writeable */ true, 0);
        ASSERT_NE(outMemory.data, nullptr);

        auto outBom = car::Writer::unique_ptr_bom(bom_alloc_empty(outMemory), bom_free);
        ASSERT_NE(outBom, nullptr);

        ext::optional<car::Writer> writer = car::Writer::Create(std::move(outBom));
        ASSERT_NE(writer, ext::nullopt);

        writer->header() = sourceReader->header();
        writer->keyfmt() = sourceReader->keyfmt();

        sourceReader->renditionFastIterate([&writer](void *key, size_t keyLen, void *value, size_t valueLen) {
            writer->addRendition(key, keyLen, value, valueLen);
        });
        sourceReader->facetIterate([&writer](car::Facet const &facet) {
            writer->addFacet(facet);
        });

        writer->write();
        /* writer goes out of scope here -- bom_free() flushes to disk
         * before the output file is re-opened for verification below. */
    }

    /* 3. Re-open the source fixture fresh (a second, independent Reader --
     * the first one's underlying mmap must not be reused for the diff) and
     * the round-tripped output, then assert the full structural pre-gate. */
    struct bom_context_memory sourceMemory2 = bom_context_memory_file(fixturePath.c_str(), false, 0);
    ASSERT_NE(sourceMemory2.data, nullptr);
    auto sourceBom2 = car::Reader::unique_ptr_bom(bom_alloc_load(sourceMemory2), bom_free);
    ASSERT_NE(sourceBom2, nullptr);
    ext::optional<car::Reader> sourceReader2 = car::Reader::Load(std::move(sourceBom2));
    ASSERT_NE(sourceReader2, ext::nullopt);

    struct bom_context_memory outMemory2 = bom_context_memory_file(outputPath.c_str(), false, 0);
    ASSERT_NE(outMemory2.data, nullptr);
    auto outBom2 = car::Reader::unique_ptr_bom(bom_alloc_load(outMemory2), bom_free);
    ASSERT_NE(outBom2, nullptr);
    ext::optional<car::Reader> outReader = car::Reader::Load(std::move(outBom2));
    ASSERT_NE(outReader, ext::nullopt);

    /* Absolute correctness (D-11 leg 2): the shell's real header is CoreUI
     * 972 / StorageVersion 17 / SchemaVersion 2 -- cross-verified via
     * assetutil and viraptor/actool (243-RESEARCH.md). */
    struct car_header *sourceHeader = sourceReader2->header();
    EXPECT_EQ(sourceHeader->ui_version, static_cast<uint32_t>(972));
    EXPECT_EQ(sourceHeader->storage_version, static_cast<uint32_t>(17));
    EXPECT_EQ(sourceHeader->schema_version, static_cast<uint32_t>(2));

    /* Relative fidelity leg 1: full CARHEADER byte-identical (all 12
     * fields, Pitfall A -- not just the 3 version fields). */
    struct car_header *outHeader = outReader->header();
    EXPECT_EQ(memcmp(sourceHeader, outHeader, sizeof(struct car_header)), 0);

    /* Relative fidelity leg 2: KEYFORMAT token list byte-identical. */
    struct car_key_format *sourceKeyfmt = sourceReader2->keyfmt();
    struct car_key_format *outKeyfmt = outReader->keyfmt();
    ASSERT_EQ(sourceKeyfmt->num_identifiers, outKeyfmt->num_identifiers);
    EXPECT_EQ(memcmp(sourceKeyfmt->identifier_list, outKeyfmt->identifier_list, sourceKeyfmt->num_identifiers * sizeof(uint32_t)), 0);

    /* Relative fidelity leg 3: per-rendition SHA-256 identity (D-13 shape --
     * hex(key) -> sha256(value) -- seeds Phase 244's CatalogVerifier). */
    std::map<std::string, std::string> sourceHashes;
    sourceReader2->renditionFastIterate([&sourceHashes](void *key, size_t keyLen, void *value, size_t valueLen) {
        sourceHashes[car::bytesToHex(key, keyLen)] = car::sha256Hex(value, valueLen);
    });
    std::map<std::string, std::string> outHashes;
    outReader->renditionFastIterate([&outHashes](void *key, size_t keyLen, void *value, size_t valueLen) {
        outHashes[car::bytesToHex(key, keyLen)] = car::sha256Hex(value, valueLen);
    });
    ASSERT_GT(sourceHashes.size(), static_cast<size_t>(0)) << "shell fixture has zero renditions -- fixture is empty/invalid";
    EXPECT_EQ(sourceHashes, outHashes);

    std::cout << "[ real-shell-roundtrip ] " << sourceHashes.size()
              << " renditions, all SHA-256-identical" << std::endl;

    /* Relative fidelity leg 4 (Open Question 1 / A4, Pitfall B): the
     * structured facetIterate -> addFacet(Facet const&) path's losslessness
     * is NOT assumed -- it is proven here directly against the raw
     * FACETKEYS bytes via facetFastIterate on both sides. If this ever
     * diverges, the contingency is a raw addFacet(void*,size_t,void*,size_t)
     * mirroring the existing raw addRendition (see the plan SUMMARY for
     * whether that edit was required). */
    std::map<std::string, std::string> sourceFacetBytes;
    sourceReader2->facetFastIterate([&sourceFacetBytes](void *key, size_t keyLen, void *value, size_t valueLen) {
        sourceFacetBytes[car::bytesToHex(key, keyLen)] = car::bytesToHex(value, valueLen);
    });
    std::map<std::string, std::string> outFacetBytes;
    outReader->facetFastIterate([&outFacetBytes](void *key, size_t keyLen, void *value, size_t valueLen) {
        outFacetBytes[car::bytesToHex(key, keyLen)] = car::bytesToHex(value, valueLen);
    });
    EXPECT_EQ(sourceFacetBytes, outFacetBytes)
        << "FACETKEYS raw bytes diverged -- the structured addFacet() path is lossy; "
        << "the raw-addFacet contingency (edit 5) is required";

    /* FACETKEYS/RENDITIONS consistency: same facet-name/rendition-key set
     * cardinality, before and after. */
    EXPECT_EQ(sourceReader2->facetCount(), outReader->facetCount());
    EXPECT_EQ(sourceReader2->renditionCount(), outReader->renditionCount());

    std::remove(outputPath.c_str());
}

TEST(Writer, TestRealShellCatalogAllVariablesPreserved)
{
    /*
     * The finding this fix addresses (discovered during the Phase 243
     * live-proof gate, validated against Apple's own assetutil, not just
     * this fork's own circular self-check): the 2019-archived libcar
     * Writer only models 4 top-level BOM variables (CARHEADER, KEYFORMAT,
     * FACETKEYS, RENDITIONS) and silently drops the newer ones a real
     * CoreUI-972 catalog also carries (APPEARANCEKEYS, BITMAPKEYS,
     * EXTENDED_METADATA) -- Apple's own assetutil rejects the resulting
     * file outright ("no header information" / "BOMStreamGetDataPointer
     * buffer overflow"). This test is the platform-independent (no
     * assetutil dependency, so it runs in the Alpine CI too) regression
     * guard: the full top-level variable NAME SET must be preserved,
     * exactly, not just the four variables Writer itself understands.
     */
    std::string fixturePath = std::string(CAR_ROUNDTRIP_REPO_ROOT) + "/Tests/fixtures/shell/Assets.car";
    std::string outputPath = "real_shell_all_variables_output.car";
    std::remove(outputPath.c_str());

    struct bom_context_memory sourceMemory = bom_context_memory_file(fixturePath.c_str(), /* writeable */ false, 0);
    ASSERT_NE(sourceMemory.data, nullptr) << "shell fixture not found or unreadable: " << fixturePath;
    ASSERT_GT(sourceMemory.size, static_cast<size_t>(0));

    auto sourceBom = car::Reader::unique_ptr_bom(bom_alloc_load(sourceMemory), bom_free);
    ASSERT_NE(sourceBom, nullptr);

    ext::optional<car::Reader> sourceReader = car::Reader::Load(std::move(sourceBom));
    ASSERT_NE(sourceReader, ext::nullopt);

    /* The exact real-fixture variable set this fix must reproduce -- fixed
     * expectation, not derived from the source read above, so a bug that
     * corrupts BOTH the source parse and the round-trip identically could
     * not silently pass this assertion. */
    std::set<std::string> const expectedVariables = {
        "APPEARANCEKEYS", "BITMAPKEYS", "CARHEADER", "EXTENDED_METADATA", "FACETKEYS", "KEYFORMAT", "RENDITIONS"
    };
    std::set<std::string> sourceVariables = car::variableNames(sourceReader->bom());
    ASSERT_EQ(sourceVariables, expectedVariables) << "fixture itself does not carry the expected 7 variables";

    {
        struct bom_context_memory outMemory = bom_context_memory_file(outputPath.c_str(), /* writeable */ true, 0);
        ASSERT_NE(outMemory.data, nullptr);

        auto outBom = car::Writer::unique_ptr_bom(bom_alloc_empty(outMemory), bom_free);
        ASSERT_NE(outBom, nullptr);

        ext::optional<car::Writer> writer = car::Writer::Create(std::move(outBom));
        ASSERT_NE(writer, ext::nullopt);

        writer->header() = sourceReader->header();
        writer->keyfmt() = sourceReader->keyfmt();

        sourceReader->renditionFastIterate([&writer](void *key, size_t keyLen, void *value, size_t valueLen) {
            writer->addRendition(key, keyLen, value, valueLen);
        });
        sourceReader->facetIterate([&writer](car::Facet const &facet) {
            writer->addFacet(facet);
        });

        writer->write();

        /* The fix under test: carry every OTHER top-level variable through
         * verbatim, then relocate the trailer to match the real BOM
         * end-of-file convention. */
        car::passthroughUnmanagedVariables(sourceReader->bom(), writer->bom());
        bom_relocate_trailer(writer->bom());
    }

    struct bom_context_memory outMemory2 = bom_context_memory_file(outputPath.c_str(), false, 0);
    ASSERT_NE(outMemory2.data, nullptr);
    auto outBom2 = car::Reader::unique_ptr_bom(bom_alloc_load(outMemory2), bom_free);
    ASSERT_NE(outBom2, nullptr);
    ext::optional<car::Reader> outReader = car::Reader::Load(std::move(outBom2));
    ASSERT_NE(outReader, ext::nullopt);

    std::set<std::string> outVariables = car::variableNames(outReader->bom());

    /* The load-bearing assertion: the round-tripped output's FULL variable
     * set equals the source's, not just the four Writer itself emits. A
     * lossy round-trip (the pre-fix behavior) fails this with outVariables
     * == {CARHEADER, FACETKEYS, KEYFORMAT, RENDITIONS} -- missing
     * APPEARANCEKEYS/BITMAPKEYS/EXTENDED_METADATA. */
    EXPECT_EQ(outVariables, sourceVariables)
        << "round-tripped output is missing top-level BOM variables present in the source -- "
        << "the fix that carries APPEARANCEKEYS/BITMAPKEYS/EXTENDED_METADATA through verbatim regressed";
    EXPECT_EQ(outVariables, expectedVariables);

    std::remove(outputPath.c_str());
}

TEST(Writer, TestWriter100Optimal)
{
    int width = 8;
    int height = 8;
    int create_facet_count = 100;
    int create_scales_count = 3;
    int create_rendition_count = create_facet_count * create_scales_count;

    /* Write out.
     * A fast write has pre-allocated space for BOM indexes
     * A baseline of 6 indexes are required: CAR Header (1), Key Format (1), and FACET (2) and RENDITION (2) trees
     * Each tree entry (facet or rendition) requires 2: one key index, one value index.
     */
    unsigned int preallocated_index_count = 6 + create_facet_count * 2 + create_rendition_count * 2;
    auto writer_bom = car::Writer::unique_ptr_bom(bom_alloc_empty(bom_context_memory(NULL, 0)), bom_free);
    bom_index_reserve(writer_bom.get(), preallocated_index_count);
    EXPECT_NE(writer_bom, nullptr);

    auto writer = car::Writer::Create(std::move(writer_bom));
    EXPECT_NE(writer, ext::nullopt);

    for (int facet_identifier = 1; facet_identifier <= create_facet_count; facet_identifier++) {
      car::AttributeList attributes = car::AttributeList({
          { car_attribute_identifier_idiom, car_attribute_identifier_idiom_value_universal },
          { car_attribute_identifier_scale, 2 },
          { car_attribute_identifier_identifier, facet_identifier },
      });

      car::Facet facet = car::Facet::Create("testpattern_" + std::to_string(facet_identifier), attributes);
      writer->addFacet(facet);

      for (int scale = 1; scale <= create_scales_count; scale++) {
        auto data = car::Rendition::Data(test_pixels, car::Rendition::Data::Format::PremultipliedBGRA8);
        car::Rendition rendition = car::Rendition::Create(attributes, data);
        rendition.width() = width;
        rendition.height() = height;
        rendition.scale() = static_cast<double>(scale);
        rendition.fileName() = "testpattern_" + std::to_string(facet_identifier) + "@" + std::to_string(scale) + "x.png";
        rendition.layout() = car_rendition_value_layout_one_part_scale;
        writer->addRendition(rendition);
      }
    }

    writer->write();

    /* Read back. */
    struct bom_context_memory const *writer_memory = bom_memory(writer->bom());
    struct bom_context_memory reader_memory = bom_context_memory(writer_memory->data, writer_memory->size);
    auto reader_bom = std::unique_ptr<struct bom_context, decltype(&bom_free)>(bom_alloc_load(reader_memory), bom_free);
    EXPECT_NE(reader_bom, nullptr);

    ext::optional<car::Reader> reader = car::Reader::Load(std::move(reader_bom));
    EXPECT_NE(reader, ext::nullopt);

    int facet_count = 0;
    int rendition_count = 0;

    reader->facetIterate([&reader, &facet_count, &rendition_count](car::Facet const &facet) {
        facet_count++;

        ext::optional<uint16_t> facet_identifier = facet.attributes().get(car_attribute_identifier_identifier);
        EXPECT_FALSE(facet_identifier == ext::nullopt);
        EXPECT_EQ(facet.name(), "testpattern_" + std::to_string(*facet_identifier));

        auto renditions = reader->lookupRenditions(facet);
        for (auto const &rendition : renditions) {
            rendition_count++;

            auto data = rendition.data()->data();
            EXPECT_EQ(data, test_pixels);
            std::string fileName = "testpattern_" + std::to_string(*facet_identifier) + "@" + std::to_string((int)rendition.scale()) + "x.png";
            EXPECT_TRUE(strcmp(rendition.fileName().c_str(), fileName.c_str()) == 0);
        }
    });

    EXPECT_EQ(facet_count, create_facet_count);
    EXPECT_EQ(rendition_count, create_rendition_count);
}


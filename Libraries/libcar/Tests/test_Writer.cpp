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

#include <cstdio>
#include <cstring>
#include <string>

#include <vector>

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


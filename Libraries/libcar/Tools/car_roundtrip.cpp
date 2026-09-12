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
 *
 * CLI contract (243-02-PLAN.md <interfaces>):
 *   car-writer roundtrip --source <in.car> --out <out.car>
 *     -> exit 0 on success; writes a freshly re-serialized catalog.
 *   car-writer verify --source <original.car> --out <roundtripped.car>
 *     -> exit 0 iff header/KEYFORMAT/per-rendition-SHA-256/FACETKEYS are
 *        all consistent between the two files; non-zero + a JSON summary
 *        on stdout otherwise.
 */

#include <car/Reader.h>
#include <car/Writer.h>
#include <car/Facet.h>
#include <car/car_format.h>
#include <car/sha256.h>
#include <bom/bom.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>

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

void printUsage()
{
    fprintf(stderr, "usage: car-writer roundtrip --source <in.car> --out <out.car>\n");
    fprintf(stderr, "       car-writer verify --source <original.car> --out <roundtripped.car>\n");
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
        "\"rendition_count\": %zu, \"facet_count\": %d, \"rendition_hashes\": %s}\n",
        sourceHeader->ui_version, sourceHeader->storage_version, sourceHeader->schema_version,
        sourceHashes.size(), sourceReader->facetCount(), mapToJson(sourceHashes).c_str()
    );
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

    for (int i = 2; i + 1 < argc; i += 2) {
        std::string flag = argv[i];
        std::string value = argv[i + 1];
        if (flag == "--source") {
            sourcePath = value;
        } else if (flag == "--out") {
            outPath = value;
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
    }

    printUsage();
    return 1;
}

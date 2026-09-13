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
 */

#include <car/Reader.h>
#include <car/Writer.h>
#include <car/Facet.h>
#include <car/car_format.h>
#include <car/sha256.h>
#include <car/VariablePassthrough.h>
#include <bom/bom.h>

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
 * car-writer patch: reproduces cmdRoundtrip()'s full BOM re-serialization
 * but substitutes exactly the AppIcon and SplashScreenLogo renditions with
 * fresh content derived from the supplied PNG inputs (Phase 244 Plan 02).
 * SplashScreenBackground and every other rendition stay byte-verbatim in
 * THIS commit -- the branding logic itself lands in a follow-up commit on
 * this same plan; at this point cmdPatch is behaviorally identical to
 * cmdRoundtrip. --icon-1024/--splash-logo/--splash-bg-hex are accepted but
 * unused here so the CLI surface is stable before the branding logic wires
 * into it.
 */
int cmdPatch(
    std::string const &sourcePath,
    std::string const &iconPath,
    std::string const &splashLogoPath,
    std::string const &splashBgHex,
    std::string const &outPath)
{
    (void)iconPath;
    (void)splashLogoPath;
    (void)splashBgHex;

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

    writer->header() = reader->header();
    writer->keyfmt() = reader->keyfmt();

    reader->renditionFastIterate([&writer](void *key, size_t keyLen, void *value, size_t valueLen) {
        writer->addRendition(key, keyLen, value, valueLen);
    });
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

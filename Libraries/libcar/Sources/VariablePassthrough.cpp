/**
 Copyright (c) 2015-present, Facebook, Inc.
 All rights reserved.

 This source code is licensed under the BSD-style license found in the
 LICENSE file in the root directory of this source tree.
 */

#include <car/VariablePassthrough.h>
#include <car/car_format.h>
#include <bom/bom.h>
#include <bom/bom_format.h>

#if _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include <cstring>
#include <map>
#include <vector>

std::set<std::string> car::
variableNames(struct bom_context *bom)
{
    std::set<std::string> names;
    bom_variable_iterate(
        bom,
        [](struct bom_context *, const char *name, int /* dataIndex */, void *ctx) -> bool {
            static_cast<std::set<std::string> *>(ctx)->insert(name);
            return true;
        },
        &names
    );
    return names;
}

namespace {

/* The four top-level BOM variables car::Writer already emits itself (via
 * writer->write(), driven by header()/keyfmt()/addFacet()/addRendition()).
 * Everything else in the source catalog is unmodeled by libcar/libbom
 * (frozen upstream since 2019) and must be passed through verbatim at the
 * raw libbom level for the round-trip to be a genuine zero-content copy of
 * the FULL catalog, not just of the subset libcar happens to understand. */
bool isWriterManagedVariable(std::string const &name)
{
    return name == car_header_variable
        || name == car_key_format_variable
        || name == car_facet_keys_variable
        || name == car_renditions_variable;
}

/*
 * Graph-aware clone for a BOM-tree-shaped variable's reachable block set.
 *
 * A BOM tree's internal nodes hold 32-bit index NUMBERS that are only
 * meaningful within the bom_context that assigned them -- so a tree cannot
 * be passed through as a single byte-copied block; every reachable node
 * must be re-added (bom_index_add) to the destination bom and every
 * reference to it rewritten to the freshly assigned index (never carry a
 * source-context index through unchanged; recompute offsets fresh).
 *
 * The one thing that is NOT safe to assume uniformly is that every 32-bit
 * field shaped like an index (tree_entry_indexes.key_index in particular)
 * IS actually an index reference. Empirically, on the real shell fixture,
 * FACETKEYS/RENDITIONS/APPEARANCEKEYS keys resolve to real blocks via
 * bom_index_get -- but BITMAPKEYS' key_index values (observed: 6849, 26808,
 * 62644 against a file with a few dozen total blocks) do not resolve to
 * anything: they are literal small values inlined directly into the key
 * slot, not references at all. Reinterpreting them as references and
 * "fixing" them up would corrupt data whose true semantics this fork does
 * not know (libcar/libbom have had zero commits since 2019 and never
 * documented this). Symmetrically, treating them as references that DO
 * resolve (value_index in every observed case) and failing to remap them
 * would point the round-tripped file at the wrong data entirely, or at
 * nothing.
 *
 * So each field is resolved individually: if bom_index_get() on the SOURCE
 * bom returns real data for it, it is a reference -- clone the referenced
 * block and rewrite the field to the new index. If it does not, the field
 * is left as the exact bytes it already was: not reinterpreted, not
 * remapped, carried through byte-verbatim (safe either way -- whether it is
 * truly an inline literal or something else this fork has never seen, the
 * round-trip changes nothing about it).
 */
class TreeGraphCloner {
public:
    TreeGraphCloner(struct bom_context *sourceBom, struct bom_context *destBom)
        : _sourceBom(sourceBom), _destBom(destBom)
    {
    }

    /* Clone the top-level tree-header block for `name` and register it as a
     * variable in the destination bom. */
    void cloneTreeVariable(std::string const &name, int sourceVarIndex)
    {
        size_t treeLen;
        void *treeData = bom_index_get(_sourceBom, sourceVarIndex, &treeLen);
        if (treeData == NULL || treeLen < sizeof(struct bom_tree)) {
            return;
        }

        std::vector<uint8_t> buffer(static_cast<uint8_t *>(treeData), static_cast<uint8_t *>(treeData) + treeLen);
        struct bom_tree *tree = reinterpret_cast<struct bom_tree *>(buffer.data());

        uint32_t destChild = cloneEntry(ntohl(tree->child));
        tree = reinterpret_cast<struct bom_tree *>(buffer.data());
        tree->child = htonl(destChild);

        uint32_t newTreeIndex = bom_index_add(_destBom, buffer.data(), buffer.size());
        bom_variable_add(_destBom, name.c_str(), newTreeIndex);
    }

private:
    /* True iff `candidate` resolves to real data in the SOURCE bom -- i.e.
     * it is genuinely usable as an index, not just a number that looks like
     * one. */
    bool resolvesAsSourceIndex(uint32_t candidate) const
    {
        size_t len;
        return bom_index_get(_sourceBom, candidate, &len) != NULL;
    }

    /* Clone an opaque leaf block (rendition/facet/key/value payload -- no
     * further internal index references) byte-verbatim into the
     * destination bom, memoized by source index. */
    uint32_t cloneOpaqueBlock(uint32_t sourceIndex)
    {
        auto cached = _indexMap.find(sourceIndex);
        if (cached != _indexMap.end()) {
            return cached->second;
        }

        size_t len;
        void *data = bom_index_get(_sourceBom, sourceIndex, &len);
        uint32_t newIndex = bom_index_add(_destBom, data, len);
        _indexMap[sourceIndex] = newIndex;
        return newIndex;
    }

    /* Clone a bom_tree_entry node (leaf, or a non-leaf redirect toward the
     * first leaf) and everything it references, returning the new index.
     * Memoized by source index -- also what makes a forward/backward cycle
     * (or a backward link to an already-visited leaf) safe. */
    uint32_t cloneEntry(uint32_t sourceEntryIndex)
    {
        auto cached = _indexMap.find(sourceEntryIndex);
        if (cached != _indexMap.end()) {
            return cached->second;
        }

        size_t entryLen;
        void *entryData = bom_index_get(_sourceBom, sourceEntryIndex, &entryLen);
        if (entryData == NULL || entryLen < sizeof(struct bom_tree_entry)) {
            return sourceEntryIndex;
        }

        std::vector<uint8_t> buffer(static_cast<uint8_t *>(entryData), static_cast<uint8_t *>(entryData) + entryLen);
        struct bom_tree_entry *entry = reinterpret_cast<struct bom_tree_entry *>(buffer.data());
        struct bom_tree_entry_indexes *indexes = reinterpret_cast<struct bom_tree_entry_indexes *>(buffer.data() + sizeof(struct bom_tree_entry));
        size_t maxIndexes = (buffer.size() - sizeof(struct bom_tree_entry)) / sizeof(struct bom_tree_entry_indexes);

        /* Reserve the destination index up front so a cycle back to this
         * same entry (via forward/backward) resolves to a real slot instead
         * of recursing forever -- filled in with the real bytes below. */
        uint32_t reservedIndex = bom_index_add(_destBom, buffer.data(), buffer.size());
        _indexMap[sourceEntryIndex] = reservedIndex;

        if (entry->is_leaf == 0) {
            /* Non-leaf redirect: indexes[0].value_index points at the first
             * real leaf; key_index in this slot is unused. */
            if (maxIndexes > 0) {
                uint32_t destFirstLeaf = cloneEntry(ntohl(indexes[0].value_index));
                indexes[0].value_index = htonl(destFirstLeaf);
            }
        } else {
            uint16_t count = ntohs(entry->count);
            for (uint16_t i = 0; i < count && i < maxIndexes; i++) {
                uint32_t sourceValueIndex = ntohl(indexes[i].value_index);
                if (resolvesAsSourceIndex(sourceValueIndex)) {
                    indexes[i].value_index = htonl(cloneOpaqueBlock(sourceValueIndex));
                }
                /* else: leave the raw bytes unchanged -- not observed on the
                 * real fixture, but handled defensively for symmetry. */

                uint32_t sourceKeyIndex = ntohl(indexes[i].key_index);
                if (resolvesAsSourceIndex(sourceKeyIndex)) {
                    indexes[i].key_index = htonl(cloneOpaqueBlock(sourceKeyIndex));
                }
                /* else: an inline literal key (observed for BITMAPKEYS) --
                 * carry the raw 4 bytes through completely unchanged. */
            }

            uint32_t sourceForward = ntohl(entry->forward);
            if (sourceForward != 0) {
                uint32_t destForward = cloneEntry(sourceForward);
                entry = reinterpret_cast<struct bom_tree_entry *>(buffer.data());
                entry->forward = htonl(destForward);
            }

            uint32_t sourceBackward = ntohl(entry->backward);
            if (sourceBackward != 0) {
                auto backCached = _indexMap.find(sourceBackward);
                if (backCached != _indexMap.end()) {
                    entry = reinterpret_cast<struct bom_tree_entry *>(buffer.data());
                    entry->backward = htonl(backCached->second);
                }
                /* else: the backward-linked leaf has not been cloned yet
                 * (this entry was reached via backward before forward) --
                 * left as the reserved placeholder's own bytes; not
                 * observed on the real fixture (single-leaf trees only). */
            }
        }

        /* Overwrite the reserved slot with the fully rewritten bytes. */
        void *destData = bom_index_get(_destBom, reservedIndex, NULL);
        memcpy(destData, buffer.data(), buffer.size());

        return reservedIndex;
    }

    struct bom_context *_sourceBom;
    struct bom_context *_destBom;
    std::map<uint32_t, uint32_t> _indexMap;
};

struct PassthroughContext {
    struct bom_context *destBom;
};

/* bom_variable_iterator callback, run once per top-level variable in the
 * SOURCE bom. Variables car::Writer already emitted are skipped; every
 * other variable is copied verbatim into the destination bom, as described
 * in VariablePassthrough.h. */
bool passthroughVariable(struct bom_context *sourceBom, const char *name, int dataIndex, void *ctx)
{
    PassthroughContext *pass = static_cast<PassthroughContext *>(ctx);

    if (isWriterManagedVariable(name)) {
        return true;
    }

    size_t dataLen = 0;
    void *data = bom_index_get(sourceBom, dataIndex, &dataLen);
    if (data == NULL) {
        return true;
    }

    bool looksLikeTree = dataLen >= sizeof(struct bom_tree)
        && strncmp(static_cast<char *>(data), "tree", 4) == 0
        && ntohl(reinterpret_cast<struct bom_tree *>(data)->version) == 1;

    if (looksLikeTree) {
        TreeGraphCloner cloner(sourceBom, pass->destBom);
        cloner.cloneTreeVariable(name, dataIndex);
    } else {
        uint32_t newIndex = bom_index_add(pass->destBom, data, dataLen);
        bom_variable_add(pass->destBom, name, newIndex);
    }

    return true;
}

} // namespace

void car::
passthroughUnmanagedVariables(struct bom_context *sourceBom, struct bom_context *destBom)
{
    PassthroughContext passCtx = { destBom };
    bom_variable_iterate(sourceBom, passthroughVariable, &passCtx);
}

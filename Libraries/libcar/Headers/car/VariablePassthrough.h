/**
 Copyright (c) 2015-present, Facebook, Inc.
 All rights reserved.

 This source code is licensed under the BSD-style license found in the
 LICENSE file in the root directory of this source tree.
 */

#ifndef _LIBCAR_VARIABLE_PASSTHROUGH_H
#define _LIBCAR_VARIABLE_PASSTHROUGH_H

#include <bom/bom.h>

#include <set>
#include <string>

namespace car {

/*
 * The full set of top-level BOM variable NAMES present in `bom` (e.g. for a
 * real CoreUI-972 catalog: APPEARANCEKEYS, BITMAPKEYS, CARHEADER,
 * EXTENDED_METADATA, FACETKEYS, KEYFORMAT, RENDITIONS). Used to assert
 * round-trip completeness against the full source variable set, not just
 * the four variables Writer itself models.
 */
std::set<std::string> variableNames(struct bom_context *bom);

/*
 * Copy every top-level variable in `sourceBom` that car::Writer does not
 * itself manage (CARHEADER, KEYFORMAT, FACETKEYS, RENDITIONS -- driven by
 * header()/keyfmt()/addFacet()/addRendition()) into `destBom`, verbatim:
 *
 *   - tree-shaped (magic "tree", version 1): rebuilt via a graph-aware
 *     clone -- every reachable block gets a freshly computed index in
 *     destBom (a BOM tree's internal index numbers are only meaningful
 *     within the bom_context that assigned them, so a tree cannot be
 *     passed through as a single byte-copied block). A field shaped like
 *     an index reference is only treated as one if it actually resolves to
 *     real data in sourceBom; fields that do not resolve (observed for
 *     BITMAPKEYS: some keys are literal small values inlined directly,
 *     not references) are carried through byte-verbatim, unchanged and
 *     unreinterpreted.
 *   - flat/opaque (e.g. EXTENDED_METADATA -- a single self-contained block
 *     with no internal index references): a direct byte-verbatim copy.
 *
 * Detection is structural, not name-based, so this also passes through any
 * variable libcar/libbom (frozen upstream since 2019) has never heard of by
 * name -- e.g. APPEARANCEKEYS, which is not declared anywhere in
 * car_format.h.
 *
 * Call this AFTER destBom's Writer::write() has run, so the four
 * writer-managed variables already exist in destBom and are correctly
 * skipped.
 */
void passthroughUnmanagedVariables(struct bom_context *sourceBom, struct bom_context *destBom);

}

#endif /* _LIBCAR_VARIABLE_PASSTHROUGH_H */

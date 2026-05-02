// pstwriter/src/nbt.cpp
//
// Implementation of the M5 Phase B NBT page writers.

#include "nbt.hpp"

#include "ndb.hpp"
#include "page.hpp"
#include "types.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace std;

namespace pstwriter {

using detail::writeU8;
using detail::writeU32;
using detail::writeU64;

// ============================================================================
// NBT leaf page ([SPEC sec 2.2.2.7.7.3] NBTENTRY layout)
//
// Sorts entries internally. Caller may pass any ordering; the builder
// emits them in NID-ascending order.
// ============================================================================
array<uint8_t, kPageSize> buildNbtLeaf(const NbtEntry* entries,
                                       size_t          count,
                                       Bid             bid,
                                       Ib              ib)
{
    if (count > kNbtLeafMaxEntries) {
        throw runtime_error(
            "buildNbtLeaf: count exceeds kNbtLeafMaxEntries (15); caller "
            "must paginate via buildNbtTree");
    }

    // Copy and sort by NID ascending (full 32-bit NID value).
    vector<NbtEntry> sorted(entries, entries + count);
    std::sort(sorted.begin(), sorted.end(),
              [](const NbtEntry& a, const NbtEntry& b) {
                  return a.nid.value < b.nid.value;
              });

    array<uint8_t, kPageSize> page{};

    // NBTENTRY layout (Unicode, 32 bytes per entry):
    //   [0..3]   nid (4 B)
    //   [4..7]   pad (4 B, zero -- nid is "extended to 8 bytes" per spec)
    //   [8..15]  bidData (8 B)
    //   [16..23] bidSub (8 B)
    //   [24..27] nidParent (4 B)
    //   [28..31] dwPadding (4 B, MUST be zero)
    for (size_t i = 0; i < sorted.size(); ++i) {
        const size_t off = i * kNbtLeafEntrySize;
        writeU32(page.data(), off + 0,  sorted[i].nid.value);
        writeU32(page.data(), off + 4,  0u);
        writeU64(page.data(), off + 8,  sorted[i].bidData.value);
        writeU64(page.data(), off + 16, sorted[i].bidSub.value);
        writeU32(page.data(), off + 24, sorted[i].nidParent.value);
        writeU32(page.data(), off + 28, 0u);
    }

    writeU8 (page.data(), kBtPageCEnt,    static_cast<uint8_t>(sorted.size()));
    writeU8 (page.data(), kBtPageCEntMax, static_cast<uint8_t>(kNbtLeafMaxEntries));
    writeU8 (page.data(), kBtPageCbEnt,   static_cast<uint8_t>(kNbtLeafEntrySize));
    writeU8 (page.data(), kBtPageCLevel,  0u);
    writeU32(page.data(), kBtPageDwPad,   0u);

    writePageTrailer(page, ptype::kNBT, bid, ib);
    return page;
}

// ============================================================================
// Intermediate BTPAGE ([SPEC sec 2.2.2.7.7.2] BTENTRY layout)
//
// Single writer for both NBT and BBT intermediate pages -- the format
// is identical except for the ptype byte (per spec text "both
// intermediate NBT and BBT pages share this format").
//
// Sorts entries by btkey ascending.
// ============================================================================
array<uint8_t, kPageSize> buildBtIntermediate(const BtEntry* entries,
                                              size_t         count,
                                              uint8_t        cLevel,
                                              uint8_t        ptype,
                                              Bid            bid,
                                              Ib             ib)
{
    if (count > kBtIntermediateMaxEntries) {
        throw runtime_error(
            "buildBtIntermediate: count exceeds kBtIntermediateMaxEntries (20)");
    }
    if (cLevel == 0u) {
        throw runtime_error(
            "buildBtIntermediate: cLevel must be >= 1 (cLevel=0 is for leaf "
            "pages; use buildNbtLeaf or buildBbtLeaf)");
    }
    if (ptype != ptype::kNBT && ptype != ptype::kBBT) {
        throw runtime_error(
            "buildBtIntermediate: ptype must be ptypeNBT (0x81) or ptypeBBT (0x80)");
    }

    // Copy and sort by btkey ascending.
    vector<BtEntry> sorted(entries, entries + count);
    std::sort(sorted.begin(), sorted.end(),
              [](const BtEntry& a, const BtEntry& b) {
                  return a.btkey < b.btkey;
              });

    array<uint8_t, kPageSize> page{};

    // BTENTRY layout (Unicode, 24 bytes per entry):
    //   [0..7]   btkey (8 B, NID zero-extended for NBT or BID for BBT)
    //   [8..15]  BREF.bid (8 B)
    //   [16..23] BREF.ib  (8 B)
    for (size_t i = 0; i < sorted.size(); ++i) {
        const size_t off = i * kBtEntrySize;
        writeU64(page.data(), off + 0,  sorted[i].btkey);
        writeU64(page.data(), off + 8,  sorted[i].bref.bid.value);
        writeU64(page.data(), off + 16, sorted[i].bref.ib.value);
    }

    writeU8 (page.data(), kBtPageCEnt,    static_cast<uint8_t>(sorted.size()));
    writeU8 (page.data(), kBtPageCEntMax, static_cast<uint8_t>(kBtIntermediateMaxEntries));
    writeU8 (page.data(), kBtPageCbEnt,   static_cast<uint8_t>(kBtEntrySize));
    writeU8 (page.data(), kBtPageCLevel,  cLevel);
    writeU32(page.data(), kBtPageDwPad,   0u);

    writePageTrailer(page, ptype, bid, ib);
    return page;
}

// ============================================================================
// Multi-page tree builder
// ============================================================================
NbtTreeOutput buildNbtTree(const NbtEntry*         entries,
                           size_t                  count,
                           const NbtTreeInputBids& bids)
{
    // Sort entries up front so leaf splits are sorted by NID across all
    // leaves (i.e. leaf[0] holds the smallest NIDs, leaf[K-1] holds the
    // largest). This is the BTPAGE invariant the reader relies on.
    vector<NbtEntry> sorted(entries, entries + count);
    std::sort(sorted.begin(), sorted.end(),
              [](const NbtEntry& a, const NbtEntry& b) {
                  return a.nid.value < b.nid.value;
              });

    const size_t leafCount =
        (sorted.empty()) ? 0u
                         : (sorted.size() + kNbtLeafMaxEntries - 1) / kNbtLeafMaxEntries;
    if (leafCount > kBtIntermediateMaxEntries) {
        throw runtime_error(
            "buildNbtTree: entry count requires more leaves than fit in one "
            "intermediate page; multi-level pagination is M7+");
    }

    NbtTreeOutput out;

    // Empty tree case: a single empty leaf.
    if (sorted.empty()) {
        out.leafPages.push_back(
            buildNbtLeaf(nullptr, 0, bids.leafBrefs[0].bid, bids.leafBrefs[0].ib));
        out.hasIntermediate = false;
        out.rootBref = bids.leafBrefs[0];
        out.treeLevel = 0;
        return out;
    }

    // Build each leaf.
    out.leafPages.reserve(leafCount);
    for (size_t li = 0; li < leafCount; ++li) {
        const size_t lo = li * kNbtLeafMaxEntries;
        const size_t hi = std::min(lo + kNbtLeafMaxEntries, sorted.size());
        out.leafPages.push_back(
            buildNbtLeaf(sorted.data() + lo, hi - lo,
                         bids.leafBrefs[li].bid,
                         bids.leafBrefs[li].ib));
    }

    if (leafCount == 1) {
        // Single leaf serves as the root.
        out.hasIntermediate = false;
        out.rootBref = bids.leafBrefs[0];
        out.treeLevel = 0;
        return out;
    }

    // Multi-leaf: build one intermediate page above. Each BTENTRY's
    // btkey is the SMALLEST NID in the corresponding leaf (per [SPEC
    // sec 2.2.2.7.7.2]: "All the entries in the child BTPAGE referenced
    // by BREF have key values greater than or equal to this key value").
    vector<BtEntry> btEntries;
    btEntries.reserve(leafCount);
    for (size_t li = 0; li < leafCount; ++li) {
        const size_t firstIdxInLeaf = li * kNbtLeafMaxEntries;
        const uint64_t btkey =
            static_cast<uint64_t>(sorted[firstIdxInLeaf].nid.value);
        btEntries.emplace_back(btkey, bids.leafBrefs[li]);
    }

    out.intermediatePage = buildBtIntermediate(
        btEntries.data(), btEntries.size(),
        /*cLevel*/ 1u,
        /*ptype */ ptype::kNBT,
        bids.intermediateBref.bid,
        bids.intermediateBref.ib);
    out.hasIntermediate = true;
    out.rootBref = bids.intermediateBref;
    out.treeLevel = 1;
    return out;
}

} // namespace pstwriter

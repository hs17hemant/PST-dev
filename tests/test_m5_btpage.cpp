// pstwriter/tests/test_m5_btpage.cpp
//
// Phase B - BTPAGE writer gate tests.
//
// Tests:
//   (a) [golden_spec_bt_intermediate] - sec 3.3 byte-for-byte round-trip.
//       Positive control (full match incl. stored CRC).
//   (b) Reversed-input ordering - writer sorts entries internally.
//       Catches the "no-op pass-through implementation passes when input
//       is already sorted" failure mode.
//   (c) NBT vs BBT shared format - intermediate BBT page differs from
//       sec 3.3 only at the ptype byte.
//   (d) Pagination - single-leaf vs multi-leaf NBT trees.
//   (e) Empty-page semantics - count == 0 produces a valid empty page.

#include <catch2/catch_test_macros.hpp>

#include "nbt.hpp"
#include "ndb.hpp"
#include "page.hpp"
#include "types.hpp"
#include "crc.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace pstwriter;
using detail::readU16;
using detail::readU32;
using detail::readU64;

namespace {

bool fileExistsBtP(const string& path)
{
    if (FILE* fp = std::fopen(path.c_str(), "rb")) {
        std::fclose(fp);
        return true;
    }
    return false;
}

bool readEntireFileBtP(const string& path, vector<uint8_t>& out)
{
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    out.resize(static_cast<size_t>(sz));
    const size_t got = std::fread(out.data(), 1, out.size(), fp);
    std::fclose(fp);
    return got == out.size();
}

string findGoldenBtP()
{
    const string candidates[] = {
        "tests/golden/spec_sample_bt_intermediate.bin",
        "../tests/golden/spec_sample_bt_intermediate.bin",
        "../../tests/golden/spec_sample_bt_intermediate.bin",
        "../../../tests/golden/spec_sample_bt_intermediate.bin",
    };
    for (const auto& c : candidates) {
        if (fileExistsBtP(c)) return c;
    }
    return "";
}

} // namespace

// ============================================================================
// Test (a): unlock [golden_spec_bt_intermediate] - sec 3.3 byte-for-byte
// ============================================================================
//
// Sample summary (from sec 3.3, transcribed in M5 pre-flight):
//   * 3 BTENTRY records (cEnt=3, cEntMax=0x14, cbEnt=0x18, cLevel=1)
//   * page IB = 0x8200, page bid = 0x206 (verified via wSig formula:
//     ib^bid = 0x8006 = stored wSig)
//   * stored wSig = 0x8006, dwCRC = 0x02E8B164 (positive control)
//   * ptype = 0x81 (intermediate NBT)
//
// BTENTRY contents decoded from the hex dump:
//   [0]: btkey=0x21    bref={bid=0x205, ib=0x7E00}
//   [1]: btkey=0x60F   bref={bid=0x141, ib=0x7000}
//   [2]: btkey=0x8022  bref={bid=0xFD,  ib=0x8400}
//
// Already sorted by btkey; Test (b) below feeds them in REVERSE order to
// confirm the writer's internal sort.
TEST_CASE("BTPAGE writer round-trips [MS-PST] Sec 3.3 sample byte-for-byte",
          "[block][bt][golden_spec_bt_intermediate]")
{
    const string path = findGoldenBtP();
    REQUIRE_FALSE(path.empty());
    vector<uint8_t> golden;
    REQUIRE(readEntireFileBtP(path, golden));
    REQUIRE(golden.size() == kPageSize);

    BtEntry entries[3];
    entries[0] = BtEntry{0x21u,    Bref{Bid{0x205u}, Ib{0x7E00u}}};
    entries[1] = BtEntry{0x60Fu,   Bref{Bid{0x141u}, Ib{0x7000u}}};
    entries[2] = BtEntry{0x8022u,  Bref{Bid{0xFDu},  Ib{0x8400u}}};

    const auto regen = buildBtIntermediate(
        entries, 3,
        /*cLevel*/ 1u,
        /*ptype */ ptype::kNBT,
        /*bid   */ Bid{0x206u},
        /*ib    */ Ib{0x8200u});

    REQUIRE(regen.size() == golden.size());

    // Byte-for-byte across all 512 bytes (positive control).
    for (size_t i = 0; i < kPageSize; ++i) {
        if (regen[i] != golden[i]) {
            INFO("first mismatch at offset 0x" << std::hex << i
                 << ": regen=0x" << +regen[i] << " golden=0x" << +golden[i]);
            FAIL("byte mismatch in Sec 3.3 BT intermediate round-trip");
        }
    }

    // Spot-check the trailer values match what the spec dump showed:
    REQUIRE(regen[kPageTrailerOffset + 0] == 0x81u); // ptype = NBT
    REQUIRE(regen[kPageTrailerOffset + 1] == 0x81u); // ptypeRepeat
    REQUIRE(readU16(regen.data(), kPageTrailerOffset + 2) == 0x8006u); // wSig
    REQUIRE(readU32(regen.data(), kPageTrailerOffset + 4) == 0x02E8B164u); // dwCRC
    REQUIRE(readU64(regen.data(), kPageTrailerOffset + 8) == 0x206u); // bid

    // BTPAGE control bytes:
    REQUIRE(regen[kBtPageCEnt]    == 3u);
    REQUIRE(regen[kBtPageCEntMax] == 0x14u);
    REQUIRE(regen[kBtPageCbEnt]   == 0x18u);
    REQUIRE(regen[kBtPageCLevel]  == 1u);
}

// ============================================================================
// Test (b): reversed-input ordering test
// ============================================================================
//
// Catches the "writer is a no-op pass-through" failure mode: if the
// caller pre-sorts entries (as the sec 3.3 sample does) AND the writer
// doesn't sort internally, a sort-test passes spuriously. Feeding entries
// in REVERSE order forces the writer to sort.
TEST_CASE("BTPAGE writer sorts intermediate entries by btkey ascending",
          "[block][bt][m5][reversed_input]")
{
    BtEntry entries[3];
    // Reversed btkey order (largest first):
    entries[0] = BtEntry{0x8022u,  Bref{Bid{0xFDu},  Ib{0x8400u}}};
    entries[1] = BtEntry{0x60Fu,   Bref{Bid{0x141u}, Ib{0x7000u}}};
    entries[2] = BtEntry{0x21u,    Bref{Bid{0x205u}, Ib{0x7E00u}}};

    const auto regen = buildBtIntermediate(
        entries, 3,
        /*cLevel*/ 1u,
        /*ptype */ ptype::kNBT,
        /*bid   */ Bid{0x206u},
        /*ib    */ Ib{0x8200u});

    // After sorting, BTENTRY[0] btkey == 0x21 (smallest), [1] = 0x60F, [2] = 0x8022.
    REQUIRE(readU64(regen.data(), 0  + 0) == 0x21u);
    REQUIRE(readU64(regen.data(), 24 + 0) == 0x60Fu);
    REQUIRE(readU64(regen.data(), 48 + 0) == 0x8022u);

    // BREFs travel with their btkeys (each BTENTRY is treated atomically by sort).
    REQUIRE(readU64(regen.data(), 0  + 8)  == 0x205u);
    REQUIRE(readU64(regen.data(), 0  + 16) == 0x7E00u);
    REQUIRE(readU64(regen.data(), 24 + 8)  == 0x141u);
    REQUIRE(readU64(regen.data(), 24 + 16) == 0x7000u);
    REQUIRE(readU64(regen.data(), 48 + 8)  == 0xFDu);
    REQUIRE(readU64(regen.data(), 48 + 16) == 0x8400u);

    // Re-call with same logical content in already-sorted order:
    BtEntry sorted[3];
    sorted[0] = BtEntry{0x21u,    Bref{Bid{0x205u}, Ib{0x7E00u}}};
    sorted[1] = BtEntry{0x60Fu,   Bref{Bid{0x141u}, Ib{0x7000u}}};
    sorted[2] = BtEntry{0x8022u,  Bref{Bid{0xFDu},  Ib{0x8400u}}};
    const auto regen2 = buildBtIntermediate(
        sorted, 3, 1u, ptype::kNBT, Bid{0x206u}, Ib{0x8200u});

    // Outputs MUST be byte-identical (sort is order-independent).
    for (size_t i = 0; i < kPageSize; ++i) {
        REQUIRE(regen[i] == regen2[i]);
    }
}

TEST_CASE("BTPAGE writer sorts NBT leaf entries by NID ascending",
          "[block][nbt][m5][reversed_input]")
{
    NbtEntry entries[5];
    // Reversed NID order:
    entries[0] = NbtEntry{Nid{0x500u}, Bid{0x40u}, Bid{0u}, Nid{0u}};
    entries[1] = NbtEntry{Nid{0x400u}, Bid{0x30u}, Bid{0u}, Nid{0u}};
    entries[2] = NbtEntry{Nid{0x300u}, Bid{0x20u}, Bid{0u}, Nid{0u}};
    entries[3] = NbtEntry{Nid{0x200u}, Bid{0x10u}, Bid{0u}, Nid{0u}};
    entries[4] = NbtEntry{Nid{0x100u}, Bid{0x08u}, Bid{0u}, Nid{0u}};

    const auto regen = buildNbtLeaf(entries, 5, Bid{0x100u}, Ib{0x10000u});

    // After sort, ascending: 0x100, 0x200, 0x300, 0x400, 0x500.
    REQUIRE(readU32(regen.data(), 0   + 0) == 0x100u);
    REQUIRE(readU32(regen.data(), 32  + 0) == 0x200u);
    REQUIRE(readU32(regen.data(), 64  + 0) == 0x300u);
    REQUIRE(readU32(regen.data(), 96  + 0) == 0x400u);
    REQUIRE(readU32(regen.data(), 128 + 0) == 0x500u);

    // bidData paired correctly with each NID after sort.
    REQUIRE(readU64(regen.data(), 0   + 8) == 0x08u);
    REQUIRE(readU64(regen.data(), 32  + 8) == 0x10u);
    REQUIRE(readU64(regen.data(), 64  + 8) == 0x20u);
    REQUIRE(readU64(regen.data(), 96  + 8) == 0x30u);
    REQUIRE(readU64(regen.data(), 128 + 8) == 0x40u);
}

// ============================================================================
// Test (c): NBT vs BBT shared format
// ============================================================================
TEST_CASE("BTPAGE intermediate NBT vs BBT differs only in ptype byte",
          "[block][bt][m5][shared_format]")
{
    BtEntry entries[3];
    entries[0] = BtEntry{0x21u,    Bref{Bid{0x205u}, Ib{0x7E00u}}};
    entries[1] = BtEntry{0x60Fu,   Bref{Bid{0x141u}, Ib{0x7000u}}};
    entries[2] = BtEntry{0x8022u,  Bref{Bid{0xFDu},  Ib{0x8400u}}};

    const auto nbt = buildBtIntermediate(
        entries, 3, 1u, ptype::kNBT, Bid{0x206u}, Ib{0x8200u});
    const auto bbt = buildBtIntermediate(
        entries, 3, 1u, ptype::kBBT, Bid{0x206u}, Ib{0x8200u});

    // Differences must be confined to:
    //   - ptype byte at kPageTrailerOffset + 0
    //   - ptypeRepeat byte at kPageTrailerOffset + 1
    //   - dwCRC at kPageTrailerOffset + 4..7 (because dwCRC is computed
    //     over bytes [0..496); ptype lives at [496..497], so dwCRC
    //     itself doesn't change. But we recompute to be safe.)
    //
    // Actually since dwCRC scope is bytes [0..496) and ptype is at [496..],
    // dwCRC should be byte-identical across NBT and BBT. Verify.
    const size_t ptypeOff       = kPageTrailerOffset + 0;
    const size_t ptypeRepeatOff = kPageTrailerOffset + 1;

    REQUIRE(nbt[ptypeOff]       == 0x81u);
    REQUIRE(bbt[ptypeOff]       == 0x80u);
    REQUIRE(nbt[ptypeRepeatOff] == 0x81u);
    REQUIRE(bbt[ptypeRepeatOff] == 0x80u);

    // Every other byte must be identical.
    for (size_t i = 0; i < kPageSize; ++i) {
        if (i == ptypeOff || i == ptypeRepeatOff) continue;
        if (nbt[i] != bbt[i]) {
            INFO("unexpected NBT/BBT divergence at offset 0x"
                 << std::hex << i << ": nbt=0x" << +nbt[i]
                 << " bbt=0x" << +bbt[i]);
            FAIL("NBT and BBT intermediate pages diverge outside ptype byte");
        }
    }

    // Specifically: dwCRC values are byte-identical (since scope ends at 496).
    REQUIRE(readU32(nbt.data(), kPageTrailerOffset + 4)
            == readU32(bbt.data(), kPageTrailerOffset + 4));
}

// ============================================================================
// Test (d): pagination (single-leaf vs multi-leaf with intermediate)
// ============================================================================
TEST_CASE("buildNbtTree single-leaf case (count <= 15)",
          "[m5][nbt][pagination]")
{
    vector<NbtEntry> entries;
    for (uint32_t i = 0; i < 10; ++i) {
        entries.emplace_back(
            Nid(NidType::NormalMessage, i + 1),
            Bid::makeData(i * 4 + 1),
            Bid{0u},
            Nid{0u});
    }

    Bref leafBref{Bid{0x100u}, Ib{0x40000u}};
    NbtTreeInputBids bids{&leafBref, /*intermediateBref*/ Bref{}};

    const auto result = buildNbtTree(entries.data(), entries.size(), bids);

    REQUIRE(result.leafPages.size() == 1u);
    REQUIRE(result.hasIntermediate == false);
    REQUIRE(result.treeLevel == 0u);
    REQUIRE(result.rootBref == leafBref);

    // The single leaf has cEnt=10.
    REQUIRE(result.leafPages[0][kBtPageCEnt] == 10u);
    REQUIRE(result.leafPages[0][kBtPageCLevel] == 0u);
}

TEST_CASE("buildNbtTree multi-leaf case (count > 15) creates intermediate page",
          "[m5][nbt][pagination]")
{
    // 25 entries -> 2 leaves (15 + 10) + 1 intermediate.
    vector<NbtEntry> entries;
    for (uint32_t i = 0; i < 25; ++i) {
        entries.emplace_back(
            Nid(NidType::NormalMessage, i + 1),
            Bid::makeData(i * 4 + 1),
            Bid{0u},
            Nid{0u});
    }

    Bref leafBrefs[2] = {
        Bref{Bid{0x100u}, Ib{0x40000u}},
        Bref{Bid{0x104u}, Ib{0x40200u}},
    };
    Bref intermediateBref{Bid{0x108u}, Ib{0x40400u}};
    NbtTreeInputBids bids{leafBrefs, intermediateBref};

    const auto result = buildNbtTree(entries.data(), entries.size(), bids);

    REQUIRE(result.leafPages.size() == 2u);
    REQUIRE(result.hasIntermediate == true);
    REQUIRE(result.treeLevel == 1u);
    REQUIRE(result.rootBref == intermediateBref);

    // Leaf 0 holds entries 1..15 (smallest NIDs).
    REQUIRE(result.leafPages[0][kBtPageCEnt]   == 15u);
    REQUIRE(result.leafPages[0][kBtPageCLevel] == 0u);
    // First NBTENTRY in leaf 0 is the smallest NID.
    const Nid expectedFirstLeaf0(NidType::NormalMessage, 1);
    REQUIRE(readU32(result.leafPages[0].data(), 0) == expectedFirstLeaf0.value);

    // Leaf 1 holds entries 16..25 (10 entries).
    REQUIRE(result.leafPages[1][kBtPageCEnt]   == 10u);
    REQUIRE(result.leafPages[1][kBtPageCLevel] == 0u);
    const Nid expectedFirstLeaf1(NidType::NormalMessage, 16);
    REQUIRE(readU32(result.leafPages[1].data(), 0) == expectedFirstLeaf1.value);

    // Intermediate page references both leaves.
    REQUIRE(result.intermediatePage[kBtPageCEnt]   == 2u);
    REQUIRE(result.intermediatePage[kBtPageCLevel] == 1u);
    REQUIRE(result.intermediatePage[kBtPageCbEnt]  == 24u);
    REQUIRE(result.intermediatePage[kPageTrailerOffset] == ptype::kNBT);

    // BTENTRY[0]: btkey = smallest NID in leaf 0 (= NID 0x21 for nidIdx=1, type=NormalMessage)
    //             bref  = leafBrefs[0]
    REQUIRE(readU64(result.intermediatePage.data(), 0 + 0) == expectedFirstLeaf0.value);
    REQUIRE(readU64(result.intermediatePage.data(), 0 + 8) == leafBrefs[0].bid.value);
    REQUIRE(readU64(result.intermediatePage.data(), 0 + 16) == leafBrefs[0].ib.value);

    // BTENTRY[1]: btkey = smallest NID in leaf 1, bref = leafBrefs[1].
    REQUIRE(readU64(result.intermediatePage.data(), 24 + 0) == expectedFirstLeaf1.value);
    REQUIRE(readU64(result.intermediatePage.data(), 24 + 8) == leafBrefs[1].bid.value);
    REQUIRE(readU64(result.intermediatePage.data(), 24 + 16) == leafBrefs[1].ib.value);
}

TEST_CASE("buildNbtTree throws when entry count exceeds tree-depth-1 capacity",
          "[m5][nbt][pagination][negative]")
{
    // kBtIntermediateMaxEntries (20) leaves * kNbtLeafMaxEntries (15) =
    // 300 entries fit in a 1-level tree. 301 is the smallest count that
    // requires depth 2 (M7+).
    vector<NbtEntry> entries(301);
    for (uint32_t i = 0; i < 301; ++i) {
        entries[i] = NbtEntry{
            Nid(NidType::NormalMessage, i + 1),
            Bid::makeData(i * 4 + 1),
            Bid{0u}, Nid{0u}};
    }

    // We don't even need to supply real BIDs; the size check fires first.
    Bref dummyLeaves[21] = {};
    NbtTreeInputBids bids{dummyLeaves, Bref{}};

    REQUIRE_THROWS_AS(
        buildNbtTree(entries.data(), entries.size(), bids),
        runtime_error);
}

TEST_CASE("buildNbtLeaf throws when count > 15",
          "[m5][nbt][pagination][negative]")
{
    vector<NbtEntry> entries(16);
    for (uint32_t i = 0; i < 16; ++i) {
        entries[i] = NbtEntry{
            Nid(NidType::NormalMessage, i + 1),
            Bid::makeData(i * 4 + 1),
            Bid{0u}, Nid{0u}};
    }
    REQUIRE_THROWS_AS(
        buildNbtLeaf(entries.data(), 16, Bid{0x100u}, Ib{0x40000u}),
        runtime_error);
}

TEST_CASE("buildBtIntermediate throws on cLevel=0 or invalid ptype",
          "[m5][bt][pagination][negative]")
{
    BtEntry entries[1];
    entries[0] = BtEntry{0x21u, Bref{Bid{0x205u}, Ib{0x7E00u}}};

    // cLevel=0 is for leaf pages, not intermediate.
    REQUIRE_THROWS_AS(
        buildBtIntermediate(entries, 1, /*cLevel*/ 0u, ptype::kNBT,
                            Bid{0x206u}, Ib{0x8200u}),
        runtime_error);

    // ptype must be NBT (0x81) or BBT (0x80); AMap (0x84) is invalid here.
    REQUIRE_THROWS_AS(
        buildBtIntermediate(entries, 1, 1u, ptype::kAMap,
                            Bid{0x206u}, Ib{0x8200u}),
        runtime_error);
}

// ============================================================================
// Test (e): empty-page semantics
// ============================================================================
//
// DECISION: count == 0 PRODUCES a valid empty page (does NOT reject).
// Rationale: M3 already supports empty pages via buildEmptyNbtLeaf /
// buildEmptyBbtLeaf for the M2 5-page-skeleton case. M5's filled writers
// unify the two paths -- count == 0 is the empty case. Rejecting would
// force callers to special-case zero-entry constructions during PST
// initialization.
TEST_CASE("buildNbtLeaf with count=0 produces valid empty page",
          "[m5][nbt][empty]")
{
    const auto page = buildNbtLeaf(nullptr, 0, Bid{0x100u}, Ib{0x40000u});

    REQUIRE(page[kBtPageCEnt]    == 0u);
    REQUIRE(page[kBtPageCEntMax] == 15u);
    REQUIRE(page[kBtPageCbEnt]   == 32u);
    REQUIRE(page[kBtPageCLevel]  == 0u);
    REQUIRE(readU32(page.data(), kBtPageDwPad) == 0u);

    // rgentries area must be zero-filled.
    for (size_t i = 0; i < kBtPageEntriesArea; ++i) {
        REQUIRE(page[i] == 0u);
    }

    // Trailer fields.
    REQUIRE(page[kPageTrailerOffset]     == ptype::kNBT);
    REQUIRE(page[kPageTrailerOffset + 1] == ptype::kNBT);
    REQUIRE(readU64(page.data(), kPageTrailerOffset + 8) == 0x100u);

    // CRC is self-consistent.
    REQUIRE(readU32(page.data(), kPageTrailerOffset + 4)
            == crc32(page.data(), kPageBodySize));
}

TEST_CASE("buildBtIntermediate with count=0 produces valid empty page",
          "[m5][bt][empty]")
{
    const auto page = buildBtIntermediate(nullptr, 0,
        /*cLevel*/ 1u, ptype::kBBT, Bid{0x100u}, Ib{0x40000u});

    REQUIRE(page[kBtPageCEnt]    == 0u);
    REQUIRE(page[kBtPageCEntMax] == 20u);
    REQUIRE(page[kBtPageCbEnt]   == 24u);
    REQUIRE(page[kBtPageCLevel]  == 1u);
    REQUIRE(page[kPageTrailerOffset] == ptype::kBBT);

    // CRC is self-consistent.
    REQUIRE(readU32(page.data(), kPageTrailerOffset + 4)
            == crc32(page.data(), kPageBodySize));
}

TEST_CASE("buildNbtTree with count=0 produces single empty leaf",
          "[m5][nbt][pagination][empty]")
{
    Bref leafBref{Bid{0x100u}, Ib{0x40000u}};
    NbtTreeInputBids bids{&leafBref, Bref{}};

    const auto result = buildNbtTree(nullptr, 0, bids);

    REQUIRE(result.leafPages.size() == 1u);
    REQUIRE(result.hasIntermediate == false);
    REQUIRE(result.treeLevel == 0u);
    REQUIRE(result.leafPages[0][kBtPageCEnt] == 0u);
}

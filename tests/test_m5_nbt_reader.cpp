// pstwriter/tests/test_m5_nbt_reader.cpp
//
// Phase C - NBT reader gate tests.
//
// (a) Round-trip: build NBT from M5Allocator-produced NIDs via Phase B
//     writer, walk back via nbtFind() / nbtForEach(), every NID
//     resolves with correct bidData/bidSub/nidParent.
// (b) CRITICAL non-monotonic NID layout (equivalent of sec 3.9
//     cross-validation for the PC HID-agnostic-reader contract):
//     build an NBT where NIDs do NOT follow M5Allocator's natural
//     sequence, verify reader resolves all NIDs.
// (c) Pagination crossing: 25 entries across 2 leaves + 1 intermediate;
//     reader descends correctly through the intermediate.
// (d) Spec invariant rejection: malformed BTPAGEs throw.

#include <catch2/catch_test_macros.hpp>

#include "m5_allocator.hpp"
#include "nbt.hpp"
#include "ndb.hpp"
#include "page.hpp"
#include "types.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace std;
using namespace pstwriter;

namespace {

// Build a minimal in-memory PST byte buffer: sized to fit pages at
// arbitrary IBs. Caller writes pages at chosen IBs; reader treats it
// as a flat byte array.
struct InMemoryPstBuf {
    vector<uint8_t> bytes;

    explicit InMemoryPstBuf(size_t size) : bytes(size, 0) {}

    void writePage(uint64_t ib, const array<uint8_t, kPageSize>& page)
    {
        REQUIRE(ib + kPageSize <= bytes.size());
        std::memcpy(bytes.data() + ib, page.data(), kPageSize);
    }
};

} // namespace

// ============================================================================
// Test (a): round-trip
// ============================================================================
TEST_CASE("NBT reader resolves every NID written by Phase B writer (single leaf)",
          "[m5][nbt_reader][round_trip]")
{
    M5Allocator alloc;

    // Allocate 5 distinct NIDs across multiple nidTypes.
    const Nid nids[] = {
        alloc.allocate(NidType::NormalFolder),  // idx 1 -> 0x22
        alloc.allocate(NidType::NormalMessage), // idx 1 -> 0x24
        alloc.allocate(NidType::Attachment),    // idx 1 -> 0x25
        alloc.allocate(NidType::Internal),      // idx 2 -> 0x41 (idx 1 reserved)
        alloc.allocate(NidType::NormalMessage), // idx 2 -> 0x44
    };
    constexpr size_t kCount = sizeof(nids) / sizeof(nids[0]);

    vector<NbtEntry> entries;
    for (size_t i = 0; i < kCount; ++i) {
        entries.emplace_back(
            nids[i],
            Bid::makeData(i + 1),
            Bid{0u},
            Nid{0u});
    }

    const uint64_t leafIb = 0x10000;
    const Bid      leafBid{0x100u};
    const Bref     leafBref{leafBid, Ib{leafIb}};
    const auto leafPage = buildNbtLeaf(entries.data(), entries.size(),
                                       leafBid, Ib{leafIb});

    InMemoryPstBuf buf(0x20000);
    buf.writePage(leafIb, leafPage);

    // Round-trip every allocated NID.
    for (size_t i = 0; i < kCount; ++i) {
        NbtRecord rec;
        const bool found = nbtFind(buf.bytes.data(), buf.bytes.size(),
                                   leafBref, nids[i], &rec);
        INFO("round-trip NID 0x" << std::hex << nids[i].value);
        REQUIRE(found);
        REQUIRE(rec.nid.value     == nids[i].value);
        REQUIRE(rec.bidData.value == Bid::makeData(i + 1).value);
        REQUIRE(rec.bidSub.value  == 0u);
        REQUIRE(rec.nidParent.value == 0u);
    }

    // Look up an unallocated NID -- not found, no throw.
    NbtRecord rec;
    REQUIRE_FALSE(nbtFind(buf.bytes.data(), buf.bytes.size(),
                          leafBref,
                          Nid(NidType::NormalMessage, 999),
                          &rec));

    // Enumerate -- must return all 5 in NID-ascending order.
    vector<NbtRecord> all;
    nbtForEach(buf.bytes.data(), buf.bytes.size(), leafBref, all);
    REQUIRE(all.size() == kCount);
    for (size_t i = 1; i < all.size(); ++i) {
        REQUIRE(all[i - 1].nid.value < all[i].nid.value);
    }
}

// ============================================================================
// Test (b): non-monotonic NID layout (load-bearing)
// ============================================================================
//
// This is the equivalent of the M4 sec 3.9 cross-validation for the PC
// HID-agnostic reader. We don't have a published spec sample with
// non-monotonic NIDs to feed the reader, so we synthesize one: build
// an NBT with NIDs that DO NOT follow M5Allocator's tidy +0x20
// sequence. Mix in pre-registered NIDs that create gaps; verify the
// reader resolves every NID by binary-search descent rather than by
// reconstructing any allocator pattern.
TEST_CASE("NBT reader resolves NIDs with non-monotonic, non-allocator-derived layout",
          "[m5][nbt_reader][non_monotonic_nids]")
{
    // Construct entries with NIDs that the M5Allocator would never
    // produce in this order:
    //   - 0x21      (NID_MESSAGE_STORE; reserved)
    //   - 0x100u    (large Internal nidIndex, stride > 0x20)
    //   - 0x40      (Internal idx=2, would be allocator's first Internal)
    //   - 0x800     (huge nidIndex)
    //   - 0x122     (NID_ROOT_FOLDER; reserved, NORMAL_FOLDER nidType)
    //   - 0x60u     (NormalMessage idx=3)
    //   - 0x205     (Attachment huge idx)
    //
    // The writer sorts internally so the leaf will hold them in NID
    // ascending order: 0x21, 0x40, 0x60, 0x100, 0x122, 0x205, 0x800.
    // Gaps: 0x40-0x21=31 (not 0x20), 0x60-0x40=32, 0x100-0x60=160,
    // 0x122-0x100=34, 0x205-0x122=227, 0x800-0x205=1531.
    // No regular stride. Reader must NOT assume one.
    const Nid testNids[] = {
        Nid{0x21u},     // MessageStore
        Nid{0x100u},
        Nid{0x40u},
        Nid{0x800u},
        Nid{0x122u},    // RootFolder
        Nid{0x60u},
        Nid{0x205u},
    };
    constexpr size_t kCount = sizeof(testNids) / sizeof(testNids[0]);

    vector<NbtEntry> entries;
    for (size_t i = 0; i < kCount; ++i) {
        entries.emplace_back(
            testNids[i],
            Bid::makeData(i + 1),
            Bid{0u},
            Nid{0u});
    }

    const uint64_t leafIb = 0x10000;
    const Bid leafBid{0x100u};
    const Bref leafBref{leafBid, Ib{leafIb}};
    const auto leafPage = buildNbtLeaf(entries.data(), entries.size(),
                                       leafBid, Ib{leafIb});

    InMemoryPstBuf buf(0x20000);
    buf.writePage(leafIb, leafPage);

    // CRITICAL: every NID must resolve, regardless of stride/contiguity.
    for (size_t i = 0; i < kCount; ++i) {
        NbtRecord rec;
        INFO("non-monotonic NID 0x" << std::hex << testNids[i].value);
        REQUIRE(nbtFind(buf.bytes.data(), buf.bytes.size(),
                        leafBref, testNids[i], &rec));
        REQUIRE(rec.nid.value == testNids[i].value);
        REQUIRE(rec.bidData.value == Bid::makeData(i + 1).value);
    }

    // Look-ups for NIDs that fall INSIDE the gaps must return false
    // (not-found), not throw.
    NbtRecord rec;
    REQUIRE_FALSE(nbtFind(buf.bytes.data(), buf.bytes.size(),
                          leafBref, Nid{0x50u}, &rec));    // gap 0x40..0x60
    REQUIRE_FALSE(nbtFind(buf.bytes.data(), buf.bytes.size(),
                          leafBref, Nid{0x300u}, &rec));   // gap 0x205..0x800
    REQUIRE_FALSE(nbtFind(buf.bytes.data(), buf.bytes.size(),
                          leafBref, Nid{0x1000u}, &rec));  // beyond largest

    // Enumeration order is NID-ascending sorted (writer's responsibility),
    // but the reader doesn't impose any specific stride.
    vector<NbtRecord> all;
    nbtForEach(buf.bytes.data(), buf.bytes.size(), leafBref, all);
    REQUIRE(all.size() == kCount);
    REQUIRE(all[0].nid.value == 0x21u);
    REQUIRE(all[1].nid.value == 0x40u);
    REQUIRE(all[2].nid.value == 0x60u);
    REQUIRE(all[3].nid.value == 0x100u);
    REQUIRE(all[4].nid.value == 0x122u);
    REQUIRE(all[5].nid.value == 0x205u);
    REQUIRE(all[6].nid.value == 0x800u);
}

// ============================================================================
// Test (c): pagination crossing
// ============================================================================
TEST_CASE("NBT reader descends through intermediate to resolve NIDs in different leaves",
          "[m5][nbt_reader][pagination]")
{
    // 25 entries -> 2 leaves (15 + 10) + 1 intermediate.
    M5Allocator alloc;
    vector<NbtEntry> entries;
    vector<Nid> allocated;
    for (uint32_t i = 0; i < 25; ++i) {
        const Nid n = alloc.allocate(NidType::NormalMessage);
        allocated.push_back(n);
        entries.emplace_back(n, Bid::makeData(i + 1), Bid{0u}, Nid{0u});
    }

    InMemoryPstBuf buf(0x100000);
    Bref leafBrefs[2] = {
        Bref{Bid{0x100u}, Ib{0x40000u}},
        Bref{Bid{0x104u}, Ib{0x40200u}},
    };
    Bref intermediateBref{Bid{0x108u}, Ib{0x40400u}};
    NbtTreeInputBids bids{leafBrefs, intermediateBref};
    const auto tree = buildNbtTree(entries.data(), entries.size(), bids);

    REQUIRE(tree.hasIntermediate);
    buf.writePage(leafBrefs[0].ib.value, tree.leafPages[0]);
    buf.writePage(leafBrefs[1].ib.value, tree.leafPages[1]);
    buf.writePage(intermediateBref.ib.value, tree.intermediatePage);

    // Walk from intermediate; every allocated NID resolves.
    for (size_t i = 0; i < allocated.size(); ++i) {
        NbtRecord rec;
        REQUIRE(nbtFind(buf.bytes.data(), buf.bytes.size(),
                        tree.rootBref, allocated[i], &rec));
        REQUIRE(rec.nid.value == allocated[i].value);
        REQUIRE(rec.bidData.value == Bid::makeData(i + 1).value);
    }

    // Cross-leaf check: NID #1 lives in leaf 0, NID #16 lives in
    // leaf 1, NID #25 also in leaf 1. The reader must pick the right
    // child via the intermediate's BTENTRY.btkey comparison.
    {
        NbtRecord rec;
        REQUIRE(nbtFind(buf.bytes.data(), buf.bytes.size(),
                        tree.rootBref, allocated[0], &rec));
        REQUIRE(rec.bidData.value == Bid::makeData(1).value);
    }
    {
        NbtRecord rec;
        REQUIRE(nbtFind(buf.bytes.data(), buf.bytes.size(),
                        tree.rootBref, allocated[15], &rec));
        REQUIRE(rec.bidData.value == Bid::makeData(16).value);
    }

    // Enumerate all -- must yield all 25, NID-ascending across leaves.
    vector<NbtRecord> all;
    nbtForEach(buf.bytes.data(), buf.bytes.size(), tree.rootBref, all);
    REQUIRE(all.size() == 25u);
    for (size_t i = 1; i < all.size(); ++i) {
        REQUIRE(all[i - 1].nid.value < all[i].nid.value);
    }
}

// ============================================================================
// Test (d): spec invariant rejection
// ============================================================================
TEST_CASE("NBT reader rejects malformed BTPAGE on bad ptype",
          "[m5][nbt_reader][negative]")
{
    NbtEntry e{Nid(NidType::NormalMessage, 1), Bid::makeData(1), Bid{0u}, Nid{0u}};
    auto page = buildNbtLeaf(&e, 1, Bid{0x100u}, Ib{0x10000u});
    page[kPageTrailerOffset + 0] = ptype::kBBT; // corrupt to BBT

    InMemoryPstBuf buf(0x20000);
    buf.writePage(0x10000, page);

    NbtRecord rec;
    REQUIRE_THROWS_AS(
        nbtFind(buf.bytes.data(), buf.bytes.size(),
                Bref{Bid{0x100u}, Ib{0x10000u}}, Nid{0x24u}, &rec),
        runtime_error);
}

TEST_CASE("NBT reader rejects malformed BTPAGE on ptype/ptypeRepeat mismatch",
          "[m5][nbt_reader][negative]")
{
    NbtEntry e{Nid(NidType::NormalMessage, 1), Bid::makeData(1), Bid{0u}, Nid{0u}};
    auto page = buildNbtLeaf(&e, 1, Bid{0x100u}, Ib{0x10000u});
    page[kPageTrailerOffset + 1] ^= 0xFFu; // corrupt ptypeRepeat

    InMemoryPstBuf buf(0x20000);
    buf.writePage(0x10000, page);

    NbtRecord rec;
    REQUIRE_THROWS_AS(
        nbtFind(buf.bytes.data(), buf.bytes.size(),
                Bref{Bid{0x100u}, Ib{0x10000u}}, Nid{0x24u}, &rec),
        runtime_error);
}

TEST_CASE("NBT reader rejects BTPAGE with cEnt > cEntMax",
          "[m5][nbt_reader][negative]")
{
    NbtEntry e{Nid(NidType::NormalMessage, 1), Bid::makeData(1), Bid{0u}, Nid{0u}};
    auto page = buildNbtLeaf(&e, 1, Bid{0x100u}, Ib{0x10000u});
    page[kBtPageCEnt] = 99u; // > cEntMax (15)
    // dwCRC will mismatch; pass strictCrc=false to isolate the cEnt check.

    InMemoryPstBuf buf(0x20000);
    buf.writePage(0x10000, page);

    NbtReadOptions opts;
    opts.strictCrc = false;

    NbtRecord rec;
    REQUIRE_THROWS_AS(
        nbtFind(buf.bytes.data(), buf.bytes.size(),
                Bref{Bid{0x100u}, Ib{0x10000u}}, Nid{0x24u}, &rec, opts),
        runtime_error);
}

TEST_CASE("NBT reader rejects BTPAGE with wrong cbEnt for cLevel",
          "[m5][nbt_reader][negative]")
{
    NbtEntry e{Nid(NidType::NormalMessage, 1), Bid::makeData(1), Bid{0u}, Nid{0u}};
    auto page = buildNbtLeaf(&e, 1, Bid{0x100u}, Ib{0x10000u});
    page[kBtPageCbEnt] = 24u; // wrong for leaf (should be 32)

    InMemoryPstBuf buf(0x20000);
    buf.writePage(0x10000, page);

    NbtReadOptions opts;
    opts.strictCrc = false;

    NbtRecord rec;
    REQUIRE_THROWS_AS(
        nbtFind(buf.bytes.data(), buf.bytes.size(),
                Bref{Bid{0x100u}, Ib{0x10000u}}, Nid{0x24u}, &rec, opts),
        runtime_error);
}

TEST_CASE("NBT reader rejects BTPAGE with cLevel > 1 (M5 cap)",
          "[m5][nbt_reader][negative]")
{
    BtEntry e{0x21u, Bref{Bid{0x100u}, Ib{0x40000u}}};
    auto page = buildBtIntermediate(&e, 1, /*cLevel*/ 1u, ptype::kNBT,
                                    Bid{0x100u}, Ib{0x10000u});
    page[kBtPageCLevel] = 2u; // M5 cap is 1

    InMemoryPstBuf buf(0x80000);
    buf.writePage(0x10000, page);

    NbtReadOptions opts;
    opts.strictCrc = false;

    NbtRecord rec;
    REQUIRE_THROWS_AS(
        nbtFind(buf.bytes.data(), buf.bytes.size(),
                Bref{Bid{0x100u}, Ib{0x10000u}}, Nid{0x24u}, &rec, opts),
        runtime_error);
}

TEST_CASE("NBT reader rejects when dwCRC is corrupted (strict mode)",
          "[m5][nbt_reader][negative]")
{
    NbtEntry e{Nid(NidType::NormalMessage, 1), Bid::makeData(1), Bid{0u}, Nid{0u}};
    auto page = buildNbtLeaf(&e, 1, Bid{0x100u}, Ib{0x10000u});
    // Corrupt one body byte WITHOUT updating dwCRC.
    page[100] ^= 0xFFu;

    InMemoryPstBuf buf(0x20000);
    buf.writePage(0x10000, page);

    NbtRecord rec;
    REQUIRE_THROWS_AS(
        nbtFind(buf.bytes.data(), buf.bytes.size(),
                Bref{Bid{0x100u}, Ib{0x10000u}}, Nid{0x24u}, &rec),
        runtime_error);

    // With strictCrc=false, the same call should succeed (or return false
    // without throwing on CRC).
    NbtReadOptions opts;
    opts.strictCrc = false;
    // Any throw now is from a different invariant; we don't assert
    // success here -- just that the throw-on-CRC path is gated by the option.
    // (For this test, the corrupted byte is in NBTENTRY data which doesn't
    // affect the structural check, so the call should succeed.)
    REQUIRE_NOTHROW(
        nbtFind(buf.bytes.data(), buf.bytes.size(),
                Bref{Bid{0x100u}, Ib{0x10000u}}, Nid{0x24u}, &rec, opts));
}

TEST_CASE("NBT reader rejects when page IB extends past EOF",
          "[m5][nbt_reader][negative]")
{
    InMemoryPstBuf buf(kPageSize); // exactly 1 page
    NbtRecord rec;
    REQUIRE_THROWS_AS(
        nbtFind(buf.bytes.data(), buf.bytes.size(),
                Bref{Bid{0x100u}, Ib{kPageSize}}, // IB = end of file
                Nid{0x24u}, &rec),
        runtime_error);
}

// pstwriter/tests/test_types.cpp
//
// Core PST scalar types ([MS-PST] §2.2.2.1 – §2.2.2.4) — gate tests for M1.

#include <catch2/catch_test_macros.hpp>

#include "types.hpp"

#include <cstdint>
#include <type_traits>

using namespace std;
using namespace pstwriter;

// ============================================================================
// Sizes & layouts
// ============================================================================
TEST_CASE("Value types occupy the on-disk size required by [MS-PST]",
          "[types][layout]")
{
    static_assert(sizeof(Nid)      == 4,  "Nid != 4 bytes");
    static_assert(sizeof(Bid)      == 8,  "Bid != 8 bytes");
    static_assert(sizeof(Ib)       == 8,  "Ib != 8 bytes");
    static_assert(sizeof(Bref)     == 16, "BREF != 16 bytes");
    static_assert(sizeof(FileTime) == 8,  "FileTime != 8 bytes");
    static_assert(sizeof(PropTag)  == 4,  "PropTag != 4 bytes");
    SUCCEED();
}

// ============================================================================
// NID — bit-packing
// ============================================================================
TEST_CASE("Nid: type and index round-trip through the bit-packing constructor",
          "[types][nid]")
{
    SECTION("Constructed from (type, index)")
    {
        const Nid n{NidType::NormalFolder, /*idx=*/0x12345u};
        REQUIRE(n.type()  == NidType::NormalFolder);
        REQUIRE(n.index() == 0x12345u);

        // Type is in low 5 bits, index in high 27.
        REQUIRE((n.value & 0x1Fu) == static_cast<uint32_t>(NidType::NormalFolder));
        REQUIRE((n.value >> 5)    == 0x12345u);
    }

    SECTION("Constructed from raw 32-bit value")
    {
        const Nid n{0x00000122u};                  // (0x09 << 5) | 0x02
        REQUIRE(n.type()  == NidType::NormalFolder);
        REQUIRE(n.index() == 0x09u);
    }

    SECTION("Index is masked to 27 bits")
    {
        // High 5 bits of the supplied index must NOT spill into the type field.
        const Nid n{NidType::Internal, /*idx=*/0xFFFFFFFFu};
        REQUIRE(n.type() == NidType::Internal);
        REQUIRE(n.index() == 0x07FFFFFFu);
    }
}

TEST_CASE("Nid: well-known NIDs from [MS-PST] Sec 2.4.8 decode correctly",
          "[types][nid]")
{
    // (raw value, expected NidType, expected index)
    struct Case {
        Nid     n;
        NidType t;
        uint32_t idx;
    };
    const Case cases[] = {
        { kNidMessageStore,           NidType::Internal,     0x01u },
        { kNidNameToIdMap,            NidType::Internal,     0x03u },
        { kNidNormalFolderTemplate,   NidType::Internal,     0x05u },
        { kNidSearchFolderTemplate,   NidType::Internal,     0x06u },
        { kNidRootFolder,             NidType::NormalFolder, 0x09u },
        { kNidSearchManagementQueue,  NidType::Internal,     0x0Fu },
        { kNidSearchActivityList,     NidType::Internal,     0x10u },
        { kNidSpoolerQueue,           NidType::Internal,     0x17u },
    };
    for (const auto& c : cases) {
        REQUIRE(c.n.type()  == c.t);
        REQUIRE(c.n.index() == c.idx);
    }

    // Equality / ordering operators.
    REQUIRE(kNidMessageStore == Nid{0x00000021u});
    REQUIRE(kNidMessageStore != kNidRootFolder);
    REQUIRE(kNidMessageStore <  kNidRootFolder);
}

TEST_CASE("PropTag: ordering is consistent (used by BTH later)",
          "[types][proptag]")
{
    // BTH stores entries sorted by key. PropTag's operator< is a plain
    // value comparison; assert the obvious ordering so a future change
    // to the comparator gets caught here.
    const PropTag a{0x0001u, PropType::Int16};
    const PropTag b{0x3001u, PropType::Unicode};
    const PropTag c{0x3001u, PropType::Unicode};

    REQUIRE(a < b);
    REQUIRE_FALSE(b < a);
    REQUIRE_FALSE(b < c);
    REQUIRE_FALSE(c < b);
    REQUIRE(b == c);
}

// ============================================================================
// BID — flags and counter
// ============================================================================
TEST_CASE("Bid: makeData() yields a data BID with both flags clear",
          "[types][bid]")
{
    const Bid b = Bid::makeData(/*idx=*/0x00ABCDEFull);

    REQUIRE(b.isData());
    REQUIRE_FALSE(b.isInternal());
    REQUIRE(b.index() == 0x00ABCDEFull);
    REQUIRE((b.value & 0x3ull) == 0u);   // bits 0/1 clear
    REQUIRE(b.value == (0x00ABCDEFull << 2));
}

TEST_CASE("Bid: makeInternal() sets bit[1] (and pstwriter also sets bit[0])",
          "[types][bid]")
{
    const Bid b = Bid::makeInternal(/*idx=*/0x42ull);

    REQUIRE(b.isInternal());
    REQUIRE_FALSE(b.isData());
    REQUIRE(b.index() == 0x42ull);

    // Spec only requires bit[1] = 1; pstwriter additionally sets bit[0]
    // to match Outlook-produced files.
    REQUIRE((b.value & 0x2ull) == 0x2ull);
    REQUIRE((b.value & 0x3ull) == 0x3ull);
}

TEST_CASE("Bid: counter increments by 4 (because flags occupy bottom two bits)",
          "[types][bid]")
{
    // Explicit values: makeData(0).value == 0, makeData(1).value == 4.
    REQUIRE(Bid::makeData(0ull).value == 0ull);
    REQUIRE(Bid::makeData(1ull).value == 4ull);
    REQUIRE(Bid::makeData(2ull).value == 8ull);

    const Bid a = Bid::makeData(1ull);
    const Bid b = Bid::makeData(2ull);
    REQUIRE(b.value - a.value == 4ull);

    const Bid c = Bid::makeInternal(1ull);
    const Bid d = Bid::makeInternal(2ull);
    REQUIRE(d.value - c.value == 4ull);
}

TEST_CASE("Bid: kBidNil is the all-zero BID", "[types][bid]")
{
    REQUIRE(kBidNil.value == 0ull);
    REQUIRE(kBidNil.isData());     // bit[1] is zero
}

// ============================================================================
// BREF
// ============================================================================
TEST_CASE("Bref: composes a BID with an IB", "[types][bref]")
{
    const Bid bid = Bid::makeData(0x10ull);
    const Ib  ib  { 0x600ull };
    const Bref ref{bid, ib};

    REQUIRE(ref.bid == bid);
    REQUIRE(ref.ib  == ib);
    REQUIRE(ref == Bref{bid, ib});
}

// ============================================================================
// PropTag — id and type packing
// ============================================================================
TEST_CASE("PropTag: id and type round-trip through the constructor",
          "[types][proptag]")
{
    const PropTag tag{0x3001u, PropType::Unicode};
    REQUIRE(tag.id()    == 0x3001u);
    REQUIRE(tag.type()  == PropType::Unicode);
    REQUIRE(tag.value   == ((0x3001u << 16) | 0x001Fu));

    REQUIRE(pid::DisplayName.id()   == 0x3001u);
    REQUIRE(pid::DisplayName.type() == PropType::Unicode);

    REQUIRE(pid::CreationTime.type()         == PropType::SystemTime);
    REQUIRE(pid::MessageDeliveryTime.type()  == PropType::SystemTime);
    REQUIRE(pid::Subject.type()              == PropType::Unicode);
    REQUIRE(pid::Body.type()                 == PropType::Unicode);
}

// ============================================================================
// detail::writeU* / readU* — little-endian round-trips
// ============================================================================
TEST_CASE("detail::write/read helpers emit and parse little-endian integers",
          "[types][endian]")
{
    using namespace pstwriter::detail;

    uint8_t buf[16] = {0};

    SECTION("U16")
    {
        writeU16(buf, 0, 0x1234u);
        REQUIRE(buf[0] == 0x34);   // low byte first
        REQUIRE(buf[1] == 0x12);
        REQUIRE(readU16(buf, 0) == 0x1234u);
    }

    SECTION("U32")
    {
        writeU32(buf, 0, 0xDEADBEEFu);
        REQUIRE(buf[0] == 0xEF);
        REQUIRE(buf[1] == 0xBE);
        REQUIRE(buf[2] == 0xAD);
        REQUIRE(buf[3] == 0xDE);
        REQUIRE(readU32(buf, 0) == 0xDEADBEEFu);
    }

    SECTION("U64")
    {
        writeU64(buf, 0, 0x0123456789ABCDEFull);
        REQUIRE(buf[0] == 0xEF);
        REQUIRE(buf[7] == 0x01);
        REQUIRE(readU64(buf, 0) == 0x0123456789ABCDEFull);
    }
}

// ============================================================================
// Trivially-copyable check (so the writer can serialize via the helpers
// without worrying about non-trivial member functions blocking memcpy use
// where it *is* legitimate, e.g. zero-init).
// ============================================================================
TEST_CASE("Value types are trivially copyable", "[types][traits]")
{
    // Using the ::value form rather than the C++17 _v inline-variable
    // helpers, since some C++17-mode standard libraries (libstdc++ 6.x)
    // don't ship the _v aliases yet.
    static_assert(is_trivially_copyable<Nid>::value,      "Nid must be trivially copyable");
    static_assert(is_trivially_copyable<Bid>::value,      "Bid must be trivially copyable");
    static_assert(is_trivially_copyable<Ib>::value,       "Ib must be trivially copyable");
    static_assert(is_trivially_copyable<Bref>::value,     "Bref must be trivially copyable");
    static_assert(is_trivially_copyable<FileTime>::value, "FileTime must be trivially copyable");
    static_assert(is_trivially_copyable<PropTag>::value,  "PropTag must be trivially copyable");
    SUCCEED();
}

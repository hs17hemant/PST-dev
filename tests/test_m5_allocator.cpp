// pstwriter/tests/test_m5_allocator.cpp
//
// Phase A — M5Allocator gate tests.
//
// Validates the NID assignment service:
//   * Reserved NID enumeration matches [SPEC sec 2.4.1] verbatim.
//   * Per-nidType counter seeding: Internal at idx=1, every other
//     user-allocatable type at idx=0x400 (per [MS-PST] §2.4.3 low-
//     index reserved range, matching real Outlook / Aspose).
//   * Counter advances by 1, skips reserved + pre-registered NIDs.
//   * Cross-nidType independence (NormalFolder counter unaffected by
//     NormalMessage allocations).
//   * registerExternal pre-reserves NIDs; subsequent allocate(...) skips.
//   * Reserved-NID rejection: allocator never auto-allocates a reserved NID.
//   * Determinism: two allocator instances with identical input produce
//     byte-equal NID streams.

#include <catch2/catch_test_macros.hpp>

#include "m5_allocator.hpp"
#include "types.hpp"

#include <cstdint>
#include <set>
#include <stdexcept>
#include <vector>

using namespace std;
using namespace pstwriter;

// ============================================================================
// [SPEC sec 2.4.1] reserved-NID enumeration (verified verbatim 2026-05-02)
// ============================================================================
TEST_CASE("M5Allocator reserved NID enumeration matches [SPEC sec 2.4.1] verbatim",
          "[m5][allocator][spec_2_4_1]")
{
    // Spec table top-to-bottom. Each row is { NID, expected nidType, expected nidIndex }.
    struct ExpectedReserved {
        uint32_t nid;
        NidType  type;
        uint32_t index;
        const char* name;
    };
    const ExpectedReserved expected[] = {
        { 0x00000021u, NidType::Internal,     0x01u, "NID_MESSAGE_STORE"              },
        { 0x00000061u, NidType::Internal,     0x03u, "NID_NAME_TO_ID_MAP"             },
        { 0x000000A1u, NidType::Internal,     0x05u, "NID_NORMAL_FOLDER_TEMPLATE"     },
        { 0x000000C1u, NidType::Internal,     0x06u, "NID_SEARCH_FOLDER_TEMPLATE"     },
        // NID_ROOT_FOLDER is the spec-text quirk: its nidType is NormalFolder
        // (0x02), NOT Internal — even though sec 2.4.1's title says
        // "Special Internal NIDs". Verified against spec text 2026-05-02.
        { 0x00000122u, NidType::NormalFolder, 0x09u, "NID_ROOT_FOLDER"                },
        { 0x000001E1u, NidType::Internal,     0x0Fu, "NID_SEARCH_MANAGEMENT_QUEUE"    },
        { 0x00000201u, NidType::Internal,     0x10u, "NID_SEARCH_ACTIVITY_LIST"       },
        { 0x00000241u, NidType::Internal,     0x12u, "NID_RESERVED1"                  },
        { 0x00000261u, NidType::Internal,     0x13u, "NID_SEARCH_DOMAIN_OBJECT"       },
        { 0x00000281u, NidType::Internal,     0x14u, "NID_SEARCH_GATHERER_QUEUE"      },
        { 0x000002A1u, NidType::Internal,     0x15u, "NID_SEARCH_GATHERER_DESCRIPTOR" },
        { 0x000002E1u, NidType::Internal,     0x17u, "NID_RESERVED2"                  },
        { 0x00000301u, NidType::Internal,     0x18u, "NID_RESERVED3"                  },
        { 0x00000321u, NidType::Internal,     0x19u, "NID_SEARCH_GATHERER_FOLDER_QUEUE"},
    };
    constexpr size_t kCount = sizeof(expected) / sizeof(expected[0]);
    REQUIRE(kCount == M5Allocator::kReservedCount);

    const Nid* const table = M5Allocator::allReservedNids();
    for (size_t i = 0; i < kCount; ++i) {
        INFO("reserved entry [" << i << "] = " << expected[i].name);
        REQUIRE(table[i].value == expected[i].nid);
        REQUIRE(table[i].type() == expected[i].type);
        REQUIRE(table[i].index() == expected[i].index);
    }
}

TEST_CASE("M5Allocator reservedNidFor() returns spec values for each enum",
          "[m5][allocator][spec_2_4_1]")
{
    using R = M5Allocator::ReservedNid;
    REQUIRE(M5Allocator::reservedNidFor(R::MessageStore).value             == 0x21u);
    REQUIRE(M5Allocator::reservedNidFor(R::NameToIdMap).value              == 0x61u);
    REQUIRE(M5Allocator::reservedNidFor(R::NormalFolderTemplate).value     == 0xA1u);
    REQUIRE(M5Allocator::reservedNidFor(R::SearchFolderTemplate).value     == 0xC1u);
    REQUIRE(M5Allocator::reservedNidFor(R::RootFolder).value               == 0x122u);
    REQUIRE(M5Allocator::reservedNidFor(R::SearchManagementQueue).value    == 0x1E1u);
    REQUIRE(M5Allocator::reservedNidFor(R::SearchActivityList).value       == 0x201u);
    REQUIRE(M5Allocator::reservedNidFor(R::Reserved1).value                == 0x241u);
    REQUIRE(M5Allocator::reservedNidFor(R::SearchDomainObject).value       == 0x261u);
    REQUIRE(M5Allocator::reservedNidFor(R::SearchGathererQueue).value      == 0x281u);
    REQUIRE(M5Allocator::reservedNidFor(R::SearchGathererDescriptor).value == 0x2A1u);
    REQUIRE(M5Allocator::reservedNidFor(R::Reserved2).value                == 0x2E1u);
    REQUIRE(M5Allocator::reservedNidFor(R::Reserved3).value                == 0x301u);
    REQUIRE(M5Allocator::reservedNidFor(R::SearchGathererFolderQueue).value== 0x321u);
}

TEST_CASE("M5Allocator constructor reserves all 14 NIDs from [SPEC sec 2.4.1]",
          "[m5][allocator]")
{
    M5Allocator a;
    REQUIRE(a.allocatedCount() == M5Allocator::kReservedCount);

    // Each spec NID must be reported as allocated (= reserved).
    const Nid* table = M5Allocator::allReservedNids();
    for (size_t i = 0; i < M5Allocator::kReservedCount; ++i) {
        REQUIRE(a.isAllocated(table[i]));
    }
}

// ============================================================================
// [SPEC sec 2.2.2.1] nidType validation
// ============================================================================
TEST_CASE("M5Allocator::isValidNidType accepts all 19 spec values, rejects gaps",
          "[m5][allocator][spec_2_2_2_1]")
{
    // Valid: 0x00, 0x01..0x08, 0x0A..0x13, 0x1F.
    for (uint32_t v = 0x00u; v <= 0x08u; ++v) {
        REQUIRE(M5Allocator::isValidNidType(static_cast<NidType>(v)));
    }
    for (uint32_t v = 0x0Au; v <= 0x13u; ++v) {
        REQUIRE(M5Allocator::isValidNidType(static_cast<NidType>(v)));
    }
    REQUIRE(M5Allocator::isValidNidType(static_cast<NidType>(0x1Fu)));

    // Gaps: 0x09 (between 0x08 ASSOC_MESSAGE and 0x0A CONTENTS_TABLE_INDEX),
    // 0x14..0x1E (between 0x13 SEARCH_TABLE_INDEX and 0x1F LTP).
    REQUIRE_FALSE(M5Allocator::isValidNidType(static_cast<NidType>(0x09u)));
    for (uint32_t v = 0x14u; v <= 0x1Eu; ++v) {
        REQUIRE_FALSE(M5Allocator::isValidNidType(static_cast<NidType>(v)));
    }
}

TEST_CASE("M5Allocator::allocate throws on invalid nidType",
          "[m5][allocator][negative]")
{
    M5Allocator a;
    REQUIRE_THROWS_AS(a.allocate(static_cast<NidType>(0x09u)), runtime_error);
    REQUIRE_THROWS_AS(a.allocate(static_cast<NidType>(0x14u)), runtime_error);
    REQUIRE_THROWS_AS(a.allocate(static_cast<NidType>(0x1Eu)), runtime_error);
}

// ============================================================================
// Per-nidType allocation: counter starts at idx=1, advances by 1, skips reserved
// ============================================================================
TEST_CASE("M5Allocator NormalFolder allocation seeds at 0x400 (above [MS-PST] §2.4.3 reserved range)",
          "[m5][allocator]")
{
    M5Allocator a;

    // NormalFolder is a user-allocatable nidType: its counter starts at
    // 0x400, well past NID_ROOT_FOLDER (idx=9). Sequence is purely
    // 0x400, 0x401, 0x402, ...
    for (uint32_t expectedIdx = 0x400u; expectedIdx <= 0x40Au; ++expectedIdx) {
        const Nid n = a.allocate(NidType::NormalFolder);
        INFO("expectedIdx=0x" << std::hex << expectedIdx);
        REQUIRE(n.type() == NidType::NormalFolder);
        REQUIRE(n.index() == expectedIdx);
    }
}

TEST_CASE("M5Allocator Internal allocation skips all 13 reserved internal NIDs",
          "[m5][allocator]")
{
    // Reserved INTERNAL nidIndex values per [SPEC sec 2.4.1]:
    // 0x01, 0x03, 0x05, 0x06, 0x0F, 0x10, 0x12, 0x13, 0x14, 0x15, 0x17, 0x18, 0x19.
    // (NID_ROOT_FOLDER = 0x122 has nidType=NormalFolder, NOT Internal — excluded.)
    const set<uint32_t> reservedInternalIdx = {
        0x01, 0x03, 0x05, 0x06, 0x0F, 0x10, 0x12, 0x13, 0x14, 0x15, 0x17, 0x18, 0x19
    };
    REQUIRE(reservedInternalIdx.size() == 13u);

    M5Allocator a;

    // Allocate 30 internal NIDs and confirm none collide with reserved + idx
    // sequence is monotonic skipping reserved.
    uint32_t expectedIdx = 1;
    for (size_t i = 0; i < 30; ++i) {
        while (reservedInternalIdx.count(expectedIdx) > 0) ++expectedIdx;
        const Nid n = a.allocate(NidType::Internal);
        INFO("allocation #" << i << " expectedIdx=" << expectedIdx);
        REQUIRE(n.type() == NidType::Internal);
        REQUIRE(n.index() == expectedIdx);
        ++expectedIdx;
    }
}

TEST_CASE("M5Allocator user-allocatable nidTypes start cleanly at idx=0x400",
          "[m5][allocator]")
{
    M5Allocator a;

    // NormalMessage (0x04) has zero reserved entries — pure
    // 0x400, 0x401, 0x402, ...
    for (uint32_t expectedIdx = 0x400u; expectedIdx <= 0x404u; ++expectedIdx) {
        const Nid n = a.allocate(NidType::NormalMessage);
        REQUIRE(n.type() == NidType::NormalMessage);
        REQUIRE(n.index() == expectedIdx);
    }

    // Same for Attachment (0x05).
    for (uint32_t expectedIdx = 0x400u; expectedIdx <= 0x402u; ++expectedIdx) {
        const Nid n = a.allocate(NidType::Attachment);
        REQUIRE(n.type() == NidType::Attachment);
        REQUIRE(n.index() == expectedIdx);
    }

    // And LTP (0x1F).
    for (uint32_t expectedIdx = 0x400u; expectedIdx <= 0x403u; ++expectedIdx) {
        const Nid n = a.allocate(NidType::LtpReserved);
        REQUIRE(n.type() == NidType::LtpReserved);
        REQUIRE(n.index() == expectedIdx);
    }
}

// ============================================================================
// Cross-nidType independence
// ============================================================================
TEST_CASE("M5Allocator nidType counters are independent",
          "[m5][allocator]")
{
    M5Allocator a;

    // Allocating NormalFolder doesn't affect NormalMessage's counter.
    // User-allocatable types each start at 0x400.
    REQUIRE(a.allocate(NidType::NormalFolder).index()  == 0x400u);
    REQUIRE(a.allocate(NidType::NormalFolder).index()  == 0x401u);
    REQUIRE(a.allocate(NidType::NormalMessage).index() == 0x400u); // not 0x402
    REQUIRE(a.allocate(NidType::NormalFolder).index()  == 0x402u);
    REQUIRE(a.allocate(NidType::NormalMessage).index() == 0x401u);
    REQUIRE(a.allocate(NidType::Attachment).index()    == 0x400u); // its own counter
    REQUIRE(a.allocate(NidType::NormalMessage).index() == 0x402u);
}

// ============================================================================
// registerExternal: pre-reserve a NID, subsequent allocate skips
// ============================================================================
TEST_CASE("M5Allocator::registerExternal causes auto-counter to skip pre-registered",
          "[m5][allocator]")
{
    M5Allocator a;

    // Pre-register a NormalMessage NID one step into the user-allocatable
    // counter range.
    const Nid pre(NidType::NormalMessage, 0x401u);
    a.registerExternal(pre);
    REQUIRE(a.isAllocated(pre));

    // First allocation: counter at 0x400, returns idx=0x400.
    REQUIRE(a.allocate(NidType::NormalMessage).index() == 0x400u);

    // Second allocation: counter would propose 0x401, but it's pre-registered;
    // skip to 0x402.
    REQUIRE(a.allocate(NidType::NormalMessage).index() == 0x402u);

    // Third allocation: 0x403 (clean).
    REQUIRE(a.allocate(NidType::NormalMessage).index() == 0x403u);
}

TEST_CASE("M5Allocator::registerExternal rejects already-allocated NID",
          "[m5][allocator][negative]")
{
    M5Allocator a;

    // Cannot pre-register a reserved NID (= NID_MESSAGE_STORE = 0x21).
    REQUIRE_THROWS_AS(a.registerExternal(Nid{0x21u}), runtime_error);

    // Pre-register a fresh NID succeeds; pre-registering it again throws.
    a.registerExternal(Nid(NidType::NormalFolder, 100));
    REQUIRE_THROWS_AS(
        a.registerExternal(Nid(NidType::NormalFolder, 100)),
        runtime_error);
}

TEST_CASE("M5Allocator::registerExternal rejects NID with invalid nidType",
          "[m5][allocator][negative]")
{
    M5Allocator a;
    // nidType 0x09 is in the gap.
    Nid bad(static_cast<NidType>(0x09u), 1);
    REQUIRE_THROWS_AS(a.registerExternal(bad), runtime_error);
}

// ============================================================================
// Reserved-NID rejection: the auto-allocator NEVER returns a reserved NID
// ============================================================================
TEST_CASE("M5Allocator auto-allocate never returns any [SPEC sec 2.4.1] reserved NID",
          "[m5][allocator]")
{
    M5Allocator a;

    set<uint32_t> reserved;
    const Nid* table = M5Allocator::allReservedNids();
    for (size_t i = 0; i < M5Allocator::kReservedCount; ++i) {
        reserved.insert(table[i].value);
    }

    // Allocate enough NIDs across all 19 nidTypes that we'd cross every
    // reserved value if the skip rule were broken. 50 allocations per type
    // is comfortably past nidIndex = 0x19 (= 25, the largest reserved idx).
    const NidType allTypes[] = {
        NidType::HID, NidType::Internal, NidType::NormalFolder,
        NidType::SearchFolder, NidType::NormalMessage, NidType::Attachment,
        NidType::SearchUpdateQueue, NidType::SearchCriteriaObject,
        NidType::AssocMessage, NidType::ContentsTableIndex,
        NidType::ReceiveFolderTable, NidType::OutgoingQueueTable,
        NidType::HierarchyTable, NidType::ContentsTable,
        NidType::AssocContentsTable, NidType::SearchContentsTable,
        NidType::AttachmentTable, NidType::RecipientTable,
        NidType::SearchTableIndex, NidType::LtpReserved,
    };
    for (NidType nt : allTypes) {
        for (size_t i = 0; i < 50; ++i) {
            const Nid n = a.allocate(nt);
            INFO("nidType=0x" << std::hex << static_cast<uint32_t>(nt)
                 << " allocation #" << std::dec << i
                 << " returned NID=0x" << std::hex << n.value);
            REQUIRE(reserved.count(n.value) == 0u);
        }
    }
}

// ============================================================================
// Determinism: two allocator instances with the same input produce the same output
// ============================================================================
TEST_CASE("M5Allocator output is deterministic across instances given same input",
          "[m5][allocator][determinism]")
{
    auto runScript = []() {
        // A script of mixed allocate / registerExternal calls. The output
        // is the sequence of NID values returned by allocate() (registerExternal
        // returns void, so we capture the call's effect via subsequent allocate).
        M5Allocator a;
        vector<uint32_t> out;
        out.push_back(a.allocate(NidType::NormalFolder).value);
        out.push_back(a.allocate(NidType::NormalMessage).value);
        out.push_back(a.allocate(NidType::NormalFolder).value);
        a.registerExternal(Nid(NidType::NormalMessage, 0x402u));
        out.push_back(a.allocate(NidType::NormalMessage).value);
        out.push_back(a.allocate(NidType::Attachment).value);
        out.push_back(a.allocate(NidType::Internal).value);
        out.push_back(a.allocate(NidType::NormalMessage).value); // should skip 0x402
        out.push_back(a.allocate(NidType::NormalMessage).value); // = 0x403
        return out;
    };

    const auto run1 = runScript();
    const auto run2 = runScript();
    REQUIRE(run1 == run2);

    // Verify the run-1 sequence matches expected values explicitly (so we
    // know determinism holds against a hard-coded ground truth, not just
    // against itself). User-allocatable types start at idx 0x400.
    REQUIRE(run1.size() == 8u);
    REQUIRE(run1[0] == ((0x400u << 5) | 0x02u));   // NormalFolder #1
    REQUIRE(run1[1] == ((0x400u << 5) | 0x04u));   // NormalMessage #1
    REQUIRE(run1[2] == ((0x401u << 5) | 0x02u));   // NormalFolder #2
    REQUIRE(run1[3] == ((0x401u << 5) | 0x04u));   // NormalMessage #2
    REQUIRE(run1[4] == ((0x400u << 5) | 0x05u));   // Attachment #1
    REQUIRE(run1[5] == ((2u     << 5) | 0x01u));   // Internal #1 (idx=2; idx=1 reserved=MessageStore)
    REQUIRE(run1[6] == ((0x403u << 5) | 0x04u));   // NormalMessage #3 (skip 0x402 pre-registered)
    REQUIRE(run1[7] == ((0x404u << 5) | 0x04u));   // NormalMessage #4
}

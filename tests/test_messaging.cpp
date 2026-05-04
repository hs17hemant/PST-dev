// pstwriter/tests/test_messaging.cpp
//
// M6 Phase B tests — messaging-layer builders.
//
// Strategy: round-trip semantic equivalence. Feed each builder its
// schema input, take the resulting HN bytes through the M4
// `readPropertyContext` (HID-agnostic, propType-driven), and verify
// the decoded properties match the schema's logical inputs.
//
// Byte-diff against §3.8 is NOT a goal — §3.8's HID layout reflects
// history-driven Outlook allocation, not our deterministic
// PidTag-ascending policy (see M4 §3.9 cross-validation lesson).
// The existing M5 `[semantic_decode_3_10]` test already locks the
// read-side decode of §3.8 bytes; this test locks the write-side.

#include <catch2/catch_test_macros.hpp>

#include "ltp.hpp"
#include "messaging.hpp"
#include "types.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace std;
using namespace pstwriter;

namespace {

// §3.10 sample's PidTagRecordKey, used as the canonical ProviderUID.
const array<uint8_t, 16> kSpec310ProviderUid = {{
    0x22, 0x9D, 0xB5, 0x0A, 0xDC, 0xD9, 0x94, 0x43,
    0x85, 0xDE, 0x90, 0xAE, 0xB0, 0x7D, 0x12, 0x70,
}};

// §3.10 sample's PidTagDisplayName: "UNICODE1" UTF-16-LE (16 bytes).
const array<uint8_t, 16> kSpec310DisplayName = {{
    0x55, 0x00, 0x4E, 0x00, 0x49, 0x00, 0x43, 0x00,
    0x4F, 0x00, 0x44, 0x00, 0x45, 0x00, 0x31, 0x00,
}};

// Small helper: find the prop with the given PidTag id.
const ReadPcProp* findProp(const vector<ReadPcProp>& props, uint16_t tag)
{
    for (const auto& p : props) {
        if (p.pidTagId == tag) return &p;
    }
    return nullptr;
}

} // namespace

// ============================================================================
// M6.1 — buildMessageStorePc round-trip against §3.10 logical schema.
// ============================================================================
TEST_CASE("buildMessageStorePc round-trip matches Sec 3.10 schema",
          "[m6][message_store][message_store_round_trip][m6_gate]")
{
    MessageStoreSchema schema{};
    schema.providerUid       = kSpec310ProviderUid;
    schema.displayNameUtf16le = kSpec310DisplayName.data();
    schema.displayNameSize    = kSpec310DisplayName.size();
    // Other fields keep their defaults (which match the §3.10 sample's
    // values: validFolderMask=0x89, replFlags=0, pstPassword=0,
    // replVersionPrefix/suffix=1, ipm/wastebasket/finder NIDs).

    // firstSubnodeNid: any non-HID NID. The message store schema has
    // no subnodes so the value isn't used; use Internal type, idx=2
    // (= 0x00000041, NOT a reserved §2.4.1 NID).
    const Nid firstSubnodeNid{0x00000041u};

    const PcResult result = buildMessageStorePc(schema, firstSubnodeNid);
    REQUIRE(result.subnodes.empty());            // no oversize props => no subnodes
    REQUIRE_FALSE(result.hnBytes.empty());

    // ---- Read back via M4's HID-agnostic readPropertyContext ----
    const auto props = readPropertyContext(result.hnBytes.data(),
                                           result.hnBytes.size());
    REQUIRE(props.size() == 11u);

    // ---- Verify each of the 11 §3.10 + §3.9 properties ----

    SECTION("PidTagReplVersionhistory (0x0E34) — 24 B HN-stored")
    {
        const auto* p = findProp(props, 0x0E34u);
        REQUIRE(p != nullptr);
        REQUIRE(static_cast<uint16_t>(p->propType) == 0x0102u);  // PtypBinary
        REQUIRE(p->storage  == ReadPcProp::Storage::HnAlloc);
        REQUIRE(p->valueSize == 24u);

        // Layout: 01 00 00 00 ‖ providerUid(16) ‖ 01 00 00 00 (§3.10 sample).
        const uint32_t prefix = detail::readU32(p->valueBytes, 0);
        const uint32_t suffix = detail::readU32(p->valueBytes, 20);
        REQUIRE(prefix == 0x00000001u);
        REQUIRE(suffix == 0x00000001u);
        REQUIRE(std::memcmp(p->valueBytes + 4,
                            kSpec310ProviderUid.data(), 16) == 0);
    }

    SECTION("PidTagReplFlags (0x0E38) — inline = 0")
    {
        const auto* p = findProp(props, 0x0E38u);
        REQUIRE(p != nullptr);
        REQUIRE(static_cast<uint16_t>(p->propType) == 0x0003u);  // PtypInteger32
        REQUIRE(p->storage     == ReadPcProp::Storage::Inline);
        REQUIRE(p->inlineValue == 0x00000000u);
    }

    SECTION("PidTagRecordKey (0x0FF9) — 16 B = ProviderUID")
    {
        const auto* p = findProp(props, 0x0FF9u);
        REQUIRE(p != nullptr);
        REQUIRE(static_cast<uint16_t>(p->propType) == 0x0102u);
        REQUIRE(p->storage  == ReadPcProp::Storage::HnAlloc);
        REQUIRE(p->valueSize == 16u);
        REQUIRE(std::memcmp(p->valueBytes,
                            kSpec310ProviderUid.data(), 16) == 0);
    }

    SECTION("PidTagDisplayName (0x3001) — UTF-16-LE \"UNICODE1\"")
    {
        const auto* p = findProp(props, 0x3001u);
        REQUIRE(p != nullptr);
        REQUIRE(static_cast<uint16_t>(p->propType) == 0x001Fu);  // PtypString (Unicode)
        REQUIRE(p->storage  == ReadPcProp::Storage::HnAlloc);
        REQUIRE(p->valueSize == 16u);
        REQUIRE(std::memcmp(p->valueBytes,
                            kSpec310DisplayName.data(), 16) == 0);
    }

    SECTION("PidTagValidFolderMask (0x35DF) — inline = 0x89")
    {
        const auto* p = findProp(props, 0x35DFu);
        REQUIRE(p != nullptr);
        REQUIRE(static_cast<uint16_t>(p->propType) == 0x0003u);
        REQUIRE(p->storage     == ReadPcProp::Storage::Inline);
        REQUIRE(p->inlineValue == 0x00000089u);
    }

    SECTION("PidTagIpmSubTreeEntryId (0x35E0) — EntryID with NID 0x8022")
    {
        const auto* p = findProp(props, 0x35E0u);
        REQUIRE(p != nullptr);
        REQUIRE(p->valueSize == 24u);
        // rgbFlags = 0
        REQUIRE(detail::readU32(p->valueBytes, 0) == 0u);
        // ProviderUID at bytes 4..19
        REQUIRE(std::memcmp(p->valueBytes + 4,
                            kSpec310ProviderUid.data(), 16) == 0);
        // entryNid at bytes 20..23
        REQUIRE(detail::readU32(p->valueBytes, 20) == 0x00008022u);
    }

    SECTION("PidTagIpmWastebasketEntryId (0x35E3) — EntryID with NID 0x8062")
    {
        const auto* p = findProp(props, 0x35E3u);
        REQUIRE(p != nullptr);
        REQUIRE(p->valueSize == 24u);
        REQUIRE(detail::readU32(p->valueBytes, 0)  == 0u);
        REQUIRE(std::memcmp(p->valueBytes + 4,
                            kSpec310ProviderUid.data(), 16) == 0);
        REQUIRE(detail::readU32(p->valueBytes, 20) == 0x00008062u);
    }

    SECTION("PidTagFinderEntryId (0x35E7) — EntryID with NID 0x8042")
    {
        const auto* p = findProp(props, 0x35E7u);
        REQUIRE(p != nullptr);
        REQUIRE(p->valueSize == 24u);
        REQUIRE(detail::readU32(p->valueBytes, 0)  == 0u);
        REQUIRE(std::memcmp(p->valueBytes + 4,
                            kSpec310ProviderUid.data(), 16) == 0);
        REQUIRE(detail::readU32(p->valueBytes, 20) == 0x00008042u);
    }

    SECTION("PidTag 0x6633 PtypBoolean — inline = 1 (§3.9 BTH extra)")
    {
        const auto* p = findProp(props, 0x6633u);
        REQUIRE(p != nullptr);
        REQUIRE(static_cast<uint16_t>(p->propType) == 0x000Bu);  // PtypBoolean
        REQUIRE(p->storage == ReadPcProp::Storage::Inline);
        // Boolean stored zero-extended: low byte = 1, upper bytes = 0.
        REQUIRE((p->inlineValue & 0xFFu) == 0x01u);
    }

    SECTION("PidTag 0x66FA PtypInteger32 — inline = 0x000E000D (§3.9 BTH extra)")
    {
        const auto* p = findProp(props, 0x66FAu);
        REQUIRE(p != nullptr);
        REQUIRE(static_cast<uint16_t>(p->propType) == 0x0003u);
        REQUIRE(p->storage     == ReadPcProp::Storage::Inline);
        REQUIRE(p->inlineValue == 0x000E000Du);
    }

    SECTION("PidTagPstPassword (0x67FF) — inline = 0")
    {
        const auto* p = findProp(props, 0x67FFu);
        REQUIRE(p != nullptr);
        REQUIRE(static_cast<uint16_t>(p->propType) == 0x0003u);
        REQUIRE(p->storage     == ReadPcProp::Storage::Inline);
        REQUIRE(p->inlineValue == 0x00000000u);
    }
}

// ============================================================================
// M6.2 / M6.3 — buildFolderPc round-trip against §3.12 Root Folder schema.
//
// §3.12 publishes the Root Folder PC as a 4-property bag:
//   PidTagDisplayName        PtypString    (UTF-16-LE)
//   PidTagContentCount       PtypInteger32 (= 0)
//   PidTagContentUnreadCount PtypInteger32 (= 0)
//   PidTagSubfolders         PtypBoolean   (= 1, Root has 3 sub-folders)
//
// The same schema covers IPM Subtree, Finder, Deleted Items — only
// the NID and dynamic data change. NID itself is wired at NBT
// registration, not embedded in PC bytes.
// ============================================================================
TEST_CASE("buildFolderPc round-trip matches Sec 3.12 Root Folder schema",
          "[m6][folder_pc][folder_pc_round_trip][m6_gate]")
{
    // §3.12's Root Folder doesn't show a display-name string in the prose
    // dump (sample is empty / not printed). Use any UTF-16-LE bytes here
    // — what matters is the round-trip.
    const array<uint8_t, 22> rootDisplayName = {{
        0x52, 0x00, 0x6F, 0x00, 0x6F, 0x00, 0x74, 0x00, // "Root "
        0x20, 0x00, 0x46, 0x00, 0x6F, 0x00, 0x6C, 0x00, // "Fol"
        0x64, 0x00, 0x65, 0x00, 0x72, 0x00,             // "der"
    }};

    FolderPcSchema schema{};
    schema.displayNameUtf16le = rootDisplayName.data();
    schema.displayNameSize    = rootDisplayName.size();
    schema.contentCount       = 0u;
    schema.contentUnreadCount = 0u;
    schema.hasSubfolders      = true;  // §3.12: Root has 3 sub-folders

    const Nid firstSubnodeNid{0x00000041u};  // Internal nidType, idx=2
    const PcResult result = buildFolderPc(schema, firstSubnodeNid);
    REQUIRE(result.subnodes.empty());
    REQUIRE_FALSE(result.hnBytes.empty());

    const auto props = readPropertyContext(result.hnBytes.data(),
                                           result.hnBytes.size());
    REQUIRE(props.size() == 4u);

    SECTION("PidTagDisplayName (0x3001) — UTF-16-LE round-trip")
    {
        const auto* p = findProp(props, 0x3001u);
        REQUIRE(p != nullptr);
        REQUIRE(static_cast<uint16_t>(p->propType) == 0x001Fu);
        REQUIRE(p->storage  == ReadPcProp::Storage::HnAlloc);
        REQUIRE(p->valueSize == rootDisplayName.size());
        REQUIRE(std::memcmp(p->valueBytes,
                            rootDisplayName.data(),
                            rootDisplayName.size()) == 0);
    }

    SECTION("PidTagContentCount (0x3602) — inline = 0")
    {
        const auto* p = findProp(props, 0x3602u);
        REQUIRE(p != nullptr);
        REQUIRE(static_cast<uint16_t>(p->propType) == 0x0003u);
        REQUIRE(p->storage     == ReadPcProp::Storage::Inline);
        REQUIRE(p->inlineValue == 0u);
    }

    SECTION("PidTagContentUnreadCount (0x3603) — inline = 0")
    {
        const auto* p = findProp(props, 0x3603u);
        REQUIRE(p != nullptr);
        REQUIRE(static_cast<uint16_t>(p->propType) == 0x0003u);
        REQUIRE(p->storage     == ReadPcProp::Storage::Inline);
        REQUIRE(p->inlineValue == 0u);
    }

    SECTION("PidTagSubfolders (0x360A) — Boolean inline = 1")
    {
        const auto* p = findProp(props, 0x360Au);
        REQUIRE(p != nullptr);
        REQUIRE(static_cast<uint16_t>(p->propType) == 0x000Bu);
        REQUIRE(p->storage == ReadPcProp::Storage::Inline);
        REQUIRE((p->inlineValue & 0xFFu) == 0x01u);
    }
}

// ============================================================================
// Empty-folder edge case — sub-folder with no children, 0 content, no display
// name. Verifies the builder handles defaults gracefully.
// ============================================================================
TEST_CASE("buildFolderPc handles minimal/default schema (empty leaf folder)",
          "[m6][folder_pc][folder_pc_empty]")
{
    FolderPcSchema schema{};
    // All defaults: empty display name, contentCount=0, hasSubfolders=false.

    const PcResult result = buildFolderPc(schema, Nid{0x00000041u});
    REQUIRE_FALSE(result.hnBytes.empty());

    const auto props = readPropertyContext(result.hnBytes.data(),
                                           result.hnBytes.size());
    REQUIRE(props.size() == 4u);

    // PidTagSubfolders should now be 0
    const auto* sub = findProp(props, 0x360Au);
    REQUIRE(sub != nullptr);
    REQUIRE((sub->inlineValue & 0xFFu) == 0x00u);

    // PidTagDisplayName should be present but with valueSize == 0
    const auto* name = findProp(props, 0x3001u);
    REQUIRE(name != nullptr);
    REQUIRE(name->valueSize == 0u);
}

// ============================================================================
// M6.6 — buildFolderHierarchyTc structural validation against §3.12 schema.
//
// Strategy (not byte-diff against §3.11): construct a 3-row Hierarchy TC
// matching §3.12's Root Folder sample, then walk the TC bytes structurally
// to verify:
//   1. HNHDR + HNPAGEMAP shape (8 allocations: BTHHEADER, TCINFO, RowIndex
//      leaf, Row Matrix, 3 display-name strings)
//   2. TCINFO header: bType=0x7C, cCols=13, hidRowIndex=0x20, hnidRows=0x80
//   3. 13 TCOLDESCs sorted by tag ascending, each matching §3.12's
//      (PidTag, ibData, cbData, iBit) verbatim
//   4. RowIndex BTH leaf: 3 records sorted by RowID ascending
//   5. Each row's display-name HID resolves to expected UTF-16-LE bytes
//
// Byte-diff against §3.11 is not a goal: §3.11 publishes its row matrix in
// Outlook-write-order (RowIDs 0x8022/0x8042/0x2223 mapped to matrix
// indices 0/1/2), whereas our M4 buildTableContext sorts the matrix by
// rowId-ascending (which would give 0x2223/0x8022/0x8042 at 0/1/2). The
// RowIndex BTH compensates with the rowId→position mapping, so both
// layouts are functionally valid.
// ============================================================================
TEST_CASE("buildFolderHierarchyTc produces a Sec 3.12-shaped 13-col Hierarchy TC",
          "[m6][hierarchy_tc][hierarchy_tc_3_12][m6_gate]")
{
    // §3.12 sample's three sub-folder names (UTF-16-LE) and NIDs.
    const array<uint8_t, 46> nameTopOfPersonal = {{
        0x54,0x00,0x6F,0x00,0x70,0x00,0x20,0x00,0x6F,0x00,0x66,0x00,0x20,0x00,0x50,0x00,
        0x65,0x00,0x72,0x00,0x73,0x00,0x6F,0x00,0x6E,0x00,0x61,0x00,0x6C,0x00,0x20,0x00,
        0x46,0x00,0x6F,0x00,0x6C,0x00,0x64,0x00,0x65,0x00,0x72,0x00,0x73,0x00,
    }};
    const array<uint8_t, 22> nameSearchRoot = {{
        0x53,0x00,0x65,0x00,0x61,0x00,0x72,0x00,0x63,0x00,0x68,0x00,0x20,0x00,0x52,0x00,
        0x6F,0x00,0x6F,0x00,0x74,0x00,
    }};
    const array<uint8_t, 40> nameSpamSearch = {{
        0x53,0x00,0x50,0x00,0x41,0x00,0x4D,0x00,0x20,0x00,0x53,0x00,0x65,0x00,0x61,0x00,
        0x72,0x00,0x63,0x00,0x68,0x00,0x20,0x00,0x46,0x00,0x6F,0x00,0x6C,0x00,0x64,0x00,
        0x65,0x00,0x72,0x00,0x20,0x00,0x32,0x00,
    }};

    HierarchyTcRow rows[3];
    rows[0].rowId              = Nid{0x00008022u};
    rows[0].displayNameUtf16le = nameTopOfPersonal.data();
    rows[0].displayNameSize    = nameTopOfPersonal.size();
    rows[0].hasSubfolders      = true;

    rows[1].rowId              = Nid{0x00008042u};
    rows[1].displayNameUtf16le = nameSearchRoot.data();
    rows[1].displayNameSize    = nameSearchRoot.size();
    rows[1].hasSubfolders      = false;

    rows[2].rowId              = Nid{0x00002223u};
    rows[2].displayNameUtf16le = nameSpamSearch.data();
    rows[2].displayNameSize    = nameSpamSearch.size();
    rows[2].hasSubfolders      = false;

    const TcResult result = buildFolderHierarchyTc(rows, 3);
    REQUIRE_FALSE(result.hnBytes.empty());
    const uint8_t* hn = result.hnBytes.data();
    const size_t   sz = result.hnBytes.size();

    SECTION("HNHDR signature + bClientSig=0x7C (TC)")
    {
        REQUIRE(hn[2] == 0xECu);             // bSig
        REQUIRE(hn[3] == 0x7Cu);             // bClientSig (TC)
        REQUIRE(detail::readU32(hn, 4) == 0x00000040u); // hidUserRoot -> TCINFO
    }

    SECTION("TCINFO header at HID 0x40 matches Sec 3.12 schema")
    {
        // HNPAGEMAP at end gives slot offsets. We compute slot 2 (TCINFO).
        const uint16_t ibHnpm = detail::readU16(hn, 0);
        const uint16_t cAlloc = detail::readU16(hn, ibHnpm);
        REQUIRE(cAlloc >= 4u);  // BTHHEADER + TCINFO + RowIndex leaf + Row Matrix at minimum
        const uint16_t tciOff = detail::readU16(hn, ibHnpm + 4 + 2 * 1); // rgibAlloc[1]
        // (rgibAlloc[0]=12, rgibAlloc[1]=20=0x14 typically — pointing at TCINFO)

        REQUIRE(hn[tciOff + 0] == 0x7Cu);   // bType
        REQUIRE(hn[tciOff + 1] == 0x0Du);   // cCols = 13
        REQUIRE(detail::readU32(hn, tciOff + 10) == 0x00000020u); // hidRowIndex
        REQUIRE(detail::readU32(hn, tciOff + 14) == 0x00000080u); // hnidRows
        // rgib[TCI_4b] = 52 (max end of 4B/8B cols), rgib[TCI_2b] = 52 (no 2-byte cols),
        // rgib[TCI_1b] = 53 (Subfolders 1B), rgib[TCI_bm] = 55 (53 + 2 CEB).
        REQUIRE(detail::readU16(hn, tciOff + 2) == 52u);
        REQUIRE(detail::readU16(hn, tciOff + 4) == 52u);
        REQUIRE(detail::readU16(hn, tciOff + 6) == 53u);
        REQUIRE(detail::readU16(hn, tciOff + 8) == 55u);
    }

    SECTION("13 TCOLDESCs sorted by tag, matching Sec 3.12 (tag, ibData, cbData, iBit)")
    {
        const uint16_t ibHnpm = detail::readU16(hn, 0);
        const uint16_t tciOff = detail::readU16(hn, ibHnpm + 4 + 2);

        // TCOLDESC array starts at tciOff + 22.
        struct ExpectedCol {
            uint32_t tag;       // (pidTagId << 16) | propType
            uint16_t ibData;
            uint8_t  cbData;
            uint8_t  iBit;
        };
        // Sorted by tag ascending. Each row pinned from §3.12.
        const ExpectedCol expected[13] = {
            { 0x0E300003u, 20, 4,  6 },  // ReplItemid
            { 0x0E330014u, 24, 8,  7 },  // ReplChangenum
            { 0x0E340102u, 32, 4,  8 },  // ReplVersionhistory
            { 0x0E380003u, 36, 4,  9 },  // ReplFlags
            { 0x3001001Fu,  8, 4,  2 },  // DisplayName_W
            { 0x36020003u, 12, 4,  3 },  // ContentCount
            { 0x36030003u, 16, 4,  4 },  // ContentUnreadCount
            { 0x360A000Bu, 52, 1,  5 },  // Subfolders
            { 0x3613001Fu, 40, 4, 10 },  // ContainerClass_W
            { 0x66350003u, 44, 4, 11 },  // (PstHiddenCount)
            { 0x66360003u, 48, 4, 12 },  // (PstHiddenUnread)
            { 0x67F20003u,  0, 4,  0 },  // LtpRowId
            { 0x67F30003u,  4, 4,  1 },  // LtpRowVer
        };
        for (size_t i = 0; i < 13; ++i) {
            const size_t off = tciOff + 22 + i * 8;
            INFO("TCOLDESC[" << i << "] expected tag=0x"
                 << std::hex << expected[i].tag);
            REQUIRE(detail::readU32(hn, off + 0) == expected[i].tag);
            REQUIRE(detail::readU16(hn, off + 4) == expected[i].ibData);
            REQUIRE(hn[off + 6]                  == expected[i].cbData);
            REQUIRE(hn[off + 7]                  == expected[i].iBit);
        }
    }

    SECTION("RowIndex BTH leaf has 3 records sorted by RowID ascending")
    {
        // RowIndex BTHHEADER at HID 0x20 (slot 1).
        const uint16_t ibHnpm = detail::readU16(hn, 0);
        const uint16_t bthHdrOff  = detail::readU16(hn, ibHnpm + 4 + 0); // slot 1
        // BTHHEADER: bType=0xB5, cbKey=4, cbEnt=4, bIdxLevels=0, hidRoot=HID 0x60
        REQUIRE(hn[bthHdrOff + 0] == 0xB5u);
        REQUIRE(hn[bthHdrOff + 1] == 4u);   // cbKey
        REQUIRE(hn[bthHdrOff + 2] == 4u);   // cbEnt
        REQUIRE(hn[bthHdrOff + 3] == 0u);   // bIdxLevels
        REQUIRE(detail::readU32(hn, bthHdrOff + 4) == 0x00000060u);

        // RowIndex BTH leaf at HID 0x60 (slot 3): 3 records, 8 bytes each.
        const uint16_t leafOff = detail::readU16(hn, ibHnpm + 4 + 2 * 2); // slot 2
        REQUIRE(detail::readU32(hn, leafOff +  0) == 0x00002223u); // sorted ascending
        REQUIRE(detail::readU32(hn, leafOff +  4) == 0u);          // matrix idx 0
        REQUIRE(detail::readU32(hn, leafOff +  8) == 0x00008022u);
        REQUIRE(detail::readU32(hn, leafOff + 12) == 1u);          // matrix idx 1
        REQUIRE(detail::readU32(hn, leafOff + 16) == 0x00008042u);
        REQUIRE(detail::readU32(hn, leafOff + 20) == 2u);          // matrix idx 2
    }

    SECTION("Row Matrix has 3 rows; CEB byte 0 = 0xFD (7-of-8 cols present)")
    {
        const uint16_t ibHnpm = detail::readU16(hn, 0);
        const uint16_t mtxOff = detail::readU16(hn, ibHnpm + 4 + 2 * 3); // slot 3

        // Each row is 55 bytes. Bytes 53..54 = CEB.
        for (size_t r = 0; r < 3; ++r) {
            const size_t rowStart = mtxOff + r * 55u;
            REQUIRE(hn[rowStart + 53] == 0xFDu);   // iBit 0..7: bits 0,1,2,3,4,5,7 set
            REQUIRE(hn[rowStart + 54] == 0x00u);   // iBit 8..12 all clear
        }

        // Row 0 (sorted: 0x2223 first) should have rowId 0x2223 in bytes 0..3.
        REQUIRE(detail::readU32(hn, mtxOff + 0 * 55u + 0) == 0x00002223u);
        REQUIRE(detail::readU32(hn, mtxOff + 1 * 55u + 0) == 0x00008022u);
        REQUIRE(detail::readU32(hn, mtxOff + 2 * 55u + 0) == 0x00008042u);

        // Subfolders byte at offset 52: row[0]=0x2223 SPAM=false, row[1]=Top=true,
        // row[2]=SearchRoot=false.
        REQUIRE(hn[mtxOff + 0 * 55u + 52] == 0u);   // SPAM
        REQUIRE(hn[mtxOff + 1 * 55u + 52] == 1u);   // Top of Personal Folders
        REQUIRE(hn[mtxOff + 2 * 55u + 52] == 0u);   // Search Root
    }

    SECTION("Each row's DisplayName HID resolves to expected UTF-16-LE bytes")
    {
        // Build a fast HID-resolver against this HN.
        const uint16_t ibHnpm = detail::readU16(hn, 0);
        const uint16_t cAlloc = detail::readU16(hn, ibHnpm);
        auto resolveHid = [&](uint32_t hid) -> std::pair<size_t, size_t> {
            // hidIndex is bits 5..15 (1-based)
            const uint16_t idx = static_cast<uint16_t>((hid >> 5) & 0x07FFu);
            REQUIRE(idx >= 1u);
            REQUIRE(idx <= cAlloc);
            const uint16_t start = detail::readU16(hn, ibHnpm + 4 + 2u * (idx - 1));
            const uint16_t end   = detail::readU16(hn, ibHnpm + 4 + 2u * idx);
            return { start, end - start };
        };

        const uint16_t mtxOff = detail::readU16(hn, ibHnpm + 4 + 2 * 3);
        // Row 0 (rowId=0x2223 SPAM); DisplayName_W HID at byte offset 8
        const auto sn0 = resolveHid(detail::readU32(hn, mtxOff + 0 * 55u + 8));
        REQUIRE(sn0.second == 40u);
        REQUIRE(std::memcmp(hn + sn0.first, nameSpamSearch.data(), 40) == 0);

        // Row 1 (rowId=0x8022 Top of Personal Folders)
        const auto sn1 = resolveHid(detail::readU32(hn, mtxOff + 1 * 55u + 8));
        REQUIRE(sn1.second == 46u);
        REQUIRE(std::memcmp(hn + sn1.first, nameTopOfPersonal.data(), 46) == 0);

        // Row 2 (rowId=0x8042 Search Root)
        const auto sn2 = resolveHid(detail::readU32(hn, mtxOff + 2 * 55u + 8));
        REQUIRE(sn2.second == 22u);
        REQUIRE(std::memcmp(hn + sn2.first, nameSearchRoot.data(), 22) == 0);
    }

    (void)sz;
}

// ============================================================================
// Empty Hierarchy TC — used by template (0x060D) and by leaf folders that
// have no children (per §2.7.1's "Columns Only" state for most folders).
// ============================================================================
TEST_CASE("buildFolderHierarchyTc handles empty (0-row) case",
          "[m6][hierarchy_tc][hierarchy_tc_empty]")
{
    const TcResult result = buildFolderHierarchyTc(nullptr, 0);
    REQUIRE_FALSE(result.hnBytes.empty());
    const uint8_t* hn = result.hnBytes.data();

    // bClientSig still TC; TCINFO.cCols still 13; hnidRows = 0.
    REQUIRE(hn[3] == 0x7Cu);
    const uint16_t ibHnpm = detail::readU16(hn, 0);
    const uint16_t tciOff = detail::readU16(hn, ibHnpm + 4 + 2);
    REQUIRE(hn[tciOff + 1] == 0x0Du);   // cCols = 13
    REQUIRE(detail::readU32(hn, tciOff + 14) == 0u);  // hnidRows = 0 for empty
}

// ============================================================================
// M6.7 — buildFolderContentsTc structural validation against §3.12 schema.
//
// Always 0-row in M6 (messages arrive in M7). Verify TCINFO header,
// 27 TCOLDESCs match §3.12 verbatim, and the empty-table sentinel
// (hnidRows = 0, RowIndex BTHHEADER.hidRoot = 0) is correctly applied.
// ============================================================================
TEST_CASE("buildFolderContentsTc produces a Sec 3.12-shaped 27-col empty TC",
          "[m6][contents_tc][contents_tc_3_12][m6_gate]")
{
    const TcResult result = buildFolderContentsTc();
    REQUIRE_FALSE(result.hnBytes.empty());
    const uint8_t* hn = result.hnBytes.data();

    REQUIRE(hn[3] == 0x7Cu);   // bClientSig (TC)

    const uint16_t ibHnpm = detail::readU16(hn, 0);
    const uint16_t tciOff = detail::readU16(hn, ibHnpm + 4 + 2);

    // TCINFO header
    REQUIRE(hn[tciOff + 0] == 0x7Cu);    // bType
    REQUIRE(hn[tciOff + 1] == 0x1Bu);    // cCols = 27 = 0x1B
    // rgib computed from schema:
    //   end4b = max(ibData+cbData) over 4/8-byte cols = 116
    //   end2b = end4b = 116 (no 2-byte cols)
    //   end1b = end2b + 2 (MessageToMe + MessageCcMe, 1 byte each) = 118
    //   endBm = end1b + ceil(27/8) = 118 + 4 = 122
    REQUIRE(detail::readU16(hn, tciOff + 2) == 116u);
    REQUIRE(detail::readU16(hn, tciOff + 4) == 116u);
    REQUIRE(detail::readU16(hn, tciOff + 6) == 118u);
    REQUIRE(detail::readU16(hn, tciOff + 8) == 122u);
    REQUIRE(detail::readU32(hn, tciOff + 10) == 0x00000020u);  // hidRowIndex
    REQUIRE(detail::readU32(hn, tciOff + 14) == 0u);           // hnidRows (0 = empty)

    // RowIndex BTHHEADER hidRoot = 0 (empty BTH)
    const uint16_t bthHdrOff = detail::readU16(hn, ibHnpm + 4 + 0);
    REQUIRE(hn[bthHdrOff + 1] == 4u);    // cbKey
    REQUIRE(hn[bthHdrOff + 2] == 4u);    // cbEnt
    REQUIRE(detail::readU32(hn, bthHdrOff + 4) == 0u);  // hidRoot=0 sentinel

    // Verify 27 TCOLDESCs sorted by tag.
    struct ExpectedCol { uint32_t tag; uint16_t ibData; uint8_t cbData; uint8_t iBit; };
    const ExpectedCol expected[27] = {
        { 0x00170003u,  20, 4,  5 },  // Importance
        { 0x001A001Fu,  12, 4,  3 },  // MessageClass_W
        { 0x00360003u,  60, 4, 15 },  // Sensitivity
        { 0x0037001Fu,  28, 4,  7 },  // Subject_W
        { 0x00390040u,  40, 8,  9 },  // ClientSubmitTime
        { 0x0042001Fu,  24, 4,  6 },  // SentRepresentingName_W
        { 0x0057000Bu, 116, 1, 13 },  // MessageToMe
        { 0x0058000Bu, 117, 1, 14 },  // MessageCcMe
        { 0x0070001Fu,  68, 4, 17 },  // ConversationTopic_W
        { 0x00710102u,  72, 4, 18 },  // ConversationIndex
        { 0x0E03001Fu,  56, 4, 12 },  // DisplayCc_W
        { 0x0E04001Fu,  52, 4, 11 },  // DisplayTo_W
        { 0x0E060040u,  32, 8,  8 },  // MessageDeliveryTime
        { 0x0E070003u,  16, 4,  4 },  // MessageFlags
        { 0x0E080003u,  48, 4, 10 },  // MessageSize
        { 0x0E170003u,   8, 4,  2 },  // MessageStatus
        { 0x0E300003u,  88, 4, 21 },  // ReplItemId
        { 0x0E330014u,  92, 8, 22 },  // ReplChangenum
        { 0x0E340102u, 100, 4, 23 },  // ReplVersionhistory
        { 0x0E380003u, 112, 4, 26 },  // ReplFlags
        { 0x0E3C0102u, 108, 4, 25 },  // ReplCopiedfromVersionhistory
        { 0x0E3D0102u, 104, 4, 24 },  // ReplCopiedfromItemid
        { 0x10970003u,  64, 4, 16 },  // ItemTemporaryFlags
        { 0x30080040u,  80, 8, 20 },  // LastModificationTime
        { 0x65C60003u,  76, 4, 19 },  // SecureSubmitFlags
        { 0x67F20003u,   0, 4,  0 },  // LtpRowId
        { 0x67F30003u,   4, 4,  1 },  // LtpRowVer
    };
    for (size_t i = 0; i < 27; ++i) {
        const size_t off = tciOff + 22 + i * 8;
        INFO("TCOLDESC[" << i << "] expected tag=0x" << std::hex << expected[i].tag);
        REQUIRE(detail::readU32(hn, off + 0) == expected[i].tag);
        REQUIRE(detail::readU16(hn, off + 4) == expected[i].ibData);
        REQUIRE(hn[off + 6]                  == expected[i].cbData);
        REQUIRE(hn[off + 7]                  == expected[i].iBit);
    }
}

// ============================================================================
// M6.8 — buildFolderFaiContentsTc structural validation.
// ============================================================================
TEST_CASE("buildFolderFaiContentsTc produces a Sec 3.12-shaped 17-col empty TC",
          "[m6][fai_contents_tc][fai_contents_tc_3_12][m6_gate]")
{
    const TcResult result = buildFolderFaiContentsTc();
    REQUIRE_FALSE(result.hnBytes.empty());
    const uint8_t* hn = result.hnBytes.data();

    REQUIRE(hn[3] == 0x7Cu);

    const uint16_t ibHnpm = detail::readU16(hn, 0);
    const uint16_t tciOff = detail::readU16(hn, ibHnpm + 4 + 2);

    REQUIRE(hn[tciOff + 0] == 0x7Cu);
    REQUIRE(hn[tciOff + 1] == 0x11u);    // cCols = 17

    // rgib:
    //   end4b = max(ibData+cbData) over 4/8-byte cols = 64
    //   end2b = 64
    //   end1b = 65 (FormMultCategorized 1 byte at ibData=64)
    //   endBm = 65 + ceil(17/8) = 65 + 3 = 68
    REQUIRE(detail::readU16(hn, tciOff + 2) == 64u);
    REQUIRE(detail::readU16(hn, tciOff + 4) == 64u);
    REQUIRE(detail::readU16(hn, tciOff + 6) == 65u);
    REQUIRE(detail::readU16(hn, tciOff + 8) == 68u);
    REQUIRE(detail::readU32(hn, tciOff + 14) == 0u);  // hnidRows=0

    // Spot-check a few TCOLDESCs (full 17-col table verified by build success
    // — the schema is already in messaging.cpp; this test pins the wire bytes).
    struct ExpectedCol { uint32_t tag; uint16_t ibData; uint8_t cbData; uint8_t iBit; };
    const ExpectedCol expected[17] = {
        { 0x001A001Fu, 12, 4,  3 },  // MessageClass_W
        { 0x003A001Fu, 60, 4, 16 },  // ReportName_W
        { 0x0070001Fu, 56, 4, 15 },  // ConversationTopic_W
        { 0x0E070003u, 16, 4,  4 },  // MessageFlags
        { 0x0E170003u,  8, 4,  2 },  // MessageStatus
        { 0x3001001Fu, 20, 4,  5 },  // DisplayName_W
        { 0x67F20003u,  0, 4,  0 },  // LtpRowId
        { 0x67F30003u,  4, 4,  1 },  // LtpRowVer
        { 0x6800001Fu, 44, 4, 11 },
        { 0x6803000Bu, 64, 1, 12 },
        { 0x68051003u, 48, 4, 13 },
        { 0x682F001Fu, 52, 4, 14 },
        { 0x70030003u, 24, 4,  6 },
        { 0x70040102u, 28, 4,  7 },
        { 0x70050102u, 32, 4,  8 },
        { 0x7006001Fu, 36, 4,  9 },
        { 0x70070003u, 40, 4, 10 },
    };
    for (size_t i = 0; i < 17; ++i) {
        const size_t off = tciOff + 22 + i * 8;
        INFO("FAI TCOLDESC[" << i << "] expected tag=0x" << std::hex << expected[i].tag);
        REQUIRE(detail::readU32(hn, off + 0) == expected[i].tag);
        REQUIRE(detail::readU16(hn, off + 4) == expected[i].ibData);
        REQUIRE(hn[off + 6]                  == expected[i].cbData);
        REQUIRE(hn[off + 7]                  == expected[i].iBit);
    }
}

// ============================================================================
// M6.1 — EntryID encoder shape verification.
//
// Reproduces all 3 §3.10 EntryIDs byte-for-byte from the schema-side
// input. This is the strongest possible test of the EntryID layout
// since §3.10's EntryID values are byte-fixed in the spec dump.
// ============================================================================
TEST_CASE("makeEntryId reproduces the 3 Sec 3.10 EntryID values byte-for-byte",
          "[m6][message_store][entry_id][m6_gate]")
{
    SECTION("PidTagIpmSubTreeEntryId — entryNid = 0x8022")
    {
        const auto eid = makeEntryId(kSpec310ProviderUid, Nid{0x00008022u});
        // §3.10 sample: 00 00 00 00 22 9D B5 0A DC D9 94 43
        //               85 DE 90 AE B0 7D 12 70 22 80 00 00
        const uint8_t expected[24] = {
            0x00, 0x00, 0x00, 0x00,
            0x22, 0x9D, 0xB5, 0x0A, 0xDC, 0xD9, 0x94, 0x43,
            0x85, 0xDE, 0x90, 0xAE, 0xB0, 0x7D, 0x12, 0x70,
            0x22, 0x80, 0x00, 0x00,
        };
        REQUIRE(std::memcmp(eid.data(), expected, 24) == 0);
    }

    SECTION("PidTagIpmWastebasketEntryId — entryNid = 0x8062")
    {
        const auto eid = makeEntryId(kSpec310ProviderUid, Nid{0x00008062u});
        const uint8_t expected[24] = {
            0x00, 0x00, 0x00, 0x00,
            0x22, 0x9D, 0xB5, 0x0A, 0xDC, 0xD9, 0x94, 0x43,
            0x85, 0xDE, 0x90, 0xAE, 0xB0, 0x7D, 0x12, 0x70,
            0x62, 0x80, 0x00, 0x00,
        };
        REQUIRE(std::memcmp(eid.data(), expected, 24) == 0);
    }

    SECTION("PidTagFinderEntryId — entryNid = 0x8042")
    {
        const auto eid = makeEntryId(kSpec310ProviderUid, Nid{0x00008042u});
        const uint8_t expected[24] = {
            0x00, 0x00, 0x00, 0x00,
            0x22, 0x9D, 0xB5, 0x0A, 0xDC, 0xD9, 0x94, 0x43,
            0x85, 0xDE, 0x90, 0xAE, 0xB0, 0x7D, 0x12, 0x70,
            0x42, 0x80, 0x00, 0x00,
        };
        REQUIRE(std::memcmp(eid.data(), expected, 24) == 0);
    }
}

// ============================================================================
// Cross-prop EntryID consistency invariant — every EntryID in a single
// PST embeds the same ProviderUID, equal to PidTagRecordKey.
//
// Catches any future drift where one EntryID generator gets out of sync
// with the message store's PidTagRecordKey.
// ============================================================================
TEST_CASE("Message store EntryIDs all share PidTagRecordKey's ProviderUID",
          "[m6][message_store][cross_prop_invariant]")
{
    MessageStoreSchema schema{};
    // Use a different ProviderUID than the §3.10 sample to confirm the
    // wiring is parameterized correctly.
    schema.providerUid = {{
        0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x23, 0x45, 0x67,
        0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x98,
    }};
    schema.displayNameUtf16le = kSpec310DisplayName.data();
    schema.displayNameSize    = kSpec310DisplayName.size();

    const PcResult result = buildMessageStorePc(schema,
                                                Nid{0x00000041u});
    const auto props = readPropertyContext(result.hnBytes.data(),
                                           result.hnBytes.size());

    // Extract PidTagRecordKey
    const auto* recordKey = findProp(props, 0x0FF9u);
    REQUIRE(recordKey != nullptr);
    REQUIRE(recordKey->valueSize == 16u);

    // Each EntryID's bytes 4..19 must equal PidTagRecordKey verbatim.
    const uint16_t entryIdTags[3] = { 0x35E0u, 0x35E3u, 0x35E7u };
    for (const uint16_t tag : entryIdTags) {
        const auto* eid = findProp(props, tag);
        INFO("EntryID PidTag = 0x" << std::hex << tag);
        REQUIRE(eid != nullptr);
        REQUIRE(eid->valueSize == 24u);
        REQUIRE(std::memcmp(eid->valueBytes + 4,
                            recordKey->valueBytes, 16) == 0);
    }
}

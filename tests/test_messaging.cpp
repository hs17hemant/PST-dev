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

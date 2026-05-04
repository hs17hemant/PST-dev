// pstwriter/messaging.hpp
//
// M6 Messaging Core ([MS-PST] §2.7.1 mandatory-node schemas).
//
// Layered above the M4 LTP builders: each M6 builder takes high-level
// schema input (display names, ProviderUID, target NIDs) and emits an
// HN body via buildPropertyContext / buildTableContext. M6 owns the
// per-NID-type schema selection — what PidTags go in a Message Store
// PC vs a Folder PC vs a Hierarchy TC, and at what default values.
//
// Reader contract: M6 has no separate reader — the M4 readPropertyContext
// already decodes any PC byte-for-byte regardless of source. Round-trip
// tests verify M6 builders' output decodes back to the schema's logical
// inputs.
//
// Spec references (verified via WebFetch 2026-05-04 unless noted):
//   * [MS-PST] §2.7.1 — Mandatory Nodes (27 nodes verbatim)
//   * [MS-PST] §3.10  — Sample Message Store (9-property prose dump)
//   * [MS-PST] §3.12  — Sample Folder Object (4-prop Root PC + 13/27/17-col TCs)
//   * [MS-PST] §2.4.7 — Named Property Lookup Map (intro)

#pragma once

#include "ltp.hpp"
#include "types.hpp"

#include <array>
#include <cstdint>

namespace pstwriter {

// ============================================================================
// EntryID encoding (verified against [MS-PST] §3.10 sample, 2026-05-04).
//
// Layout (24 bytes total):
//   [bytes  0..  3]   rgbFlags    — 4 bytes, all-zero in §3.10 sample
//   [bytes  4.. 19]   ProviderUID — 16-byte GUID; equals message store's
//                                    PidTagRecordKey across the entire PST
//   [bytes 20.. 23]   entryNid    — 4-byte NID, little-endian
//
// rgbFlags = 0 is the M6 default. Outlook may use non-zero rgbFlags in
// some scenarios per [MS-OXCDATA]; pre-Phase-B verification queue item 5.
// ============================================================================
constexpr size_t kEntryIdSize = 24;

std::array<uint8_t, kEntryIdSize>
makeEntryId(const std::array<uint8_t, 16>& providerUid,
            Nid                            entryNid,
            uint32_t                       rgbFlags = 0u) noexcept;

// ============================================================================
// MessageStoreSchema — input to buildMessageStorePc(...).
//
// Defaults match [MS-PST] §3.10's published values where the spec pins
// them. Caller-supplied for fields where §3.10 only shows a sample value
// (display name, ProviderUID).
//
// Invariant the builder honors: ProviderUID is used as PidTagRecordKey
// AND inside every EntryID's bytes 4..19. Single source of truth — no
// way for the GUID to drift across props within one PST.
// ============================================================================
struct MessageStoreSchema {
    // The 16-byte GUID. Single source of truth for the PST's identity.
    // §3.10 sample value: 22 9D B5 0A DC D9 94 43 85 DE 90 AE B0 7D 12 70.
    std::array<uint8_t, 16> providerUid;

    // PidTagDisplayName UTF-16-LE bytes. May be empty (zero-length).
    // §3.10 sample value: "UNICODE1" (16 bytes).
    const uint8_t* displayNameUtf16le {nullptr};
    size_t         displayNameSize    {0};

    // EntryID-target NIDs for the 3 §2.7.1 sub-folders.
    Nid ipmSubTreeNid     {0x00008022u}; // IPM SuBTree
    Nid ipmWastebasketNid {0x00008062u}; // Deleted Items (wastebasket)
    Nid finderNid         {0x00008042u}; // Search Folder objects (finder)

    // PidTagValidFolderMask. §3.10 sample value: 0x89 (bits 0,3,7 set).
    // Bitmask semantics per [MS-OXCSTOR]; pre-Phase-B verification item 8.
    uint32_t validFolderMask {0x00000089u};

    // PidTagPstPassword. §3.10 sample: 0 (no password).
    uint32_t pstPassword {0u};

    // PidTagReplFlags. §3.10 sample: 0.
    uint32_t replFlags {0u};

    // PidTagReplVersionhistory layout: prefix(4) + providerUid(16) + suffix(4).
    // §3.10 sample: prefix=0x00000001, suffix=0x00000001.
    uint32_t replVersionPrefix {0x00000001u};
    uint32_t replVersionSuffix {0x00000001u};

    // §3.9 BTH extras (PidTags not surfaced in §3.10 prose but present in
    // the §3.9 BTH bytes per M5 closure finding). Canonical names not yet
    // identified — pre-Phase-B verification queue item 6.
    uint8_t  pidTag6633Boolean   {1u};            // §3.9 byte: 0x01
    uint32_t pidTag66FAInteger32 {0x000E000Du};   // §3.9 bytes
};

// ============================================================================
// buildMessageStorePc — emit the §2.7.1 NID_MESSAGE_STORE PC.
//
// Returns the HN body (ready to wrap with buildDataBlock). All 11
// properties are inlined or HN-stored — message store has no subnodes.
//
// `firstSubnodeNid` is required by buildPropertyContext's contract but
// is unused for the message store schema; pass any non-HID NID
// (M5Allocator's reserved-NID lookup is the canonical source).
//
// Throws std::invalid_argument on schema validation failure (e.g.
// firstSubnodeNid has nidType==HID).
// ============================================================================
PcResult buildMessageStorePc(const MessageStoreSchema& schema,
                             Nid                       firstSubnodeNid);

// ============================================================================
// FolderPcSchema — input to buildFolderPc(...).
//
// Covers §2.7.1's "PC / Schema Props" rows for NORMAL_FOLDER nodes:
//   * NID_ROOT_FOLDER (0x122)
//   * <IPM SuBTree>   (0x8022)
//   * <Search Folder objects>, the Finder (0x8042)
//   * <Deleted Items>, wastebasket    (0x8062)
//
// Per §3.12, all 4 share the same 4-property schema:
//   0x3001  PidTagDisplayName        PtypString    UTF-16-LE display name
//   0x3602  PidTagContentCount       PtypInteger32 message count in folder
//   0x3603  PidTagContentUnreadCount PtypInteger32 unread message count
//   0x360A  PidTagSubfolders         PtypBoolean   1 iff folder has children
//
// Search Folder PC (NID 0x2223, nidType=SEARCH_FOLDER) has a different
// schema and uses a separate builder (M6.4, not yet implemented).
// ============================================================================
struct FolderPcSchema {
    // PidTagDisplayName UTF-16-LE bytes. May be empty (zero-length).
    const uint8_t* displayNameUtf16le {nullptr};
    size_t         displayNameSize    {0};

    // PidTagContentCount — number of messages in this folder.
    // Always 0 in M6 (messages arrive in M7).
    uint32_t contentCount {0u};

    // PidTagContentUnreadCount — unread-message subset of contentCount.
    uint32_t contentUnreadCount {0u};

    // PidTagSubfolders — true iff this folder has child folders.
    // §3.12 sample: Root Folder = true (3 sub-folders), each sub-folder = ?
    // (sample doesn't show — we default to false; caller overrides for
    // multi-level hierarchies).
    bool hasSubfolders {false};
};

// Build the folder PC for any NORMAL_FOLDER node. Same builder for Root
// and sub-folders — only the schema input varies. The NID itself is
// caller-supplied at NBT-wiring time, not embedded in the PC bytes.
//
// `firstSubnodeNid` is required by buildPropertyContext but is unused
// for this 4-property schema (everything fits inline or in HN). Pass
// any non-HID NID.
PcResult buildFolderPc(const FolderPcSchema& schema,
                       Nid                   firstSubnodeNid);

// ============================================================================
// HierarchyTcRow — input to buildFolderHierarchyTc(...).
//
// One row per child folder. PidTagLtpRowId is the child folder's NID;
// the M4 reader walks the parent NBT entry's HierarchyTC and uses each
// row's RowId to navigate to the child folder's PC.
//
// §3.12 evidence: each row's "present" cells are exactly:
//   LtpRowId, LtpRowVer, DisplayName_W, ContentCount, ContentUnreadCount,
//   ReplChangenum, Subfolders.
// The other 6 columns (ReplItemid, ReplVersionhistory, ReplFlags,
// ContainerClass_W, 0x6635, 0x6636) have CEB bit clear.
// ============================================================================
struct HierarchyTcRow {
    // PidTagLtpRowId — child folder's NID. Required; identifies the row
    // in the RowIndex BTH and in any cross-table references.
    Nid rowId;

    // PidTagLtpRowVer — row version counter. Spec allows 0 for creators.
    uint32_t rowVer {0u};

    // PidTagDisplayName_W — UTF-16-LE bytes of the child folder's name.
    // Stored as a varlen HN allocation referenced by HID from the row data.
    const uint8_t* displayNameUtf16le {nullptr};
    size_t         displayNameSize    {0};

    // PidTagContentCount / ContentUnreadCount — message counts in child folder.
    uint32_t contentCount       {0u};
    uint32_t contentUnreadCount {0u};

    // PidTagSubfolders — true iff the child folder itself has children.
    bool hasSubfolders {false};
};

// ============================================================================
// buildFolderHierarchyTc — emit the §3.12-schema Hierarchy TC.
//
// Used for: NID_HIERARCHY_TABLE_TEMPLATE (0x060D) + per-folder hierarchy
// tables (0x012D, 0x802D, 0x804D, 0x806D from §2.7.1).
//
// 13-column schema per [SPEC §3.12]:
//   tag (sorted ascending) | ibData | cbData | iBit
//   ----------------------------------------------
//   0x0E300003 ReplItemid          | 20 |   4 |  6
//   0x0E330014 ReplChangenum       | 24 |   8 |  7
//   0x0E340102 ReplVersionhistory  | 32 |   4 |  8
//   0x0E380003 ReplFlags           | 36 |   4 |  9
//   0x3001001F DisplayName_W       |  8 |   4 |  2
//   0x36020003 ContentCount        | 12 |   4 |  3
//   0x36030003 ContentUnreadCount  | 16 |   4 |  4
//   0x360A000B Subfolders          | 52 |   1 |  5
//   0x3613001F ContainerClass_W    | 40 |   4 | 10
//   0x66350003 (PstHiddenCount)    | 44 |   4 | 11
//   0x66360003 (PstHiddenUnread)   | 48 |   4 | 12
//   0x67F20003 LtpRowId            |  0 |   4 |  0
//   0x67F30003 LtpRowVer           |  4 |   4 |  1
//
// Per-row endBm = 55 bytes (53 fixed-data + 2-byte CEB).
//
// Pass `rowCount = 0` for the empty-table case used by template TCs and
// by sub-folder Hierarchy tables that have no children.
// ============================================================================
TcResult buildFolderHierarchyTc(const HierarchyTcRow* rows,
                                size_t                rowCount);

// ============================================================================
// buildFolderContentsTc — emit the §3.12-schema 27-column Contents TC.
//
// Used for: NID_CONTENTS_TABLE_TEMPLATE (0x060E) + per-folder contents
// tables (0x012E, 0x802E, 0x804E, 0x806E from §2.7.1).
//
// 27-column schema per [SPEC §3.12], sorted by tag ascending. Per-row
// endBm = 122 (118 fixed + 4 CEB). Always 0-row in M6 — actual message
// rows arrive in M7.
//
// The function takes no row argument: M6 emits "Columns Only" for every
// Contents TC, matching §3.12's published "Row Matrix Data Not Present
// (0 Rows)" state and §2.7.1's "Columns Only" minimal state.
// ============================================================================
TcResult buildFolderContentsTc();

// ============================================================================
// buildFolderFaiContentsTc — emit the §3.12-schema 17-column FAI Contents TC.
//
// Used for: NID_ASSOC_CONTENTS_TABLE_TEMPLATE (0x060F) + per-folder FAI
// tables (0x012F, 0x802F, 0x804F, 0x806F from §2.7.1).
//
// 17-column schema per [SPEC §3.12], sorted by tag ascending. Per-row
// endBm = 68 (65 fixed + 3 CEB). Always 0-row in M6.
// ============================================================================
TcResult buildFolderFaiContentsTc();

} // namespace pstwriter

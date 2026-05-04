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

} // namespace pstwriter

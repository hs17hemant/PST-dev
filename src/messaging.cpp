// pstwriter/src/messaging.cpp
//
// M6 Messaging Core — implementations.
//
// First builder: buildMessageStorePc (§2.7.1 NID_MESSAGE_STORE).
// Subsequent Phase B/C builders (Root Folder PC, sub-folder PCs,
// hierarchy/contents/FAI TCs, templates, recipient/attachment, NameToId
// map) layer on top of the same M4 LTP primitives.

#include "messaging.hpp"

#include "ltp.hpp"
#include "types.hpp"

#include <array>
#include <cstring>
#include <vector>

using namespace std;
using namespace pstwriter;

namespace pstwriter {

// ----------------------------------------------------------------------------
// EntryID encoder — 4-byte rgbFlags + 16-byte ProviderUID + 4-byte NID.
// Verified against [MS-PST] §3.10 sample's 3 EntryID values (2026-05-04).
// ----------------------------------------------------------------------------
array<uint8_t, kEntryIdSize>
makeEntryId(const array<uint8_t, 16>& providerUid,
            Nid                       entryNid,
            uint32_t                  rgbFlags) noexcept
{
    array<uint8_t, kEntryIdSize> out{};
    detail::writeU32(out.data(),  0, rgbFlags);
    std::memcpy   (out.data() + 4, providerUid.data(), 16);
    detail::writeU32(out.data(), 20, entryNid.value);
    return out;
}

// ----------------------------------------------------------------------------
// buildMessageStorePc — 11-property PC matching the §3.10 schema.
//
// Property list and order (sorted by PidTag id ascending — buildPropertyContext
// re-sorts internally so caller order is irrelevant; we list in spec order
// for readability):
//
//   0x0E34  PidTagReplVersionhistory     PtypBinary    24 B  (HN-stored)
//   0x0E38  PidTagReplFlags              PtypInteger32  4 B  (inline)
//   0x0FF9  PidTagRecordKey              PtypBinary    16 B  (HN-stored)
//   0x3001  PidTagDisplayName            PtypString    var.  (HN-stored)
//   0x35DF  PidTagValidFolderMask        PtypInteger32  4 B  (inline)
//   0x35E0  PidTagIpmSubTreeEntryId      PtypBinary    24 B  (HN-stored)
//   0x35E3  PidTagIpmWastebasketEntryId  PtypBinary    24 B  (HN-stored)
//   0x35E7  PidTagFinderEntryId          PtypBinary    24 B  (HN-stored)
//   0x6633  (PstHidden* boolean)         PtypBoolean    1 B  (inline)
//   0x66FA  (PstHidden* int32)           PtypInteger32  4 B  (inline)
//   0x67FF  PidTagPstPassword            PtypInteger32  4 B  (inline)
// ----------------------------------------------------------------------------
PcResult buildMessageStorePc(const MessageStoreSchema& schema,
                             Nid                       firstSubnodeNid)
{
    // ---- HN-stored value buffers ----
    // PidTagReplVersionhistory: prefix(4) + providerUid(16) + suffix(4).
    array<uint8_t, 24> replVersion{};
    detail::writeU32(replVersion.data(),  0, schema.replVersionPrefix);
    std::memcpy   (replVersion.data() + 4, schema.providerUid.data(), 16);
    detail::writeU32(replVersion.data(), 20, schema.replVersionSuffix);

    const auto subtreeEntryId = makeEntryId(schema.providerUid, schema.ipmSubTreeNid);
    const auto wastebasketEid = makeEntryId(schema.providerUid, schema.ipmWastebasketNid);
    const auto finderEntryId  = makeEntryId(schema.providerUid, schema.finderNid);

    // ---- Inline value buffers (LE-packed, portable across endianness) ----
    array<uint8_t, 4> replFlagsBytes{};
    detail::writeU32(replFlagsBytes.data(), 0, schema.replFlags);

    array<uint8_t, 4> validFolderMaskBytes{};
    detail::writeU32(validFolderMaskBytes.data(), 0, schema.validFolderMask);

    array<uint8_t, 1> pidTag6633Bytes{ schema.pidTag6633Boolean };

    array<uint8_t, 4> pidTag66FABytes{};
    detail::writeU32(pidTag66FABytes.data(), 0, schema.pidTag66FAInteger32);

    array<uint8_t, 4> pstPasswordBytes{};
    detail::writeU32(pstPasswordBytes.data(), 0, schema.pstPassword);

    // ---- Property descriptor array ----
    PcProperty props[11] = {
        // PidTagReplVersionhistory (HN-stored, 24 B)
        { 0x0E34u, PropType::Binary,
          replVersion.data(), 24u, PropStorageHint::Auto },
        // PidTagReplFlags (inline)
        { 0x0E38u, PropType::Int32,
          replFlagsBytes.data(), 4u, PropStorageHint::Auto },
        // PidTagRecordKey (HN-stored, 16 B)
        { 0x0FF9u, PropType::Binary,
          schema.providerUid.data(), 16u, PropStorageHint::Auto },
        // PidTagDisplayName (HN-stored, variable)
        { 0x3001u, PropType::Unicode,
          schema.displayNameUtf16le, schema.displayNameSize,
          PropStorageHint::Auto },
        // PidTagValidFolderMask (inline)
        { 0x35DFu, PropType::Int32,
          validFolderMaskBytes.data(), 4u, PropStorageHint::Auto },
        // PidTagIpmSubTreeEntryId (HN-stored, 24 B)
        { 0x35E0u, PropType::Binary,
          subtreeEntryId.data(), 24u, PropStorageHint::Auto },
        // PidTagIpmWastebasketEntryId (HN-stored, 24 B)
        { 0x35E3u, PropType::Binary,
          wastebasketEid.data(), 24u, PropStorageHint::Auto },
        // PidTagFinderEntryId (HN-stored, 24 B)
        { 0x35E7u, PropType::Binary,
          finderEntryId.data(), 24u, PropStorageHint::Auto },
        // PidTag 0x6633 PtypBoolean (inline; §3.9 BTH extra)
        { 0x6633u, PropType::Boolean,
          pidTag6633Bytes.data(), 1u, PropStorageHint::Auto },
        // PidTag 0x66FA PtypInteger32 (inline; §3.9 BTH extra)
        { 0x66FAu, PropType::Int32,
          pidTag66FABytes.data(), 4u, PropStorageHint::Auto },
        // PidTagPstPassword (inline)
        { 0x67FFu, PropType::Int32,
          pstPasswordBytes.data(), 4u, PropStorageHint::Auto },
    };

    return buildPropertyContext(props, 11, firstSubnodeNid);
}

// ----------------------------------------------------------------------------
// buildFolderPc — 4-property PC for NORMAL_FOLDER nodes (§3.12 schema).
//
// Used for: Root Folder (NID 0x122), IPM SuBTree (0x8022), Finder (0x8042),
// Deleted Items (0x8062), and any caller-defined sub-folder.
// ----------------------------------------------------------------------------
PcResult buildFolderPc(const FolderPcSchema& schema,
                       Nid                   firstSubnodeNid)
{
    // Inline value buffers (LE-packed).
    array<uint8_t, 4> contentCountBytes{};
    detail::writeU32(contentCountBytes.data(), 0, schema.contentCount);

    array<uint8_t, 4> contentUnreadBytes{};
    detail::writeU32(contentUnreadBytes.data(), 0, schema.contentUnreadCount);

    array<uint8_t, 1> subfoldersBytes{
        static_cast<uint8_t>(schema.hasSubfolders ? 1u : 0u)
    };

    PcProperty props[4] = {
        // PidTagDisplayName (HN-stored, variable)
        { 0x3001u, PropType::Unicode,
          schema.displayNameUtf16le, schema.displayNameSize,
          PropStorageHint::Auto },
        // PidTagContentCount (inline)
        { 0x3602u, PropType::Int32,
          contentCountBytes.data(), 4u, PropStorageHint::Auto },
        // PidTagContentUnreadCount (inline)
        { 0x3603u, PropType::Int32,
          contentUnreadBytes.data(), 4u, PropStorageHint::Auto },
        // PidTagSubfolders (inline, Boolean — 1 byte zero-extended to 4)
        { 0x360Au, PropType::Boolean,
          subfoldersBytes.data(), 1u, PropStorageHint::Auto },
    };

    return buildPropertyContext(props, 4, firstSubnodeNid);
}

// ----------------------------------------------------------------------------
// buildFolderHierarchyTc — 13-column TC matching §3.12 Hierarchy schema.
// ----------------------------------------------------------------------------
namespace {

// §3.12 13-column schema (sorted by PidTag-tag ascending — buildTableContext
// re-sorts internally so caller order is irrelevant; we list in spec order
// for readability).
constexpr TcColumn kHierarchyCols[13] = {
    // PidTag, propType, ibData, cbData, iBit
    { 0x0E30u, PropType::Int32,    20,  4,  6 },  // ReplItemid
    { 0x0E33u, PropType::Int64,    24,  8,  7 },  // ReplChangenum (PtypInteger64 0x14)
    { 0x0E34u, PropType::Binary,   32,  4,  8 },  // ReplVersionhistory (HID)
    { 0x0E38u, PropType::Int32,    36,  4,  9 },  // ReplFlags
    { 0x3001u, PropType::Unicode,   8,  4,  2 },  // DisplayName_W (HID)
    { 0x3602u, PropType::Int32,    12,  4,  3 },  // ContentCount
    { 0x3603u, PropType::Int32,    16,  4,  4 },  // ContentUnreadCount
    { 0x360Au, PropType::Boolean,  52,  1,  5 },  // Subfolders
    { 0x3613u, PropType::Unicode,  40,  4, 10 },  // ContainerClass_W (HID)
    { 0x6635u, PropType::Int32,    44,  4, 11 },  // (PstHiddenCount)
    { 0x6636u, PropType::Int32,    48,  4, 12 },  // (PstHiddenUnread)
    { 0x67F2u, PropType::Int32,     0,  4,  0 },  // LtpRowId
    { 0x67F3u, PropType::Int32,     4,  4,  1 },  // LtpRowVer
};

// Per-row matrix size derived from rgib (53 fixed + 2 CEB = 55).
constexpr size_t kHierarchyRowSize = 55;
// CEB byte count = ceil(13 / 8) = 2.
constexpr size_t kHierarchyCebSize = 2;

// CEB bit pattern for a "present" row: which columns of the 13 are
// actually populated. Per §3.12 Row 0 (and the M6 default schema):
// LtpRowId(0), LtpRowVer(1), DisplayName_W(2), ContentCount(3),
// ContentUnreadCount(4), Subfolders(5), ReplChangenum(7) — 7 cells set.
// Bit layout: byte 0 high-bit-first for iBit 0..7; byte 1 high-bit-first
// for iBit 8..15.
//   byte 0: iBit 0,1,2,3,4,5,_,7 → 0b11111101 = 0xFD
//   byte 1: (iBit 8..12 all clear; iBit 13..15 unused) → 0x00
constexpr uint8_t kHierarchyCebByte0 = 0xFDu;
constexpr uint8_t kHierarchyCebByte1 = 0x00u;

} // namespace

TcResult buildFolderHierarchyTc(const HierarchyTcRow* rows, size_t rowCount)
{
    // Pre-pack each row's 55 bytes. Varlen DisplayName_W cells are slotted
    // separately as TcVarlenCell entries; the writer patches the row's
    // 4-byte HID slot at ibData=8 with the assigned HID.
    vector<array<uint8_t, kHierarchyRowSize>> rowBuffers(rowCount);
    vector<TcVarlenCell>                      varlenStore;
    varlenStore.reserve(rowCount);            // 1 varlen cell per row (DisplayName)

    // Per-row varlen cells need stable storage during the buildTableContext
    // call. We use a flat vector and per-row slices.
    vector<TcRow>          tcRows(rowCount);
    vector<vector<TcVarlenCell>> perRowVarlen(rowCount);

    for (size_t r = 0; r < rowCount; ++r) {
        const HierarchyTcRow& src = rows[r];
        uint8_t* dst = rowBuffers[r].data();
        std::memset(dst, 0, kHierarchyRowSize);

        // Bytes 0..3:   LtpRowId
        detail::writeU32(dst,  0, src.rowId.value);
        // Bytes 4..7:   LtpRowVer
        detail::writeU32(dst,  4, src.rowVer);
        // Bytes 8..11:  DisplayName_W HID (placeholder; writer patches)
        // Bytes 12..15: ContentCount
        detail::writeU32(dst, 12, src.contentCount);
        // Bytes 16..19: ContentUnreadCount
        detail::writeU32(dst, 16, src.contentUnreadCount);
        // Bytes 20..23: ReplItemid (zero, CEB clear)
        // Bytes 24..31: ReplChangenum (zero, CEB set per §3.12)
        // Bytes 32..35: ReplVersionhistory HID (zero, CEB clear)
        // Bytes 36..39: ReplFlags (zero, CEB clear)
        // Bytes 40..43: ContainerClass_W HID (zero, CEB clear)
        // Bytes 44..47: 0x6635 (zero, CEB clear)
        // Bytes 48..51: 0x6636 (zero, CEB clear)
        // Byte  52:     Subfolders
        dst[52] = src.hasSubfolders ? 1u : 0u;
        // Bytes 53..54: CEB
        dst[53] = kHierarchyCebByte0;
        dst[54] = kHierarchyCebByte1;

        // Find the column-index of DisplayName_W in our schema (its
        // colIndex is the position in kHierarchyCols, which is sorted by
        // tag ascending — DisplayName tag 0x3001001F is at index 4).
        constexpr size_t kDisplayNameColIdx = 4;

        if (src.displayNameUtf16le != nullptr && src.displayNameSize > 0) {
            perRowVarlen[r].push_back({ kDisplayNameColIdx,
                                        src.displayNameUtf16le,
                                        src.displayNameSize });
        }

        tcRows[r].rowId       = src.rowId.value;
        tcRows[r].rowBytes    = rowBuffers[r].data();
        tcRows[r].rowSize     = kHierarchyRowSize;
        tcRows[r].varlenCells = perRowVarlen[r].data();
        tcRows[r].varlenCount = perRowVarlen[r].size();
    }

    return buildTableContext(kHierarchyCols, 13,
                             tcRows.data(), rowCount);
}

// ----------------------------------------------------------------------------
// buildFolderContentsTc — 27-column Contents TC matching §3.12 schema.
// Always 0-row in M6.
// ----------------------------------------------------------------------------
namespace {

constexpr TcColumn kContentsCols[27] = {
    // tag-sorted ascending
    { 0x0017u, PropType::Int32,      20, 4,  5 },  // Importance
    { 0x001Au, PropType::Unicode,    12, 4,  3 },  // MessageClass_W
    { 0x0036u, PropType::Int32,      60, 4, 15 },  // Sensitivity
    { 0x0037u, PropType::Unicode,    28, 4,  7 },  // Subject_W
    { 0x0039u, PropType::SystemTime, 40, 8,  9 },  // ClientSubmitTime
    { 0x0042u, PropType::Unicode,    24, 4,  6 },  // SentRepresentingName_W
    { 0x0057u, PropType::Boolean,   116, 1, 13 },  // MessageToMe
    { 0x0058u, PropType::Boolean,   117, 1, 14 },  // MessageCcMe
    { 0x0070u, PropType::Unicode,    68, 4, 17 },  // ConversationTopic_W
    { 0x0071u, PropType::Binary,     72, 4, 18 },  // ConversationIndex
    { 0x0E03u, PropType::Unicode,    56, 4, 12 },  // DisplayCc_W
    { 0x0E04u, PropType::Unicode,    52, 4, 11 },  // DisplayTo_W
    { 0x0E06u, PropType::SystemTime, 32, 8,  8 },  // MessageDeliveryTime
    { 0x0E07u, PropType::Int32,      16, 4,  4 },  // MessageFlags
    { 0x0E08u, PropType::Int32,      48, 4, 10 },  // MessageSize
    { 0x0E17u, PropType::Int32,       8, 4,  2 },  // MessageStatus
    { 0x0E30u, PropType::Int32,      88, 4, 21 },  // ReplItemId
    { 0x0E33u, PropType::Int64,      92, 8, 22 },  // ReplChangenum
    { 0x0E34u, PropType::Binary,    100, 4, 23 },  // ReplVersionhistory
    { 0x0E38u, PropType::Int32,     112, 4, 26 },  // ReplFlags
    { 0x0E3Cu, PropType::Binary,    108, 4, 25 },  // ReplCopiedfromVersionhistory
    { 0x0E3Du, PropType::Binary,    104, 4, 24 },  // ReplCopiedfromItemid
    { 0x1097u, PropType::Int32,      64, 4, 16 },  // ItemTemporaryFlags
    { 0x3008u, PropType::SystemTime, 80, 8, 20 },  // LastModificationTime
    { 0x65C6u, PropType::Int32,      76, 4, 19 },  // SecureSubmitFlags
    { 0x67F2u, PropType::Int32,       0, 4,  0 },  // LtpRowId
    { 0x67F3u, PropType::Int32,       4, 4,  1 },  // LtpRowVer
};

constexpr TcColumn kFaiContentsCols[17] = {
    // tag-sorted ascending
    { 0x001Au, PropType::Unicode,    12, 4,  3 },  // MessageClass_W
    { 0x003Au, PropType::Unicode,    60, 4, 16 },  // ReportName_W
    { 0x0070u, PropType::Unicode,    56, 4, 15 },  // ConversationTopic_W
    { 0x0E07u, PropType::Int32,      16, 4,  4 },  // MessageFlags
    { 0x0E17u, PropType::Int32,       8, 4,  2 },  // MessageStatus
    { 0x3001u, PropType::Unicode,    20, 4,  5 },  // DisplayName_W
    { 0x67F2u, PropType::Int32,       0, 4,  0 },  // LtpRowId
    { 0x67F3u, PropType::Int32,       4, 4,  1 },  // LtpRowVer
    { 0x6800u, PropType::Unicode,    44, 4, 11 },  // MapiformMessageclass_W (alias)
    { 0x6803u, PropType::Boolean,    64, 1, 12 },  // FormMultCategorized (alias)
    { 0x6805u, PropType::MvInt32,    48, 4, 13 },  // OfflineAddressBookTruncatedProperties
    { 0x682Fu, PropType::Unicode,    52, 4, 14 },  // ReplItemid (Unicode in FAI per §3.12)
    { 0x7003u, PropType::Int32,      24, 4,  6 },  // ViewDescriptorFlags
    { 0x7004u, PropType::Binary,     28, 4,  7 },  // ViewDescriptorLinkTo
    { 0x7005u, PropType::Binary,     32, 4,  8 },  // ViewDescriptorViewFolder
    { 0x7006u, PropType::Unicode,    36, 4,  9 },  // ViewDescriptorName_W
    { 0x7007u, PropType::Int32,      40, 4, 10 },  // ViewDescriptorVersion
};

} // namespace

TcResult buildFolderContentsTc()
{
    return buildTableContext(kContentsCols, 27, nullptr, 0);
}

TcResult buildFolderFaiContentsTc()
{
    return buildTableContext(kFaiContentsCols, 17, nullptr, 0);
}

} // namespace pstwriter

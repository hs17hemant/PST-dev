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

} // namespace pstwriter

// pstwriter/ltp.hpp
//
// LTP layer ([MS-PST] §2.3) — Heap-on-Node, BTH, PropertyContext,
// TableContext.
//
// M4 first cut delivers `buildHeapOnNode(...)` — the foundation that
// BTH/PC/TC all sit on. Higher-level builders are layered on top.
//
// This header is the M4 API surface; src/ltp.cpp holds the
// implementations.

#pragma once

#include "types.hpp"

#include <array>
#include <cstdint>
#include <vector>

using namespace std;

namespace pstwriter {

// ============================================================================
// HID encoding ([MS-PST] §2.3.1.1) — 32 bits packed:
//   bits  0..4   hidType         (0 for HN-resident HIDs; non-zero indicates
//                                  HNID variants like NID-as-subnode)
//   bits  5..15  hidIndex        1-based index into HNPAGEMAP.rgibAlloc[]
//   bits 16..31  hidBlockIndex   0 for first HN block, 1..N for subsequent
//
// Helpers: build a HID from its parts and decode.
// ============================================================================
constexpr uint32_t makeHid(uint16_t hidIndex1Based,
                           uint16_t hidBlockIndex = 0,
                           uint8_t  hidType       = 0) noexcept
{
    return static_cast<uint32_t>(hidType & 0x1Fu)
         | (static_cast<uint32_t>(hidIndex1Based & 0x07FFu) << 5)
         | (static_cast<uint32_t>(hidBlockIndex) << 16);
}

constexpr uint8_t  hidType (uint32_t hid) noexcept { return static_cast<uint8_t>(hid & 0x1Fu); }
constexpr uint16_t hidIndex(uint32_t hid) noexcept { return static_cast<uint16_t>((hid >> 5) & 0x07FFu); }
constexpr uint16_t hidBlock(uint32_t hid) noexcept { return static_cast<uint16_t>((hid >> 16) & 0xFFFFu); }

// ============================================================================
// HN block constants ([MS-PST] §2.3.1)
// ============================================================================
constexpr uint8_t  kHnSignature  = 0xEC;  // bSig
constexpr uint8_t  kBthSignature = 0xB5;  // BTHHEADER.bType
constexpr uint8_t  kTcSignature  = 0x7C;  // TCINFO.bType

// bClientSig values ([MS-PST] §2.3.1.2)
constexpr uint8_t  kBClientSigPC = 0xBC;  // bTypePC — Property Context
constexpr uint8_t  kBClientSigTC = 0x7C;  // bTypeTC — Table Context

constexpr size_t   kHnHdrSize        = 12;   // HNHDR
constexpr size_t   kHnPageMapHdrSize = 4;    // cAlloc + cFree
constexpr size_t   kHnPageMapEntrySize = 2;  // each rgibAlloc[i]

// ============================================================================
// HnAllocation — opaque view onto one user allocation.
//
// Lifetime: the bytes pointed to must outlive the buildHeapOnNode call.
// ============================================================================
struct HnAllocation {
    const uint8_t* data;
    size_t         size;
};

// ============================================================================
// buildHeapOnNode — emit the structured body of a single-block HN.
//
// Layout produced (matches [MS-PST] §2.3.1):
//   [0          .. 11]            HNHDR (ibHnpm, bSig=0xEC, bClientSig,
//                                  hidUserRoot, rgbFillLevel)
//   [12         .. 12+sumSizes-1] user allocations laid out contiguously
//                                  in the order given (no per-allocation
//                                  alignment — the spec doesn't require it
//                                  and the §3.8 sample shows variable-sized
//                                  records packed tight)
//   [ibHnpm     .. end-1]         HNPAGEMAP (cAlloc, cFree, rgibAlloc[N+1])
//
// `bClientSig` distinguishes PC (0xBC) / TC (0x7C) / NameID (0x7F) /
// Subnode (0x80) HNs.
//
// `hidUserRoot` is the HID of the allocation that holds the client's
// metadata (BTHHEADER for PC/TC RowIndex, TCINFO for TC). Caller is
// responsible for picking the right value from the allocation list:
//   makeHid(1) = HID 0x20  → first allocation
//   makeHid(2) = HID 0x40  → second
//   ...
//
// `rgbFillLevel` defaults to all zero. The §3.8 / §3.11 samples show
// {0,0,0,0} even though the blocks have lots of free space — Outlook's
// fill-level updates appear to lag actual occupancy. We accept the
// default and let M5+ override if a real Outlook strategy emerges.
//
// Returns the structured HN body. Caller wraps it in a data block via
// buildDataBlock(...) to produce the on-disk form (encrypts, appends
// BLOCKTRAILER, 64-byte aligns).
vector<uint8_t> buildHeapOnNode(const HnAllocation* allocs,
                                size_t              allocCount,
                                uint8_t             bClientSig,
                                uint32_t            hidUserRoot,
                                array<uint8_t, 4>   rgbFillLevel = {{0, 0, 0, 0}}) noexcept;

// ============================================================================
// Convenience constructors for the structured fields callers will need
// to build before passing them as HN allocations.
// ============================================================================

// Encode an 8-byte BTHHEADER ([MS-PST] §2.3.2.1).
//   cbKey       in {1, 2, 4, 8, 16}
//   cbEnt       in {1..32}
//   bIdxLevels  0 for leaf-only BTH; 1..6 for intermediate index levels
//   hidRoot     HID of the first BTH page (intermediate or leaf) inside
//               the parent HN
array<uint8_t, 8> encodeBthHeader(uint8_t  cbKey,
                                  uint8_t  cbEnt,
                                  uint8_t  bIdxLevels,
                                  uint32_t hidRoot) noexcept;

} // namespace pstwriter

// pstwriter/src/ltp.cpp
//
// LTP layer implementation ([MS-PST] §2.3).
//
// First cut: buildHeapOnNode + encodeBthHeader. BTH/PC/TC builders
// layer on top in subsequent files.

#include "ltp.hpp"

#include "types.hpp"

#include <cassert>
#include <cstring>

using namespace std;
using namespace pstwriter;
using detail::writeU8;
using detail::writeU16;
using detail::writeU32;

namespace pstwriter {

// ----------------------------------------------------------------------------
// buildHeapOnNode
// ----------------------------------------------------------------------------
vector<uint8_t> buildHeapOnNode(const HnAllocation* allocs,
                                size_t              allocCount,
                                uint8_t             bClientSig,
                                uint32_t            hidUserRoot,
                                array<uint8_t, 4>   rgbFillLevel) noexcept
{
    // ------------------------------------------------------------------
    // Compute the layout.
    //   ibAlloc[i]   = absolute heap offset of allocation i
    //   ibHnpm       = absolute offset of the HNPAGEMAP
    //
    // The HNPAGEMAP is the FINAL element of the structured body: it
    // immediately follows the last allocation. Total HN body size =
    // ibHnpm + HNPAGEMAP size.
    // ------------------------------------------------------------------
    vector<uint16_t> ibAlloc;
    ibAlloc.reserve(allocCount + 1);
    uint16_t cursor = static_cast<uint16_t>(kHnHdrSize);  // first alloc starts after HNHDR
    for (size_t i = 0; i < allocCount; ++i) {
        ibAlloc.push_back(cursor);
        cursor = static_cast<uint16_t>(cursor + allocs[i].size);
    }
    ibAlloc.push_back(cursor);  // sentinel: where the next-to-allocate would begin

    // HNPAGEMAP must start at a 4-byte (DWORD) boundary. Empirically
    // confirmed by [MS-PST] §3.8 (allocs end at 0xEC, naturally DWORD-
    // aligned, ibHnpm=0xEC) AND §3.11 (allocs end at 0x1BB, 1 byte of
    // alignment pad before ibHnpm=0x1BC). The pad bytes between the
    // sentinel and ibHnpm are zero-filled and belong to neither the
    // user allocations nor the HNPAGEMAP.
    const uint16_t ibHnpm = static_cast<uint16_t>((cursor + 3u) & ~uint32_t{3u});

    const size_t   pageMapBytes  = kHnPageMapHdrSize
                                 + (allocCount + 1) * kHnPageMapEntrySize;
    const size_t   totalSize     = static_cast<size_t>(ibHnpm) + pageMapBytes;

    vector<uint8_t> out(totalSize, 0u);

    // ------------------------------------------------------------------
    // HNHDR ([MS-PST] §2.3.1.2) — bytes [0..11]
    // ------------------------------------------------------------------
    writeU16(out.data(), 0, ibHnpm);                  // ibHnpm
    writeU8 (out.data(), 2, kHnSignature);            // bSig = 0xEC
    writeU8 (out.data(), 3, bClientSig);              // bClientSig
    writeU32(out.data(), 4, hidUserRoot);             // hidUserRoot
    out[ 8] = rgbFillLevel[0];
    out[ 9] = rgbFillLevel[1];
    out[10] = rgbFillLevel[2];
    out[11] = rgbFillLevel[3];

    // ------------------------------------------------------------------
    // User allocations — copied verbatim.
    // ------------------------------------------------------------------
    for (size_t i = 0; i < allocCount; ++i) {
        if (allocs[i].size == 0) continue;
        std::memcpy(out.data() + ibAlloc[i], allocs[i].data, allocs[i].size);
    }

    // ------------------------------------------------------------------
    // HNPAGEMAP ([MS-PST] §2.3.1.5) — at offset ibHnpm.
    // ------------------------------------------------------------------
    const size_t pmOff = ibHnpm;
    writeU16(out.data(), pmOff + 0, static_cast<uint16_t>(allocCount));  // cAlloc
    writeU16(out.data(), pmOff + 2, 0u);                                  // cFree
    for (size_t i = 0; i <= allocCount; ++i) {
        writeU16(out.data(),
                 pmOff + kHnPageMapHdrSize + i * kHnPageMapEntrySize,
                 ibAlloc[i]);
    }

    return out;
}

// ----------------------------------------------------------------------------
// encodeBthHeader
// ----------------------------------------------------------------------------
array<uint8_t, 8> encodeBthHeader(uint8_t  cbKey,
                                  uint8_t  cbEnt,
                                  uint8_t  bIdxLevels,
                                  uint32_t hidRoot) noexcept
{
    array<uint8_t, 8> out{};
    writeU8 (out.data(), 0, kBthSignature);  // bType = 0xB5
    writeU8 (out.data(), 1, cbKey);
    writeU8 (out.data(), 2, cbEnt);
    writeU8 (out.data(), 3, bIdxLevels);
    writeU32(out.data(), 4, hidRoot);
    return out;
}

} // namespace pstwriter

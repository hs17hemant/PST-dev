// pstwriter/writer.hpp
//
// Public API for creating Unicode PST files.
//
// M2 scope: produce a 5-page skeleton (HEADER + zero pad + AMap + empty
// NBT leaf + empty BBT leaf) that opens cleanly in Outlook.
// M3+ will add block writing; M5+ adds folders/messages.
//
// Layout produced by Writer::create() in M2 (SPEC_GROUND_TRUTH):
//
//   0x000..0x233  HEADER (564 bytes, including ROOT at 0xB4..0xFB)
//   0x234..0x3FF  Zero padding (460 bytes)
//   0x400..0x5FF  AMap page (ptype 0x84)
//   0x600..0x7FF  Empty NBT root leaf (ptype 0x81)
//   0x800..0x9FF  Empty BBT root leaf (ptype 0x80)
//   ibFileEof   = 0x0A00

#pragma once

#include "types.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace std;

namespace pstwriter {

// Result of a Writer operation. Set `ok == false` and inspect `message`
// for failure detail. The library never throws.
struct WriteResult {
    bool   ok      {true};
    string message {};   // empty when ok == true
};

// M2: write an empty 5-page skeleton at `path`. The file is created (or
// truncated if it already exists). Crypt method is fixed at PERMUTE.
//
// Returns ok=true on success. On failure, `message` contains a short
// human-readable cause (e.g. "fopen failed: <errno>", "fwrite truncated").
WriteResult writeEmptyPst(const string& path) noexcept;

// ============================================================================
// M3: multi-block PST writer.
//
// `dataBlocks` is a list of pre-encryption payloads, one per data block
// the caller wants registered in the BBT.  Crypt method is PERMUTE.
//
// File layout produced:
//   0x000..0x233   HEADER
//   0x234..0x3FF   zero pad
//   0x400..0x5FF   AMap
//   0x600..        data blocks, packed at 64-byte alignment
//                  (zero-padded to next 0x200 boundary)
//   ..             1+ BBT leaves (filled with BBTENTRY for every block)
//                  + 1 BBT intermediate page when leaves > 1
//   ..             empty NBT leaf (no nodes registered yet — M5)
//   ibFileEof     end of last page
//
// NIDs / NBT entries are intentionally absent; this milestone is about
// making the BBT correct and pst_info-clean.  Real Outlook would treat
// these blocks as orphaned, but the M3 gate is byte-level integrity, not
// Outlook acceptance.
//
// Caller-side limit: number of blocks must produce a BBT that fits in
// at most 20 leaves (= 400 blocks), since the M3 BBT only supports a
// single intermediate level.  Returns failure if exceeded.
// ============================================================================
WriteResult writeBlocksPst(const string&                 path,
                           const vector<vector<uint8_t>>& dataBlocks) noexcept;

// ============================================================================
// M3: write a PST with one large logical payload split across multiple
// data blocks indirected by a single XBLOCK.  Useful as the M3
// "multi-XBLOCK" gate file.
//
// `payload` is split into chunks of `chunkSize` bytes (last chunk may be
// smaller).  Caller must keep total payload <= 8176 * 1019 (one XBLOCK's
// addressing range) — beyond that you'd need an XXBLOCK and we'll wire
// that up in M4.
// ============================================================================
WriteResult writeXBlockPst(const string&  path,
                           const uint8_t* payload,
                           size_t         cbPayload,
                           size_t         chunkSize) noexcept;

} // namespace pstwriter

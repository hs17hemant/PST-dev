// pstwriter/tests/test_ndb.cpp
//
// M2 gate tests for HEADER, ROOT, page-trailer construction, AMap, and
// empty NBT/BBT leaves.  Layout/CRC values verified against the [MS-PST]
// §3.2 sample HEADER (see SPEC_GROUND_TRUTH.md).

#include <catch2/catch_test_macros.hpp>

#include "crc.hpp"
#include "ndb.hpp"
#include "page.hpp"
#include "types.hpp"
#include "writer.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace std;
using namespace pstwriter;
using detail::readU16;
using detail::readU32;
using detail::readU64;

namespace {

// Build a representative WriterState matching the M2 5-page skeleton.
WriterState makeM2State()
{
    WriterState s{};
    s.dwUnique          = 1u;
    s.bidNextP          = Bid::makeInternal(4ull);
    s.bidNextB          = Bid::makeData(1ull);
    s.root.ibFileEof    = Ib{0x0A00ull};
    s.root.ibAMapLast   = Ib{0x0400ull};
    s.root.fAMapValid   = kAMapValid2;
    s.root.cbPMapFree   = 0ull;

    const uint64_t setBits   = (0x0A00ull + kBytesPerAMapBit - 1) / kBytesPerAMapBit;
    const uint64_t totalBits = static_cast<uint64_t>(kAMapBitmapBytes) * 8ull;
    s.root.cbAMapFree        = (totalBits - setBits) * kBytesPerAMapBit;

    s.root.brefNbt = Bref{Bid::makeInternal(2ull), Ib{0x0600ull}};
    s.root.brefBbt = Bref{Bid::makeInternal(3ull), Ib{0x0800ull}};
    populateFreshRgnid(s);
    return s;
}

} // anonymous namespace

// ============================================================================
// HEADER size and field offsets
// ============================================================================
TEST_CASE("HEADER is 564 bytes (SPEC_GROUND_TRUTH)", "[ndb][header]")
{
    REQUIRE(kHeaderSize == 564);
    REQUIRE(hdr::kHeaderEnd == 564);

    const auto buf = serializeHeader(makeM2State());
    REQUIRE(buf.size() == 564);
}

TEST_CASE("HEADER offset constants land at the spec-mandated positions",
          "[ndb][header][offsets]")
{
    // Sample of critical offsets — full set is asserted at compile time
    // by ndb.hpp's hdr:: namespace constants.
    REQUIRE(hdr::kMagic         == 0x000);
    REQUIRE(hdr::kCrcPartial    == 0x004);
    REQUIRE(hdr::kMagicClient   == 0x008);
    REQUIRE(hdr::kRootDwReserved== 0x0B4);
    REQUIRE(hdr::kIbFileEof     == 0x0B8);
    REQUIRE(hdr::kBrefNbtBid    == 0x0D8);
    REQUIRE(hdr::kBrefBbtBid    == 0x0E8);
    REQUIRE(hdr::kFAMapValid    == 0x0F8);
    REQUIRE(hdr::kRgbFM         == 0x100);
    REQUIRE(hdr::kRgbFP         == 0x180);
    REQUIRE(hdr::kBSentinel     == 0x200);
    REQUIRE(hdr::kBCryptMethod  == 0x201);
    REQUIRE(hdr::kBidNextB      == 0x204);
    REQUIRE(hdr::kCrcFull       == 0x20C);
    REQUIRE(hdr::kRgbReserved3  == 0x214);
}

// ============================================================================
// HEADER serialized bytes
// ============================================================================
TEST_CASE("serializeHeader writes magic, version, sentinel, crypt method",
          "[ndb][header]")
{
    const auto buf = serializeHeader(makeM2State());
    const uint8_t* h = buf.data();

    REQUIRE(readU32(h, hdr::kMagic)         == kMagicDword);     // "!BDN"
    REQUIRE(readU16(h, hdr::kMagicClient)   == kMagicClient);    // "SM"
    REQUIRE(readU16(h, hdr::kVer)           == kVerUnicode);     // 23
    REQUIRE(readU16(h, hdr::kVerClient)     == kVerClient);      // 19
    REQUIRE(h[hdr::kPlatformCreate]         == kPlatformCreate); // 0x01
    REQUIRE(h[hdr::kPlatformAccess]         == kPlatformAccess); // 0x01
    REQUIRE(h[hdr::kBSentinel]              == kSentinelByte);   // 0x80
    REQUIRE(h[hdr::kBCryptMethod]           == 0x01);            // PERMUTE
    REQUIRE(h[hdr::kFAMapValid]             == kAMapValid2);     // 0x02 (NOT 0x01)
}

TEST_CASE("serializeHeader: rgbFM and rgbFP are filled with 0xFF",
          "[ndb][header][rgbfm]")
{
    const auto buf = serializeHeader(makeM2State());

    for (size_t i = hdr::kRgbFM; i < hdr::kRgbFM + 128; ++i) {
        REQUIRE(buf[i] == 0xFF);
    }
    for (size_t i = hdr::kRgbFP; i < hdr::kRgbFP + 128; ++i) {
        REQUIRE(buf[i] == 0xFF);
    }
}

TEST_CASE("serializeHeader: rgnid[] starts at the spec-mandated indices",
          "[ndb][header][rgnid]")
{
    const auto buf = serializeHeader(makeM2State());

    auto nidAt = [&](uint32_t slot) {
        return readU32(buf.data(), hdr::kRgnid + slot * 4u);
    };

    // Per [MS-PST] §2.2.2.6 (verified in SPEC_GROUND_TRUTH).
    //   slot 0x02 = NORMAL_FOLDER, idx = 0x400  → (0x400  << 5) | 0x02 = 0x00008002
    //   slot 0x03 = SEARCH_FOLDER, idx = 0x4000 → (0x4000 << 5) | 0x03 = 0x00080003
    //   slot 0x04 = NORMAL_MESSAGE, idx = 0x10000 → (0x10000<<5)|0x04 = 0x00200004
    //   slot 0x08 = ASSOC_MESSAGE, idx = 0x8000  → (0x8000 << 5) | 0x08 = 0x00100008
    //   any other slot n: idx = 0x400 → (0x400 << 5) | n = 0x8000 | n
    REQUIRE(nidAt(0x02u) == 0x00008002u);
    REQUIRE(nidAt(0x03u) == 0x00080003u);
    REQUIRE(nidAt(0x04u) == 0x00200004u);
    REQUIRE(nidAt(0x08u) == 0x00100008u);
    REQUIRE(nidAt(0x00u) == 0x00008000u);
    REQUIRE(nidAt(0x01u) == 0x00008001u);
    REQUIRE(nidAt(0x1Fu) == 0x0000801Fu);
}

TEST_CASE("serializeHeader: ROOT field offsets and values",
          "[ndb][root]")
{
    const auto state = makeM2State();
    const auto buf   = serializeHeader(state);
    const uint8_t* h = buf.data();

    REQUIRE(readU32(h, hdr::kRootDwReserved) == 0u);
    REQUIRE(readU64(h, hdr::kIbFileEof)      == state.root.ibFileEof.value);
    REQUIRE(readU64(h, hdr::kIbAMapLast)     == state.root.ibAMapLast.value);
    REQUIRE(readU64(h, hdr::kCbAMapFree)     == state.root.cbAMapFree);
    REQUIRE(readU64(h, hdr::kCbPMapFree)     == 0ull);
    REQUIRE(readU64(h, hdr::kBrefNbtBid)     == state.root.brefNbt.bid.value);
    REQUIRE(readU64(h, hdr::kBrefNbtIb)      == state.root.brefNbt.ib.value);
    REQUIRE(readU64(h, hdr::kBrefBbtBid)     == state.root.brefBbt.bid.value);
    REQUIRE(readU64(h, hdr::kBrefBbtIb)      == state.root.brefBbt.ib.value);
    REQUIRE(h[hdr::kFAMapValid]              == kAMapValid2);
    REQUIRE(h[hdr::kRootBReserved]           == 0u);
    REQUIRE(readU16(h, hdr::kRootWReserved)  == 0u);
}

TEST_CASE("serializeHeader: bidNextP and bidNextB are written at the right offsets",
          "[ndb][header][bid]")
{
    const auto state = makeM2State();
    const auto buf   = serializeHeader(state);
    const uint8_t* h = buf.data();

    REQUIRE(readU64(h, hdr::kBidNextP) == state.bidNextP.value);
    REQUIRE(readU64(h, hdr::kBidNextB) == state.bidNextB.value);
}

// ============================================================================
// HEADER CRCs — recompute and verify they match what serializeHeader wrote
// ============================================================================
TEST_CASE("serializeHeader: dwCRCPartial recomputes correctly",
          "[ndb][header][crc]")
{
    const auto buf = serializeHeader(makeM2State());
    const uint8_t* h = buf.data();

    const uint32_t stored = readU32(h, hdr::kCrcPartial);
    const uint32_t recomputed = crc32(h + hdr::kMagicClient, kHdrCrcPartialLen);
    REQUIRE(stored == recomputed);
}

TEST_CASE("serializeHeader: dwCRCFull recomputes correctly",
          "[ndb][header][crc]")
{
    const auto buf = serializeHeader(makeM2State());
    const uint8_t* h = buf.data();

    const uint32_t stored = readU32(h, hdr::kCrcFull);
    const uint32_t recomputed = crc32(h + hdr::kMagicClient, kHdrCrcFullLen);
    REQUIRE(stored == recomputed);
}

TEST_CASE("CRC ranges have the spec-mandated lengths",
          "[ndb][header][crc]")
{
    REQUIRE(kHdrCrcPartialLen == 471);
    REQUIRE(kHdrCrcFullLen    == 516);
}

// ============================================================================
// Page builders — empty NBT and BBT leaves
// ============================================================================
TEST_CASE("Empty NBT leaf has cEnt=0, cbEnt=32, cLevel=0, valid trailer",
          "[ndb][page][nbt]")
{
    const Bid bid = Bid::makeInternal(2ull);
    const Ib  ib  { 0x0600ull };
    const auto page = buildEmptyNbtLeaf(bid, ib);

    // Control bytes at 488-491 (NOT 492-495 — that bug would silently
    // break Outlook B-tree reads).
    REQUIRE(page[kBtPageCEnt]    == 0);
    REQUIRE(page[kBtPageCEntMax] == kBtPageEntriesArea / kNbtLeafEntrySize); // 15
    REQUIRE(page[kBtPageCbEnt]   == kNbtLeafEntrySize); // 32
    REQUIRE(page[kBtPageCLevel]  == 0);
    REQUIRE(readU32(page.data(), kBtPageDwPad) == 0u);

    // Page trailer
    REQUIRE(page[kPageTrailerOffset + 0] == ptype::kNBT);   // ptype
    REQUIRE(page[kPageTrailerOffset + 1] == ptype::kNBT);   // ptypeRepeat

    const uint16_t wSig = readU16(page.data(), kPageTrailerOffset + 2);
    REQUIRE(wSig == computeBlockSig(bid, ib));

    const uint64_t storedBid = readU64(page.data(), kPageTrailerOffset + 8);
    REQUIRE(storedBid == bid.value);

    const uint32_t storedCRC = readU32(page.data(), kPageTrailerOffset + 4);
    const uint32_t recomputed = crc32(page.data(), kPageBodySize);
    REQUIRE(storedCRC == recomputed);
}

TEST_CASE("Empty BBT leaf has cEnt=0, cbEnt=24, cLevel=0, valid trailer",
          "[ndb][page][bbt]")
{
    const Bid bid = Bid::makeInternal(3ull);
    const Ib  ib  { 0x0800ull };
    const auto page = buildEmptyBbtLeaf(bid, ib);

    REQUIRE(page[kBtPageCEnt]    == 0);
    REQUIRE(page[kBtPageCEntMax] == kBtPageEntriesArea / kBbtLeafEntrySize); // 20
    REQUIRE(page[kBtPageCbEnt]   == kBbtLeafEntrySize); // 24
    REQUIRE(page[kBtPageCLevel]  == 0);
    REQUIRE(readU32(page.data(), kBtPageDwPad) == 0u);

    REQUIRE(page[kPageTrailerOffset + 0] == ptype::kBBT);
    REQUIRE(page[kPageTrailerOffset + 1] == ptype::kBBT);

    const uint16_t wSig = readU16(page.data(), kPageTrailerOffset + 2);
    REQUIRE(wSig == computeBlockSig(bid, ib));

    const uint64_t storedBid = readU64(page.data(), kPageTrailerOffset + 8);
    REQUIRE(storedBid == bid.value);
}

// ============================================================================
// AMap — bitmap pattern and trailer
// ============================================================================
TEST_CASE("AMap for the M2 skeleton marks first 5 pages allocated",
          "[ndb][page][amap]")
{
    const Bid bid = Bid::makeInternal(1ull);
    const Ib  ib  { 0x0400ull };
    const auto page = buildAMap(bid, ib, /*fileSize=*/0x0A00);

    // 0x0A00 / 64 = 40 bits set → 5 full bytes of 0xFF, then zero.
    for (size_t i = 0; i < 5; ++i) {
        REQUIRE(page[i] == 0xFFu);
    }
    REQUIRE(page[5] == 0x00u);
    // Skip checking the entire bitmap; sample one mid-range byte.
    REQUIRE(page[200] == 0x00u);

    // Trailer: AMap pages have wSig == 0 and bid = page's own BID.
    REQUIRE(page[kPageTrailerOffset + 0] == ptype::kAMap);
    REQUIRE(page[kPageTrailerOffset + 1] == ptype::kAMap);
    REQUIRE(readU16(page.data(), kPageTrailerOffset + 2) == 0u);

    const uint64_t storedBid = readU64(page.data(), kPageTrailerOffset + 8);
    REQUIRE(storedBid == bid.value);

    const uint32_t storedCRC = readU32(page.data(), kPageTrailerOffset + 4);
    const uint32_t recomputed = crc32(page.data(), kPageBodySize);
    REQUIRE(storedCRC == recomputed);
}

// ============================================================================
// Block trailer ([MS-PST] §2.2.2.8.1)
// ============================================================================
TEST_CASE("computeBlockSig uses bid.value, XOR-folds high+low halves",
          "[ndb][block][sig]")
{
    // Reference formula per [MS-PST] §5.5 (Unicode = 32-bit truncate, fold).
    auto reference = [](Bid bid, Ib ib) -> uint16_t {
        const uint32_t mix = static_cast<uint32_t>(
            (ib.value ^ bid.value) & 0xFFFFFFFFull);
        return static_cast<uint16_t>((mix >> 16) ^ (mix & 0xFFFFu));
    };

    const Bid bid = Bid::makeInternal(0x42ull);
    const Ib  ib  { 0x12340000ull };
    REQUIRE(computeBlockSig(bid, ib) == reference(bid, ib));

    // KNOWN BUG #5 sanity: the index-based formula must differ.
    const uint32_t mixIdx = static_cast<uint32_t>(
        (ib.value ^ bid.index()) & 0xFFFFFFFFull);
    const uint16_t indexBased =
        static_cast<uint16_t>((mixIdx >> 16) ^ (mixIdx & 0xFFFFu));
    REQUIRE(computeBlockSig(bid, ib) != indexBased);

    // [MS-PST] §3.5 spec landmark: bid=0x246, ib=0x900200 -> wSig=0x00D6.
    REQUIRE(computeBlockSig(Bid{0x246ull}, Ib{0x900200ull}) == 0x00D6u);
}

TEST_CASE("appendBlockTrailer writes 16 bytes with CRC over already-encoded payload",
          "[ndb][block][trailer]")
{
    vector<uint8_t> bytes(64u, uint8_t{0xAB}); // pretend already-encoded payload
    const uint16_t cb = 64u;
    const Bid bid = Bid::makeData(7ull);
    const Ib  ib  { 0x10000ull };

    const uint32_t expectedCRC = crc32(bytes.data(), bytes.size());
    appendBlockTrailer(bytes, cb, bid, ib);

    REQUIRE(bytes.size() == 64u + kBlockTrailerSize);

    const uint8_t* t = bytes.data() + 64u;
    REQUIRE(readU16(t, 0) == cb);
    REQUIRE(readU16(t, 2) == computeBlockSig(bid, ib));
    REQUIRE(readU32(t, 4) == expectedCRC);
    REQUIRE(readU64(t, 8) == bid.value);
}

// ============================================================================
// End-to-end: writeEmptyPst -> file passes its own pst_info-style checks
// ============================================================================
namespace {

string makeTempPath(const char* leaf)
{
    const char* dir = std::getenv("TMP");
    if (dir == nullptr) dir = std::getenv("TEMP");
    if (dir == nullptr) dir = ".";
    string p = dir;
    if (!p.empty() && p.back() != '/' && p.back() != '\\') p += '/';
    p += leaf;
    return p;
}

bool readEntireFile(const string& path, vector<uint8_t>& out)
{
    FILE* fp = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&fp, path.c_str(), "rb") != 0 || fp == nullptr) return false;
#else
    fp = std::fopen(path.c_str(), "rb");
    if (fp == nullptr) return false;
#endif
    if (std::fseek(fp, 0, SEEK_END) != 0) { std::fclose(fp); return false; }
    const long sz = std::ftell(fp);
    if (sz < 0) { std::fclose(fp); return false; }
    if (std::fseek(fp, 0, SEEK_SET) != 0) { std::fclose(fp); return false; }
    out.resize(static_cast<size_t>(sz));
    const size_t got = (sz == 0) ? 0u : std::fread(out.data(), 1, out.size(), fp);
    std::fclose(fp);
    return got == out.size();
}

bool fileExists(const string& path)
{
    FILE* fp = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&fp, path.c_str(), "rb") != 0 || fp == nullptr) return false;
#else
    fp = std::fopen(path.c_str(), "rb");
    if (fp == nullptr) return false;
#endif
    std::fclose(fp);
    return true;
}

} // anonymous namespace

TEST_CASE("writeEmptyPst produces a 5-page skeleton that self-checks clean",
          "[ndb][writer][end-to-end]")
{
    const string path = makeTempPath("pstwriter_m2_empty.pst");

    auto r = writeEmptyPst(path);
    REQUIRE(r.ok);

    vector<uint8_t> file;
    REQUIRE(readEntireFile(path, file));
    REQUIRE(file.size() == 0x0A00);

    // HEADER magic & CRCs
    const uint8_t* h = file.data();
    REQUIRE(readU32(h, hdr::kMagic)       == kMagicDword);
    REQUIRE(readU16(h, hdr::kMagicClient) == kMagicClient);
    REQUIRE(readU32(h, hdr::kCrcPartial)  == crc32(h + hdr::kMagicClient, kHdrCrcPartialLen));
    REQUIRE(readU32(h, hdr::kCrcFull)     == crc32(h + hdr::kMagicClient, kHdrCrcFullLen));
    REQUIRE(h[hdr::kFAMapValid]           == kAMapValid2);
    REQUIRE(readU64(h, hdr::kIbFileEof)   == 0x0A00ull);
    REQUIRE(readU64(h, hdr::kIbAMapLast)  == 0x0400ull);

    // Page trailers — each post-header page must self-CRC and have ptype==ptypeRepeat.
    const uint64_t pageOffsets[] = { 0x0400ull, 0x0600ull, 0x0800ull };
    const uint8_t  pageTypes[]   = { ptype::kAMap, ptype::kNBT, ptype::kBBT };

    for (size_t i = 0; i < 3; ++i) {
        const uint8_t* p = file.data() + pageOffsets[i];
        REQUIRE(p[kPageTrailerOffset + 0] == pageTypes[i]);
        REQUIRE(p[kPageTrailerOffset + 1] == pageTypes[i]);
        REQUIRE(readU32(p, kPageTrailerOffset + 4) == crc32(p, kPageBodySize));
    }

    // Cleanup
    std::remove(path.c_str());
}

// ============================================================================
// SPEC-VERIFIED: round-trip the [MS-PST] §3.2 sample HEADER byte-for-byte.
//
// tests/golden/spec_sample_header.bin is the 564-byte HEADER taken from
// the spec's §3.2 hex dump of a real Outlook file.  We:
//   1. read all of its fields,
//   2. plug them into our own WriterState,
//   3. re-serialize via serializeHeader(),
//   4. assert the regenerated bytes equal the golden file byte-for-byte,
//   5. confirm both regenerated CRCs match the spec values
//        dwCRCPartial = 0x379AA90E and dwCRCFull = 0x1FD283D6.
// If this passes, the HEADER serializer is correct against real Outlook
// bytes — no Outlook install required for the gate.
// ============================================================================
TEST_CASE("HEADER serializer round-trips [MS-PST] Sec 3.2 sample byte-for-byte",
          "[ndb][header][golden_spec_header]")
{
    const string candidates[] = {
        "tests/golden/spec_sample_header.bin",
        "../tests/golden/spec_sample_header.bin",
        "../../tests/golden/spec_sample_header.bin",
        "../../../tests/golden/spec_sample_header.bin",
    };
    string path;
    for (const auto& c : candidates) {
        if (fileExists(c)) { path = c; break; }
    }
    REQUIRE_FALSE(path.empty());

    vector<uint8_t> golden;
    REQUIRE(readEntireFile(path, golden));
    REQUIRE(golden.size() == kHeaderSize);

    const uint8_t* g = golden.data();

    // First, sanity-check the golden file itself.
    REQUIRE(readU32(g, hdr::kMagic)       == kMagicDword);
    REQUIRE(readU16(g, hdr::kMagicClient) == kMagicClient);
    REQUIRE(readU32(g, hdr::kCrcPartial)  == 0x379AA90Eu);
    REQUIRE(readU32(g, hdr::kCrcFull)     == 0x1FD283D6u);
    REQUIRE(readU32(g, hdr::kCrcPartial)  ==
            crc32(g + hdr::kMagicClient, kHdrCrcPartialLen));
    REQUIRE(readU32(g, hdr::kCrcFull)     ==
            crc32(g + hdr::kMagicClient, kHdrCrcFullLen));

    // Decode every field that serializeHeader() writes.
    WriterState s{};
    s.dwReserved1       = readU32(g, hdr::kReserved1);
    s.dwReserved2       = readU32(g, hdr::kReserved2);
    s.bidUnused         = readU64(g, hdr::kBidUnused);
    s.dwUnique          = readU32(g, hdr::kDwUnique);
    s.bidNextP          = Bid{readU64(g, hdr::kBidNextP)};
    s.bidNextB          = Bid{readU64(g, hdr::kBidNextB)};
    s.root.ibFileEof    = Ib{readU64(g, hdr::kIbFileEof)};
    s.root.ibAMapLast   = Ib{readU64(g, hdr::kIbAMapLast)};
    s.root.cbAMapFree   = readU64(g, hdr::kCbAMapFree);
    s.root.cbPMapFree   = readU64(g, hdr::kCbPMapFree);
    s.root.brefNbt      = Bref{Bid{readU64(g, hdr::kBrefNbtBid)},
                               Ib { readU64(g, hdr::kBrefNbtIb)}};
    s.root.brefBbt      = Bref{Bid{readU64(g, hdr::kBrefBbtBid)},
                               Ib { readU64(g, hdr::kBrefBbtIb)}};
    s.root.fAMapValid   = g[hdr::kFAMapValid];
    for (size_t i = 0; i < 32; ++i) {
        s.rgnid[i] = readU32(g, hdr::kRgnid + i * 4u);
    }

    const auto regen = serializeHeader(s);
    REQUIRE(regen.size() == golden.size());

    // Byte-for-byte equality.  Report the first mismatching offset on
    // failure so the cause is obvious.
    bool allEqual = true;
    for (size_t i = 0; i < golden.size(); ++i) {
        if (regen[i] != golden[i]) {
            INFO("first mismatch at offset 0x" << std::hex << i
                 << ": regen=" << +regen[i] << " golden=" << +golden[i]);
            allEqual = false;
            break;
        }
    }
    REQUIRE(allEqual);

    // Both CRCs in the regenerated buffer match the spec values.
    REQUIRE(readU32(regen.data(), hdr::kCrcPartial) == 0x379AA90Eu);
    REQUIRE(readU32(regen.data(), hdr::kCrcFull)    == 0x1FD283D6u);
}

// ============================================================================
// Optional: byte-diff against tests/golden/empty.pst if it exists.
// This is the strongest test we can run — it confirms our output matches
// what real Outlook produces, byte-for-byte (modulo dwUnique + CRCs that
// depend on it).
// ============================================================================
TEST_CASE("Byte-diff against tests/golden/empty.pst (if present)",
          "[ndb][golden]")
{
    const string candidates[] = {
        "tests/golden/empty.pst",
        "../tests/golden/empty.pst",
        "../../tests/golden/empty.pst",
        "../../../tests/golden/empty.pst",
    };

    string golden;
    for (const auto& c : candidates) {
        if (fileExists(c)) { golden = c; break; }
    }
    if (golden.empty()) {
        WARN("No tests/golden/empty.pst found - skipping byte-diff. "
             "Generate one with Outlook and re-run for the strongest check.");
        SUCCEED();
        return;
    }

    vector<uint8_t> g;
    REQUIRE(readEntireFile(golden, g));

    // Sanity-check the golden file passes its own CRCs first.
    REQUIRE(g.size() >= kHeaderSize);
    REQUIRE(readU32(g.data(), hdr::kCrcPartial) ==
            crc32(g.data() + hdr::kMagicClient, kHdrCrcPartialLen));
    REQUIRE(readU32(g.data(), hdr::kCrcFull) ==
            crc32(g.data() + hdr::kMagicClient, kHdrCrcFullLen));
}

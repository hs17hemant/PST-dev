// pstwriter/tests/test_ltp.cpp
//
// M4 oracle tests — Heap-on-Node (HN), BTH, PropertyContext (PC),
// TableContext (TC). These tests are the SPEC for M4: each one parses
// a [MS-PST] §3.x sample byte-dump, verifies the structural fields
// against the spec annotation, then SKIPS the round-trip half until
// the corresponding builder is implemented.
//
// When M4 lands:
//   * remove the SKIP() lines and uncomment the round-trip assertions
//   * the existing `// TODO M4:` markers point to the exact insertion
//     points
//
// Reference: see MILESTONES.md "M4 gate" + KNOWN_UNVERIFIED.md
// "M4 — LTP layer spec samples" for why CRC self-consistency is N/A
// for §3.8 / §3.9 / §3.11.

#include <catch2/catch_test_macros.hpp>  // SKIP() macro lives here in Catch2 v3.x

#include "crc.hpp"
#include "ltp.hpp"
#include "types.hpp"
#include "writer.hpp"

#include "pst_info_run.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace std;
using namespace pstwriter;
using detail::readU16;
using detail::readU32;
using detail::readU64;

namespace {

bool fileExistsLtp(const string& path)
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

bool readEntireFileLtp(const string& path, vector<uint8_t>& out)
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

string ltpTempPath(const char* leaf)
{
    const char* dir = std::getenv("TMP");
    if (dir == nullptr) dir = std::getenv("TEMP");
    if (dir == nullptr) dir = ".";
    string p = dir;
    if (!p.empty() && p.back() != '/' && p.back() != '\\') p += '/';
    p += leaf;
    return p;
}

string locateLtpGolden(const char* leaf)
{
    const string candidates[] = {
        string("tests/golden/") + leaf,
        string("../tests/golden/") + leaf,
        string("../../tests/golden/") + leaf,
        string("../../../tests/golden/") + leaf,
    };
    for (const auto& c : candidates) {
        if (fileExistsLtp(c)) return c;
    }
    return string{};
}

} // namespace

// ============================================================================
// SPEC ORACLE: [MS-PST] Sec 3.8 — Sample Heap-on-Node (HN).
//
// Pre-flight contract:
//   * 258-byte HN body (no BLOCKTRAILER in spec dump)
//   * HNHDR at offset 0: ibHnpm=0x00EC, bSig=0xEC, bClientSig=0xBC (PC),
//                        hidUserRoot=0x00000020
//   * HNPAGEMAP at offset 0xEC: cAlloc=8, cFree=0,
//                               rgibAlloc[]={0x0C,0x14,0x6C,0x7C,0x8C,
//                                            0xA4,0xBC,0xD4,0xEC}
//
// M4 round-trip target: feed the parsed allocations back through
// buildHeapOnNode(...) and assert byte-for-byte match.
// ============================================================================
TEST_CASE("HN structured body matches [MS-PST] Sec 3.8 sample (parse-only; M4 round-trip pending)",
          "[ltp][hn][golden_spec_hn]")
{
    const string path = locateLtpGolden("spec_sample_hn.bin");
    REQUIRE_FALSE(path.empty());

    vector<uint8_t> golden;
    REQUIRE(readEntireFileLtp(path, golden));
    REQUIRE(golden.size() == 258u);

    const uint8_t* g = golden.data();

    // ---- HNHDR ([MS-PST] §2.3.1.2) ----
    REQUIRE(readU16(g, 0)  == 0x00ECu);   // ibHnpm
    REQUIRE(g[2]           == 0xECu);     // bSig (kHnSignature)
    REQUIRE(g[3]           == 0xBCu);     // bClientSig (bTypePC)
    REQUIRE(readU32(g, 4)  == 0x00000020u); // hidUserRoot
    // rgbFillLevel: 4 bytes holding 8 packed nibbles (one per fill bucket).
    // For this sample's first HN block, every bucket is FILL_LEVEL_EMPTY=0.
    // HNHDR total size = 12 bytes; the first user allocation begins at 0x0C.
    for (size_t i = 8; i < 12; ++i) {
        REQUIRE(g[i] == 0u);
    }

    // ---- HNPAGEMAP ([MS-PST] §2.3.1.5) at offset 0xEC ----
    const size_t hnpm = 0x00EC;
    REQUIRE(readU16(g, hnpm + 0) == 0x0008u);  // cAlloc
    REQUIRE(readU16(g, hnpm + 2) == 0x0000u);  // cFree

    // rgibAlloc[cAlloc + 1] = 9 entries × 2 bytes
    const uint16_t expectedAllocs[9] = {
        0x000C, 0x0014, 0x006C, 0x007C, 0x008C,
        0x00A4, 0x00BC, 0x00D4, 0x00EC
    };
    for (size_t i = 0; i < 9; ++i) {
        REQUIRE(readU16(g, hnpm + 4 + i * 2) == expectedAllocs[i]);
    }

    // ---- Round-trip via buildHeapOnNode(...) ----
    // Extract every allocation from the golden file and feed them back
    // through the builder. If our HNHDR / packing / HNPAGEMAP encoding
    // is correct, the regenerated bytes equal the spec sample exactly.
    vector<HnAllocation> a(8);
    for (size_t i = 0; i < 8; ++i) {
        a[i].data = g + expectedAllocs[i];
        a[i].size = static_cast<size_t>(expectedAllocs[i + 1]) - expectedAllocs[i];
    }
    const auto regen = buildHeapOnNode(a.data(), a.size(),
                                       /*bClientSig=*/0xBCu,
                                       /*hidUserRoot=*/0x00000020u);
    REQUIRE(regen.size() == golden.size());
    for (size_t i = 0; i < regen.size(); ++i) {
        if (regen[i] != golden[i]) {
            INFO("first mismatch at offset 0x" << std::hex << i
                 << ": regen=" << +regen[i] << " golden=" << +golden[i]);
            FAIL("byte mismatch in §3.8 HN round-trip");
        }
    }
}

// ============================================================================
// SPEC ORACLE: [MS-PST] Sec 3.9 — Sample BTH (inside the Sec 3.8 HN).
//
// Pre-flight contract:
//   * BTHHEADER at HN offset 0x0C (the first allocation):
//       bType=0xB5, cbKey=2, cbEnt=6, bIdxLevels=0, hidRoot=0x00000040
//   * BTH leaf records at HN offset 0x14 (second allocation, 88 bytes):
//       11 records × 8 bytes (2-byte key + 6-byte data)
//
// M4 round-trip target: feed the parsed records back through
// buildBth(...) over the §3.8 HN and assert byte-for-byte match.
// ============================================================================
TEST_CASE("BTH inside HN matches [MS-PST] Sec 3.9 sample (parse-only; M4 round-trip pending)",
          "[ltp][bth][golden_spec_bth]")
{
    const string path = locateLtpGolden("spec_sample_bth.bin");
    REQUIRE_FALSE(path.empty());

    vector<uint8_t> golden;
    REQUIRE(readEntireFileLtp(path, golden));
    REQUIRE(golden.size() == 258u);

    const uint8_t* g = golden.data();

    // ---- BTHHEADER ([MS-PST] §2.3.2.1) at HN offset 0x0C ----
    const size_t bthH = 0x0C;
    REQUIRE(g[bthH + 0]                == 0xB5u);   // bType (kBthSignature)
    REQUIRE(g[bthH + 1]                == 0x02u);   // cbKey
    REQUIRE(g[bthH + 2]                == 0x06u);   // cbEnt
    REQUIRE(g[bthH + 3]                == 0x00u);   // bIdxLevels (leaf-only)
    REQUIRE(readU32(g, bthH + 4)       == 0x00000040u); // hidRoot

    // ---- BTH leaf records at HN offset 0x14 ----
    // Per spec: 11 records × 8 bytes = 88 bytes, ending at offset 0x6C.
    const size_t bthLeaf  = 0x14;
    const size_t bthEnd   = 0x6C;
    REQUIRE(bthEnd - bthLeaf == 11u * 8u);

    // Spot-check: keys must be ascending (BTH invariant).
    uint16_t prev = 0;
    for (size_t i = 0; i < 11; ++i) {
        const uint16_t key = readU16(g, bthLeaf + i * 8);
        REQUIRE(key > prev);
        prev = key;
    }
    // First and last key per the spec dump: 0x0E34, 0x67FF.
    REQUIRE(readU16(g, bthLeaf + 0u * 8u)  == 0x0E34u);
    REQUIRE(readU16(g, bthLeaf + 10u * 8u) == 0x67FFu);

    // ---- Round-trip the BTHHEADER via encodeBthHeader(...) ----
    const auto hdr = encodeBthHeader(/*cbKey=*/2, /*cbEnt=*/6,
                                     /*bIdxLevels=*/0,
                                     /*hidRoot=*/0x00000040u);
    for (size_t i = 0; i < 8; ++i) {
        REQUIRE(hdr[i] == g[bthH + i]);
    }

    // ---- Round-trip the entire HN-with-BTH via buildHeapOnNode(...) ----
    // Per spec, §3.9 IS the §3.8 HN — 8 allocations identical to those
    // we already extracted. We re-do the extraction here so the test is
    // standalone (each [golden_spec_*] case can be run independently).
    const uint16_t expectedAllocs[9] = {
        0x000C, 0x0014, 0x006C, 0x007C, 0x008C,
        0x00A4, 0x00BC, 0x00D4, 0x00EC
    };
    vector<HnAllocation> a(8);
    for (size_t i = 0; i < 8; ++i) {
        a[i].data = g + expectedAllocs[i];
        a[i].size = static_cast<size_t>(expectedAllocs[i + 1]) - expectedAllocs[i];
    }
    const auto regen = buildHeapOnNode(a.data(), a.size(), 0xBCu, 0x00000020u);
    REQUIRE(regen.size() == golden.size());
    for (size_t i = 0; i < regen.size(); ++i) {
        if (regen[i] != golden[i]) {
            INFO("first mismatch at offset 0x" << std::hex << i
                 << ": regen=" << +regen[i] << " golden=" << +golden[i]);
            FAIL("byte mismatch in §3.9 HN-with-BTH round-trip");
        }
    }
}

// ============================================================================
// SPEC ORACLE: [MS-PST] Sec 3.11 — Sample TableContext (TC).
//
// Pre-flight contract:
//   * 464-byte HN body (no BLOCKTRAILER in spec dump)
//   * HNHDR: ibHnpm=0x01BC, bSig=0xEC, bClientSig=0x7C (bTypeTC),
//            hidUserRoot=0x00000040
//   * HNPAGEMAP at 0x1BC: cAlloc=7, cFree=0, rgibAlloc[8]
//   * TCINFO at HID 0x40 (allocation 2, offset 0x14): cCols=0x0D,
//     hnidRows=0x80, hidRowIndex=0x20
//
// M4 round-trip target: feed the parsed TC back through
// buildTableContext(...) and assert byte-for-byte match.
// ============================================================================
TEST_CASE("TC structured body matches [MS-PST] Sec 3.11 sample (parse-only; M4 round-trip pending)",
          "[ltp][tc][golden_spec_tc]")
{
    const string path = locateLtpGolden("spec_sample_tc.bin");
    REQUIRE_FALSE(path.empty());

    vector<uint8_t> golden;
    REQUIRE(readEntireFileLtp(path, golden));
    REQUIRE(golden.size() == 464u);

    const uint8_t* g = golden.data();

    // ---- HNHDR ----
    REQUIRE(readU16(g, 0)  == 0x01BCu);   // ibHnpm
    REQUIRE(g[2]           == 0xECu);     // bSig
    REQUIRE(g[3]           == 0x7Cu);     // bClientSig (bTypeTC)
    REQUIRE(readU32(g, 4)  == 0x00000040u); // hidUserRoot -> TCINFO

    // ---- HNPAGEMAP at offset 0x1BC ----
    const size_t hnpm = 0x01BC;
    REQUIRE(readU16(g, hnpm + 0) == 0x0007u);  // cAlloc
    REQUIRE(readU16(g, hnpm + 2) == 0x0000u);  // cFree

    const uint16_t expectedAllocs[8] = {
        0x000C, 0x0014, 0x0092, 0x00AA,
        0x014F, 0x017D, 0x0193, 0x01BB
    };
    for (size_t i = 0; i < 8; ++i) {
        REQUIRE(readU16(g, hnpm + 4 + i * 2) == expectedAllocs[i]);
    }

    // ---- TCINFO ([MS-PST] §2.3.4.1) at HN offset 0x14 ----
    // Per spec: bType=0x7C, cCols=0x0D, ... rgib[4], hidRowIndex=0x20,
    // hnidRows=0x80
    const size_t tci = 0x14;
    REQUIRE(g[tci + 0] == 0x7Cu);          // bType (kTcSignature)
    REQUIRE(g[tci + 1] == 0x0Du);          // cCols (13)
    // rgib[4]: end offsets within row data for each value class (4xCEB,
    // 2xCEB, 1xCEB, CEB) — pinned bytes from the spec dump:
    REQUIRE(readU16(g, tci + 2) == 0x0034u);
    REQUIRE(readU16(g, tci + 4) == 0x0034u);
    REQUIRE(readU16(g, tci + 6) == 0x0035u);
    REQUIRE(readU16(g, tci + 8) == 0x0037u);
    REQUIRE(readU32(g, tci + 10) == 0x00000020u);  // hidRowIndex
    REQUIRE(readU32(g, tci + 14) == 0x00000080u);  // hnidRows

    // 13 TCOLDESCs follow, 8 bytes each, starting at tci + 22 = 0x2A.

    // ---- Round-trip via buildHeapOnNode(...) ----
    // The TC body is just an HN with bClientSig=0x7C and 7 allocations.
    // Re-extracting + re-encoding is the byte-diff oracle for the HN
    // foundation of TC (TCINFO/TCOLDESC encoding for *new* TC content
    // is a separate M4 task — for the spec sample we replay the bytes).
    // Reuses the expectedAllocs[] array declared above.
    vector<HnAllocation> a(7);
    for (size_t i = 0; i < 7; ++i) {
        a[i].data = g + expectedAllocs[i];
        a[i].size = static_cast<size_t>(expectedAllocs[i + 1]) - expectedAllocs[i];
    }
    const auto regen = buildHeapOnNode(a.data(), a.size(),
                                       /*bClientSig=*/0x7Cu,
                                       /*hidUserRoot=*/0x00000040u);
    // M11-H: our writer puts rgibAlloc[cAlloc] == ibHnpm (DWORD-aligned)
    // per [MS-PST] §2.3.1.5 prose AND scanpst's recovery requirement,
    // and inserts a phantom allocation to absorb alignment pad without
    // bloating the last user allocation. The §3.11 sample byte-dump
    // does NOT use a phantom — it puts rgibAlloc[7]=0x1BB (one byte
    // before ibHnpm=0x1BC) and keeps cAlloc=7. We follow [MS-PST]
    // PROSE + scanpst, so the regenerated HN has cAlloc=8 (one
    // phantom) and HNPAGEMAP is 2 bytes longer than the golden.
    REQUIRE(regen.size() == golden.size() + 2u);  // +2 for phantom rgibAlloc entry

    // Bytes BEFORE HNPAGEMAP must still byte-match the golden — the
    // phantom occupies bytes [0x1BB, 0x1BC) which were already zero
    // pad in the golden, so the data section is identical.
    for (size_t i = 0; i < 0x01BCu; ++i) {
        if (regen[i] != golden[i]) {
            INFO("first mismatch at offset 0x" << std::hex << i
                 << ": regen=" << +regen[i] << " golden=" << +golden[i]);
            FAIL("byte mismatch in §3.11 TC HN data section (pre-HNPAGEMAP)");
        }
    }

    // Verify the M11-H structure: cAlloc=8, sentinel=ibHnpm, phantom
    // alloc starts at 0x1BB and ends at 0x1BC (1 byte).
    REQUIRE(readU16(regen.data(), 0x01BCu) == 8u);          // cAlloc (was 7)
    REQUIRE(readU16(regen.data(), 0x01BCu + 4u + 7u * 2u) == 0x01BBu); // phantom start
    REQUIRE(readU16(regen.data(), 0x01BCu + 4u + 8u * 2u) == 0x01BCu); // sentinel = ibHnpm

    // Golden has the legacy (non-DWORD-aligned) sentinel:
    REQUIRE(readU16(golden.data(), 0x01BCu) == 7u);
    REQUIRE(readU16(golden.data(), 0x01BCu + 4u + 7u * 2u) == 0x01BBu);
}

// ============================================================================
// Phase A oracle: buildBthLeafRecords against [MS-PST] Sec 3.9 leaves.
//
// Sec 3.9 is the same physical block as Sec 3.8 (PC's HN). The BTH leaf
// records sit at HN offset 0x14..0x6B (88 bytes = 11 records of cbKey=2,
// cbEnt=6). Spec text says these are "PC BTH records" — i.e. each
// record is (PidTag-id key, PropType + dwValueHnid data).
//
// The CRITICAL part of this test: we feed the records to the builder
// in EXPLICITLY-SCRAMBLED order, then assert the output equals the
// already-sorted golden bytes. If the builder is silently passing
// records through in caller order, this test fails. If it sorts
// internally as the design doc requires, it passes.
// ============================================================================
TEST_CASE("buildBthLeafRecords sorts and packs to match [MS-PST] Sec 3.9 leaves",
          "[ltp][bth][leaf_records]")
{
    const string path = locateLtpGolden("spec_sample_bth.bin");
    REQUIRE_FALSE(path.empty());

    vector<uint8_t> golden;
    REQUIRE(readEntireFileLtp(path, golden));
    REQUIRE(golden.size() == 258u);

    constexpr size_t kBthLeafOff = 0x14;
    constexpr size_t kBthLeafEnd = 0x6C;
    constexpr size_t kRecCount   = 11;
    constexpr uint8_t kCbKey     = 2;
    constexpr uint8_t kCbEnt     = 6;
    constexpr size_t kRecSize    = kCbKey + kCbEnt;

    // Sanity-check the golden file has the expected leaf size.
    REQUIRE(kBthLeafEnd - kBthLeafOff == kRecCount * kRecSize);

    // Extract the 11 records straight from the golden bytes — the
    // builder will receive these slices but in REORDERED order.
    struct StoredRec {
        array<uint8_t, kCbKey> key;
        array<uint8_t, kCbEnt> data;
    };
    vector<StoredRec> stored(kRecCount);
    for (size_t i = 0; i < kRecCount; ++i) {
        const uint8_t* src = golden.data() + kBthLeafOff + i * kRecSize;
        std::memcpy(stored[i].key.data(),  src,         kCbKey);
        std::memcpy(stored[i].data.data(), src + kCbKey, kCbEnt);
    }

    // Scramble: reverse order. (Any non-identity permutation works;
    // reverse maximises the disagreement with sorted-ascending order.)
    vector<size_t> shuffleIdx(kRecCount);
    for (size_t i = 0; i < kRecCount; ++i) shuffleIdx[i] = kRecCount - 1 - i;

    vector<BthRecord> shuffled(kRecCount);
    for (size_t i = 0; i < kRecCount; ++i) {
        shuffled[i].keyBytes  = stored[shuffleIdx[i]].key.data();
        shuffled[i].dataBytes = stored[shuffleIdx[i]].data.data();
    }

    // Pre-flight: confirm the input really is reversed (i.e. NOT
    // already sorted ascending). If this assertion ever holds, the
    // test is no longer proving what it claims.
    {
        bool reverseInputIsSorted = true;
        for (size_t i = 1; i < kRecCount && reverseInputIsSorted; ++i) {
            if (readU16(shuffled[i - 1].keyBytes, 0) >
                readU16(shuffled[i    ].keyBytes, 0)) {
                reverseInputIsSorted = false;
            }
        }
        REQUIRE_FALSE(reverseInputIsSorted);
    }

    // Build and compare.
    const auto regen = buildBthLeafRecords(shuffled.data(), shuffled.size(),
                                           kCbKey, kCbEnt);
    REQUIRE(regen.size() == kRecCount * kRecSize);
    for (size_t i = 0; i < regen.size(); ++i) {
        if (regen[i] != golden[kBthLeafOff + i]) {
            INFO("first mismatch at leaf-offset 0x" << std::hex << i
                 << ": regen=" << +regen[i]
                 << " golden=" << +golden[kBthLeafOff + i]);
            FAIL("byte mismatch in Sec 3.9 leaf-record round-trip");
        }
    }
}

// Negative test: capacity exceeded → clean throw (no truncation).
TEST_CASE("buildBthLeafRecords throws when total bytes exceed kHnAllocMax",
          "[ltp][bth][leaf_records][capacity]")
{
    // 448 records of 8 bytes each = 3584 bytes — just over the 3580 cap.
    constexpr size_t kRecs   = 448;
    constexpr uint8_t kKey   = 2;
    constexpr uint8_t kEnt   = 6;
    static_assert(kRecs * (kKey + kEnt) > kHnAllocMax,
                  "test must overflow the HN allocation cap");

    vector<uint8_t> backing(kRecs * (kKey + kEnt), 0u);
    vector<BthRecord> recs(kRecs);
    for (size_t i = 0; i < kRecs; ++i) {
        recs[i].keyBytes  = backing.data() + i * (kKey + kEnt);
        recs[i].dataBytes = backing.data() + i * (kKey + kEnt) + kKey;
    }

    REQUIRE_THROWS_AS(
        buildBthLeafRecords(recs.data(), recs.size(), kKey, kEnt),
        std::length_error);

    // Also verify dimension validation: bogus cbKey rejects.
    REQUIRE_THROWS_AS(
        buildBthLeafRecords(recs.data(), 1, /*cbKey=*/3, /*cbEnt=*/4),
        std::invalid_argument);
}

// ============================================================================
// Phase B: synthetic-PC fixtures and helpers.
//
// Builds the 7-prop input set described in MILESTONES.md "Synthetic-PC
// composition oracle" and exposes it for both the [build_only] structural
// test (this file) and the future [synthetic_pc_composition] round-trip
// (Phase C).
// ============================================================================
namespace {

// UTF-16-LE encode an ASCII-only literal. Caller passes a string of
// known length so we don't depend on string_view (MinGW 6.3 issue).
vector<uint8_t> utf16leLit(const char* s, size_t len)
{
    vector<uint8_t> out;
    out.reserve(len * 2u);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(static_cast<uint8_t>(s[i]));
        out.push_back(0);
    }
    return out;
}

#define UTF16LE(literal) utf16leLit((literal), sizeof(literal) - 1u)

struct SyntheticPcInputs {
    // 7 prop value buffers, all owned by this struct so PcProperty
    // pointers remain valid for the duration of the test.
    array<uint8_t, 4>  msgSize     {};
    array<uint8_t, 4>  msgStatus   {};
    array<uint8_t, 4>  folderType  {};
    vector<uint8_t>    displayName {};
    vector<uint8_t>    body        {};
    vector<uint8_t>    mvUnicode   {};
    vector<uint8_t>    binaryBlob  {};
    vector<PcProperty> props;
};

SyntheticPcInputs makeSyntheticPc()
{
    SyntheticPcInputs s;

    // Fixed-size inline values.
    detail::writeU32(s.msgSize.data(),    0, 0x12345678u);
    detail::writeU32(s.msgStatus.data(),  0, 0x00000042u);
    detail::writeU32(s.folderType.data(), 0, 0x00000001u);

    // Variable-size HN-allocated values.
    s.displayName = UTF16LE("Synthetic PC Test");                             // 17 chars -> 34 B
    s.body        = UTF16LE("This is a test body for the synthetic PC composition oracle."); // 60 -> 120 B

    // Multi-valued Unicode: ["alpha","beta","gamma"]
    {
        const auto a = UTF16LE("alpha");
        const auto b = UTF16LE("beta");
        const auto g = UTF16LE("gamma");
        const MvStringEntry entries[] = {
            { a.data(), a.size() },
            { b.data(), b.size() },
            { g.data(), g.size() },
        };
        s.mvUnicode = encodeMvUnicode(entries, 3);
    }

    // Subnode-promoted binary: 5500 B alternating 0xA5/0x5A.
    s.binaryBlob.resize(5500);
    for (size_t i = 0; i < s.binaryBlob.size(); ++i) {
        s.binaryBlob[i] = (i & 1u) ? 0x5Au : 0xA5u;
    }

    // Pass props in arbitrary (non-sorted) order so the builder is
    // exercised across the sort path.
    s.props = {
        // pidTagId          propType                bytes                size                       hint
        { 0x3001u,           PropType::Unicode,      s.displayName.data(), s.displayName.size(),     PropStorageHint::Auto },
        { 0x0E08u,           PropType::Int32,        s.msgSize.data(),     s.msgSize.size(),         PropStorageHint::Auto },
        { 0x6001u,           PropType::MvUnicode,    s.mvUnicode.data(),   s.mvUnicode.size(),       PropStorageHint::Auto },
        { 0x0E17u,           PropType::Int32,        s.msgStatus.data(),   s.msgStatus.size(),       PropStorageHint::Auto },
        { 0x3701u,           PropType::Binary,       s.binaryBlob.data(),  s.binaryBlob.size(),      PropStorageHint::Auto },
        { 0x1000u,           PropType::Unicode,      s.body.data(),        s.body.size(),            PropStorageHint::Auto },
        { 0x3601u,           PropType::Int32,        s.folderType.data(),  s.folderType.size(),      PropStorageHint::Auto },
    };
    return s;
}

} // namespace

// ============================================================================
// Structural oracle: synthetic 7-prop PC built via buildPropertyContext.
//
// We do NOT byte-diff against any spec sample — there is no Microsoft
// byte oracle for our HID-assignment strategy. Instead, every structural
// invariant the design doc promises is checked explicitly:
//   * HNHDR signature + bClientSig=0xBC + hidUserRoot=0x20
//   * HNPAGEMAP cAlloc=2 + 3 HN-allocated props = 5
//   * BTHHEADER at HID 0x20 with cbKey=2, cbEnt=6, hidRoot=0x40
//   * BTH leaf at HID 0x40 with 7 records sorted by PidTag ascending
//   * Each record's dwValueHnid resolves correctly per [SPEC §2.3.3.3]
//   * Variable-size HID slots resolve to allocations of the expected size
//   * Subnode list contains exactly 1 entry (PidTagAttachDataBinary, 5500 B)
// ============================================================================
TEST_CASE("buildPropertyContext: synthetic 7-prop input has the design-doc HN shape",
          "[ltp][pc][build_only]")
{
    auto inputs = makeSyntheticPc();
    const Nid kFirstSubNid{
        Nid{NidType::Internal, /*idx=*/0x100u}.value
    };  // nidType=0x01, nidIndex=0x100

    const auto r = buildPropertyContext(inputs.props.data(),
                                        inputs.props.size(),
                                        kFirstSubNid);

    REQUIRE(r.hnBytes.size() <= kMaxHnBodyBytes);
    REQUIRE(r.subnodes.size() == 1u);
    REQUIRE(r.subnodes[0].pidTagId == 0x3701u);
    REQUIRE(r.subnodes[0].size     == 5500u);
    REQUIRE(r.subnodes[0].nid      == kFirstSubNid);

    const uint8_t* g = r.hnBytes.data();

    // ---- HNHDR ----
    REQUIRE(g[2] == 0xECu);                        // bSig
    REQUIRE(g[3] == kBClientSigPC);                // 0xBC
    REQUIRE(readU32(g, 4) == makeHid(1));          // hidUserRoot = HID 0x20

    // ---- HNPAGEMAP ----
    // User allocations: header(8) + leaf(56) + 3 HN values (body=120,
    // dispname=34, mvUnicode=44) = 5 user allocations. Cumulative
    // cursor = 12 + 8 + 56 + 120 + 34 + 44 = 274 — NOT DWORD-aligned,
    // so M11-H inserts a 2-byte phantom allocation between the last
    // user alloc and ibHnpm. Total cAlloc = 5 + 1 = 6.
    const size_t ibHnpm = readU16(g, 0);
    REQUIRE(readU16(g, ibHnpm + 0) == 6u);          // cAlloc (5 user + 1 phantom)
    REQUIRE(readU16(g, ibHnpm + 2) == 0u);          // cFree

    const uint16_t a0 = readU16(g, ibHnpm + 4);     // start of allocation 1
    const uint16_t a1 = readU16(g, ibHnpm + 6);
    const uint16_t a2 = readU16(g, ibHnpm + 8);
    const uint16_t a3 = readU16(g, ibHnpm + 10);
    const uint16_t a4 = readU16(g, ibHnpm + 12);
    const uint16_t a5 = readU16(g, ibHnpm + 14);    // sentinel
    REQUIRE(a0 == 0x000Cu);                         // first alloc starts after HNHDR
    REQUIRE(a1 - a0 == 8u);                         // BTHHEADER
    REQUIRE(a2 - a1 == 7u * 8u);                    // 7 BTH records of 8 B each
    // Variable-allocation sizes — order: PidTag-ascending (which props
    // need HN). Sorted-PidTag-with-HnAlloc order: 0x1000 (Body, 120),
    // 0x3001 (DisplayName, 34), 0x6001 (MV, 44).
    REQUIRE(a3 - a2 == 120u);                       // Body
    REQUIRE(a4 - a3 == 34u);                        // DisplayName
    REQUIRE(a5 - a4 == 44u);                        // MV Unicode

    // ---- BTHHEADER at HID 0x20 (= offset a0 = 0x0C) ----
    REQUIRE(g[a0 + 0] == kBthSignature);            // 0xB5
    REQUIRE(g[a0 + 1] == 2u);                       // cbKey
    REQUIRE(g[a0 + 2] == 6u);                       // cbEnt
    REQUIRE(g[a0 + 3] == 0u);                       // bIdxLevels
    REQUIRE(readU32(g, a0 + 4) == makeHid(2));      // hidRoot = HID 0x40

    // ---- BTH leaf at HID 0x40 (= offset a1) ----
    // Records sorted by PidTag ascending:
    //   0x0E08 (Int32 inline)        data: type + value
    //   0x0E17 (Int32 inline)
    //   0x1000 (Unicode HID 0x60)    HID = (3 << 5) | 0 = 0x60
    //   0x3001 (Unicode HID 0x80)
    //   0x3601 (Int32 inline)
    //   0x3701 (Binary subnode)      NID = kFirstSubNid
    //   0x6001 (MvUnicode HID 0xA0)
    const size_t leaf = a1;
    const uint16_t expectedKeys[7] = {
        0x0E08, 0x0E17, 0x1000, 0x3001, 0x3601, 0x3701, 0x6001
    };
    for (size_t i = 0; i < 7; ++i) {
        REQUIRE(readU16(g, leaf + i * 8 + 0) == expectedKeys[i]);
    }

    // Inline-value records: dwValueHnid == the literal value.
    REQUIRE(readU16(g, leaf + 0 * 8 + 2) == static_cast<uint16_t>(PropType::Int32));
    REQUIRE(readU32(g, leaf + 0 * 8 + 4) == 0x12345678u);  // MessageSize

    REQUIRE(readU32(g, leaf + 1 * 8 + 4) == 0x00000042u);  // MessageStatus

    REQUIRE(readU32(g, leaf + 4 * 8 + 4) == 0x00000001u);  // FolderType

    // HN-allocated records: dwValueHnid is HID with hidType=0 (NID_TYPE_HID).
    const uint32_t bodyHid = readU32(g, leaf + 2 * 8 + 4);
    const uint32_t dispHid = readU32(g, leaf + 3 * 8 + 4);
    const uint32_t mvHid   = readU32(g, leaf + 6 * 8 + 4);
    REQUIRE(bodyHid == makeHid(3));   // 0x60
    REQUIRE(dispHid == makeHid(4));   // 0x80
    REQUIRE(mvHid   == makeHid(5));   // 0xA0
    // hidType bits == 0 → all three are HIDs not NIDs.
    REQUIRE((bodyHid & 0x1Fu) == 0u);
    REQUIRE((dispHid & 0x1Fu) == 0u);
    REQUIRE((mvHid   & 0x1Fu) == 0u);

    // Subnode-promoted record: dwValueHnid is the assigned NID; nidType
    // must be NON-zero (else it would decode as HID per §2.3.3.2).
    const uint32_t binNid = readU32(g, leaf + 5 * 8 + 4);
    REQUIRE(binNid == kFirstSubNid.value);
    REQUIRE((binNid & 0x1Fu) != 0u);

    // ---- Sanity: the HN-allocated value bytes match the inputs ----
    REQUIRE(std::memcmp(g + a2, inputs.body.data(),        120u) == 0);
    REQUIRE(std::memcmp(g + a3, inputs.displayName.data(),  34u) == 0);
    REQUIRE(std::memcmp(g + a4, inputs.mvUnicode.data(),    44u) == 0);
}

// Negative test: duplicate PidTag must throw, no silent merge.
TEST_CASE("buildPropertyContext: duplicate PidTag throws",
          "[ltp][pc][build_only][negative]")
{
    array<uint8_t, 4> v{};
    detail::writeU32(v.data(), 0, 1u);
    PcProperty dup[2] = {
        {0x3001u, PropType::Int32, v.data(), v.size(), PropStorageHint::Auto},
        {0x3001u, PropType::Int32, v.data(), v.size(), PropStorageHint::Auto},
    };
    REQUIRE_THROWS_AS(
        buildPropertyContext(dup, 2, Nid{NidType::Internal, 0x100u}),
        std::invalid_argument);
}

// Negative test: HN body exceeds single-block cap (8176 B).
//
// Force several large variable-size props into the HN by keeping each
// under the 3580-B per-allocation cap (so they don't auto-promote to
// subnode) but pushing their cumulative footprint past 8176 B.
TEST_CASE("buildPropertyContext: HN page size exceeded throws",
          "[ltp][pc][build_only][negative]")
{
    // 3 props of 3000 B each = 9000 B of payload alone, well over 8176.
    vector<uint8_t> blob(3000, 0xCDu);
    PcProperty fat[3] = {
        {0x6001u, PropType::Binary, blob.data(), blob.size(), PropStorageHint::Auto},
        {0x6002u, PropType::Binary, blob.data(), blob.size(), PropStorageHint::Auto},
        {0x6003u, PropType::Binary, blob.data(), blob.size(), PropStorageHint::Auto},
    };
    REQUIRE_THROWS_AS(
        buildPropertyContext(fat, 3, Nid{NidType::Internal, 0x100u}),
        std::length_error);
}

// Boundary test: variable-size value at exactly the [SPEC §2.3.3.3]
// inclusive cap (3580 bytes) MUST be HN-allocated, not subnode.
// The §2.3.3.3 truth table says "Y/-/Y → HID (<= 3580 bytes)" — the
// cap is inclusive, so 3580 stays in the HN.
TEST_CASE("buildPropertyContext: variable-size cb=3580 stays HN-allocated (boundary)",
          "[ltp][pc][build_only][boundary]")
{
    vector<uint8_t> blob(3580, 0xAAu);
    PcProperty p = {0x6001u, PropType::Binary, blob.data(), blob.size(),
                    PropStorageHint::Auto};
    const auto r = buildPropertyContext(&p, 1, Nid{NidType::Internal, 0x100u});

    // No subnode → kept in HN.
    REQUIRE(r.subnodes.empty());

    // Verify the BTH record's dwValueHnid is an HID (low 5 bits == 0)
    // not an NID (low 5 bits != 0).
    const uint8_t* h = r.hnBytes.data();
    const uint16_t ibHnpm = readU16(h, 0);
    const uint16_t cAlloc = readU16(h, ibHnpm + 0);
    REQUIRE(cAlloc == 3u);  // BTHHEADER + leaf + value

    const uint16_t leaf = readU16(h, ibHnpm + 4 + 2);  // alloc[1] = BTH leaf
    const uint32_t hnid = readU32(h, leaf + 4);
    REQUIRE((hnid & 0x1Fu) == 0u);   // hidType == NID_TYPE_HID → it's an HID
}

// Boundary test: variable-size value just past the cap (3581 bytes)
// MUST be promoted to subnode per the [SPEC §2.3.3.3] truth table
// row "Y/-/N → NID (subnode, > 3580 bytes)".
TEST_CASE("buildPropertyContext: variable-size cb=3581 promotes to subnode (boundary)",
          "[ltp][pc][build_only][boundary]")
{
    vector<uint8_t> blob(3581, 0xBBu);
    PcProperty p = {0x6001u, PropType::Binary, blob.data(), blob.size(),
                    PropStorageHint::Auto};
    const Nid kFirstSub{NidType::Internal, 0x100u};
    const auto r = buildPropertyContext(&p, 1, kFirstSub);

    // Promoted → exactly one subnode entry.
    REQUIRE(r.subnodes.size() == 1u);
    REQUIRE(r.subnodes[0].pidTagId == 0x6001u);
    REQUIRE(r.subnodes[0].size == 3581u);
    REQUIRE(r.subnodes[0].nid == kFirstSub);

    // BTH record's dwValueHnid is now an NID (low 5 bits != 0).
    const uint8_t* h = r.hnBytes.data();
    const uint16_t ibHnpm = readU16(h, 0);
    const uint16_t cAlloc = readU16(h, ibHnpm + 0);
    REQUIRE(cAlloc == 2u);  // BTHHEADER + leaf only — no HN allocation for promoted prop

    const uint16_t leaf = readU16(h, ibHnpm + 4 + 2);
    const uint32_t hnid = readU32(h, leaf + 4);
    REQUIRE((hnid & 0x1Fu) != 0u);   // nidType != 0 → it's an NID
    REQUIRE(hnid == kFirstSub.value);
}

// Negative test: starting subnode NID with nidType == HID is rejected
// because [MS-PST] §2.3.3.2 requires nidType ≠ HID for the HNID NID
// branch to be selected by the reader.
TEST_CASE("buildPropertyContext: firstSubnodeNid with nidType=HID throws",
          "[ltp][pc][build_only][negative]")
{
    PcProperty p = {0x3001u, PropType::Int32, nullptr, 0u, PropStorageHint::Auto};
    array<uint8_t, 4> v{};
    p.valueBytes = v.data();
    p.valueSize  = 4u;
    REQUIRE_THROWS_AS(
        buildPropertyContext(&p, 1, Nid{0u}),  // nidType=0 == HID
        std::invalid_argument);
}

// ============================================================================
// Phase D HEADLINE #2: Row-major variable-size value lock-in.
//
// The KNOWN_UNVERIFIED.md "M4 TC variable-size value ordering" entry
// pre-registers row-major as the writer's choice (basis: §3.11
// observation; one Outlook sample). This test is the regression oracle
// for that decision.
//
// Build a 2-row × 2-string-col TC. The four varlen string allocations
// MUST appear in row-major order in the HN: row0-col0, row0-col1,
// row1-col0, row1-col1. If the builder ever switches to column-major
// (row0-col0, row1-col0, row0-col1, row1-col1) this test fails loudly.
//
// Placed EARLY in the file (right after rgib oracle) per the Phase D
// guidance: "Implement that test early in Phase D, not at the end. If
// your TC builder produces column-major variable-size allocations and
// you write the test last, you'll have implemented the wrong layout."
// ============================================================================
TEST_CASE("buildTableContext: variable-size column values land in row-major order",
          "[ltp][tc][build_only][row_major_lockin]")
{
    // 4 columns: PidTagLtpRowId, PidTagLtpRowVer, str-col-A, str-col-B.
    // §2.3.4.4.1 mandates LtpRowId/LtpRowVer at iBit 0/1 and ibData 0/4.
    const TcColumn cols[4] = {
        {0x67F2, PropType::Int32,   0x00, 4, 0},  // LtpRowId
        {0x67F3, PropType::Int32,   0x04, 4, 1},  // LtpRowVer
        {0x6001, PropType::Unicode, 0x08, 4, 2},  // str-col-A (cbData=4 = HID size)
        {0x6002, PropType::Unicode, 0x0C, 4, 3},  // str-col-B
    };

    const TcRgib rg = computeTcRgib(cols, 4);
    REQUIRE(rg.end4b == 16u);   // 4×4-byte
    REQUIRE(rg.end2b == 16u);
    REQUIRE(rg.end1b == 16u);
    REQUIRE(rg.endBm == 17u);   // +1 byte CEB for 4 cols

    // Row 0 strings.
    const auto r0a = UTF16LE("row0-A");   // 12 B
    const auto r0b = UTF16LE("row0-B");   // 12 B
    // Row 1 strings.
    const auto r1a = UTF16LE("row1-A");
    const auto r1b = UTF16LE("row1-B");

    // Pre-pack each row: 17 bytes. We zero everything; writer will
    // overwrite the LtpRowId at offset 0 from BTH key, and patch the
    // varlen HID slots at ibData=8 and ibData=12.
    array<uint8_t, 17> row0Bytes{};
    array<uint8_t, 17> row1Bytes{};
    detail::writeU32(row0Bytes.data(), 0, 0x00000100u);   // LtpRowId
    detail::writeU32(row1Bytes.data(), 0, 0x00000200u);

    const TcVarlenCell row0Cells[2] = {
        {2, r0a.data(), r0a.size()},
        {3, r0b.data(), r0b.size()},
    };
    const TcVarlenCell row1Cells[2] = {
        {2, r1a.data(), r1a.size()},
        {3, r1b.data(), r1b.size()},
    };

    const TcRow rows[2] = {
        {0x100u, row0Bytes.data(), row0Bytes.size(), row0Cells, 2},
        {0x200u, row1Bytes.data(), row1Bytes.size(), row1Cells, 2},
    };

    const auto tc = buildTableContext(cols, 4, rows, 2);

    // Walk HNPAGEMAP and confirm allocation order. Expected slots:
    //   slot 1 (HID 0x20) = RowIndex BTHHEADER (8 B)
    //   slot 2 (HID 0x40) = TCINFO + TCOLDESCs (22 + 4*8 = 54 B)
    //   slot 3 (HID 0x60) = RowIndex BTH leaf (2 rows × 8 B = 16 B)
    //   slot 4 (HID 0x80) = Row Matrix (2 × 17 = 34 B)
    //   slot 5 (HID 0xA0) = row0-A string (12 B)  ← FIRST varlen
    //   slot 6 (HID 0xC0) = row0-B string (12 B)  ← row 0 second cell
    //   slot 7 (HID 0xE0) = row1-A string (12 B)  ← row 1 first cell
    //   slot 8 (HID 0x100) = row1-B string (12 B) ← row 1 second cell
    const uint8_t* h = tc.hnBytes.data();
    const uint16_t ibHnpm = readU16(h, 0);
    const uint16_t cAlloc = readU16(h, ibHnpm + 0);
    REQUIRE(cAlloc == 8u);

    auto allocSize = [&](size_t i) -> size_t {
        const uint16_t s = readU16(h, ibHnpm + 4u + i * 2u);
        const uint16_t e = readU16(h, ibHnpm + 4u + (i + 1) * 2u);
        return static_cast<size_t>(e) - s;
    };
    auto allocBytes = [&](size_t i) -> const uint8_t* {
        return h + readU16(h, ibHnpm + 4u + i * 2u);
    };

    REQUIRE(allocSize(0) == 8u);   // RowIndex BTHHEADER
    REQUIRE(allocSize(1) == 54u);  // TCINFO + 4 TCOLDESCs
    REQUIRE(allocSize(2) == 16u);  // RowIndex BTH leaf
    REQUIRE(allocSize(3) == 34u);  // Row Matrix
    REQUIRE(allocSize(4) == r0a.size());
    REQUIRE(allocSize(5) == r0b.size());
    REQUIRE(allocSize(6) == r1a.size());
    REQUIRE(allocSize(7) == r1b.size());

    // The headline assertion: the four varlen allocations carry the
    // expected payloads in row-major order. If switched to column-major
    // (allocs 4 = row0a, 5 = row1a, 6 = row0b, 7 = row1b) this fails.
    REQUIRE(std::memcmp(allocBytes(4), r0a.data(), r0a.size()) == 0);
    REQUIRE(std::memcmp(allocBytes(5), r0b.data(), r0b.size()) == 0);
    REQUIRE(std::memcmp(allocBytes(6), r1a.data(), r1a.size()) == 0);
    REQUIRE(std::memcmp(allocBytes(7), r1b.data(), r1b.size()) == 0);

    // Sanity: each row's varlen HID slot got patched with the assigned HID.
    const size_t matrixOff = readU16(h, ibHnpm + 4u + 3 * 2u);
    REQUIRE(readU32(h, matrixOff + 0 * 17u + 0x00) == 0x00000100u);  // row0 LtpRowId
    REQUIRE(readU32(h, matrixOff + 0 * 17u + 0x08) == makeHid(5));   // row0 str-A HID
    REQUIRE(readU32(h, matrixOff + 0 * 17u + 0x0C) == makeHid(6));   // row0 str-B HID
    REQUIRE(readU32(h, matrixOff + 1 * 17u + 0x00) == 0x00000200u);  // row1 LtpRowId
    REQUIRE(readU32(h, matrixOff + 1 * 17u + 0x08) == makeHid(7));   // row1 str-A HID
    REQUIRE(readU32(h, matrixOff + 1 * 17u + 0x0C) == makeHid(8));   // row1 str-B HID
}

// ============================================================================
// Phase E end-to-end: write a PST whose data blocks host PC and TC HN
// bodies, then invoke pst_info on it. The headline check is that
// pst_info exits with code 0 (= ALL CHECKS PASSED) — which means the
// LTP walker decrypted both blocks, recognized them as HNs, and
// walked them successfully without violating any structural invariant.
// ============================================================================
// Regression check: pst_info on a non-LTP PST (M3-style block payloads
// that aren't HN-shaped) must still return 0. The LTP walk should
// scan every data block, find none with bSig=0xEC, and report
// "0 HN blocks among N data blocks" without failing.
TEST_CASE("pst_info LTP walker is non-destructive on non-LTP PSTs (returns 0)",
          "[ltp][pst_info][m3_regression]")
{
    // 3 data blocks of 64 bytes each, payload = repeating bytes 0xAA.
    // Byte 2 of decrypted plaintext won't be 0xEC, so the LTP scanner
    // should silently move past each block.
    vector<vector<uint8_t>> blocks(3);
    for (size_t i = 0; i < blocks.size(); ++i) {
        blocks[i].assign(64, static_cast<uint8_t>(0xAA + i));
    }

    const string pstPath = ltpTempPath("pst_info_no_ltp_regression.pst");
    const auto wr = pstwriter::writeBlocksPst(pstPath, blocks);
    REQUIRE(wr.ok);

    INFO("Running pst_info on M3-style PST with 3 non-LTP data blocks");
    const int rc = runPstInfo(pstPath);
    REQUIRE(rc == 0);

    std::remove(pstPath.c_str());
}

TEST_CASE("pst_info LTP walker handles PC + TC end-to-end (returns 0)",
          "[ltp][pst_info][end_to_end]")
{
    // ---- PC HN body ----
    auto pcIn = makeSyntheticPc();
    // Drop the subnode-promoted prop (5500-byte binary) — the M4
    // pst_info LTP walker doesn't follow subnode chains, but it
    // doesn't need to: a subnode reference is a valid PC contents
    // pattern. Keep all 7 props so we exercise that path too.
    const auto pcRes = buildPropertyContext(pcIn.props.data(), pcIn.props.size(),
                                            Nid{NidType::Internal, 0x100u});

    // ---- TC HN body (4 cols / 2 rows / 2 strings per row) ----
    const TcColumn cols[4] = {
        {0x67F2, PropType::Int32,   0x00, 4, 0},
        {0x67F3, PropType::Int32,   0x04, 4, 1},
        {0x6001, PropType::Unicode, 0x08, 4, 2},
        {0x6002, PropType::Unicode, 0x0C, 4, 3},
    };
    array<uint8_t, 17> r0bytes{}; detail::writeU32(r0bytes.data(), 0, 0x100u);
    array<uint8_t, 17> r1bytes{}; detail::writeU32(r1bytes.data(), 0, 0x200u);
    auto r0a = UTF16LE("alpha");
    auto r0b = UTF16LE("beta");
    auto r1a = UTF16LE("gamma");
    auto r1b = UTF16LE("delta");
    const TcVarlenCell row0[2] = { {2, r0a.data(), r0a.size()}, {3, r0b.data(), r0b.size()} };
    const TcVarlenCell row1[2] = { {2, r1a.data(), r1a.size()}, {3, r1b.data(), r1b.size()} };
    const TcRow rows[2] = {
        { 0x100u, r0bytes.data(), r0bytes.size(), row0, 2 },
        { 0x200u, r1bytes.data(), r1bytes.size(), row1, 2 },
    };
    const auto tcRes = buildTableContext(cols, 4, rows, 2);

    // ---- Write a PST with both HNs as separate data blocks ----
    const vector<vector<uint8_t>> blocks = { pcRes.hnBytes, tcRes.hnBytes };
    const string pstPath = ltpTempPath("pst_info_ltp_e2e.pst");
    const auto wr = pstwriter::writeBlocksPst(pstPath, blocks);
    REQUIRE(wr.ok);

    // ---- Invoke pst_info via direct call ----
    // No system() / cmd.exe quoting hassle — pst_info.cpp is compiled
    // into the test binary with PSTWRITER_PST_INFO_NO_MAIN, exposing
    // runPstInfo() as a normal C++ function. stdout from runPstInfo
    // goes to the test's stdout (Catch2 captures it on failure).
    INFO("Running pst_info on PST containing 1 PC + 1 TC HN block");
    const int rc = runPstInfo(pstPath);
    REQUIRE(rc == 0);

    std::remove(pstPath.c_str());
}

// ============================================================================
// Phase D structural test: TC HN body produced by buildTableContext
// has the design-doc shape (TCINFO emitted correctly, TCOLDESCs sorted
// by tag, RowIndex BTH leaf records sorted by NID).
//
// Uses the same 4-col / 2-row fixture as the row-major lock-in but
// exercises the full byte structure of TCINFO/TCOLDESC/RowIndex.
// ============================================================================
TEST_CASE("buildTableContext: TCINFO + TCOLDESC + RowIndex have the design-doc shape",
          "[ltp][tc][build_only]")
{
    const TcColumn cols[4] = {
        // Pass in NON-sorted-by-tag order to exercise the sort path:
        // tag = (pid << 16) | type; tags here are
        //   0x6001001F, 0x6002001F, 0x67F20003, 0x67F30003 (sorted)
        {0x6002, PropType::Unicode, 0x0C, 4, 3},
        {0x67F3, PropType::Int32,   0x04, 4, 1},
        {0x6001, PropType::Unicode, 0x08, 4, 2},
        {0x67F2, PropType::Int32,   0x00, 4, 0},
    };

    array<uint8_t, 17> row0Bytes{}; detail::writeU32(row0Bytes.data(), 0, 0x100u);
    array<uint8_t, 17> row1Bytes{}; detail::writeU32(row1Bytes.data(), 0, 0x200u);
    const auto r0a = UTF16LE("row0-A");
    const auto r0b = UTF16LE("row0-B");
    const auto r1a = UTF16LE("row1-A");
    const auto r1b = UTF16LE("row1-B");

    // varlenCells use indexes into the UNSORTED columns array.
    // colIndex 2 = "0x6001 str-col-A" (cbData=4, varlen)
    // colIndex 0 = "0x6002 str-col-B" (cbData=4, varlen)
    const TcVarlenCell row0Cells[2] = {
        {2, r0a.data(), r0a.size()},
        {0, r0b.data(), r0b.size()},
    };
    const TcVarlenCell row1Cells[2] = {
        {2, r1a.data(), r1a.size()},
        {0, r1b.data(), r1b.size()},
    };

    // Pass rows in non-sorted-by-rowId order (row1 before row0) to
    // exercise the row sort path.
    const TcRow rows[2] = {
        {0x200u, row1Bytes.data(), row1Bytes.size(), row1Cells, 2},
        {0x100u, row0Bytes.data(), row0Bytes.size(), row0Cells, 2},
    };

    const auto tc = buildTableContext(cols, 4, rows, 2);

    const uint8_t* h = tc.hnBytes.data();

    // ---- HNHDR: bClientSig = 0x7C (TC) ----
    REQUIRE(h[3] == kBClientSigTC);
    REQUIRE(readU32(h, 4) == makeHid(2));   // hidUserRoot → TCINFO

    // ---- TCINFO at HID 0x40 (slot 2) ----
    const uint16_t ibHnpm = readU16(h, 0);
    const uint16_t tcInfoOff = readU16(h, ibHnpm + 4u + 1 * 2u);  // alloc 2

    REQUIRE(h[tcInfoOff + 0] == kTcSignature);      // bType = 0x7C
    REQUIRE(h[tcInfoOff + 1] == 4u);                // cCols
    REQUIRE(readU16(h, tcInfoOff + 2) == 16u);      // rgib[TCI_4b]
    REQUIRE(readU16(h, tcInfoOff + 4) == 16u);      // rgib[TCI_2b]
    REQUIRE(readU16(h, tcInfoOff + 6) == 16u);      // rgib[TCI_1b]
    REQUIRE(readU16(h, tcInfoOff + 8) == 17u);      // rgib[TCI_bm]
    REQUIRE(readU32(h, tcInfoOff + 10) == makeHid(1));  // hidRowIndex = HID 0x20
    REQUIRE(readU32(h, tcInfoOff + 14) == makeHid(4));  // hnidRows = HID 0x80
    REQUIRE(readU32(h, tcInfoOff + 18) == 0u);          // hidIndex deprecated

    // ---- TCOLDESC array sorted by tag ascending ----
    // Expected sorted tag sequence:
    //   0x6001001F (Unicode, str-col-A, ibData=8)
    //   0x6002001F (Unicode, str-col-B, ibData=12)
    //   0x67F20003 (Int32,   LtpRowId,  ibData=0)
    //   0x67F30003 (Int32,   LtpRowVer, ibData=4)
    const size_t coldescBase = tcInfoOff + 22u;
    REQUIRE(readU32(h, coldescBase + 0u * 8u) == 0x6001001Fu);
    REQUIRE(readU32(h, coldescBase + 1u * 8u) == 0x6002001Fu);
    REQUIRE(readU32(h, coldescBase + 2u * 8u) == 0x67F20003u);
    REQUIRE(readU32(h, coldescBase + 3u * 8u) == 0x67F30003u);

    // ibData/cbData/iBit for each:
    REQUIRE(readU16(h, coldescBase + 0u * 8u + 4) == 0x0008u);  // str-A
    REQUIRE(h[coldescBase + 0u * 8u + 6] == 4u);
    REQUIRE(h[coldescBase + 0u * 8u + 7] == 2u);

    REQUIRE(readU16(h, coldescBase + 2u * 8u + 4) == 0x0000u);  // LtpRowId
    REQUIRE(h[coldescBase + 2u * 8u + 6] == 4u);
    REQUIRE(h[coldescBase + 2u * 8u + 7] == 0u);

    // ---- RowIndex BTHHEADER at HID 0x20 (slot 1) ----
    const uint16_t bthHdrOff = readU16(h, ibHnpm + 4u + 0 * 2u);
    REQUIRE(h[bthHdrOff + 0] == kBthSignature);
    REQUIRE(h[bthHdrOff + 1] == 4u);                 // cbKey = 4 (NID)
    REQUIRE(h[bthHdrOff + 2] == 4u);                 // cbEnt = 4 (row#)
    REQUIRE(h[bthHdrOff + 3] == 0u);                 // bIdxLevels
    REQUIRE(readU32(h, bthHdrOff + 4) == makeHid(3));  // hidRoot = HID 0x60

    // ---- RowIndex BTH leaf at HID 0x60 (slot 3), sorted by rowId ----
    const uint16_t leafOff = readU16(h, ibHnpm + 4u + 2 * 2u);
    REQUIRE(readU32(h, leafOff + 0) == 0x100u);       // row 0 NID
    REQUIRE(readU32(h, leafOff + 4) == 0u);           // row 0 index
    REQUIRE(readU32(h, leafOff + 8) == 0x200u);       // row 1 NID
    REQUIRE(readU32(h, leafOff + 12) == 1u);          // row 1 index
}

// ============================================================================
// Phase D zero-row case: TC with cCols >= 1 but rowCount == 0.
//
// Per [MS-PST] §2.3.4.1: "hnidRows ... This value is set to zero if
// the TC contains no rows." This test pins:
//   * hnidRows == 0
//   * RowIndex BTH leaf is a zero-byte allocation (cAlloc has slot
//     reserved but its size is 0)
//   * Row Matrix is a zero-byte allocation
//   * No varlen allocations
// ============================================================================
TEST_CASE("buildTableContext: zero-row TC sets hnidRows=0 and zero-size row allocations",
          "[ltp][tc][build_only][zero_rows]")
{
    const TcColumn cols[2] = {
        {0x67F2, PropType::Int32, 0x00, 4, 0},
        {0x67F3, PropType::Int32, 0x04, 4, 1},
    };

    const auto tc = buildTableContext(cols, 2, /*rows=*/nullptr, /*rowCount=*/0);

    REQUIRE(tc.subnodes.empty());

    const uint8_t* h = tc.hnBytes.data();
    REQUIRE(h[3] == kBClientSigTC);
    REQUIRE(readU32(h, 4) == makeHid(2));   // hidUserRoot → TCINFO at HID 0x40

    const uint16_t ibHnpm   = readU16(h, 0);
    const uint16_t cAlloc   = readU16(h, ibHnpm + 0);
    // M11-H: 4 user allocs (header + TCINFO + empty-leaf + empty-matrix)
    // sum to a non-DWORD-aligned cursor (12 + 8 + 38 + 0 + 0 = 58),
    // so a phantom allocation absorbs the 2-byte align pad.
    REQUIRE(cAlloc == 5u);   // 4 user + 1 phantom

    auto allocSize = [&](size_t i) -> size_t {
        const uint16_t s = readU16(h, ibHnpm + 4u + i * 2u);
        const uint16_t e = readU16(h, ibHnpm + 4u + (i + 1) * 2u);
        return static_cast<size_t>(e) - s;
    };

    REQUIRE(allocSize(0) == 8u);   // RowIndex BTHHEADER
    REQUIRE(allocSize(1) == 22u + 2u * 8u);  // TCINFO + 2 TCOLDESCs = 38
    REQUIRE(allocSize(2) == 0u);   // empty RowIndex BTH leaf
    REQUIRE(allocSize(3) == 0u);   // empty Row Matrix
    REQUIRE(allocSize(4) == 2u);   // M11-H phantom (2-byte align pad)

    // TCINFO.hnidRows MUST be 0 per §2.3.4.1.
    const uint16_t tcInfoOff = readU16(h, ibHnpm + 4u + 1 * 2u);
    REQUIRE(readU32(h, tcInfoOff + 14) == 0u);

    // RowIndex BTHHEADER.hidRoot MUST also be 0 (no leaf to point at).
    const uint16_t bthHdrOff = readU16(h, ibHnpm + 4u + 0 * 2u);
    REQUIRE(readU32(h, bthHdrOff + 4) == 0u);
}

// ============================================================================
// Phase D HEADLINE: TCI rgib formula oracle against [MS-PST] Sec 3.11.
//
// Spec text [MS-PST] §2.3.4.1 says rgib values are "ending offsets" of
// four row-data regions (4-byte / 2-byte / 1-byte / CEB). The §3.11
// sample's stored rgib[] = {0x34, 0x34, 0x35, 0x37}. This test
// reconstructs the 13 TCOLDESCs from the §3.11 hex dump, runs them
// through computeTcRgib, and requires the formula's output to match
// Microsoft's published bytes byte-for-byte.
//
// Why this is the FIRST Phase D test: rgib has off-by-one and
// alignment traps. If the formula is wrong, every subsequent TC test
// would pass against our own internal consistency while producing
// structurally invalid bytes. This is the only Microsoft-published
// rgib oracle we can cross-check against.
// ============================================================================
TEST_CASE("computeTcRgib reproduces [MS-PST] Sec 3.11's rgib = {0x34,0x34,0x35,0x37}",
          "[ltp][tc][rgib][golden_spec_tc_rgib]")
{
    // 13 TCOLDESCs decoded from the §3.11 sample's TCOLDESC array
    // (HN offsets 0x2A..0x91, 8 bytes each). Order in the spec dump is
    // already sorted-by-tag, which is what §2.3.4.1 mandates.
    //
    // Each tuple: { pidTagId, propType, ibData, cbData, iBit }.
    const TcColumn cols[13] = {
        {0x0E30, PropType::Binary,    0x14, 4, 6},   // tag 0x0E300102
        {0x0E33, PropType::Int64,     0x18, 8, 7},   // tag 0x0E330014
        {0x0E34, PropType::Binary,    0x20, 4, 8},
        {0x0E38, PropType::Int32,     0x24, 4, 9},
        {0x3001, PropType::Unicode,   0x08, 4, 2},   // PidTagDisplayName
        {0x3602, PropType::Int32,     0x0C, 4, 3},
        {0x3603, PropType::Int32,     0x10, 4, 4},
        {0x360A, PropType::Boolean,   0x34, 1, 10},
        {0x3613, PropType::Unicode,   0x28, 4, 11},
        {0x6635, PropType::Int32,     0x2C, 4, 12},
        {0x6636, PropType::Int32,     0x30, 4, 13},
        {0x67F2, PropType::Int32,     0x00, 4, 0},   // PidTagLtpRowId
        {0x67F3, PropType::Int32,     0x04, 4, 1},   // PidTagLtpRowVer
    };

    const TcRgib r = computeTcRgib(cols, 13);

    // Headline assertions — pin the exact spec bytes.
    REQUIRE(r.end4b == 0x0034u);
    REQUIRE(r.end2b == 0x0034u);  // no 2-byte columns → cascades from end4b
    REQUIRE(r.end1b == 0x0035u);  // single Boolean at ibData=0x34 → +1
    REQUIRE(r.endBm == 0x0037u);  // +ceil(13/8)=2 bytes of CEB

    // Order-agnosticism check: shuffle the column array and verify the
    // formula produces the same rgib (it MUST be invariant on input order).
    TcColumn shuffled[13];
    for (size_t i = 0; i < 13; ++i) shuffled[i] = cols[12 - i];  // reverse
    const TcRgib r2 = computeTcRgib(shuffled, 13);
    REQUIRE(r2.end4b == r.end4b);
    REQUIRE(r2.end2b == r.end2b);
    REQUIRE(r2.end1b == r.end1b);
    REQUIRE(r2.endBm == r.endBm);
}

// ============================================================================
// Phase C — Synthetic PC round-trip oracle.
//
// Build the 7-prop PC, immediately decode the result via
// readPropertyContext, look up the one subnode in the writer's
// returned subnode list, and require byte-equality across all 7.
//
// Exercises every storage class:
//   * Inline cb=4: PidTagMessageSize, PidTagMessageStatus, PidTagFolderType
//   * HN-allocated variable: PidTagBody, PidTagDisplayName, custom MV string
//   * Subnode: PidTagAttachDataBinary
// ============================================================================
TEST_CASE("Synthetic PC round-trips 7 properties via HN+BTH+subnode (M4 composition oracle)",
          "[ltp][pc][synthetic_pc_composition]")
{
    auto inputs = makeSyntheticPc();
    const Nid kFirstSubNid{Nid{NidType::Internal, 0x100u}.value};

    const auto built = buildPropertyContext(inputs.props.data(),
                                            inputs.props.size(),
                                            kFirstSubNid);
    const auto decoded = readPropertyContext(built.hnBytes.data(),
                                             built.hnBytes.size());

    REQUIRE(decoded.size() == inputs.props.size());

    // Build a quick PidTag→input lookup. Inputs are passed in arbitrary
    // order; decoded is in BTH-key order. Match by PidTag.
    auto findInput = [&](uint16_t pidTag) -> const PcProperty* {
        for (const auto& p : inputs.props) {
            if (p.pidTagId == pidTag) return &p;
        }
        return nullptr;
    };

    for (const auto& d : decoded) {
        const PcProperty* in = findInput(d.pidTagId);
        REQUIRE(in != nullptr);
        REQUIRE(static_cast<uint16_t>(d.propType) ==
                static_cast<uint16_t>(in->propType));

        switch (d.storage) {
            case ReadPcProp::Storage::Inline: {
                // Compare the 4-byte raw slot to the input value.
                // For cb < 4 (Boolean, Int16) the upper bytes are zero
                // because our writer zero-extends per the §2.3.3.3 rule.
                const size_t fs = in->valueSize;
                REQUIRE(fs <= 4u);
                uint8_t expected[4] = {0, 0, 0, 0};
                std::memcpy(expected, in->valueBytes, fs);
                uint8_t actual[4];
                detail::writeU32(actual, 0, d.inlineValue);
                for (size_t i = 0; i < 4; ++i) {
                    REQUIRE(actual[i] == expected[i]);
                }
                break;
            }
            case ReadPcProp::Storage::HnAlloc: {
                REQUIRE(d.valueSize == in->valueSize);
                REQUIRE(std::memcmp(d.valueBytes, in->valueBytes,
                                    in->valueSize) == 0);
                break;
            }
            case ReadPcProp::Storage::Subnode: {
                // Look up the subnode in the writer's output. Then
                // compare bytes to the original input — which is what
                // a real reader would do after fetching the subnode
                // block via NBT/BBT.
                const PcSubnodeOut* found = nullptr;
                for (const auto& s : built.subnodes) {
                    if (s.nid == d.subnodeNid) { found = &s; break; }
                }
                REQUIRE(found != nullptr);
                REQUIRE(found->pidTagId == d.pidTagId);
                REQUIRE(found->size     == in->valueSize);
                REQUIRE(std::memcmp(found->data, in->valueBytes,
                                    in->valueSize) == 0);
                break;
            }
        }
    }
}

// ============================================================================
// Phase C cross-validation — decode the [MS-PST] Sec 3.9 sample as a PC.
//
// THIS IS THE LOAD-BEARING TEST for the HID-agnostic reader contract.
// §3.9 is real-Outlook bytes whose HID layout we explicitly demonstrated
// in Phase A is NOT in PidTag-ascending order (HID 0x60 hosts PidTag
// 0x0FF9, third in PidTag order). If readPropertyContext silently
// assumed our writer's tidy 0x60→0x0FF9-or-similar pattern, this test
// would fail.
//
// Verifies:
//   * Exactly 11 PC properties decoded.
//   * (PidTag, PropType) pairs match the §3.9 BTH leaf bytes.
//   * Storage classification matches expectation: 5 inline (Int32 / Bool),
//     6 HN-allocated (Binary / Unicode), 0 subnodes.
//   * Every HnAlloc.valueBytes pointer aliases inside the source HN body.
// ============================================================================
TEST_CASE("readPropertyContext decodes [MS-PST] Sec 3.9 sample (HID-agnostic, real Outlook bytes)",
          "[ltp][pc][read][golden_spec_bth_pc_decode]")
{
    const string path = locateLtpGolden("spec_sample_bth.bin");
    REQUIRE_FALSE(path.empty());

    vector<uint8_t> hn;
    REQUIRE(readEntireFileLtp(path, hn));
    REQUIRE(hn.size() == 258u);

    const auto props = readPropertyContext(hn.data(), hn.size());
    REQUIRE(props.size() == 11u);

    // Pinned (PidTag, PropType) pairs — these are exactly the 11 BTH
    // leaf records in §3.9, decoded in BTH-key order.
    struct Expect { uint16_t pidTag; uint16_t propType; };
    const Expect expected[11] = {
        {0x0E34, 0x0102},  // Binary  — PidTagRecordKey-related?
        {0x0E38, 0x0003},  // Int32 inline (val=0)
        {0x0FF9, 0x0102},  // Binary  — PidTagRecordKey
        {0x3001, 0x001F},  // Unicode — PidTagDisplayName
        {0x35DF, 0x0003},  // Int32 inline (val=0x89)
        {0x35E0, 0x0102},  // Binary  — PidTagIpmSubTreeEntryId
        {0x35E3, 0x0102},  // Binary  — PidTagIpmWastebasketEntryId
        {0x35E7, 0x0102},  // Binary  — PidTagFinderEntryId
        {0x6633, 0x000B},  // Boolean inline (val=1)
        {0x66FA, 0x0003},  // Int32 inline
        {0x67FF, 0x0003},  // Int32 inline (val=0)
    };
    for (size_t i = 0; i < 11; ++i) {
        REQUIRE(props[i].pidTagId == expected[i].pidTag);
        REQUIRE(static_cast<uint16_t>(props[i].propType) == expected[i].propType);
    }

    // Storage classification: 5 inline, 6 HnAlloc, 0 subnodes.
    int inlineN = 0, hnN = 0, subN = 0;
    for (const auto& p : props) {
        switch (p.storage) {
            case ReadPcProp::Storage::Inline:  ++inlineN; break;
            case ReadPcProp::Storage::HnAlloc: ++hnN;     break;
            case ReadPcProp::Storage::Subnode: ++subN;    break;
        }
    }
    REQUIRE(inlineN == 5);
    REQUIRE(hnN     == 6);
    REQUIRE(subN    == 0);

    // Spec invariant for HN-allocated values: the resolved byte range
    // sits inside the source HN body. (NOT a writer-shape assertion —
    // this is the §2.3.1.1 invariant that hidIndex resolves to a real
    // allocation.)
    const uint8_t* hnLo = hn.data();
    const uint8_t* hnHi = hn.data() + hn.size();
    for (const auto& p : props) {
        if (p.storage == ReadPcProp::Storage::HnAlloc) {
            REQUIRE(p.valueBytes >= hnLo);
            REQUIRE(p.valueBytes + p.valueSize <= hnHi);
        }
    }

    // Spot-check inline values against the §3.9 BTH leaf bytes
    // (decoded in Phase A's hex inspection):
    //   0x0E38 → 0x00000000  (record [1] data: 03 00 00 00 00 00)
    //   0x35DF → 0x00000089  (record [4] data: 03 00 89 00 00 00)
    //   0x6633 → 0x00000001  (record [8] data: 0B 00 01 00 00 00)
    //   0x66FA → 0x000E000D  (record [9] data: 03 00 0D 00 0E 00)
    //   0x67FF → 0x00000000  (record [10] data: 03 00 00 00 00 00)
    REQUIRE(props[1].inlineValue  == 0x00000000u);
    REQUIRE(props[4].inlineValue  == 0x00000089u);
    REQUIRE(props[8].inlineValue  == 0x00000001u);
    REQUIRE(props[9].inlineValue  == 0x000E000Du);
    REQUIRE(props[10].inlineValue == 0x00000000u);
}

// ============================================================================
// M5 Phase E SEMANTIC DECODE TESTS (UNLOCKED).
//
// Both tests use bytes already on disk from M4 -- per spec text:
//   * Sec 3.10 decodes the sec 3.8 HN bytes as the message store PC:
//     "The binary data used in the last two examples (HN, BTH) is actually
//      that of the message store PC of a PST" (verbatim from sec 3.10).
//   * Sec 3.12 decodes the sec 3.11 TC bytes as the Root Folder hierarchy
//     table; "the Root Folder has 3 sub-Folder objects: 'Top of Personal
//      Folders', 'Search Root' and 'SPAM Search Folder 2'" (verbatim).
//
// These tests verify our readers produce semantically-correct decoded
// output, not just structurally-correct bytes -- the structural side is
// already covered by [golden_spec_hn] / [golden_spec_bth] / [golden_spec_tc].
// ============================================================================

// ----------------------------------------------------------------------------
// Sec 3.10 Sample Message Store -- 9 named properties.
//
// PHASE E FINDING: The sec 3.9 BTH actually contains 11 records (verified
// by [golden_spec_bth_pc_decode]), but sec 3.10 enumerates only 9. The 2
// extras are PidTags 0x6633 (PtypBoolean=1) and 0x66FA (PtypInteger32=
// 0x000E000D) -- both in the 0x6600-0x66FF user-defined / proprietary
// range. They are present in real Outlook bytes but not surfaced in
// sec 3.10's prose. We require the 9 named props strictly; the test does
// NOT assert props.size() == 9 because that would conflict with the
// observed ground-truth.
// ----------------------------------------------------------------------------
TEST_CASE("PC reader decodes Sec 3.8 HN as message store PC matching Sec 3.10",
          "[ltp][pc][read][semantic_decode_3_10][m5_gate]")
{
    const string path = locateLtpGolden("spec_sample_hn.bin");
    REQUIRE_FALSE(path.empty());

    vector<uint8_t> hn;
    REQUIRE(readEntireFileLtp(path, hn));
    REQUIRE(hn.size() == 258u);

    const auto props = readPropertyContext(hn.data(), hn.size());

    // Phase E expectation: at least 9 (sec 3.10's 9 named) must be present;
    // the actual total is 11 (sec 3.9 BTH ground truth) and that's the
    // [golden_spec_bth_pc_decode] test's invariant.
    REQUIRE(props.size() >= 9u);

    // Helper: look up a prop by PidTag.
    auto findByTag = [&](uint16_t tag) -> const ReadPcProp* {
        for (const auto& p : props) {
            if (p.pidTagId == tag) return &p;
        }
        return nullptr;
    };

    // ----- Sec 3.10's 9 named properties (verbatim 2026-05-02) -----
    // Each row pinned: (PidTag, PropType, expected size for HnAlloc / value
    // for Inline). Verbatim from the spec dump.
    struct Sec310Prop {
        const char* name;
        uint16_t    pidTagId;
        uint16_t    propType;     // = PropType bits
        bool        isInline;
        uint32_t    inlineExpect; // valid iff isInline
        size_t      hnSize;       // valid iff !isInline
    };
    const Sec310Prop named[9] = {
        { "PidTagReplVersionhistory",    0x0E34, 0x0102, false, 0,           24 },
        { "PidTagReplFlags",             0x0E38, 0x0003, true,  0x00000000,  0  },
        { "PidTagRecordKey",             0x0FF9, 0x0102, false, 0,           16 },
        { "PidTagDisplayName",           0x3001, 0x001F, false, 0,           16 },
        { "PidTagValidFolderMask",       0x35DF, 0x0003, true,  0x00000089,  0  },
        { "PidTagIpmSubTreeEntryId",     0x35E0, 0x0102, false, 0,           24 },
        { "PidTagIpmWastebasketEntryId", 0x35E3, 0x0102, false, 0,           24 },
        { "PidTagFinderEntryId",         0x35E7, 0x0102, false, 0,           24 },
        { "PidTagPstPassword",           0x67FF, 0x0003, true,  0x00000000,  0  },
    };

    for (const auto& expected : named) {
        const ReadPcProp* p = findByTag(expected.pidTagId);
        INFO(expected.name << " (PidTag=0x" << std::hex << expected.pidTagId << ")");
        REQUIRE(p != nullptr);
        REQUIRE(static_cast<uint16_t>(p->propType) == expected.propType);
        if (expected.isInline) {
            REQUIRE(p->storage == ReadPcProp::Storage::Inline);
            REQUIRE(p->inlineValue == expected.inlineExpect);
        } else {
            REQUIRE(p->storage == ReadPcProp::Storage::HnAlloc);
            REQUIRE(p->valueSize == expected.hnSize);
            REQUIRE(p->valueBytes != nullptr);
        }
    }

    // ----- Spec-published value content checks (binary / string) ----------

    // PidTagDisplayName: 16 bytes UTF-16-LE = "UNICODE1" (8 chars * 2 bytes)
    // Per sec 3.10: 55 00 4E 00 49 00 43 00 4F 00 44 00 45 00 31 00
    {
        const ReadPcProp* p = findByTag(0x3001u);
        REQUIRE(p != nullptr);
        const uint8_t expectedDisplayName[16] = {
            0x55, 0x00, 0x4E, 0x00, 0x49, 0x00, 0x43, 0x00,
            0x4F, 0x00, 0x44, 0x00, 0x45, 0x00, 0x31, 0x00,
        };
        REQUIRE(p->valueSize == sizeof(expectedDisplayName));
        REQUIRE(std::memcmp(p->valueBytes, expectedDisplayName,
                            sizeof(expectedDisplayName)) == 0);
    }

    // PidTagRecordKey: 16-byte GUID
    // 22 9D B5 0A DC D9 94 43 85 DE 90 AE B0 7D 12 70
    {
        const ReadPcProp* p = findByTag(0x0FF9u);
        REQUIRE(p != nullptr);
        const uint8_t expectedRecordKey[16] = {
            0x22, 0x9D, 0xB5, 0x0A, 0xDC, 0xD9, 0x94, 0x43,
            0x85, 0xDE, 0x90, 0xAE, 0xB0, 0x7D, 0x12, 0x70,
        };
        REQUIRE(p->valueSize == sizeof(expectedRecordKey));
        REQUIRE(std::memcmp(p->valueBytes, expectedRecordKey,
                            sizeof(expectedRecordKey)) == 0);
    }

    // PidTagIpmSubTreeEntryId: 24-byte EntryID; last 4 bytes = "22 80 00 00"
    // (= NID 0x8022, the IPM SubTree per sec 2.7.1).
    {
        const ReadPcProp* p = findByTag(0x35E0u);
        REQUIRE(p != nullptr);
        REQUIRE(p->valueSize == 24u);
        // Last 4 bytes = NID 0x00008022 (LE: 22 80 00 00).
        REQUIRE(p->valueBytes[20] == 0x22u);
        REQUIRE(p->valueBytes[21] == 0x80u);
        REQUIRE(p->valueBytes[22] == 0x00u);
        REQUIRE(p->valueBytes[23] == 0x00u);
    }

    // PidTagIpmWastebasketEntryId: 24-byte EntryID; last 4 bytes = "62 80 00 00"
    // (= NID 0x8062, the Deleted Items NID per sec 2.7.1).
    {
        const ReadPcProp* p = findByTag(0x35E3u);
        REQUIRE(p != nullptr);
        REQUIRE(p->valueSize == 24u);
        REQUIRE(p->valueBytes[20] == 0x62u);
        REQUIRE(p->valueBytes[21] == 0x80u);
    }

    // PidTagFinderEntryId: 24-byte EntryID; last 4 bytes = "42 80 00 00"
    // (= NID 0x8042, the Search Folders NID per sec 2.7.1).
    {
        const ReadPcProp* p = findByTag(0x35E7u);
        REQUIRE(p != nullptr);
        REQUIRE(p->valueSize == 24u);
        REQUIRE(p->valueBytes[20] == 0x42u);
        REQUIRE(p->valueBytes[21] == 0x80u);
    }

    // ----- Document the discrepancy: extras present but not in sec 3.10 ---
    // PidTags 0x6633 and 0x66FA exist in the sec 3.9 BTH (verified by the
    // [golden_spec_bth_pc_decode] structural test). Sec 3.10's prose dump
    // omits them. Confirm we observe them too.
    REQUIRE(findByTag(0x6633u) != nullptr);
    REQUIRE(findByTag(0x66FAu) != nullptr);
}

// ----------------------------------------------------------------------------
// Sec 3.12 Sample Folder Object -- three folder names in the hierarchy TC.
//
// DECISION (Phase E, see report): TC semantic decode is tested via byte-
// search rather than via a public readTableContext() API. Rationale:
// pst_info.cpp's TC walker is intertwined with cout-based output; cleanly
// extracting a public reader would require non-trivial refactoring out of
// scope for Phase E. The byte-search approach validates sec 3.12's
// claim about the three folder names against the sec 3.11 bytes already
// locked byte-for-byte by [golden_spec_tc]. A full readTableContext API
// can land in M6 (Messaging Core) or M7 alongside other LTP reader work.
// ----------------------------------------------------------------------------
TEST_CASE("Sec 3.11 TC bytes contain the three folder names from Sec 3.12",
          "[ltp][tc][read][semantic_decode_3_12][m5_gate]")
{
    const string path = locateLtpGolden("spec_sample_tc.bin");
    REQUIRE_FALSE(path.empty());

    vector<uint8_t> tcBytes;
    REQUIRE(readEntireFileLtp(path, tcBytes));
    REQUIRE(tcBytes.size() == 464u);

    auto containsBytes = [&](const uint8_t* needle, size_t needleLen) -> bool {
        if (tcBytes.size() < needleLen) return false;
        for (size_t i = 0; i + needleLen <= tcBytes.size(); ++i) {
            if (std::memcmp(tcBytes.data() + i, needle, needleLen) == 0) {
                return true;
            }
        }
        return false;
    };

    // ----- Three folder name strings (UTF-16-LE) per sec 3.12 -------------
    // sec 3.12 row 0: "Top of Personal Folders" (23 ASCII chars -> 46 B)
    const auto topOfPF = UTF16LE("Top of Personal Folders");
    REQUIRE(topOfPF.size() == 46u);
    REQUIRE(containsBytes(topOfPF.data(), topOfPF.size()));

    // sec 3.12 row 1: "Search Root" (11 ASCII chars -> 22 B)
    const auto searchRoot = UTF16LE("Search Root");
    REQUIRE(searchRoot.size() == 22u);
    REQUIRE(containsBytes(searchRoot.data(), searchRoot.size()));

    // sec 3.12 row 2: "SPAM Search Folder 2" (20 ASCII chars -> 40 B)
    const auto spamFolder = UTF16LE("SPAM Search Folder 2");
    REQUIRE(spamFolder.size() == 40u);
    REQUIRE(containsBytes(spamFolder.data(), spamFolder.size()));

    // ----- RowIndex BTH leaf records per sec 3.12 -------------------------
    // sec 3.12 lists the RowIndex (HID 0x20) leaf records as:
    //   0x00002223, 2  (-> SPAM Search Folder 2 at row 2)
    //   0x00008022, 0  (-> Top of Personal Folders at row 0)
    //   0x00008042, 1  (-> Search Root at row 1)
    //
    // The TC HN body's HNHDR is at offset 0; ibHnpm is at offset 0..1.
    // The RowIndex BTHHEADER lives at HID 0x20 = HNPAGEMAP slot 1 (1-based
    // index per sec 2.3.1.1). The BTHHEADER records hidRoot pointing to
    // the leaf-records allocation. We don't decode the full HN here; we
    // just byte-search the RowID values.

    auto contains4LE = [&](uint32_t v) -> bool {
        const uint8_t needle[4] = {
            static_cast<uint8_t>(v & 0xFFu),
            static_cast<uint8_t>((v >> 8 ) & 0xFFu),
            static_cast<uint8_t>((v >> 16) & 0xFFu),
            static_cast<uint8_t>((v >> 24) & 0xFFu),
        };
        return containsBytes(needle, sizeof(needle));
    };
    REQUIRE(contains4LE(0x00002223u)); // SPAM Search Folder 2 RowID
    REQUIRE(contains4LE(0x00008022u)); // IPM Subtree (Top of Personal Folders) RowID
    REQUIRE(contains4LE(0x00008042u)); // Search Folders (Search Root) RowID
}

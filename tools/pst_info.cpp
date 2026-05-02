// pstwriter/tools/pst_info.cpp
//
// M2 self-check CLI.  Opens a Unicode PST, prints all HEADER fields, and
// verifies:
//   1. dwMagic == "!BDN"
//   2. wMagicClient == "SM"
//   3. wVer == 23 and wVerClient == 19
//   4. dwCRCPartial matches crc32 of the 471 spec-mandated bytes
//   5. dwCRCFull    matches crc32 of the 516 spec-mandated bytes
//   6. fAMapValid is 0x02 (VALID_AMAP2)
//   7. PAGETRAILER on each post-header page (AMap, NBT root, BBT root):
//        ptype == ptypeRepeat, dwCRC matches crc32 of first 496 bytes
//
// On all-pass: prints field summary then "ALL CHECKS PASSED" to stdout
// and returns 0. On any failure: prints "FAIL: <detail>" lines then
// "CHECKS FAILED" and returns 1.

#include "block.hpp"
#include "crc.hpp"
#include "ndb.hpp"
#include "page.hpp"
#include "types.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

using namespace std;
using namespace pstwriter;
using detail::readU16;
using detail::readU32;
using detail::readU64;

namespace {

struct CheckLog {
    int failures = 0;

    void pass(const string& what) const
    {
        cout << "  [ OK ] " << what << "\n";
    }
    void fail(const string& what)
    {
        cout << "  [FAIL] " << what << "\n";
        ++failures;
    }
};

// Read entire file into a vector<uint8_t>. Returns true on success.
bool slurp(const string& path, vector<uint8_t>& out)
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

string asciiOf(uint16_t v)
{
    char s[3] = { static_cast<char>(v & 0xFFu),
                  static_cast<char>((v >> 8) & 0xFFu),
                  '\0' };
    return string{s};
}

string asciiOf32(uint32_t v)
{
    char s[5];
    s[0] = static_cast<char>(v & 0xFFu);
    s[1] = static_cast<char>((v >> 8)  & 0xFFu);
    s[2] = static_cast<char>((v >> 16) & 0xFFu);
    s[3] = static_cast<char>((v >> 24) & 0xFFu);
    s[4] = '\0';
    return string{s};
}

void printHex(const string& label, uint64_t v, int width = 16)
{
    // `left` is set for the label and would leak into the value's setw
    // (zero-padding on the wrong side and producing things like
    // "7000000000000000" instead of "0000000000000007"); flip back to
    // `right` before emitting the value.
    cout << "  " << left << setw(20) << label << "0x"
         << right << hex << uppercase << setw(width) << setfill('0') << v
         << dec << setfill(' ') << left << "\n";
}

void checkPageTrailer(const uint8_t* page, uint64_t ibPage,
                      const string& label, CheckLog& log)
{
    const size_t off = kPageTrailerOffset;

    const uint8_t  ptype       = page[off + 0];
    const uint8_t  ptypeRepeat = page[off + 1];
    const uint16_t wSig        = readU16(page, off + 2);
    const uint32_t dwCRC       = readU32(page, off + 4);
    const uint64_t bid         = readU64(page, off + 8);

    cout << "\n" << label << " @ 0x" << hex << uppercase << ibPage << dec << "\n";
    printHex("  ptype",         ptype,       2);
    printHex("  ptypeRepeat",   ptypeRepeat, 2);
    printHex("  wSig",          wSig,        4);
    printHex("  dwCRC",         dwCRC,       8);
    printHex("  bid",           bid,         16);

    if (ptype == ptypeRepeat) {
        log.pass(label + " ptype == ptypeRepeat");
    } else {
        log.fail(label + " ptype != ptypeRepeat");
    }

    const uint32_t recomputed = crc32(page, kPageBodySize);
    if (recomputed == dwCRC) {
        log.pass(label + " page CRC matches");
    } else {
        char msg[128];
        std::snprintf(msg, sizeof(msg),
                      "%s page CRC mismatch (stored=0x%08X, computed=0x%08X)",
                      label.c_str(), dwCRC, recomputed);
        log.fail(msg);
    }
}

} // anonymous namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        cerr << "usage: pst_info <path.pst>\n";
        return 2;
    }

    const string path = argv[1];
    vector<uint8_t> file;
    if (!slurp(path, file)) {
        cerr << "FAIL: cannot read '" << path << "'\n";
        return 1;
    }
    if (file.size() < kHeaderSize) {
        cerr << "FAIL: file is " << file.size()
             << " bytes, smaller than 564-byte HEADER\n";
        return 1;
    }

    CheckLog log;

    // ---- HEADER readout ----
    const uint8_t* h = file.data();

    const uint32_t dwMagic         = readU32(h, hdr::kMagic);
    const uint32_t dwCRCPartial    = readU32(h, hdr::kCrcPartial);
    const uint16_t wMagicClient    = readU16(h, hdr::kMagicClient);
    const uint16_t wVer            = readU16(h, hdr::kVer);
    const uint16_t wVerClient      = readU16(h, hdr::kVerClient);
    const uint8_t  bPlatformCreate = h[hdr::kPlatformCreate];
    const uint8_t  bPlatformAccess = h[hdr::kPlatformAccess];
    const uint64_t bidNextP        = readU64(h, hdr::kBidNextP);
    const uint32_t dwUnique        = readU32(h, hdr::kDwUnique);
    const uint64_t ibFileEof       = readU64(h, hdr::kIbFileEof);
    const uint64_t ibAMapLast      = readU64(h, hdr::kIbAMapLast);
    const uint64_t cbAMapFree      = readU64(h, hdr::kCbAMapFree);
    const uint64_t cbPMapFree      = readU64(h, hdr::kCbPMapFree);
    const uint64_t brefNbtBid      = readU64(h, hdr::kBrefNbtBid);
    const uint64_t brefNbtIb       = readU64(h, hdr::kBrefNbtIb);
    const uint64_t brefBbtBid      = readU64(h, hdr::kBrefBbtBid);
    const uint64_t brefBbtIb       = readU64(h, hdr::kBrefBbtIb);
    const uint8_t  fAMapValid      = h[hdr::kFAMapValid];
    const uint8_t  bSentinel       = h[hdr::kBSentinel];
    const uint8_t  bCryptMethod    = h[hdr::kBCryptMethod];
    const uint64_t bidNextB        = readU64(h, hdr::kBidNextB);
    const uint32_t dwCRCFull       = readU32(h, hdr::kCrcFull);

    cout << "HEADER\n";
    cout << "  dwMagic            "  << "\"" << asciiOf32(dwMagic) << "\"  (0x"
         << hex << uppercase << dwMagic << dec << ")\n";
    cout << "  wMagicClient       "  << "\"" << asciiOf(wMagicClient) << "\"  (0x"
         << hex << uppercase << wMagicClient << dec << ")\n";
    cout << "  wVer               "  << wVer << "\n";
    cout << "  wVerClient         "  << wVerClient << "\n";
    cout << "  bPlatformCreate    0x" << hex << uppercase << +bPlatformCreate << dec << "\n";
    cout << "  bPlatformAccess    0x" << hex << uppercase << +bPlatformAccess << dec << "\n";
    cout << "  dwUnique           "  << dwUnique << "\n";
    cout << "  bidNextP           0x" << hex << uppercase << bidNextP << dec << "\n";
    cout << "  bidNextB           0x" << hex << uppercase << bidNextB << dec << "\n";
    cout << "  ROOT.ibFileEof     0x" << hex << uppercase << ibFileEof << dec
         << "  (" << ibFileEof << " bytes)\n";
    cout << "  ROOT.ibAMapLast    0x" << hex << uppercase << ibAMapLast << dec << "\n";
    cout << "  ROOT.cbAMapFree    " << cbAMapFree << " bytes\n";
    cout << "  ROOT.cbPMapFree    " << cbPMapFree << " bytes\n";
    cout << "  ROOT.BREFNBT       bid=0x" << hex << uppercase << brefNbtBid
         << " ib=0x" << brefNbtIb << dec << "\n";
    cout << "  ROOT.BREFBBT       bid=0x" << hex << uppercase << brefBbtBid
         << " ib=0x" << brefBbtIb << dec << "\n";
    cout << "  ROOT.fAMapValid    0x" << hex << uppercase << +fAMapValid << dec << "\n";
    cout << "  bSentinel          0x" << hex << uppercase << +bSentinel << dec << "\n";
    cout << "  bCryptMethod       0x" << hex << uppercase << +bCryptMethod << dec << "\n";
    cout << "  dwCRCPartial       0x" << right << hex << uppercase
         << setw(8) << setfill('0') << dwCRCPartial
         << dec << setfill(' ') << "\n";
    cout << "  dwCRCFull          0x" << right << hex << uppercase
         << setw(8) << setfill('0') << dwCRCFull
         << dec << setfill(' ') << "\n";

    cout << "\nChecks:\n";

    if (dwMagic == kMagicDword)        log.pass("dwMagic == \"!BDN\"");
    else                               log.fail("dwMagic != \"!BDN\"");

    if (wMagicClient == kMagicClient)  log.pass("wMagicClient == \"SM\"");
    else                               log.fail("wMagicClient != \"SM\"");

    if (wVer == kVerUnicode)           log.pass("wVer == 23 (Unicode PST)");
    else                               log.fail("wVer != 23");

    if (wVerClient == kVerClient)      log.pass("wVerClient == 19");
    else                               log.fail("wVerClient != 19");

    if (bSentinel == kSentinelByte)    log.pass("bSentinel == 0x80");
    else                               log.fail("bSentinel != 0x80");

    if (bCryptMethod == static_cast<uint8_t>(CryptMethod::Permute))
                                       log.pass("bCryptMethod == 0x01 (PERMUTE)");
    else                               log.fail("bCryptMethod != 0x01");

    if (fAMapValid == kAMapValid2)     log.pass("fAMapValid == 0x02 (VALID_AMAP2)");
    else                               log.fail("fAMapValid != 0x02");

    {
        const uint32_t recomputed = crc32(h + hdr::kMagicClient, kHdrCrcPartialLen);
        if (recomputed == dwCRCPartial) log.pass("dwCRCPartial matches crc32 of 471 bytes from 0x008");
        else {
            char msg[128];
            std::snprintf(msg, sizeof(msg),
                          "dwCRCPartial mismatch (stored=0x%08X, computed=0x%08X)",
                          dwCRCPartial, recomputed);
            log.fail(msg);
        }
    }
    {
        const uint32_t recomputed = crc32(h + hdr::kMagicClient, kHdrCrcFullLen);
        if (recomputed == dwCRCFull)   log.pass("dwCRCFull matches crc32 of 516 bytes from 0x008");
        else {
            char msg[128];
            std::snprintf(msg, sizeof(msg),
                          "dwCRCFull mismatch (stored=0x%08X, computed=0x%08X)",
                          dwCRCFull, recomputed);
            log.fail(msg);
        }
    }

    if (ibFileEof == file.size())      log.pass("ROOT.ibFileEof matches actual file size");
    else                               log.fail("ROOT.ibFileEof does not match actual file size");

    // ---- Page trailer checks for the three known pages ----
    const auto checkPageAt = [&](uint64_t off, const string& label) {
        if (file.size() < off + kPageSize) {
            log.fail(label + " page is missing or truncated");
            return;
        }
        checkPageTrailer(file.data() + off, off, label, log);
    };

    checkPageAt(0x0400, "AMap");
    checkPageAt(brefNbtIb, "NBT root leaf");
    checkPageAt(brefBbtIb, "BBT root");

    // ---- Walk the BBT and verify every BBTENTRY's block CRC ----
    if (brefBbtIb + kPageSize <= file.size()) {
        const uint8_t* bbtRoot = file.data() + brefBbtIb;
        const uint8_t  rootLevel = bbtRoot[kBtPageCLevel];
        const uint8_t  rootCEnt  = bbtRoot[kBtPageCEnt];

        size_t totalBbtEntries  = 0;
        size_t verifiedBlockCRCs = 0;
        size_t blockCrcFails     = 0;

        auto verifyLeaf = [&](const uint8_t* leaf, const string& tag) {
            const uint8_t cEnt = leaf[kBtPageCEnt];
            for (size_t i = 0; i < cEnt; ++i) {
                const size_t off = i * kBbtEntrySize;
                const uint64_t blockBid = readU64(leaf, off + 0);
                const uint64_t blockIb  = readU64(leaf, off + 8);
                const uint16_t cb       = readU16(leaf, off + 16);
                ++totalBbtEntries;

                const size_t totalCb = roundBlockSize(cb);
                if (blockIb + totalCb > file.size()) {
                    char msg[160];
                    std::snprintf(msg, sizeof(msg),
                                  "%s entry %zu (bid=0x%llX) ib=0x%llX cb=%u "
                                  "extends past EOF",
                                  tag.c_str(), i,
                                  static_cast<unsigned long long>(blockBid),
                                  static_cast<unsigned long long>(blockIb),
                                  static_cast<unsigned>(cb));
                    log.fail(msg);
                    ++blockCrcFails;
                    continue;
                }
                const uint8_t* blk = file.data() + blockIb;
                const uint32_t storedCRC =
                    readU32(blk, totalCb - kBlockTrailerSize + 4);
                const uint32_t recomputedCRC =
                    crc32(blk, totalCb - kBlockTrailerSize);
                if (storedCRC == recomputedCRC) {
                    ++verifiedBlockCRCs;
                } else {
                    char msg[200];
                    std::snprintf(msg, sizeof(msg),
                                  "%s block bid=0x%llX ib=0x%llX CRC mismatch "
                                  "(stored=0x%08X computed=0x%08X)",
                                  tag.c_str(),
                                  static_cast<unsigned long long>(blockBid),
                                  static_cast<unsigned long long>(blockIb),
                                  storedCRC, recomputedCRC);
                    log.fail(msg);
                    ++blockCrcFails;
                }
            }
        };

        if (rootLevel == 0) {
            verifyLeaf(bbtRoot, "BBT");
        } else {
            // Intermediate page; walk each child leaf.
            for (size_t i = 0; i < rootCEnt; ++i) {
                const size_t off = i * kBtEntrySize;
                const uint64_t childIb = readU64(bbtRoot, off + 8 + 8);
                if (childIb + kPageSize > file.size()) {
                    char msg[120];
                    std::snprintf(msg, sizeof(msg),
                                  "BBT child page %zu ib=0x%llX past EOF",
                                  i, static_cast<unsigned long long>(childIb));
                    log.fail(msg);
                    continue;
                }
                const uint8_t* child = file.data() + childIb;
                const uint32_t childCRC =
                    readU32(child, kPageTrailerOffset + 4);
                if (childCRC == crc32(child, kPageBodySize)) {
                    log.pass("BBT child leaf page CRC matches");
                } else {
                    log.fail("BBT child leaf page CRC mismatch");
                }
                char tag[32];
                std::snprintf(tag, sizeof(tag), "BBT[%zu]", i);
                verifyLeaf(child, tag);
            }
        }

        char summary[160];
        std::snprintf(summary, sizeof(summary),
                      "BBT walked: %zu entries, %zu block CRCs verified, "
                      "%zu mismatches",
                      totalBbtEntries, verifiedBlockCRCs, blockCrcFails);
        if (blockCrcFails == 0) {
            log.pass(summary);
        } else {
            log.fail(summary);
        }
    }

    cout << "\n";
    if (log.failures == 0) {
        cout << "ALL CHECKS PASSED\n";
        return 0;
    }
    cout << "CHECKS FAILED (" << log.failures << " failure"
         << (log.failures == 1 ? "" : "s") << ")\n";
    return 1;
}

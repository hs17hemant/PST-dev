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
#include "encoding.hpp"
#include "ltp.hpp"
#include "ndb.hpp"
#include "page.hpp"
#include "types.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sstream>
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

// In-place inverse-permute decryption — the §5.1 mpbbI table is at
// permuteTable() + 512. This is the read-side complement of
// encodePermute(), which we need here to peer into encrypted data
// blocks. We don't expose this in encoding.hpp because the writer
// library is write-only by charter; the tool decrypts for diagnostics.
void decodePermuteInPlace(uint8_t* data, size_t length) noexcept
{
    const uint8_t* mpbbI = pstwriter::permuteTable() + 512;
    for (size_t i = 0; i < length; ++i) {
        data[i] = mpbbI[data[i]];
    }
}

// Resolve an HID against a single-block HN body. Reports failures via
// CheckLog rather than throwing — this is a diagnostic tool, not a
// reader path, so we want every issue surfaced to the user.
struct HidSlice {
    bool   ok;
    size_t off;
    size_t size;
};

HidSlice resolveHid(uint32_t hid, const vector<uint8_t>& hn,
                    size_t hnpmOff, uint16_t cAlloc) noexcept
{
    const uint8_t  hType = static_cast<uint8_t> (hid & 0x1Fu);
    const uint16_t hIdx  = static_cast<uint16_t>((hid >> 5) & 0x07FFu);
    const uint16_t hBlk  = static_cast<uint16_t>((hid >> 16) & 0xFFFFu);
    if (hType != 0u || hIdx == 0u || hBlk != 0u || hIdx > cAlloc) {
        return {false, 0, 0};
    }
    const size_t startOff = hnpmOff + 4u + (size_t(hIdx) - 1u) * 2u;
    const size_t endOff   = hnpmOff + 4u + size_t(hIdx)        * 2u;
    if (endOff + 2u > hn.size()) return {false, 0, 0};
    const uint16_t s = readU16(hn.data(), startOff);
    const uint16_t e = readU16(hn.data(), endOff);
    if (e < s || e > hn.size()) return {false, 0, 0};
    return {true, size_t(s), size_t(e) - s};
}

// Walk a data block's plaintext as if it were an HN. Reports findings
// to stdout and increments log.failures on structural violations.
//
// `plaintext` is the post-decryption HN body (cb bytes — the same `cb`
// stored in BBTENTRY). All HN structures live entirely within this
// buffer (single-block HN, M4 cut).
//
// Returns true if the block is a recognized PC or TC; false if it
// doesn't look like an HN (caller can move on without complaint —
// not every data block is an LTP).
bool walkHnDataBlock(const vector<uint8_t>& plaintext,
                     uint64_t               blockBid,
                     uint64_t               blockIb,
                     CheckLog&              log)
{
    using namespace pstwriter;
    if (plaintext.size() < kHnHdrSize) return false;
    if (plaintext[2] != kHnSignature) return false;  // not an HN

    const uint16_t ibHnpm     = readU16(plaintext.data(), 0);
    const uint8_t  bClientSig = plaintext[3];
    const uint32_t hidUserRoot = readU32(plaintext.data(), 4);

    cout << "\nLTP HN @ block bid=0x" << hex << uppercase << blockBid
         << " ib=0x" << blockIb << dec << "\n";
    printHex("  ibHnpm",         ibHnpm,      4);
    printHex("  bClientSig",     bClientSig,  2);
    printHex("  hidUserRoot",    hidUserRoot, 8);

    if (ibHnpm + 4u > plaintext.size()) {
        log.fail("HN ibHnpm out of range");
        return true;
    }
    const uint16_t cAlloc = readU16(plaintext.data(), ibHnpm + 0);
    const uint16_t cFree  = readU16(plaintext.data(), ibHnpm + 2);
    const size_t   pmEnd  = ibHnpm + 4u + (size_t(cAlloc) + 1u) * 2u;
    if (pmEnd > plaintext.size()) {
        log.fail("HN HNPAGEMAP extends past plaintext end");
        return true;
    }
    cout << "  HNPAGEMAP cAlloc=" << cAlloc << " cFree=" << cFree << "\n";
    log.pass("HN signature + HNPAGEMAP bounds OK");

    // Validate every allocation slot (rgibAlloc[i] <= rgibAlloc[i+1] and
    // <= ibHnpm). Empty allocations (size 0) are legal — emitted by TC
    // for zero-row matrices.
    bool slotsOk = true;
    for (size_t i = 0; i <= cAlloc; ++i) {
        const uint16_t v = readU16(plaintext.data(), ibHnpm + 4u + i * 2u);
        if (v > ibHnpm) { slotsOk = false; break; }
        if (i > 0) {
            const uint16_t prev = readU16(plaintext.data(), ibHnpm + 4u + (i - 1) * 2u);
            if (v < prev) { slotsOk = false; break; }
        }
    }
    if (slotsOk) log.pass("HN allocation offsets monotonic + within ibHnpm");
    else         log.fail("HN allocation offsets violate ordering / bounds");

    // Branch on bClientSig.
    if (bClientSig == kBClientSigPC) {
        // PC: walk via readPropertyContext (HID-agnostic, validates
        // BTHHEADER + leaf-record structure for us).
        try {
            const auto props = readPropertyContext(plaintext.data(), plaintext.size());
            cout << "  PC properties: " << props.size() << "\n";
            for (const auto& p : props) {
                const char* sclass = "?";
                switch (p.storage) {
                    case ReadPcProp::Storage::Inline:  sclass = "inline";  break;
                    case ReadPcProp::Storage::HnAlloc: sclass = "hn-alloc"; break;
                    case ReadPcProp::Storage::Subnode: sclass = "subnode";  break;
                }
                // `right` is asserted before each setw use so the
                // earlier `left` from printHex doesn't leak in and
                // left-pad with '0' (which produces "E080" instead of
                // "0E08" for pidTagId 0x0E08).
                cout << "    PidTag=0x" << right << hex << uppercase
                     << setw(4) << setfill('0') << p.pidTagId
                     << " Type=0x" << setw(4) << setfill('0')
                     << static_cast<uint16_t>(p.propType)
                     << dec << setfill(' ') << "  storage=" << sclass;
                if (p.storage == ReadPcProp::Storage::HnAlloc) {
                    cout << "  size=" << p.valueSize;
                } else if (p.storage == ReadPcProp::Storage::Subnode) {
                    cout << "  nid=0x" << right << hex << uppercase
                         << setw(8) << setfill('0') << p.subnodeNid.value
                         << dec << setfill(' ');
                } else {
                    cout << "  value=0x" << right << hex << uppercase
                         << setw(8) << setfill('0') << p.inlineValue
                         << dec << setfill(' ');
                }
                cout << "\n";
            }
            log.pass("PC walk: BTH structure valid + every HID resolves");
        } catch (const std::exception& e) {
            string msg = "PC walk failed: ";
            msg += e.what();
            log.fail(msg);
        }
    } else if (bClientSig == kBClientSigTC) {
        // TC: walk TCINFO + RowIndex BTH + Row Matrix manually (we
        // don't have a readTableContext yet — that's an M5 hardening
        // task).
        const HidSlice tcInfo = resolveHid(hidUserRoot, plaintext, ibHnpm, cAlloc);
        if (!tcInfo.ok || tcInfo.size < 22u) {
            log.fail("TC: hidUserRoot does not resolve to a valid TCINFO allocation");
            return true;
        }
        const uint8_t  bType   = plaintext[tcInfo.off + 0];
        const uint8_t  cCols   = plaintext[tcInfo.off + 1];
        const uint16_t r4b     = readU16(plaintext.data(), tcInfo.off + 2);
        const uint16_t r2b     = readU16(plaintext.data(), tcInfo.off + 4);
        const uint16_t r1b     = readU16(plaintext.data(), tcInfo.off + 6);
        const uint16_t rbm     = readU16(plaintext.data(), tcInfo.off + 8);
        const uint32_t hidRow  = readU32(plaintext.data(), tcInfo.off + 10);
        const uint32_t hnidRow = readU32(plaintext.data(), tcInfo.off + 14);

        if (bType != kTcSignature) {
            log.fail("TC: TCINFO.bType != 0x7C");
            return true;
        }
        if (tcInfo.size != 22u + 8u * size_t(cCols)) {
            log.fail("TC: TCINFO allocation size doesn't match 22 + 8*cCols");
            return true;
        }
        cout << "  TC cCols=" << +cCols
             << " rgib={0x" << right << hex << uppercase << setw(4) << setfill('0')
             << r4b << ", 0x" << setw(4) << setfill('0') << r2b
             << ", 0x" << setw(4) << setfill('0') << r1b
             << ", 0x" << setw(4) << setfill('0') << rbm
             << dec << setfill(' ') << "}\n";
        cout << "    hidRowIndex=0x" << right << hex << uppercase
             << setw(8) << setfill('0') << hidRow
             << " hnidRows=0x" << setw(8) << setfill('0') << hnidRow
             << dec << setfill(' ') << "\n";

        // RowIndex BTH walk (count rows).
        size_t rowCount = 0;
        const HidSlice rIdxHdr = resolveHid(hidRow, plaintext, ibHnpm, cAlloc);
        if (!rIdxHdr.ok || rIdxHdr.size != 8u) {
            log.fail("TC: hidRowIndex does not resolve to an 8-byte BTHHEADER");
        } else {
            const uint8_t  cbKey   = plaintext[rIdxHdr.off + 1];
            const uint8_t  cbEnt   = plaintext[rIdxHdr.off + 2];
            const uint8_t  bIdxLvl = plaintext[rIdxHdr.off + 3];
            const uint32_t hidLeaf = readU32(plaintext.data(), rIdxHdr.off + 4);
            if (cbKey != 4u || cbEnt != 4u || bIdxLvl != 0u) {
                log.fail("TC: RowIndex BTHHEADER must be cbKey=4 cbEnt=4 bIdxLevels=0");
            } else if (((hidLeaf >> 5) & 0x7FFu) == 0u) {
                rowCount = 0;  // empty TC
            } else {
                const HidSlice leaf = resolveHid(hidLeaf, plaintext, ibHnpm, cAlloc);
                if (!leaf.ok || (leaf.size % 8u) != 0u) {
                    log.fail("TC: RowIndex leaf size not divisible by 8");
                } else {
                    rowCount = leaf.size / 8u;
                }
            }
        }
        cout << "    rowCount=" << rowCount << "\n";

        // Row Matrix size check (cross-check against RowIndex count).
        if (rowCount > 0 && hnidRow != 0) {
            // hnidRows is HID iff low 5 bits == 0 (the M4 cut never
            // promotes Row Matrix to subnode).
            if ((hnidRow & 0x1Fu) != 0u) {
                log.pass("TC: Row Matrix is in a subnode (NID), not in HN");
            } else {
                const HidSlice rm = resolveHid(hnidRow, plaintext, ibHnpm, cAlloc);
                if (!rm.ok) {
                    log.fail("TC: hnidRows HID does not resolve");
                } else if (rm.size != rowCount * size_t(rbm)) {
                    log.fail("TC: Row Matrix size != rowCount * endBm");
                } else {
                    log.pass("TC: Row Matrix size matches rowCount * endBm");
                }
            }
        } else if (rowCount == 0 && hnidRow == 0) {
            log.pass("TC: zero-row TC has hnidRows == 0 per spec");
        }
    } else {
        cout << "  bClientSig=0x" << hex << uppercase << +bClientSig << dec
             << " (not PC or TC; skipping deep walk)\n";
    }
    return true;
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

    // Per [MS-PST] §2.2.2.7.2 + §2.6.1, AMap (ptype 0x84) and PMap (ptype
    // 0x83) pages MUST carry PAGETRAILER.bid == page file offset (ib).
    // Real Outlook walks Read(@ib) and rejects mismatches as
    // "Outlook Data File Corruption". See KNOWN_UNVERIFIED.md M11-E.
    if (ptype == ptype::kAMap || ptype == ptype::kPMap) {
        if (bid == ibPage) {
            log.pass(label + " trailer.bid == ib (M11-E invariant)");
        } else {
            std::ostringstream msg;
            msg << label << " trailer.bid=0x" << std::hex << std::uppercase
                << bid << " but ib=0x" << ibPage
                << " (M11-E: AMap/PMap must store ib in trailer.bid)";
            log.fail(msg.str());
        }
    }
}

} // anonymous namespace

// Body of `main`, exposed so tests can invoke pst_info's diagnostic
// pipeline without round-tripping through cmd.exe's quoting quirks.
// Returns 0 on success (== "ALL CHECKS PASSED"), 1 on any check
// failure, 2 on usage error / unreadable file.
int runPstInfo(const string& path)
{
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

        struct DataBlockEntry {
            uint64_t bid;
            uint64_t ib;
            uint16_t cb;
        };
        vector<DataBlockEntry> dataBlocks;

        auto verifyLeaf = [&](const uint8_t* leaf, const string& tag) {
            const uint8_t cEnt = leaf[kBtPageCEnt];
            for (size_t i = 0; i < cEnt; ++i) {
                const size_t off = i * kBbtEntrySize;
                const uint64_t blockBid = readU64(leaf, off + 0);
                const uint64_t blockIb  = readU64(leaf, off + 8);
                const uint16_t cb       = readU16(leaf, off + 16);
                ++totalBbtEntries;
                // bit[1] of BID == 0 marks data blocks (vs internal
                // XBLOCK/SLBLOCK/etc.). Data blocks are the only kind
                // that can host an HN per [MS-PST] §2.3.1.
                if ((blockBid & 0x2ull) == 0ull) {
                    dataBlocks.push_back({blockBid, blockIb, cb});
                }

                const size_t totalCb = roundBlockSize(cb);
                if (blockIb + totalCb > file.size()) {
                    char msg[160];
                    std::snprintf(msg, sizeof(msg),
                                  "%s entry %lu (bid=0x%08X%08X) "
                                  "ib=0x%08X%08X cb=%u extends past EOF",
                                  tag.c_str(),
                                  static_cast<unsigned long>(i),
                                  static_cast<unsigned>((blockBid >> 32) & 0xFFFFFFFFu),
                                  static_cast<unsigned>(blockBid & 0xFFFFFFFFu),
                                  static_cast<unsigned>((blockIb  >> 32) & 0xFFFFFFFFu),
                                  static_cast<unsigned>(blockIb  & 0xFFFFFFFFu),
                                  static_cast<unsigned>(cb));
                    log.fail(msg);
                    ++blockCrcFails;
                    continue;
                }
                const uint8_t* blk = file.data() + blockIb;
                const uint32_t storedCRC =
                    readU32(blk, totalCb - kBlockTrailerSize + 4);
                // [MS-PST] §2.2.2.8.1: dwCRC scope is `cb` bytes only,
                // NOT including 64-byte alignment padding. (Empirically
                // verified against backup.pst on 2026-05-04.)
                const uint32_t recomputedCRC =
                    crc32(blk, cb);
                if (storedCRC == recomputedCRC) {
                    ++verifiedBlockCRCs;
                } else {
                    char msg[200];
                    std::snprintf(msg, sizeof(msg),
                                  "%s block bid=0x%08X%08X "
                                  "ib=0x%08X%08X CRC mismatch "
                                  "(stored=0x%08X computed=0x%08X)",
                                  tag.c_str(),
                                  static_cast<unsigned>((blockBid >> 32) & 0xFFFFFFFFu),
                                  static_cast<unsigned>(blockBid & 0xFFFFFFFFu),
                                  static_cast<unsigned>((blockIb  >> 32) & 0xFFFFFFFFu),
                                  static_cast<unsigned>(blockIb  & 0xFFFFFFFFu),
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
                                  "BBT child page %lu ib=0x%08X%08X past EOF",
                                  static_cast<unsigned long>(i),
                                  static_cast<unsigned>((childIb >> 32) & 0xFFFFFFFFu),
                                  static_cast<unsigned>(childIb & 0xFFFFFFFFu));
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
                std::snprintf(tag, sizeof(tag), "BBT[%lu]",
                              static_cast<unsigned long>(i));
                verifyLeaf(child, tag);
            }
        }

        char summary[160];
        std::snprintf(summary, sizeof(summary),
                      "BBT walked: %lu entries, %lu block CRCs verified, "
                      "%lu mismatches",
                      static_cast<unsigned long>(totalBbtEntries),
                      static_cast<unsigned long>(verifiedBlockCRCs),
                      static_cast<unsigned long>(blockCrcFails));
        if (blockCrcFails == 0) {
            log.pass(summary);
        } else {
            log.fail(summary);
        }

        // ---- Step 4: LTP walk -------------------------------------------------
        // For every data block, decrypt the cb-byte payload and check the
        // [MS-PST] §2.3.1.2 HN signature (bSig at offset 2). If found,
        // walk the HN per its bClientSig (PC / TC / other).
        //
        // Decryption uses the HEADER's bCryptMethod (typically PERMUTE).
        // For Cyclic the key is the lower 32 bits of the block's BID.
        size_t hnFound  = 0;
        size_t pcFound  = 0;
        size_t tcFound  = 0;
        for (const auto& db : dataBlocks) {
            if (db.ib + db.cb > file.size()) continue;

            vector<uint8_t> plaintext(db.cb);
            std::memcpy(plaintext.data(), file.data() + db.ib, db.cb);
            switch (static_cast<CryptMethod>(bCryptMethod)) {
                case CryptMethod::None:
                    break;
                case CryptMethod::Permute:
                    decodePermuteInPlace(plaintext.data(), plaintext.size());
                    break;
                case CryptMethod::Cyclic:
                    encodeCyclic(plaintext.data(), plaintext.size(),
                                 static_cast<uint32_t>(db.bid & 0xFFFFFFFFu));
                    break;
                default:
                    // Unknown crypt method — skip this block's LTP walk.
                    continue;
            }

            if (plaintext.size() < kHnHdrSize) continue;
            if (plaintext[2] != kHnSignature) continue;

            ++hnFound;
            if (plaintext[3] == kBClientSigPC) ++pcFound;
            if (plaintext[3] == kBClientSigTC) ++tcFound;
            walkHnDataBlock(plaintext, db.bid, db.ib, log);
        }

        if (hnFound > 0) {
            char ltpSummary[160];
            std::snprintf(ltpSummary, sizeof(ltpSummary),
                          "LTP walked: %lu HN block(s) - %lu PC, %lu TC",
                          static_cast<unsigned long>(hnFound),
                          static_cast<unsigned long>(pcFound),
                          static_cast<unsigned long>(tcFound));
            log.pass(ltpSummary);
        } else if (!dataBlocks.empty()) {
            // PSTs with data blocks but no LTP (e.g. M3 test PSTs full
            // of synthetic byte payloads) — not a failure, just a note.
            char info[120];
            std::snprintf(info, sizeof(info),
                          "LTP scan: 0 HN blocks among %lu data block(s) "
                          "(no LTP content detected)",
                          static_cast<unsigned long>(dataBlocks.size()));
            log.pass(info);
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

#ifndef PSTWRITER_PST_INFO_NO_MAIN
int main(int argc, char** argv)
{
    if (argc != 2) {
        cerr << "usage: pst_info <path.pst>\n";
        return 2;
    }
    return runPstInfo(argv[1]);
}
#endif

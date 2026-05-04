// pstwriter/tests/test_m5_end_to_end.cpp
//
// Phase D - end-to-end PST gate test.
//
// Builds a PST containing:
//   * 1 PC node (M4 buildPropertyContext with 6 simple properties)
//   * 1 TC node (M4 buildTableContext with 4 cols / 2 rows)
// Wires both via NBT entries pointing to data-block BIDs registered in
// the BBT. Then invokes pst_info and asserts:
//   * exit code 0 (ALL CHECKS PASSED)
//   * The NBT walk via nbtForEach() yields exactly the 2 expected NIDs.
//   * Both NIDs are individually resolvable via nbtFind().
//   * Each NID's bidData matches a block actually written to disk.
//
// Phase D scope is the M5 PLUMBING demonstration (M4 PC + TC fixtures
// wired via NBT/BBT with no orphan blocks). The full [SPEC sec 2.7.1]
// mandatory-nodes set (27 nodes including hierarchy/contents/FAI tables,
// IPM subtree, deleted items, etc.) is deferred to M6 (Messaging Core)
// because it requires full messaging-layer schema work. See MILESTONES.md
// "Phase D mandatory-nodes deferral" subsection.

#include <catch2/catch_test_macros.hpp>

#include "block.hpp"
#include "ltp.hpp"
#include "m5_allocator.hpp"
#include "nbt.hpp"
#include "ndb.hpp"
#include "page.hpp"
#include "types.hpp"
#include "writer.hpp"

#include "pst_info_run.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace pstwriter;

namespace {

string m5TempPath(const char* leaf)
{
    const char* dir = std::getenv("TMP");
    if (dir == nullptr) dir = std::getenv("TEMP");
    if (dir == nullptr) dir = ".";
    string p = dir;
    if (!p.empty() && p.back() != '/' && p.back() != '\\') p += '/';
    p += leaf;
    return p;
}

bool readEntirePst(const string& path, vector<uint8_t>& out)
{
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    out.resize(static_cast<size_t>(sz));
    const size_t got = std::fread(out.data(), 1, out.size(), fp);
    std::fclose(fp);
    return got == out.size();
}

// Encode an ASCII string as UTF-16-LE (matching the M4 test convention).
vector<uint8_t> utf16leAscii(const char* s)
{
    vector<uint8_t> out;
    while (*s) {
        out.push_back(static_cast<uint8_t>(*s));
        out.push_back(0);
        ++s;
    }
    return out;
}

} // namespace

// ============================================================================
// Phase D end-to-end: PC + TC + NBT entries; pst_info ALL CHECKS PASSED
// ============================================================================
TEST_CASE("End-to-end PST: PC + TC nodes wired via NBT, pst_info passes, zero orphan blocks",
          "[m5][end_to_end][m5_gate]")
{
    M5Allocator alloc;

    // ---- Allocate NIDs via M5Allocator -------------------------------------
    // PC NID: NormalFolder type (a typical PC owner kind).
    const Nid pcNid = alloc.allocate(NidType::NormalFolder);
    // TC NID: HierarchyTable type (one of the typical TC nidTypes).
    const Nid tcNid = alloc.allocate(NidType::HierarchyTable);

    // ---- Build PC HN body (6 simple props, no subnode promotion) ----------
    array<uint8_t, 4> msgSize{};   detail::writeU32(msgSize.data(),    0, 0x12345678u);
    array<uint8_t, 4> msgStatus{}; detail::writeU32(msgStatus.data(),  0, 0x42u);
    array<uint8_t, 4> folderType{};detail::writeU32(folderType.data(), 0, 0x1u);
    const auto displayName = utf16leAscii("Phase D PC test");
    const auto body        = utf16leAscii("end-to-end demonstration body");
    array<uint8_t, 4> recordKey{}; detail::writeU32(recordKey.data(), 0, 0xDEADBEEFu);

    const PcProperty pcProps[6] = {
        { 0x3001u, PropType::Unicode, displayName.data(), displayName.size(), PropStorageHint::Auto },
        { 0x0E08u, PropType::Int32,   msgSize.data(),     msgSize.size(),     PropStorageHint::Auto },
        { 0x0E17u, PropType::Int32,   msgStatus.data(),   msgStatus.size(),   PropStorageHint::Auto },
        { 0x3601u, PropType::Int32,   folderType.data(),  folderType.size(),  PropStorageHint::Auto },
        { 0x1000u, PropType::Unicode, body.data(),        body.size(),        PropStorageHint::Auto },
        { 0x0FF9u, PropType::Int32,   recordKey.data(),   recordKey.size(),   PropStorageHint::Auto },
    };
    // firstSubnodeNid won't be used (no props promote to subnode), but it
    // must be a valid non-HID NID per the M4 contract.
    const Nid firstSub = alloc.allocate(NidType::LtpReserved);
    const auto pcResult = buildPropertyContext(pcProps, 6, firstSub);
    REQUIRE(pcResult.subnodes.empty()); // confirm no subnode promotion

    // ---- Build TC HN body (4 cols / 2 rows / 2 varlen strings per row) ----
    const TcColumn cols[4] = {
        // PidTagLtpRowId / LtpRowVer are reserved at iBit 0/1 ibData 0/4.
        { 0x67F2u, PropType::Int32,   0x00u, 4, 0 },
        { 0x67F3u, PropType::Int32,   0x04u, 4, 1 },
        { 0x6001u, PropType::Unicode, 0x08u, 4, 2 },
        { 0x6002u, PropType::Unicode, 0x0Cu, 4, 3 },
    };
    array<uint8_t, 17> row0Bytes{}; detail::writeU32(row0Bytes.data(), 0, 0x100u);
    array<uint8_t, 17> row1Bytes{}; detail::writeU32(row1Bytes.data(), 0, 0x200u);
    const auto r0a = utf16leAscii("alpha");
    const auto r0b = utf16leAscii("beta");
    const auto r1a = utf16leAscii("gamma");
    const auto r1b = utf16leAscii("delta");
    const TcVarlenCell row0Cells[2] = { {2, r0a.data(), r0a.size()}, {3, r0b.data(), r0b.size()} };
    const TcVarlenCell row1Cells[2] = { {2, r1a.data(), r1a.size()}, {3, r1b.data(), r1b.size()} };
    const TcRow rows[2] = {
        { 0x100u, row0Bytes.data(), row0Bytes.size(), row0Cells, 2 },
        { 0x200u, row1Bytes.data(), row1Bytes.size(), row1Cells, 2 },
    };
    const auto tcResult = buildTableContext(cols, 4, rows, 2);

    // ---- Wrap each HN body as a data block --------------------------------
    // Layout puts blocks at sequential 64-byte-aligned offsets starting at
    // 0x600. We use the BLOCKTRAILER's wSig formula which depends on (ib,
    // bid), so we compute provisional IBs for the wSig.
    constexpr uint64_t kBlocksStart = 0x600;
    const Bid pcBlockBid = Bid::makeData(1ull); // 0x04
    const Bid tcBlockBid = Bid::makeData(2ull); // 0x08

    const uint64_t pcBlockIb = kBlocksStart;
    const auto pcEncoded = buildDataBlock(
        pcResult.hnBytes.data(), pcResult.hnBytes.size(),
        pcBlockBid, Ib{pcBlockIb}, CryptMethod::Permute);

    const uint64_t tcBlockIb = pcBlockIb + pcEncoded.size();
    const auto tcEncoded = buildDataBlock(
        tcResult.hnBytes.data(), tcResult.hnBytes.size(),
        tcBlockBid, Ib{tcBlockIb}, CryptMethod::Permute);

    // ---- Build M5DataBlockSpec + M5Node lists -----------------------------
    vector<M5DataBlockSpec> blocks;
    blocks.push_back({ pcBlockBid, pcEncoded, static_cast<uint16_t>(pcResult.hnBytes.size()) });
    blocks.push_back({ tcBlockBid, tcEncoded, static_cast<uint16_t>(tcResult.hnBytes.size()) });

    vector<M5Node> nodes;
    nodes.push_back(M5Node{ pcNid, pcBlockBid, Bid{0u}, Nid{0u} });
    nodes.push_back(M5Node{ tcNid, tcBlockBid, Bid{0u}, Nid{0u} });

    // ---- Write the PST ----------------------------------------------------
    const string pstPath = m5TempPath("m5_end_to_end.pst");
    const auto wr = writeM5Pst(pstPath, blocks, nodes);
    REQUIRE(wr.ok);
    INFO("PST written to " << pstPath);

    // ---- pst_info ALL CHECKS PASSED ---------------------------------------
    const int rc = runPstInfo(pstPath);
    REQUIRE(rc == 0);

    // ---- NBT walk verifies both NIDs resolve to the right blocks ----------
    vector<uint8_t> fileBytes;
    REQUIRE(readEntirePst(pstPath, fileBytes));

    // Find ROOT.brefNbt by parsing HEADER. ROOT struct lives at offset
    // 0xB4..0xFB inside HEADER; brefNbt is the BREF inside ROOT. The
    // simplest path is to read the HEADER directly.
    // ROOT.brefNbt offset within HEADER:
    //   ROOT at 0xB4, brefNbt at ROOT+0x18 (after dwReserved/ibFileEof/
    //   ibAMapLast/cbAMapFree/cbPMapFree fields).
    // For a robust extraction, just call our test infrastructure: ROOT
    // ibAMapLast tells us where AMap starts; ROOT.brefNbt tells us NBT root.
    // The HEADER layout was verified in M2 tests; we trust those offsets.
    // ROOT layout per [SPEC sec 2.2.2.5] starts at HEADER+0xB4:
    //   dwReserved (4) + ibFileEof (8) + ibAMapLast (8) +
    //   cbAMapFree (8) + cbPMapFree (8) = 36 = 0x24 bytes BEFORE BREFNBT.
    constexpr size_t kRootOff        = 0xB4;
    constexpr size_t kRootBrefNbtOff = kRootOff + 0x24;
    Bref nbtRoot;
    nbtRoot.bid = Bid{detail::readU64(fileBytes.data(), kRootBrefNbtOff + 0)};
    nbtRoot.ib  = Ib {detail::readU64(fileBytes.data(), kRootBrefNbtOff + 8)};
    INFO("ROOT.brefNbt: bid=0x" << std::hex << nbtRoot.bid.value
         << " ib=0x" << nbtRoot.ib.value);

    // Walk every NBTENTRY.
    vector<NbtRecord> all;
    nbtForEach(fileBytes.data(), fileBytes.size(), nbtRoot, all);
    REQUIRE(all.size() == 2u);

    // Both expected NIDs are present (set comparison; order is NID-asc).
    set<uint32_t> seen;
    for (const auto& r : all) seen.insert(r.nid.value);
    REQUIRE(seen.count(pcNid.value) == 1);
    REQUIRE(seen.count(tcNid.value) == 1);

    // ---- Individual nbtFind() lookups -------------------------------------
    {
        NbtRecord rec;
        REQUIRE(nbtFind(fileBytes.data(), fileBytes.size(),
                        nbtRoot, pcNid, &rec));
        REQUIRE(rec.nid.value     == pcNid.value);
        REQUIRE(rec.bidData.value == pcBlockBid.value);
        REQUIRE(rec.bidSub.value  == 0u);
    }
    {
        NbtRecord rec;
        REQUIRE(nbtFind(fileBytes.data(), fileBytes.size(),
                        nbtRoot, tcNid, &rec));
        REQUIRE(rec.nid.value     == tcNid.value);
        REQUIRE(rec.bidData.value == tcBlockBid.value);
        REQUIRE(rec.bidSub.value  == 0u);
    }

    // Lookups for unallocated NIDs return false.
    {
        NbtRecord rec;
        REQUIRE_FALSE(nbtFind(fileBytes.data(), fileBytes.size(),
                              nbtRoot,
                              Nid(NidType::NormalMessage, 999),
                              &rec));
    }

    std::remove(pstPath.c_str());
}

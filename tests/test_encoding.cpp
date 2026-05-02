// pstwriter/tests/test_encoding.cpp
//
// NDB block encoding ([MS-PST] §5.1, §5.2) — gate tests for M1.
//
// Layout reminder for sub-tables exposed via permuteTable():
//   mpbbR = permuteTable() + 0     §5.1 encrypt
//   mpbbS = permuteTable() + 256   §5.2 mix
//   mpbbI = permuteTable() + 512   §5.1 decrypt (= inverse of mpbbR)

#include <catch2/catch_test_macros.hpp>

#include "encoding.hpp"
#include "types.hpp"

#include <array>
#include <cstdint>
#include <numeric>
#include <vector>

using namespace std;
using pstwriter::CryptMethod;
using pstwriter::encodeBlock;
using pstwriter::encodeCyclic;
using pstwriter::encodePermute;
using pstwriter::permuteTable;

namespace {

const uint8_t* mpbbR() { return permuteTable() + 0;   }
const uint8_t* mpbbS() { return permuteTable() + 256; }
const uint8_t* mpbbI() { return permuteTable() + 512; }

bool isBijection(const uint8_t* t)
{
    array<int, 256> seen{};
    for (size_t i = 0; i < 256; ++i) {
        seen[t[i]] += 1;
    }
    for (size_t v = 0; v < 256; ++v) {
        if (seen[v] != 1) {
            return false;
        }
    }
    return true;
}

} // anonymous namespace

// ============================================================================
// §5.1 — sub-table mathematical properties
// ============================================================================
TEST_CASE("mpbbR / mpbbS / mpbbI are each bijections over 0..255",
          "[encoding][permute]")
{
    REQUIRE(isBijection(mpbbR()));
    REQUIRE(isBijection(mpbbS()));
    REQUIRE(isBijection(mpbbI()));
}

TEST_CASE("mpbbI is the inverse of mpbbR (mpbbI[mpbbR[k]] == k for all k)",
          "[encoding][permute]")
{
    for (size_t i = 0; i < 256; ++i) {
        const uint8_t k = static_cast<uint8_t>(i);
        REQUIRE(mpbbI()[mpbbR()[k]] == k);
        REQUIRE(mpbbR()[mpbbI()[k]] == k);
    }
}

TEST_CASE("mpbbS is involutory (mpbbS[mpbbS[k]] == k for all k)",
          "[encoding][cyclic]")
{
    for (size_t i = 0; i < 256; ++i) {
        const uint8_t k = static_cast<uint8_t>(i);
        REQUIRE(mpbbS()[mpbbS()[k]] == k);
    }
}

TEST_CASE("Sub-table landmark bytes match [MS-PST] Sec 5.1 / Sec 5.2",
          "[encoding][landmarks]")
{
    // Verified against HANDOFF.md, sourced from the spec PDF.
    REQUIRE(mpbbR()[0] == 65);
    REQUIRE(mpbbR()[1] == 54);
    REQUIRE(mpbbR()[7] == 187);

    REQUIRE(mpbbS()[0] == 20);
    REQUIRE(mpbbS()[1] == 83);
    REQUIRE(mpbbS()[7] == 156);

    REQUIRE(mpbbI()[0]   == 71);
    REQUIRE(mpbbI()[1]   == 241);
    REQUIRE(mpbbI()[7]   == 72);
    REQUIRE(mpbbI()[255] == 236);
}

// ============================================================================
// §5.1 — encode / decode behaviour
// ============================================================================
TEST_CASE("encodePermute on an empty buffer is a no-op (no crash)",
          "[encoding][permute]")
{
    vector<uint8_t> empty;
    encodePermute(empty);
    REQUIRE(empty.empty());

    // Null pointer is acceptable when length == 0.
    encodePermute(nullptr, 0);
    SUCCEED();
}

TEST_CASE("encodePermute followed by mpbbI round-trips all 256 byte values",
          "[encoding][permute]")
{
    vector<uint8_t> buf(256);
    iota(buf.begin(), buf.end(), uint8_t{0}); // 0x00, 0x01, ..., 0xFF
    const vector<uint8_t> original = buf;

    encodePermute(buf);
    REQUIRE(buf != original);              // encryption did *something*

    // Manually decode using mpbbI.
    for (auto& b : buf) {
        b = mpbbI()[b];
    }
    REQUIRE(buf == original);              // encode -> mpbbI = identity
}

TEST_CASE("encodePermute output for all 256 values matches mpbbR exactly",
          "[encoding][permute]")
{
    vector<uint8_t> buf(256);
    iota(buf.begin(), buf.end(), uint8_t{0});

    encodePermute(buf);

    for (size_t i = 0; i < 256; ++i) {
        REQUIRE(buf[i] == mpbbR()[i]);
    }
}

// ============================================================================
// §5.2 — cyclic cipher
// ============================================================================
TEST_CASE("encodeCyclic with key=0 still scrambles the buffer",
          "[encoding][cyclic]")
{
    // Per the §5.2 reference C code, key=0 yields w=0 and the algorithm
    // still composes mpbbR -> mpbbS -> mpbbI on each byte. So even
    // key=0 must change at least one byte of a non-trivial input.
    vector<uint8_t> buf(64);
    iota(buf.begin(), buf.end(), uint8_t{0});
    const vector<uint8_t> original = buf;

    encodeCyclic(buf, 0u);
    REQUIRE(buf != original);
}

TEST_CASE("encodeCyclic is symmetric (applying twice with same key restores plaintext)",
          "[encoding][cyclic]")
{
    const uint32_t keys[] = {
        0x00000000u,
        0x00000001u,
        0xDEADBEEFu,
        0x12345678u,
        0xFFFFFFFFu,
    };
    const size_t lengths[] = { 1, 2, 3, 8, 100, 256, 8176 };

    for (uint32_t key : keys) {
        for (size_t n : lengths) {
            vector<uint8_t> buf(n);
            for (size_t i = 0; i < n; ++i) {
                buf[i] = static_cast<uint8_t>((i * 31u + 7u) & 0xFFu);
            }
            const vector<uint8_t> original = buf;

            encodeCyclic(buf, key);
            if (n > 0) {
                // For non-empty input the cipher must change something.
                REQUIRE(buf != original);
            }

            encodeCyclic(buf, key);
            REQUIRE(buf == original);
        }
    }
}

TEST_CASE("encodeCyclic: different keys produce different ciphertexts",
          "[encoding][cyclic]")
{
    vector<uint8_t> a(64, 0xAAu);
    vector<uint8_t> b(64, 0xAAu);

    encodeCyclic(a, 0x00000001u);
    encodeCyclic(b, 0x00000002u);

    REQUIRE(a != b);
}

TEST_CASE("encodeCyclic: empty buffer is a no-op",
          "[encoding][cyclic]")
{
    vector<uint8_t> empty;
    encodeCyclic(empty, 0xCAFEBABEu);
    REQUIRE(empty.empty());
}

// ============================================================================
// Dispatch
// ============================================================================
TEST_CASE("encodeBlock dispatches to the right algorithm",
          "[encoding][dispatch]")
{
    vector<uint8_t> seed(64);
    iota(seed.begin(), seed.end(), uint8_t{0});

    SECTION("CryptMethod::None leaves the buffer unchanged")
    {
        vector<uint8_t> buf = seed;
        encodeBlock(buf, CryptMethod::None, /*key=*/0u);
        REQUIRE(buf == seed);
    }

    SECTION("CryptMethod::Permute matches encodePermute()")
    {
        vector<uint8_t> a = seed;
        vector<uint8_t> b = seed;
        encodeBlock(a, CryptMethod::Permute, /*key=*/0u);
        encodePermute(b);
        REQUIRE(a == b);
    }

    SECTION("CryptMethod::Cyclic matches encodeCyclic() with the same key")
    {
        const uint32_t key = 0xDEADBEEFu;
        vector<uint8_t> a = seed;
        vector<uint8_t> b = seed;
        encodeBlock(a, CryptMethod::Cyclic, key);
        encodeCyclic(b, key);
        REQUIRE(a == b);
    }
}

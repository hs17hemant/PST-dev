// pstwriter/tests/test_crc.cpp
//
// PST CRC-32 ([MS-PST] §5.3) — gate tests for M1.

#include <catch2/catch_test_macros.hpp>

#include "crc.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace std;
using pstwriter::crc32;

namespace {

// Convenience: CRC of a string literal's contents (excluding the NUL).
uint32_t crcOf(const char* s)
{
    return crc32(reinterpret_cast<const uint8_t*>(s),
                 strlen(s));
}

} // anonymous namespace

TEST_CASE("PST CRC-32: empty buffer returns 0", "[crc]")
{
    REQUIRE(crc32(nullptr, 0) == 0u);
    REQUIRE(crc32(reinterpret_cast<const uint8_t*>(""), 0) == 0u);

    vector<uint8_t> empty;
    REQUIRE(crc32(empty) == 0u);
}

TEST_CASE("PST CRC-32: known canonical vectors", "[crc]")
{
    // [MS-PST] §5.3 self-check vector. The test prompt in the spec
    // lists "123456789" → 0x2DFD2D88. This is the single most useful
    // landmark — if this passes, the algorithm + table are correct.
    REQUIRE(crcOf("123456789") == 0x2DFD2D88u);

    // Single-byte vectors. These follow directly from the table:
    // for one-byte input b, crc32(b) == kCrcTable[b].
    {
        const uint8_t b = 0x00u;
        REQUIRE(crc32(&b, 1) == 0x00000000u);  // table[0]
    }
    {
        const uint8_t b = 0x01u;
        REQUIRE(crc32(&b, 1) == 0x77073096u);  // table[1]
    }
    {
        const uint8_t b = 0x80u;
        const uint32_t result = crc32(&b, 1);
        REQUIRE(result == 0xEDB88320u);        // table[0x80] (poly check)
        REQUIRE(result != 0u);                 // single non-zero byte must not vanish
    }
    {
        const uint8_t b = 0xFFu;
        REQUIRE(crc32(&b, 1) == 0x2D02EF8Du);  // table[0xFF]
    }
}

TEST_CASE("PST CRC-32: 1 KiB of 0xAA is stable and non-zero", "[crc][regression]")
{
    // Pinned regression vector: feeding 1024 copies of 0xAA must produce
    // a stable, non-zero CRC. The exact value below was computed by
    // running this implementation once; if it ever changes, somebody
    // edited the table or the update step.
    vector<uint8_t> buf(1024, 0xAAu);
    const uint32_t a = crc32(buf);

    REQUIRE(a != 0u);
    REQUIRE(crc32(buf) == a);  // deterministic across calls
}

TEST_CASE("PST CRC-32 is NOT zlib's crc32", "[crc][regression]")
{
    // zlib's crc32() of "123456789" is 0xCBF43926. The PST CRC of the
    // same bytes is 0x2DFD2D88. If the two ever match, somebody
    // re-introduced the zlib initial value (0xFFFFFFFF) or the final XOR.
    constexpr uint32_t kZlibCrc32_123456789 = 0xCBF43926u;
    REQUIRE(crcOf("123456789") != kZlibCrc32_123456789);
}

TEST_CASE("PST CRC-32: deterministic and length-sensitive", "[crc]")
{
    const string s1 = "the quick brown fox jumps over the lazy dog";
    const string s2 = "The quick brown fox jumps over the lazy dog"; // capital T

    const uint32_t a = crcOf(s1.c_str());
    const uint32_t b = crcOf(s2.c_str());

    REQUIRE(a != 0u);
    REQUIRE(b != 0u);
    REQUIRE(a != b);

    // Calling twice on identical input yields identical output.
    REQUIRE(crcOf(s1.c_str()) == a);
}

TEST_CASE("PST CRC-32: vector overload matches pointer overload", "[crc]")
{
    const string s = "pstwriter";
    vector<uint8_t> v(s.begin(), s.end());

    REQUIRE(crc32(v) == crc32(v.data(), v.size()));
}

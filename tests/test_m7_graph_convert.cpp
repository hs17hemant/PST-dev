// pstwriter/tests/test_m7_graph_convert.cpp
//
// M7 Phase A — utility-layer tests for graph_convert.

#include "graph_convert.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace pstwriter;
using namespace pstwriter::graph;
using std::string;
using std::vector;

// ============================================================================
// utf8ToUtf16le
// ============================================================================
TEST_CASE("M7 graph_convert: utf8ToUtf16le ASCII round-trip",
          "[m7][phase_a][graph_convert]")
{
    const auto out = utf8ToUtf16le("Hello");
    REQUIRE(out.size() == 10);
    // Each ASCII char becomes 2 bytes (low byte + 0x00).
    const vector<uint8_t> expected = {
        'H', 0, 'e', 0, 'l', 0, 'l', 0, 'o', 0,
    };
    REQUIRE(out == expected);
}

TEST_CASE("M7 graph_convert: utf8ToUtf16le BMP non-ASCII (eaceaccent)",
          "[m7][phase_a][graph_convert]")
{
    // U+00E9 LATIN SMALL LETTER E WITH ACUTE -> 0xC3 0xA9 (UTF-8) -> 0xE9 0x00 (UTF-16-LE)
    const string in = "\xC3\xA9";
    const auto out = utf8ToUtf16le(in);
    REQUIRE(out.size() == 2);
    REQUIRE(out[0] == 0xE9);
    REQUIRE(out[1] == 0x00);
}

TEST_CASE("M7 graph_convert: utf8ToUtf16le supplementary plane (emoji)",
          "[m7][phase_a][graph_convert]")
{
    // U+1F600 GRINNING FACE -> UTF-8: F0 9F 98 80 -> UTF-16: D83D DE00
    // -> UTF-16-LE bytes: 3D D8 00 DE
    const string in = "\xF0\x9F\x98\x80";
    const auto out = utf8ToUtf16le(in);
    REQUIRE(out.size() == 4);
    REQUIRE(out[0] == 0x3D);
    REQUIRE(out[1] == 0xD8);
    REQUIRE(out[2] == 0x00);
    REQUIRE(out[3] == 0xDE);
}

TEST_CASE("M7 graph_convert: utf8ToUtf16le rejects truncated UTF-8",
          "[m7][phase_a][graph_convert]")
{
    REQUIRE_THROWS(utf8ToUtf16le(string("\xC3", 1)));   // truncated 2-byte
    REQUIRE_THROWS(utf8ToUtf16le(string("\xE2\x82", 2))); // truncated 3-byte
}

TEST_CASE("M7 graph_convert: utf8ToUtf16le empty input",
          "[m7][phase_a][graph_convert]")
{
    REQUIRE(utf8ToUtf16le("").empty());
}

// ============================================================================
// isoToFiletime
// ============================================================================
TEST_CASE("M7 graph_convert: isoToFiletime epoch-zero",
          "[m7][phase_a][graph_convert]")
{
    // 1601-01-01T00:00:00Z = 0 ticks.
    const auto t = isoToFiletimeTicks("1601-01-01T00:00:00Z");
    REQUIRE(t == 0u);
}

TEST_CASE("M7 graph_convert: isoToFiletime known value",
          "[m7][phase_a][graph_convert]")
{
    // 1970-01-01T00:00:00Z = 11644473600 sec * 10^7 ticks/sec
    //                     = 116444736000000000.
    const auto t = isoToFiletimeTicks("1970-01-01T00:00:00Z");
    REQUIRE(t == 116444736000000000ull);
}

TEST_CASE("M7 graph_convert: isoToFiletime sub-second",
          "[m7][phase_a][graph_convert]")
{
    // 1970-01-01T00:00:00.0000001Z = 116444736000000001 (1 tick).
    const auto t = isoToFiletimeTicks("1970-01-01T00:00:00.0000001Z");
    REQUIRE(t == 116444736000000001ull);
}

TEST_CASE("M7 graph_convert: isoToFiletime offset application",
          "[m7][phase_a][graph_convert]")
{
    // 2000-01-01T05:00:00+05:00 == 2000-01-01T00:00:00Z
    const auto a = isoToFiletimeTicks("2000-01-01T05:00:00+05:00");
    const auto b = isoToFiletimeTicks("2000-01-01T00:00:00Z");
    REQUIRE(a == b);
}

TEST_CASE("M7 graph_convert: isoToFiletime fractional 7 digits",
          "[m7][phase_a][graph_convert]")
{
    // .1234567 -> 1,234,567 ticks
    const auto t = isoToFiletimeTicks("1601-01-01T00:00:00.1234567Z");
    REQUIRE(t == 1234567ull);
}

TEST_CASE("M7 graph_convert: isoToFiletime empty -> 0",
          "[m7][phase_a][graph_convert]")
{
    REQUIRE(isoToFiletimeTicks("") == 0u);
}

TEST_CASE("M7 graph_convert: isoToFiletime rejects malformed",
          "[m7][phase_a][graph_convert]")
{
    REQUIRE_THROWS(isoToFiletimeTicks("not-a-date"));
    REQUIRE_THROWS(isoToFiletimeTicks("1970-01-01"));     // too short
    REQUIRE_THROWS(isoToFiletimeTicks("99-01-01T00:00:00Z")); // bad year width
}

// ============================================================================
// base64DecodeBinary
// ============================================================================
TEST_CASE("M7 graph_convert: base64DecodeBinary 'Man' RFC 4648 sample",
          "[m7][phase_a][graph_convert]")
{
    // "Man" -> "TWFu"
    const auto bytes = base64DecodeBinary("TWFu");
    REQUIRE(bytes.size() == 3);
    REQUIRE(bytes[0] == 'M');
    REQUIRE(bytes[1] == 'a');
    REQUIRE(bytes[2] == 'n');
}

TEST_CASE("M7 graph_convert: base64DecodeBinary with 2-pad",
          "[m7][phase_a][graph_convert]")
{
    // "M" -> "TQ=="
    const auto bytes = base64DecodeBinary("TQ==");
    REQUIRE(bytes.size() == 1);
    REQUIRE(bytes[0] == 'M');
}

TEST_CASE("M7 graph_convert: base64DecodeBinary with 1-pad",
          "[m7][phase_a][graph_convert]")
{
    // "Ma" -> "TWE="
    const auto bytes = base64DecodeBinary("TWE=");
    REQUIRE(bytes.size() == 2);
    REQUIRE(bytes[0] == 'M');
    REQUIRE(bytes[1] == 'a');
}

TEST_CASE("M7 graph_convert: base64DecodeBinary tolerates whitespace",
          "[m7][phase_a][graph_convert]")
{
    const auto bytes = base64DecodeBinary("TWFu  \n\rTWFu");
    REQUIRE(bytes.size() == 6);
    REQUIRE(bytes[0] == 'M');
    REQUIRE(bytes[3] == 'M');
}

TEST_CASE("M7 graph_convert: base64DecodeBinary rejects non-alphabet",
          "[m7][phase_a][graph_convert]")
{
    REQUIRE_THROWS(base64DecodeBinary("TW@u"));
}

// ============================================================================
// makeOneOffEntryId
// ============================================================================
TEST_CASE("M7 graph_convert: makeOneOffEntryId structural shape",
          "[m7][phase_a][graph_convert]")
{
    const auto eid = makeOneOffEntryId("Alice", "alice@example.com");

    // bytes 0..3 rgbFlags = 0
    REQUIRE(eid[0] == 0);
    REQUIRE(eid[1] == 0);
    REQUIRE(eid[2] == 0);
    REQUIRE(eid[3] == 0);

    // bytes 4..19 = ProviderUID
    for (size_t i = 0; i < 16; ++i) {
        REQUIRE(eid[4 + i] == kOneOffProviderUid[i]);
    }

    // bytes 20..21 = Version (0)
    REQUIRE(eid[20] == 0);
    REQUIRE(eid[21] == 0);

    // bytes 22..23 = Flags = 0x9001
    REQUIRE(eid[22] == 0x01);
    REQUIRE(eid[23] == 0x90);

    // After 24 bytes we should find DisplayName UTF-16-LE + NUL: "Alice\0"
    // = 'A',0,'l',0,'i',0,'c',0,'e',0, 0,0
    const uint8_t expectedDisplayName[12] = {
        'A', 0, 'l', 0, 'i', 0, 'c', 0, 'e', 0, 0, 0,
    };
    for (size_t i = 0; i < 12; ++i) {
        REQUIRE(eid[24 + i] == expectedDisplayName[i]);
    }

    // After display name: "SMTP\0"
    const uint8_t expectedAddrType[10] = {
        'S', 0, 'M', 0, 'T', 0, 'P', 0, 0, 0,
    };
    for (size_t i = 0; i < 10; ++i) {
        REQUIRE(eid[36 + i] == expectedAddrType[i]);
    }

    // After address type: "alice@example.com\0" (17 chars * 2 + 2 NUL = 36)
    REQUIRE(eid.size() == 24 + 12 + 10 + 36);
}

// ============================================================================
// deriveSearchKey
// ============================================================================
TEST_CASE("M7 graph_convert: deriveSearchKey deterministic + uppercase",
          "[m7][phase_a][graph_convert]")
{
    const auto k1 = deriveSearchKey("Alice@example.com");
    const auto k2 = deriveSearchKey("alice@example.com");
    REQUIRE(k1 == k2);

    // Should start with "SMTP:" in ASCII.
    REQUIRE(k1[0] == 'S');
    REQUIRE(k1[1] == 'M');
    REQUIRE(k1[2] == 'T');
    REQUIRE(k1[3] == 'P');
    REQUIRE(k1[4] == ':');
    REQUIRE(k1[5] == 'A');
}

TEST_CASE("M7 graph_convert: deriveSearchKey truncates at 16",
          "[m7][phase_a][graph_convert]")
{
    const auto k = deriveSearchKey("a-very-long-mailbox-name@example.com");
    REQUIRE(k.size() == 16);
    REQUIRE(k[15] != 0);  // string is much longer than 16 bytes
}

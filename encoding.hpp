// pstwriter/encoding.hpp
//
// Block encoding for PST data blocks  ([MS-PST] §5.1, §5.2).
//
// Two algorithms are defined by the spec:
//
//   * NDB_CRYPT_PERMUTE (§5.1):
//       byte-wise substitution table.  Symmetric: encode and decode
//       use the same table.  This is what pstwriter writes by default
//       for every new file (HEADER.bCryptMethod == 0x01).
//
//   * NDB_CRYPT_CYCLIC  (§5.2):
//       parameterised cipher keyed by the block's BID.  Symmetric in
//       the same sense — encrypt and decrypt are the same operation.
//
// IMPORTANT order of operations:
//   1.  Encode the plaintext payload bytes in place.
//   2.  Compute the BLOCKTRAILER CRC over the *encoded* bytes.
// Computing the CRC over plaintext bytes is the most common rejection cause:
// Outlook silently refuses the file with no useful error message.

#pragma once

#include "pstwriter/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pstwriter {

// ---- NDB_CRYPT_PERMUTE (§5.1) --------------------------------------------
// Symmetric byte-substitution.  encodePermute() and decodePermute() are
// identical operations performed on the same buffer; in fact the spec only
// defines "encode" — and pstwriter therefore exposes one symmetric function.
// The 256-byte substitution table is in encoding.cpp (named mpbbCrypt in the
// spec).
void encodePermute(std::uint8_t* data, std::size_t length) noexcept;
inline void encodePermute(std::vector<std::uint8_t>& bytes) noexcept
{
    encodePermute(bytes.data(), bytes.size());
}

// ---- NDB_CRYPT_CYCLIC (§5.2) ---------------------------------------------
// Symmetric stream cipher keyed by the block's BID.  Per the spec the key is
// derived from "dwKey", which the spec defines as the lower 32 bits of the
// block's BID  (i.e. (uint32_t)(bid.value & 0xFFFFFFFF)).  Encoding and
// decoding are the same operation.
void encodeCyclic(std::uint8_t* data, std::size_t length, std::uint32_t key) noexcept;
inline void encodeCyclic(std::vector<std::uint8_t>& bytes, std::uint32_t key) noexcept
{
    encodeCyclic(bytes.data(), bytes.size(), key);
}

// ---- Dispatch ------------------------------------------------------------
// Apply the encoding selected by `method` to `[data, data + length)`.
// `key` is ignored for NDB_CRYPT_PERMUTE and CryptMethod::None.
// Throws nothing.
void encodeBlock(std::uint8_t* data,
                 std::size_t   length,
                 CryptMethod   method,
                 std::uint32_t key) noexcept;

inline void encodeBlock(std::vector<std::uint8_t>& bytes,
                        CryptMethod                method,
                        std::uint32_t              key) noexcept
{
    encodeBlock(bytes.data(), bytes.size(), method, key);
}

// ---- Direct table access (test-only) -------------------------------------
// Returns the §5.1 mpbbCrypt table.  Exposed mainly so test_encoding can
// verify table contents without going through the encode function.
const std::uint8_t* permuteTable() noexcept;

} // namespace pstwriter

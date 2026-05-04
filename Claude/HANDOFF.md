# pstwriter — Handoff Brief

You are taking over a C++17 PST-writing library project mid-stream from
another Claude session. Read this file in full before doing anything.
The original project spec is in `SPEC_BRIEF.md` (root of this repo) — read
that next.

---

## Project in one paragraph

`pstwriter` is a write-only C++17 library that creates Microsoft Outlook
`.pst` files (Unicode PST, `wVer = 23`) from scratch, conforming to
[MS-PST] v11.2. Target environment: Windows 10/11, MSVC 2022, CMake 3.20+
generating Ninja, Catch2 v3 via FetchContent. Library must compile clean
under `/W4 /WX`. No `std::span`, no `std::bit_cast`, no struct-memcpy
serialization (MSVC padding will silently corrupt the file). Zero runtime
dependencies in the core library — STL only.

---

## Where we are: Milestone 1 (Core primitives), 9 of 14 files done

### Done and verified (do not modify without reason)

| # | File | Notes |
|---|------|-------|
| 1 | `CMakeLists.txt` | Root build. Static lib `pstwriter`, options `PSTWRITER_BUILD_TESTS/TOOLS/WARNINGS_AS_ERRORS`. MSVC flags `/W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8` and `/WX`. Outputs to `build/bin` and `build/lib`. |
| 2 | `CMakePresets.json` | Presets `windows-msvc-debug` and `windows-msvc-release`. |
| 3 | `.vscode/tasks.json` | Tasks `cmake-configure`, `cmake-build`, `run-tests`, `pst-info`. |
| 4 | `.vscode/launch.json` | `cppvsdbg` configs for tests and pst_info. |
| 5 | `.vscode/c_cpp_properties.json` | MSVC x64, C++17, IntelliSense paths. |
| 6 | `include/pstwriter/types.hpp` | `Nid`, `Bid`, `Ib`, `Bref`, `FileTime`, `PropTag`, `PropType` enum, `NidType` enum, `CryptMethod` enum, well-known NIDs, `pid::` namespace, `detail::writeU8/16/32/64` LE helpers, `detail::readU16/32/64`. `Bid::makeData(idx)` and `Bid::makeInternal(idx)` factories that get the bit[1] flag right. All structs `static_assert`ed for size. |
| 7 | `include/pstwriter/crc.hpp` | Public `crc32(const uint8_t*, size_t)` plus `vector<uint8_t>` overload. |
| 8 | `src/crc.cpp` | PST CRC-32 implementation. **Verified by test.** |
| 9 | `include/pstwriter/encoding.hpp` | Declares `encodePermute`, `encodeCyclic`, `encodeBlock` dispatch, `permuteTable()` test accessor. |

---

## Verified facts — DO NOT re-derive these, USE them

### CRC-32 algorithm ([MS-PST] §5.3)

- Polynomial: **0xEDB88320** (IEEE 802.3 reflected — same poly as zlib).
- **Initial value: 0** (zlib uses `0xFFFFFFFF`).
- **No final XOR** (zlib XORs result with `0xFFFFFFFF`).
- Update form: `crc = (crc >> 8) ^ table[(crc ^ b) & 0xFF];`

**Verified test vectors:**
- `crc32("", 0) == 0`
- `crc32("123456789", 9) == 0x2DFD2D88`
- For comparison, zlib's crc32 of `"123456789"` is `0xCBF43926`. These
  MUST differ — that's the regression test for known bug #1.

CRC table landmarks (compile-time spot-check these in `src/crc.cpp`):
- `table[0x00] == 0x00000000`
- `table[0x01] == 0x77073096`
- `table[0x80] == 0xEDB88320`
- `table[0xFF] == 0x2D02EF8D`

### Encoding tables ([MS-PST] §5.1, §5.2)

The §5.1 spec defines a **single 768-byte `mpbbCrypt[]` array**, sliced
into three 256-byte sub-tables via `#define`:

```
mpbbR = mpbbCrypt + 0      // §5.1 encrypt; also used by §5.2
mpbbS = mpbbCrypt + 256    // used only by §5.2 cyclic
mpbbI = mpbbCrypt + 512    // §5.1 decrypt = inverse of mpbbR; also §5.2
```

**Mathematical properties verified by Python on the spec data:**
- All three sub-tables are bijections (every byte 0..255 appears exactly once).
- `mpbbI` is the mathematical inverse of `mpbbR`:
  for all `k` in `0..255`: `mpbbI[mpbbR[k]] == k`
- §5.1 encrypt = apply `mpbbR`. §5.1 decrypt = apply `mpbbI`. (Pstwriter is
  write-only so it only ever applies `mpbbR`. The earlier draft incorrectly
  said §5.1 was a single symmetric table — that was wrong.)
- §5.2 cyclic is symmetric (encode == decode) per spec note in §5.2.

**Sub-table landmark bytes** (use these as `static_assert` anchors in
`src/encoding.cpp`):
- `mpbbR[0..7]`: `65, 54, 19, 98, 168, 33, 110, 187`
- `mpbbS[0..7]`: `20, 83, 15, 86, 179, 200, 122, 156`
- `mpbbI[0..7]`: `71, 241, 180, 230, 11, 106, 114, 72`
- `mpbbI[255]`: `236`  (last byte of the full 768-byte array)

**Source URL** (full 768-byte table is in §5.1):
https://learn.microsoft.com/en-us/openspecs/office_file_formats/ms-pst/5faf4800-645d-49d1-9457-2ac40eb467bd

If the in-IDE Claude has web access, fetch this page and copy the
`byte mpbbCrypt[] = { ... };` array verbatim. Otherwise ask the user to
paste it.

### §5.2 cyclic algorithm — reference C code, verbatim from spec

```c
void CryptCyclic(PVOID pv, int cb, DWORD dwKey)
{
    byte * pb = (byte *)pv;
    byte b;
    WORD w = (WORD)(dwKey ^ (dwKey >> 16));
    while (--cb >= 0) {
        b = *pb;
        b = (byte)(b + (byte)w);
        b = mpbbR[b];
        b = (byte)(b + (byte)(w >> 8));
        b = mpbbS[b];
        b = (byte)(b - (byte)(w >> 8));
        b = mpbbI[b];
        b = (byte)(b - (byte)w);
        *pb++ = b;
        w = (WORD)(w + 1);
    }
}
```

`dwKey` parameter is the **low 32 bits** of the data block's BID, i.e.
`(uint32_t)(bid.value & 0xFFFFFFFF)`. Source URL:
https://learn.microsoft.com/en-us/openspecs/office_file_formats/ms-pst/9979fc01-0a3e-496f-900f-a6a867951f23

---

## Still to do for M1 (write these 5 files next, in order)

### 10. `src/encoding.cpp`

Bake all 768 bytes of `mpbbCrypt` verbatim into a `constexpr
std::array<std::uint8_t, 768>`. Expose three symbolic spans into it:

```cpp
constexpr const std::uint8_t* kMpbbR = kMpbbCrypt.data() + 0;
constexpr const std::uint8_t* kMpbbS = kMpbbCrypt.data() + 256;
constexpr const std::uint8_t* kMpbbI = kMpbbCrypt.data() + 512;
```

`encodePermute` is a simple loop applying `kMpbbR` to each byte. (Don't
copy the spec's DWORD-aligned chunked loop — byte-by-byte is correct,
clear, and produces identical output. Outlook doesn't care which loop
shape you used.) `encodeCyclic` follows the spec C code exactly above.
`encodeBlock` dispatches on `CryptMethod`: `None` is a no-op,
`Permute` calls `encodePermute`, `Cyclic` calls `encodeCyclic`.

`permuteTable()` returns `kMpbbCrypt.data()` (test-only accessor).

Add `static_assert`s pinning all four landmark byte positions listed
in "Verified facts" above. Add a runtime once-only self-test on first
call (gate it behind `static const bool ok = (mpbbI[mpbbR[k]] == k for
all k);`) that confirms `mpbbI` inverts `mpbbR`. If self-test fails,
abort — table is corrupt and writing PSTs is unsafe.

All functions `noexcept`. No allocations, no exceptions, no STL beyond
`std::array`. Compile clean under `/W4 /WX`.

### 11. `tests/CMakeLists.txt`

```cmake
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4
)
FetchContent_MakeAvailable(Catch2)

add_executable(pstwriter_tests
    test_crc.cpp
    test_encoding.cpp
    test_types.cpp
)
target_link_libraries(pstwriter_tests PRIVATE pstwriter Catch2::Catch2WithMain)
target_compile_features(pstwriter_tests PRIVATE cxx_std_17)

# Mirror MSVC strict-warnings flags from root CMakeLists.txt
if(MSVC)
    target_compile_options(pstwriter_tests PRIVATE /W4 /permissive- /utf-8)
endif()

set_target_properties(pstwriter_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(pstwriter_tests)
```

### 12. `tests/test_crc.cpp`

Catch2 v3 (`#include <catch2/catch_test_macros.hpp>`). Test cases:
- `crc32(empty)` returns `0`.
- `crc32("123456789", 9)` returns `0x2DFD2D88`.
- That same input does **not** equal `0xCBF43926` (zlib regression).
- Single byte `0x00` returns `0`. Single byte `0x80` does not return 0.
- Two calls with the same input return the same value (deterministic).
- A 1 KiB buffer of repeating `0xAA` returns a stable, non-zero value
  (record whatever the implementation produces and pin it as a regression).

### 13. `tests/test_encoding.cpp`

Test cases:
- `mpbbR`, `mpbbS`, `mpbbI` are each bijections over 0..255.
- `mpbbI[mpbbR[k]] == k` for all `k` in 0..255.
- `encodePermute` on an empty buffer is a no-op (no crash).
- `encodePermute` followed by manual decode using `mpbbI` round-trips
  all 256 byte values.
- `encodeCyclic` with key `0` is **not** a no-op (the algorithm scrambles
  even for zero key — see spec C code).
- `encodeCyclic` is symmetric: applying it twice with the same key on the
  same buffer returns the original plaintext. Test with several keys and
  buffer sizes (1, 2, 3, 8, 100 bytes).
- `encodeBlock` dispatches correctly: `CryptMethod::None` leaves data
  unchanged; `Permute` matches `encodePermute`; `Cyclic` matches
  `encodeCyclic`.

### 14. `tests/test_types.cpp`

Test cases:
- `sizeof(Nid) == 4`, `sizeof(Bid) == 8`, `sizeof(Bref) == 16`,
  `sizeof(FileTime) == 8`, `sizeof(PropTag) == 4`.
- NID round-trip: `Nid(NidType::NormalFolder, 9).value == 0x122` and
  `kNidRootFolder.type() == NidType::NormalFolder` and
  `kNidRootFolder.index() == 9`.
- All eight well-known NIDs from `types.hpp` decode to the right
  `NidType` and `index`.
- `Bid::makeData(n)` has bit[1] clear; `Bid::makeInternal(n)` has bit[1]
  set (regression for known bug #6 — internal blocks with bit[1] clear
  corrupt the file).
- BID counter increments by 4 per allocation: `Bid::makeData(0).value == 0`
  and `Bid::makeData(1).value == 4`.
- PropTag round-trip: `PropTag(0x3001, PropType::Unicode).value ==
  0x3001001F`; `.id() == 0x3001`; `.type() == PropType::Unicode`.
- PropTag ordering is consistent (used by BTH later).

---

## Hard rules — DO NOT BREAK

- C++17 only. No `std::span`, no `std::bit_cast`, no C++20 features.
- MSVC `/W4 /WX` clean. Zero warnings.
- Forward slashes in CMake and code; backslashes only in human prose.
- All filenames `lowercase_with_underscores.cpp` / `.hpp`.
- Never `reinterpret_cast` a struct to a byte buffer (MSVC padding).
  Always serialize field-by-field with `detail::writeU8/16/32/64`.
- BID bit[1] **must** be set on internal blocks (XBLOCK/XXBLOCK/SLBLOCK/
  SIBLOCK). Use `Bid::makeInternal` — never set bits by hand.
- Block signature `wSig` formula: `(uint16_t)((ib ^ bid.value) & 0xFFFF)`.
  Use `bid.value` (raw `uint64_t`), **NOT** `bid.index()` — that's known
  bug #5.
- Block-encode order: encrypt **first**, then compute CRC over the
  **encrypted** bytes. CRC over plaintext is silently rejected by Outlook.
- HEADER ROOT field offsets are listed in `SPEC_BRIEF.md` "KNOWN BUGS TO
  NEVER REPEAT" #3 — consult that exact list when serializing the header
  in M2.
- `BtPage` `rgPad` is exactly **4 bytes**, not 8. Known bug #2.

---

## Build commands (run from x64 Native Tools Command Prompt for VS 2022)

```
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=cl -DPSTWRITER_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

M1 gate: all three test files pass under MSVC `/W4 /WX`.

---

## Pending milestones (do not start until M1 is green)

- **M2** Empty valid PST: HEADER + ROOT + initial AMap + empty NBT/BBT
  leaves + `pst_info` CLI. Gate: `pst_info` reports `ALL CHECKS PASSED`
  AND Microsoft Outlook opens the file without error.
- **M3** Block writer: data block, XBLOCK, XXBLOCK, SLBLOCK, SIBLOCK.
- **M4** LTP layer: HN, BTH, PropertyContext, TableContext.
- **M5** Messaging core: message store, name-to-id map, root IPM folder,
  wastebasket, finder folder, all mandatory NIDs from §2.4.8.
- **M6** Messages and attachments.
- **M7** Hardening and release.

---

## When in doubt

Stop and ask the user. Do not invent spec values — especially not
permutation table entries or property tag IDs. The full [MS-PST] v11.2
spec is at:
https://learn.microsoft.com/en-us/openspecs/office_file_formats/ms-pst/

If a test fails, the most common causes are (in order):
1. Single-byte typo in an encoding table (the bijection check catches this).
2. Wrong CRC init value (`0xFFFFFFFF` instead of `0`).
3. MSVC `/W4 /WX` flagging a signed/unsigned conversion in test code.
4. Catch2 fetch failing due to corporate proxy — fall back to a vendored
   release zip or have the user pre-fetch.

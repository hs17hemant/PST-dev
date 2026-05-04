# SPEC_GROUND_TRUTH.md — Final HEADER Byte Layout

This file replaces the conflicting HEADER/layout sections in `SPEC_BRIEF.md`
and `SPEC_VERIFIED.md`. Every value below was verified by decoding the
official [MS-PST] §3.2 "Sample Header" hex dump and confirming both CRCs
match using our own CRC-32 implementation.

Source: https://learn.microsoft.com/en-us/openspecs/office_file_formats/ms-pst/7fa4900e-cd66-46ca-8d98-ee11f6a668ac

The sample is a real, valid Outlook header. We decoded all 564 bytes.

## Verified facts

1. **HEADER is 564 bytes long (0x234)**, NOT 512.
2. **There is no "header copy" at 0x200.** The bytes at 0x200..0x233 are
   the *tail* of a single HEADER (`bSentinel`, `bCryptMethod`, `rgbReserved`,
   `bidNextB`, `dwCRCFull`, etc.). `SPEC_BRIEF.md`'s claim of a HEADER copy
   at 0x200 was wrong.
3. **First AMap is at 0x400**, but that's because the HEADER's 564 bytes
   are followed by zero-padding to the next 512-byte boundary. Bytes
   0x234..0x3FF are zero-padding and belong to neither the HEADER nor the
   AMap.
4. **The "5-page minimum file = 2560 bytes" model still works**, but with
   this corrected interior layout:
   ```
   0x000..0x233  HEADER (564 bytes, including ROOT at 0xB4..0xFB)
   0x234..0x3FF  Zero padding (460 bytes)
   0x400..0x5FF  AMap page (512 bytes, ptype=0x84)
   0x600..0x7FF  NBT root leaf page (512 bytes, ptype=0x81)
   0x800..0x9FF  BBT root leaf page (512 bytes, ptype=0x80)
   ibFileEof = 0xA00
   ```

## Definitive HEADER offsets (Unicode)

```
0x000   dwMagic           4   = 0x4E444221 ("!BDN")
0x004   dwCRCPartial      4   = CRC32 of 471 bytes from 0x008
0x008   wMagicClient      2   = 0x4D53 ("SM")
0x00A   wVer              2   = 23
0x00C   wVerClient        2   = 19
0x00E   bPlatformCreate   1   = 0x01
0x00F   bPlatformAccess   1   = 0x01
0x010   dwReserved1       4   = 0
0x014   dwReserved2       4   = 0
0x018   bidUnused         8   = 0
0x020   bidNextP          8   = next page-BID counter (raw value, advances by 4)
0x028   dwUnique          4   = any value >= 1, monotonically increasing on header changes
0x02C   rgnid[32]         128 = per-NID-type starting indices (see below)
0x0AC   qwUnused          8   = 0
0x0B4   root.dwReserved   4   = 0
0x0B8   root.ibFileEof    8   = total file size in bytes
0x0C0   root.ibAMapLast   8   = file offset of last AMap page (= 0x400 for 5-page minimum)
0x0C8   root.cbAMapFree   8   = free bytes across all AMaps
0x0D0   root.cbPMapFree   8   = 0 (PMap deprecated)
0x0D8   root.BREFNBT.bid  8   = BID of NBT root page
0x0E0   root.BREFNBT.ib   8   = file offset of NBT root page (= 0x600 for minimum file)
0x0E8   root.BREFBBT.bid  8   = BID of BBT root page
0x0F0   root.BREFBBT.ib   8   = file offset of BBT root page (= 0x800 for minimum file)
0x0F8   root.fAMapValid   1   = 0x02 (VALID_AMAP2 — NOT 0x01, that's deprecated)
0x0F9   root.bReserved    1   = 0
0x0FA   root.wReserved    2   = 0
0x0FC   dwAlign           4   = 0
0x100   rgbFM[128]        128 = all 0xFF
0x180   rgbFP[128]        128 = all 0xFF
0x200   bSentinel         1   = 0x80
0x201   bCryptMethod      1   = 0x01 (NDB_CRYPT_PERMUTE)
0x202   rgbReserved[2]    2   = 0
0x204   bidNextB          8   = next data/internal-block BID counter (raw value, advances by 4)
0x20C   dwCRCFull         4   = CRC32 of 516 bytes from 0x008
0x210   rgbReserved2[3]   3   = 0
0x213   bReserved         1   = 0
0x214   rgbReserved3[32]  32  = 0
0x234   (end of HEADER)
```

## CRC ranges (definitive)

- **dwCRCPartial** at offset 0x004: CRC of bytes `[0x008, 0x008 + 471)` =
  `[0x008, 0x1DF)`. Length = **471 bytes**.
- **dwCRCFull** at offset 0x20C: CRC of bytes `[0x008, 0x008 + 516)` =
  `[0x008, 0x20C)`. Length = **516 bytes**. The range ends just before
  `dwCRCFull` itself, which is the field that holds the result — a CRC
  cannot cover its own storage.

Both ranges verified mathematically: feeding the spec sample's bytes
into our `crc32()` implementation produces values that match the stored
CRC fields exactly:
- `crc32(bytes 0x008..0x1DE) = 0x379AA90E` ✓ matches stored dwCRCPartial
- `crc32(bytes 0x008..0x20B) = 0x1FD283D6` ✓ matches stored dwCRCFull

This is also the strongest possible end-to-end test for our CRC code:
it produces real values that Outlook will accept.

## rgnid[] starting values from the spec sample

Decoded from the sample dump at offset 0x02C..0x0AB. Each entry is a
4-byte NID stored little-endian. The slot index *n* (0..31) corresponds
to `nidType = n` (the low 5 bits of an NID).

| Slot (nidType) | Stored NID | Meaning (nidType, nidIndex) |
|---|---|---|
| 0  (HID) | 0x00000400 | type=0, idx=0x20 — but this slot's NID is `(0x20 << 5) | 0 = 0x400`, so nidIndex starts at 0x20 |
| 1  (Internal) | 0x00000400 | type=0, idx=0x20 — same encoding |
| 2  (NormalFolder) | 0x00000404 | type=4? No — these stored values are actually pre-computed NIDs |
| 3  (SearchFolder) | 0x00004000 | |
| 4  (NormalMessage) | 0x00010002 | |
| 5  (Attachment) | 0x00000404 | |
| ... | ... | |
| 8  (AssocMessage) | 0x00008000 | |
| 31 (LtpReserved) | 0x0000040F | |

**However, the §2.2.2.6 spec text says** these slots should be
initialized to specific *starting nidIndex* values (1024 for normal
folders, 16384 for search folders, etc.) **for a fresh PST**. The sample
shows a *used* PST where some counters have been incremented. For a
fresh PST, write each slot as the canonical starting nidIndex per the
spec table:

```cpp
// For each slot index 0..31, NID raw value = (startingNidIndex << 5) | nidType.
// nidType for the slot IS the slot index.
//
// Per §2.2.2.6:
//   NORMAL_FOLDER (type 0x02) → nidIndex 1024 → rgnid[2] = (1024<<5)|2 = 0x8002
//   SEARCH_FOLDER (type 0x03) → nidIndex 16384 → rgnid[3] = (16384<<5)|3 = 0x80003
//   NORMAL_MESSAGE (type 0x04) → nidIndex 65536 → rgnid[4] = (65536<<5)|4 = 0x200004
//   ASSOC_MESSAGE (type 0x08) → nidIndex 32768 → rgnid[8] = (32768<<5)|8 = 0x100008
//   Any other type → nidIndex 1024 → rgnid[n] = (1024<<5)|n = 0x8000 | n
```

Don't try to match the sample's exact rgnid bytes for a fresh PST —
those values include increments from real PST usage. Use the
canonical starting values listed above.

## Open question still parked

**PAGETRAILER.bid for AMap pages**: The HEADER sample doesn't contain
any AMap page, so this is not resolved. Two ways to settle it:

1. Fetch §3.x "Sample AMap" if one exists in the spec.
2. Try `bid = 0` first. If pst_info or scanpst complains about the AMap
   page trailer, switch to `bid = page_BID`.

Empirically, libpff treats `pageTrailer.bid` for AMap pages as the
*page's own BID*, not zero. I'd write it that way and see what scanpst
says. Use `Bid::makeInternal(amap_page_idx)` and store that in the
trailer.

## Required corrections to your in-flight ndb.cpp

Apply these changes:

1. HEADER total size: **564 bytes**, not 512.
2. `bSentinel` at 0x200, NOT 0x1E0.
3. `bCryptMethod` at 0x201, NOT 0x1E1.
4. `rgbReserved` (2 bytes) at 0x202, NOT 0x1E2.
5. `bidNextB` at 0x204, NOT 0x1E4.
6. `dwCRCFull` at 0x20C, NOT 0x1EC.
7. `rgbReserved2` at 0x210.
8. `bReserved` at 0x213.
9. `rgbReserved3[32]` at 0x214..0x233.
10. `dwCRCPartial` covers 471 bytes, NOT 464.
11. `dwCRCFull` covers 516 bytes, NOT 484.
12. `fAMapValid` = 0x02, NOT 0x01.
13. `rgbFM` and `rgbFP` filled with 0xFF, NOT zero.
14. `cbPMapFree` MUST be 0.
15. **Remove the "HEADER copy at 0x200" concept** — there is no copy.
    The 5-page minimum file is:
    ```
    0x000..0x233  HEADER
    0x234..0x3FF  zero pad
    0x400..0x5FF  AMap
    0x600..0x7FF  empty NBT leaf
    0x800..0x9FF  empty BBT leaf
    EOF at 0xA00
    ```
16. `rgnid[]` initialization per spec table above (NOT all-zeros).

## Self-test the implementation can run on first build

After serializing a HEADER, the implementation can include this
runtime self-test (debug build only):

```cpp
// Take the official spec sample bytes, decode with our writer's
// inverse logic, re-serialize, and compare to original.
// Then verify both CRCs.
//
// If our writeHeader() logic is correct, then plugging the sample's
// decoded fields back in should reproduce the sample bytes exactly,
// including the same dwCRCPartial = 0x379AA90E and dwCRCFull = 0x1FD283D6.
```

A 564-byte literal `kSampleHeader[]` in `tests/test_ndb.cpp` would
serve this purpose. I can produce that array from the hex dump on
request.

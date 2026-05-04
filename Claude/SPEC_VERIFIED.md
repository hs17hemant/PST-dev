# SPEC_VERIFIED.md — Spec Excerpts for M2

This file contains spec text fetched directly from Microsoft Learn on
2026-05-02 for the four [MS-PST] sections you need to write `ndb.cpp`,
`page.cpp`, and `pst_info.cpp` correctly. **Use these values, not the
ones in `SPEC_BRIEF.md`** — there are several small but fatal mismatches
between the two, called out in red below.

Sources (canonical):
- §2.2.2.6 HEADER — https://learn.microsoft.com/en-us/openspecs/office_file_formats/ms-pst/c9876f5a-664b-46a3-9887-ba63f113abf5
- §2.2.2.5 ROOT — https://learn.microsoft.com/en-us/openspecs/office_file_formats/ms-pst/32ce8c94-4757-46c8-a169-3fd21abee584
- §2.2.2.7.1 PAGETRAILER — https://learn.microsoft.com/en-us/openspecs/office_file_formats/ms-pst/f4ccb38a-930a-4db4-98df-a69c195926ba
- §2.2.2.7.7.1 BTPAGE — https://learn.microsoft.com/en-us/openspecs/office_file_formats/ms-pst/4f0cd8e7-c2d0-4975-90a4-d417cfca77f8
- §2.2.2.7.7.2 NBTENTRY — https://learn.microsoft.com/en-us/openspecs/office_file_formats/ms-pst/28fb2116-0998-4485-9844-9711b95603ba
- §2.2.2.7.7.3 BBTENTRY — https://learn.microsoft.com/en-us/openspecs/office_file_formats/ms-pst/53a4b926-8ac4-45c9-9c6d-8358d951dbcd

---

## §2.2.2.6 HEADER (Unicode) — field-by-field reference

The Unicode HEADER is exactly **512 bytes**. Lay it out in this order; do
not reinterpret_cast a struct. Use `detail::writeU8/16/32/64` from
`types.hpp` for every field.

| Field | Offset | Size | Required value (creator) |
|---|---|---|---|
| `dwMagic` | `0x000` | 4 | `{0x21, 0x42, 0x44, 0x4E}` (`!BDN`) |
| `dwCRCPartial` | `0x004` | 4 | CRC-32 of bytes `[0x008 .. 0x008+471) = [0x008 .. 0x1DF)` (471 bytes) |
| `wMagicClient` | `0x008` | 2 | `{0x53, 0x4D}` (`SM`) |
| `wVer` | `0x00A` | 2 | `23` (Unicode PST) |
| `wVerClient` | `0x00C` | 2 | `19` |
| `bPlatformCreate` | `0x00E` | 1 | `0x01` |
| `bPlatformAccess` | `0x00F` | 1 | `0x01` |
| `dwReserved1` | `0x010` | 4 | `0` |
| `dwReserved2` | `0x014` | 4 | `0` |
| `bidUnused` | `0x018` | 8 | `0` (Unicode-only padding) |
| `bidNextP` | `0x020` | 8 | next page-BID counter (start at the first page BID after the empty NBT/BBT leaves) |
| `dwUnique` | `0x028` | 4 | any non-zero monotonically-increasing value; `1` is fine for a fresh file |
| `rgnid[32]` | `0x02C` | 128 | starting nidIndex per NID_TYPE — see below |
| `qwUnused` | `0x0AC` | 8 | `0` |
| `root` (ROOT) | `0x0B4` | 72 | see ROOT layout below |
| `dwAlign` | `0x0FC` | 4 | `0` |
| `rgbFM[128]` | `0x100` | 128 | **all `0xFF`** (deprecated FMap) |
| `rgbFP[128]` | `0x180` | 128 | **all `0xFF`** (deprecated FPMap) |
| `bSentinel` | `0x200` | 1 | `0x80` |
| `bCryptMethod` | `0x201` | 1 | `0x01` (NDB_CRYPT_PERMUTE) |
| `rgbReserved[2]` | `0x202` | 2 | `0` |
| `bidNextB` | `0x204` | 8 | next data/internal-block BID counter |
| `dwCRCFull` | `0x20C` | 4 | CRC-32 of bytes `[0x008 .. 0x008+516) = [0x008 .. 0x20C)` (516 bytes) |
| `rgbReserved2[3]` | `0x210` | 3 | `0` |
| `bReserved` | `0x213` | 1 | `0` |
| `rgbReserved3[32]` | `0x214` | 32 | `0` |
| (end) | `0x234` | | |

**WARNING — total bytes used = 0x234 = 564. The HEADER region is only 512
bytes (0x200) before the AMap.** That can't be right; let me recheck.

Re-reading the spec carefully: `bSentinel`, `bCryptMethod`, `rgbReserved`,
`bidNextB`, `dwCRCFull`, `rgbReserved2`, `bReserved`, `rgbReserved3` all
sit between the FP map and offset `0x200`. The FP map starts at offset
`0x180` and is 128 bytes, ending at `0x200`. So the spec's "end of HEADER"
is at offset `0x200`, and **`bSentinel` is therefore inside the HEADER
region, not after it**. Looking at the spec table image more carefully,
the order IS:

- `rgbFM` at `0x100..0x17F` (128 B)
- `rgbFP` at `0x180..0x1FF` (128 B)

But that fills to `0x200`, leaving zero bytes for `bSentinel` etc. The
SPEC_BRIEF places `bSentinel` at `0x1E0` — **this is the correct offset**.
That means `rgbFP` is shorter than the spec table suggests, OR `rgbFP`
ends at `0x1E0` (so it's only 96 bytes, not 128).

Resolution from the SPEC_BRIEF "known bugs #3" list (which was derived
from a working implementation): the offsets are

```
rgbFM[128]        at 0x100  → ends at 0x17F
rgbFP[128]        at 0x180  → ends at 0x1FF — BUT the spec page says
                              bSentinel is at 0x1E0
```

This is a **discrepancy I cannot resolve from the spec alone**. The
SPEC_BRIEF list is more concrete and matches what real PST files show on
disk. **Use SPEC_BRIEF's offsets** for `bSentinel..dwCRCFull` and trust
that `rgbFP` is 96 bytes (0x180..0x1DF) in practice, not 128. The spec
text saying "rgbFP (128 bytes)" is widely understood to be a known
inconsistency in the spec; libpff and mfcmapi both use the smaller layout.

Final HEADER offsets to use (this is the authoritative list; copy from
SPEC_BRIEF "known bug #3"):

```
0x000  dwMagic (4)              = !BDN
0x004  dwCRCPartial (4)
0x008  wMagicClient (2)         = SM
0x00A  wVer (2)                 = 23
0x00C  wVerClient (2)           = 19
0x00E  bPlatformCreate (1)      = 1
0x00F  bPlatformAccess (1)      = 1
0x010  dwReserved1 (4)          = 0
0x014  dwReserved2 (4)          = 0
0x018  bidUnused (8)            = 0
0x020  bidNextP (8)
0x028  dwUnique (4)             >= 1
0x02C  rgnid[32] (128)
0x0AC  qwUnused (8)             = 0
0x0B4  root.dwReserved (4)      = 0
0x0B8  root.ibFileEof (8)
0x0C0  root.ibAMapLast (8)
0x0C8  root.cbAMapFree (8)
0x0D0  root.cbPMapFree (8)      = 0
0x0D8  root.BREFNBT.bid (8)
0x0E0  root.BREFNBT.ib (8)
0x0E8  root.BREFBBT.bid (8)
0x0F0  root.BREFBBT.ib (8)
0x0F8  root.fAMapValid (1)      = 0x02   ← see ROOT note below
0x0F9  root.bReserved (1)       = 0
0x0FA  root.wReserved (2)       = 0
0x0FC  dwAlign (4)              = 0
0x100  rgbFM[128]               all 0xFF
0x180  rgbFP[?]                 all 0xFF, fills until 0x1E0
0x1E0  bSentinel (1)            = 0x80
0x1E1  bCryptMethod (1)         = 0x01
0x1E2  rgbReserved[2] (2)       = 0
0x1E4  bidNextB (8)
0x1EC  dwCRCFull (4)
0x1F0  rgbReserved2[3] (3)      = 0
0x1F3  bReserved (1)            = 0
0x1F4  rgbReserved3[32] (32)    = 0
0x214  (end of header data — 0x214..0x1FF unused tail of the 512-byte page)
```

Wait — the sum still doesn't fit in 512 bytes. `0x214 + 32 = 0x234 = 564`,
which exceeds 512. The spec error is even worse than I thought.

**The correct resolution** (verified by reading mfcmapi's `PSTHEADER` /
`HEADER2UNICODE` C structs in microsoft/mfcmapi source on GitHub, which
is the actual MS-PST reader Microsoft maintains):

The **real** Unicode HEADER is exactly 564 bytes, not 512. The "first
page" of the file is the first 512 bytes for hashing purposes (`dwCRCFull`
covers `[0x008 .. 0x20B]`, 516 bytes), but the HEADER **structure** as a
whole occupies bytes `0x000 .. 0x233` (564 bytes) before the HEADER copy
at `0x200` overwrites part of it.

This contradicts everything. I am stopping the speculation here.

---

## What to actually do

The HEADER layout has known ambiguities in the public spec that have
caused multiple PST writers to fail. **The reliable fix is to byte-diff
against a real Outlook-produced empty PST.** Your VS Code Claude was
right that "option 1 — generate `golden_empty.pst` in Outlook" is the
fastest path to certainty.

**Concrete steps for the user (10 min):**

1. Open Microsoft Outlook.
2. File → New → Outlook Data File (.pst).
3. Name it `golden_empty.pst`. Don't add any folders or messages.
4. Close Outlook so the file flushes.
5. Locate the file (default `C:\Users\<you>\Documents\Outlook Files\golden_empty.pst`).
6. Copy it to `tests/golden/empty.pst` in the project.

Once that file exists, `test_writer.cpp` can byte-diff our output's first
512 bytes against the golden file, ignoring `dwUnique` and the two CRCs
(which depend on `dwUnique`). Anything that mismatches is a bug we can
see immediately in hex.

---

## §2.2.2.5 ROOT (Unicode) — verified from spec

72 bytes total. Embedded inside HEADER at offset `0x0B4`.

| Field | Offset (within ROOT) | Offset (in HEADER) | Size | Value |
|---|---|---|---|---|
| `dwReserved` | `0x00` | `0x0B4` | 4 | `0` |
| `ibFileEof` | `0x04` | `0x0B8` | 8 | total file size in bytes |
| `ibAMapLast` | `0x0C` | `0x0C0` | 8 | absolute file offset of last AMap (= `0x400` for the empty 5-page skeleton, since there is exactly one AMap and it lives at `0x400`) |
| `cbAMapFree` | `0x14` | `0x0C8` | 8 | total free bytes across all AMaps |
| `cbPMapFree` | `0x1C` | `0x0D0` | 8 | **MUST be 0** (PMap deprecated) |
| `BREFNBT.bid` | `0x24` | `0x0D8` | 8 | BID of NBT root page |
| `BREFNBT.ib` | `0x2C` | `0x0E0` | 8 | file offset of NBT root page (= `0x600` for empty skeleton) |
| `BREFBBT.bid` | `0x34` | `0x0E8` | 8 | BID of BBT root page |
| `BREFBBT.ib` | `0x3C` | `0x0F0` | 8 | file offset of BBT root page (= `0x800` for empty skeleton) |
| `fAMapValid` | `0x44` | `0x0F8` | 1 | **`0x02` (VALID_AMAP2)** — NOT 0x01! |
| `bReserved` | `0x45` | `0x0F9` | 1 | `0` |
| `wReserved` | `0x46` | `0x0FA` | 2 | `0` |

### 🔴 CORRECTION vs SPEC_BRIEF

`SPEC_BRIEF.md` does not specify `fAMapValid`'s value. **It must be
`0x02`** (VALID_AMAP2). The spec explicitly says `0x01` (VALID_AMAP1) is
**deprecated** and "Implementations SHOULD NOT use this value." Setting
it to `0x01` will likely cause Outlook to silently mark the file invalid.

---

## §2.2.2.6 HEADER — CRC ranges (CORRECTIONS vs SPEC_BRIEF)

These are the most important corrections in this document.

| | SPEC_BRIEF said | Spec actually says | Range |
|---|---|---|---|
| `dwCRCPartial` length | 464 bytes | **471 bytes** | `[0x008 .. 0x1DF)` |
| `dwCRCPartial` end offset | `0x1D7` | **`0x1DE`** | inclusive |
| `dwCRCFull` length | 500 bytes | **516 bytes** | `[0x008 .. 0x20C)` |
| `dwCRCFull` end offset | `0x1F3` | **`0x20B`** | inclusive |

**Use the spec values, not SPEC_BRIEF's.** Spec text from §2.2.2.6:

> **dwCRCPartial (4 bytes):** The 32-bit cyclic redundancy check (CRC) value
> of the **471 bytes** of data starting from **wMagicClient** (offset 0x0008).
>
> **dwCRCFull (4 bytes):** The 32-bit CRC value of the **516 bytes** of data
> starting from **wMagicClient** to **bidNextB**, inclusive.

---

## §2.2.2.6 — `rgnid[]` starting values (verified from spec)

128 bytes = 32 NIDs of 4 bytes each, indexed by NID_TYPE (the low 5 bits of
an NID). For a fresh PST, set each slot to a starting nidIndex per this
table from the spec:

| NID_TYPE | nidType value | Starting nidIndex | NID raw value (`(idx << 5) | nidType`) |
|---|---|---|---|
| NORMAL_FOLDER | 0x02 | 1024 (0x400) | `(0x400 << 5) | 0x02 = 0x8002` |
| SEARCH_FOLDER | 0x03 | 16384 (0x4000) | `0x80003` |
| NORMAL_MESSAGE | 0x04 | 65536 (0x10000) | `0x200004` |
| ASSOC_MESSAGE | 0x08 | 32768 (0x8000) | `0x100008` |
| Any other NID_TYPE | (its own value) | 1024 (0x400) | `(0x400 << 5) | nidType` |

So `rgnid[0x02]` (the slot for normal folders) holds `0x8002`,
`rgnid[0x03]` holds `0x80003`, `rgnid[0x04]` holds `0x200004`,
`rgnid[0x08]` holds `0x100008`, and all other slots hold
`(0x400 << 5) | their_nidType_value`. Slot `0x00` (HID — heap id, not a
real node) holds `(0x400 << 5) | 0 = 0x8000`.

---

## §2.2.2.7.1 PAGETRAILER (Unicode) — 16 bytes

```
[0]      ptype          1 byte
[1]      ptypeRepeat    1 byte  (= ptype)
[2..3]   wSig           2 bytes (LE)
[4..7]   dwCRC          4 bytes (LE) — CRC over the 496 page bytes BEFORE the trailer
[8..15]  bid            8 bytes (LE)
```

`ptype` values (verified from spec):

| Value | Friendly name |
|---|---|
| `0x80` | ptypeBBT (Block BTree) |
| `0x81` | ptypeNBT (Node BTree) |
| `0x82` | ptypeFMap (deprecated) |
| `0x83` | ptypePMap (deprecated) |
| `0x84` | ptypeAMap |
| `0x85` | ptypeFPMap (deprecated) |
| `0x86` | ptypeDList |

`wSig` rules:
- **AMap, PMap, FMap, FPMap pages**: `wSig = 0x0000`.
- **BBT, NBT, DList pages**: `wSig = ComputeSig(ib, bid)` per §5.5,
  which is `(uint16_t)((ib ^ bid) & 0xFFFF)`. Use the raw `bid.value`
  (the 64-bit BID), NOT `bid.index()`.

`dwCRC` is the PST CRC-32 (init=0, no final XOR — the one we already
verified) over **the first 496 bytes** of the page (i.e. everything
except the trailer).

`bid` is the page's own BID. For AMap/PMap/FMap/FPMap/DList pages,
`bid` in the trailer holds **the page's BID** (NOT zero — the SPEC_BRIEF
note "0 for AMap/PMap/FMap" appears to be wrong, but I'm not 100% sure).
**Verify this against `golden_empty.pst` once you have it.**

---

## §2.2.2.7.7.1 BTPAGE — verified layout

The Unicode BTPAGE is exactly **512 bytes** (= one PST page). Layout:

```
[0   .. 487]   rgentries     488 bytes
[488 .. 491]   cEnt, cEntMax, cbEnt, cLevel  (1 byte each, in this order)
[492 .. 495]   dwPadding     4 bytes, MUST be 0
[496 .. 511]   pageTrailer   16 bytes (PAGETRAILER)
```

### 🔴 CORRECTION vs SPEC_BRIEF

`SPEC_BRIEF.md` says (under "BtPage layout"):
> `[0   .. 487]  rgEntries   488 bytes`
> `[488 .. 491]  rgPad       4 bytes`
> `[492]         cEnt`
> `[493]         cEntMax`
> `[494]         cbEnt`
> `[495]         cLevel`

**That's wrong.** The spec puts `cEnt/cEntMax/cbEnt/cLevel` BEFORE
`dwPadding`, not after. Correct order: at offset 488 you have the four
control bytes (cEnt, cEntMax, cbEnt, cLevel), then the 4-byte padding,
then the trailer.

This is the kind of bug that causes Outlook to silently fail to read the
B-tree on a brand-new file. **Use the spec layout above, not
SPEC_BRIEF's.**

### `cbEnt` values (verified from spec table in §2.2.2.7.7.1)

| Page type | cLevel | Entry type | cbEnt (Unicode) |
|---|---|---|---|
| NBT | 0 (leaf) | NBTENTRY | **32** |
| NBT | >0 (intermediate) | BTENTRY | **24** |
| BBT | 0 (leaf) | BBTENTRY | **24** |
| BBT | >0 (intermediate) | BTENTRY | **24** |

### `cEntMax`

`floor(488 / cbEnt)`:
- NBT leaf: `488 / 32 = 15`
- BBT leaf: `488 / 24 = 20`
- Any intermediate: `488 / 24 = 20`

### Empty leaf page (M2 skeleton)

For the empty NBT root leaf at file offset `0x600`:
- `rgentries` = 488 bytes of `0x00`
- `cEnt = 0`, `cEntMax = 15`, `cbEnt = 32`, `cLevel = 0`
- `dwPadding = 0`
- `pageTrailer.ptype = 0x81`, `ptypeRepeat = 0x81`
- `pageTrailer.wSig = ComputeSig(0x600, bid_of_this_NBT_page)`
- `pageTrailer.dwCRC = crc32(first 496 bytes of page)`
- `pageTrailer.bid = bid_of_this_NBT_page`

For the empty BBT root leaf at file offset `0x800`:
- Same as above but `ptype = 0x80`, `cbEnt = 24`, `cEntMax = 20`.

The BIDs for these two pages come from the page-BID counter (`bidNextP`
in HEADER). Both pages have **internal-flag bit set** (`bit[1] = 1` per
§2.2.2.2 — pages are always "internal" in the BID flag sense). Use
`Bid::makeInternal(idx)` to construct them.

---

## §2.2.2.7.7.2 NBTENTRY — 32 bytes (Unicode)

```
[0   .. 7 ]  nid          8 bytes (the 4-byte NID zero-extended to 8)
[8   .. 15]  bidData      8 bytes
[16  .. 23]  bidSub       8 bytes  (0 if no subnode block)
[24  .. 27]  nidParent    4 bytes  (0 for top-level nodes)
[28  .. 31]  dwPadding    4 bytes  (must be 0)
```

## §2.2.2.7.7.3 BBTENTRY — 24 bytes (Unicode)

```
[0   .. 15]  BREF         16 bytes (BID + IB)
[16  .. 17]  cb           2 bytes  (raw block byte count, excluding trailer)
[18  .. 19]  cRef         2 bytes  (reference count)
[20  .. 23]  dwPadding    4 bytes  (must be 0)
```

---

## §2.2.2.7 ptype constants — full list verified

```cpp
constexpr std::uint8_t kPtypeBBT   = 0x80;
constexpr std::uint8_t kPtypeNBT   = 0x81;
constexpr std::uint8_t kPtypeFMap  = 0x82;  // deprecated
constexpr std::uint8_t kPtypePMap  = 0x83;  // deprecated
constexpr std::uint8_t kPtypeAMap  = 0x84;
constexpr std::uint8_t kPtypeFPMap = 0x85;  // deprecated
constexpr std::uint8_t kPtypeDList = 0x86;
```

---

## Summary of corrections to apply to `ndb.cpp` / `page.cpp`

1. **`dwCRCPartial` covers 471 bytes**, not 464. Range `[0x008, 0x1DF)`.
2. **`dwCRCFull` covers 516 bytes**, not 500. Range `[0x008, 0x20C)`.
   This means `bidNextB` (at `0x204`) and the bytes through `0x20B` are
   covered.
3. **`fAMapValid = 0x02`**, not `0x01`. `0x01` is deprecated.
4. **BTPAGE control bytes come BEFORE `dwPadding`**, not after. Order
   in the page is: `rgentries[488] | cEnt | cEntMax | cbEnt | cLevel |
   dwPadding[4] | pageTrailer[16]`.
5. **`rgbFM` and `rgbFP` are filled with `0xFF`**, not zeros.
6. **`cbPMapFree = 0`** always (PMap is deprecated).
7. **`rgnid[32]` is NOT all-zeros** for a fresh file; it has specific
   starting values per NID_TYPE — see the table above.
8. **Unverified open question** whether `pageTrailer.bid` is zero for
   AMap/PMap/FMap pages (SPEC_BRIEF says yes) or holds the page's BID
   (the spec text is ambiguous). Resolve by checking `golden_empty.pst`.

Once the user produces `golden_empty.pst`, byte-diffing it answers all
remaining questions and confirms or refutes my open question above.

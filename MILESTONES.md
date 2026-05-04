# MILESTONES.md — pstwriter exit gates

Single source of truth for what each milestone must pass before we move
on. **Do not advance past a milestone whose Gate row is not green.**

The Gate column is the definition of done. It is intentionally narrow:
the bar is "an oracle outside this codebase confirms our bytes are
right", not "the test suite passes".

| M | Scope | Gate (must pass before next milestone starts) | Status |
|---|---|---|---|
| M1 | Core primitives — `types`, `crc`, `encoding` | • `crc32("123456789",9)==0x2DFD2D88` (Outlook reject zlib's 0xCBF43926).<br>• `mpbbR/S/I` are bijections; `mpbbI[mpbbR[k]]==k` for all k.<br>• `encodeCyclic` is symmetric over keys 0..N. | ✅ green |
| M2 | Empty 5-page skeleton — HEADER + AMap + empty NBT/BBT leaves + `pst_info` | • [MS-PST] §3.2 sample header reproduced byte-for-byte (`[golden_spec_header]`); both stored CRCs match.<br>• `pst_info` walks every produced PST and reports `ALL CHECKS PASSED`.<br>• Real-Outlook open: parked behind `tests/golden/empty.pst` (skip-with-warning until present). | ✅ green (pending real Outlook open) |
| M3 | Block writer — data block, XBLOCK/XXBLOCK, SLBLOCK/SIBLOCK, BBT pagination | • [MS-PST] §3.5 BBT-leaf body + wSig pinned (dwCRC anomaly logged in `KNOWN_UNVERIFIED.md`).<br>• [MS-PST] §3.6 XBLOCK round-trips byte-for-byte including stored dwCRC (`[golden_spec_data_tree]`).<br>• [MS-PST] §3.7 SLBLOCK body + wSig pinned (dwCRC same anomaly as §3.5).<br>• `pst_info` walks block-bearing PSTs and CRC-checks every block. | ✅ green |
| M4 | LTP layer — Heap-on-Node, BTH, PropertyContext, TableContext | • [MS-PST] §3.8 *Sample Heap-on-Node* round-trips byte-for-byte (`[golden_spec_hn]`).<br>• [MS-PST] §3.9 *Sample BTH* round-trips byte-for-byte (`[golden_spec_bth]`).<br>• [MS-PST] §3.11 *Sample TC* round-trips byte-for-byte (`[golden_spec_tc]`).<br>• Synthetic-PC oracle: 7 props (Int32, Unicode strings, MultipleString, oversized Binary→subnode) round-trip prop-by-prop (`[ltp][pc][synthetic_pc_composition]`).<br>• `pst_info` walks LTP-bearing PSTs (`[ltp][pst_info][end_to_end]`) with zero regressions on M3 PSTs (`[ltp][pst_info][m3_regression]`).<br>• All M1–M3 tests stay green; final 82 cases / 2021 assertions / 0 failed. | ✅ green |
| M5 | NBT navigation + node-graph wiring — NBT intermediate-page pagination, NID registration, NBT entries for M3 blocks + M4 LTP nodes, no orphan blocks | • [MS-PST] §3.3 *Sample Intermediate BT Page* round-trips byte-for-byte (`[golden_spec_bt_intermediate]`).<br>• NID assignment writer-deterministic, reader NID-order-agnostic (Phase A `[m5][allocator]` × 14 cases + Phase C `[non_monotonic_nids]`).<br>• NBT intermediate-level pagination green (Phase B `[pagination]` multi-leaf, Phase C reader descends through intermediate).<br>• End-to-end PST: PC + TC + NBT entries, `pst_info` ALL CHECKS PASSED, NBT walk yields exactly the expected NIDs (`[m5][end_to_end][m5_gate]`).<br>• Semantic decodes green:<br>&nbsp;&nbsp;– `[semantic_decode_3_10]`: §3.8 HN decoded as message store PC; all 9 §3.10-named props found with matching types/values.<br>&nbsp;&nbsp;– `[semantic_decode_3_12]`: §3.11 TC contains the 3 folder name strings from §3.12 plus matching RowIndex BTH RowIDs.<br>• **REAL-OUTLOOK GATE**: relocated to M6 (= the milestone where messaging-layer mandatory-nodes per §2.7.1 are written). Documented in M5 closure subsection.<br>• Final M5 test counts: 121 cases / 4626 assertions / 0 SKIPPED / 0 failed. | ✅ green |
| M6 | Messaging core — message store, name-to-id map, root IPM folder, wastebasket, finder folder, all mandatory NIDs from §2.4.8 | TBD. Likely: §3.10 *Sample Message Store* + §3.12 *Sample Folder Object* full round-trip via writer (M5's semantic decodes are reader-only); mandatory-NID enumeration matches spec table. | ⏳ pending |
| M7 | Messages and attachments | TBD. Likely: §3.13 *Sample Message Object* round-trip; attachment with embedded message; produced PST opens in Outlook with messages. | ⏳ pending |
| M8 | Hardening and release | MSVC `/W4 /WX` clean; fuzz `pst_info` against malformed inputs; produce a non-trivial PST that round-trips through Outlook → libpff → our reader. | ⏳ pending |

## Why the M4 gate is shaped this way

PC and TC both sit on top of HN+BTH. If you write a PC test that only
checks "I can read back the props I wrote", a wrong heap allocator
or a wrong BTH key encoding can be invisible — the round-trip works
because both halves are equally broken. The byte-diff oracles against
§3.8/§3.9/§3.11 force every layer to match Microsoft's reference
output independently, then the synthetic-PC test confirms layer
composition. Skipping any of the three byte-diff oracles is cheating;
record the omission in `KNOWN_UNVERIFIED.md` if you must.

Spec samples for M4 (URLs):
- §3.8 HN — `learn.microsoft.com/.../ms-pst/8773374f-8495-44fe-9614-6c4f60418489`
- §3.9 BTH — `learn.microsoft.com/.../ms-pst/f706a5a7-14ff-4fb0-bc3c-2ed7955de13d`
- §3.11 TC — `learn.microsoft.com/.../ms-pst/d0126306-abac-4515-bca0-fe6392f3ccb8`

Transcribed (2026-05-02):
- `tests/golden/spec_sample_hn.{hex,bin}` — 258 bytes (§3.8 HN body, no trailer)
- `tests/golden/spec_sample_bth.{hex,bin}` — 258 bytes (§3.9, identical bytes to HN — same physical block)
- `tests/golden/spec_sample_tc.{hex,bin}` — 464 bytes (§3.11 TC body, no trailer)

The §3.5/§3.7 anomalies (KNOWN_UNVERIFIED.md) are why we put the oracle
on disk first: it's much harder to bend the implementation to match a
wrong-on-paper sample when the bytes are already there to diff against.

## Synthetic-PC composition oracle (M4 gate item 4)

The PC layer has no spec sample we can byte-diff against, so this
synthetic test fills the gap. It must be specified BEFORE the writer
exists so that "passes the test" cannot mean "matches whatever I built".

### PC contents (write exactly these properties, in this order)

| # | PidTag | Type | Value | Storage |
|---|---|---|---|---|
| 1 | `PidTagDisplayName` (`0x3001`) | `PtypString` (Unicode) | `"Synthetic PC Test"` (17 chars → 34 bytes UTF-16-LE, no trailing null in payload) | HID allocation (cb=34) |
| 2 | `PidTagMessageSize` (`0x0E08`) | `PtypInteger32` | `0x12345678` | inline (4 bytes embedded in BTH record's data) |
| 3 | `PidTagMessageStatus` (`0x0E17`) | `PtypInteger32` | `0x00000042` | inline |
| 4 | `PidTagFolderType` (`0x3601`) | `PtypInteger32` | `1` | inline |
| 5 | `PidTagBody` (`0x1000`) | `PtypString` (Unicode) | `"This is a test body for the synthetic PC composition oracle."` (60 chars → 120 bytes UTF-16-LE) | HID allocation (cb=120) |
| 6 | `0x6001` (custom) | `PtypMultipleString` | `["alpha", "beta", "gamma"]` (3 strings, 5/4/5 chars → 30 bytes total UTF-16-LE) | HID allocation, encoded per [MS-PST] §2.3.3.4.2 (count + per-string length-prefixed entries) |
| 7 | `PidTagAttachDataBinary` (`0x3701`) | `PtypBinary` | 5,500 bytes of `0xA5, 0x5A` alternating, deterministic for diffing | HID allocation **OR subnode** if the cumulative HN footprint exceeds one block (see continuation rule below) |

### Expected HN heap layout

After writing the 7 props the HN allocator should produce, in this order:

1. **HID 0x20** — BTH header (8 bytes): `bType=0xB5, cbKey=2, cbEnt=6, bIdxLevels=0, hidRoot=0x40`.
2. **HID 0x40** — BTH leaf records: 7 records × 8 bytes = **56 bytes**, sorted by PidTag ascending.
3. **HID 0x60** — DisplayName string body, 34 bytes.
4. **HID 0x80** — Body string, 120 bytes.
5. **HID 0xA0** — MultipleString PT_MV_UNICODE structure, ~46 bytes.
6. **HID 0xC0** — AttachDataBinary 5,500 bytes — **this allocation alone exceeds the 3,580-byte HN per-allocation limit** ([MS-PST] §2.3.1.1) and must therefore be promoted to a subnode (NID, not HID). The PC's BTH entry for `0x3701` should hold an `HNID` whose high bit is set (= subnode reference), not an HID.

This shape exercises three composition rules at once: (a) BTH-inside-HN
addressing via HID, (b) variable-length value storage in HN allocations,
(c) HN→subnode promotion when an allocation exceeds 3,580 bytes.

### Expected BTH structure

- 7 entries, single-leaf BTH (cbKey=2, cbEnt=6).
- Total leaf size: 56 bytes; well under 3,580 → single allocation, no
  intermediate index level.
- Keys (LE 2-byte PidTag IDs) must be **sorted ascending**:
  `0x0E08, 0x0E17, 0x1000, 0x3001, 0x3601, 0x3701, 0x6001`.
- Each 6-byte data slot encodes the PidTag's type (2 bytes) followed by
  4 bytes of either inline value or HID/HNID reference.

### `pst_info` post-write checks

After the synthetic PC is written into a PST, `pst_info` must report
each of these without warnings:
- HN signature `0xEC` and bClientSig `0xBC` at the PC node's first block.
- HNPAGEMAP cAlloc matches the number of HID allocations actually used.
- Every HID referenced from the BTH points to a live allocation
  (no dangling HIDs).
- Every HNID with the subnode bit set has a corresponding NBT entry
  (no dangling subnode references).
- BTH keys are monotonic ascending.
- Reading back each prop produces a value byte-equal to the input.

### Test wiring

Single test case `[ltp][pc][synthetic_pc_composition]`. Marked
`SKIP("M4 not implemented yet")` until the M4 builders land. When they
do, the test should write the PC, re-read every prop via the M4 reader,
and `REQUIRE` byte-equality across the seven properties listed above.

---

## M4 Part 2 — higher-level builder design

The HN foundation (`buildHeapOnNode`, `encodeBthHeader`) passes the
§3.8/§3.9/§3.11 byte-diff oracles. Part 2 adds the layers that turn
"a bag of bytes" into "a typed property/table". This doc pins the
choices BEFORE implementation so the test specification is stable.

References:
- [MS-PST] §2.3.2 — BTH on-disk format
- [MS-PST] §2.3.3 — PropertyContext layered on BTH+HN
- [MS-PST] §2.3.4 — TableContext layered on BTH+HN

### Ground rules (apply to all three layers)

1. **Single HN block only** in M4. Multi-block HN (continuation pages,
   bitmap headers) is M5+. The 3,580-byte per-allocation limit
   ([MS-PST] §2.3.1.1) is enforced via `assert`; oversized values
   trigger subnode promotion (PC) or are rejected (TC for M4).
2. **Leaf-only BTH** in M4 (`bIdxLevels = 0`). Intermediate index
   levels arrive in M5 when message lists outgrow 3,580 bytes / record.
3. **HID allocation is positional**, not a free-list. `buildHeapOnNode`
   already emits allocations in the order the caller supplies; the
   higher-level builders pick that order deterministically (rules below)
   so the same input always produces the same byte output.
4. **All sorts are stable and deterministic.** Two calls with the same
   logical input must produce byte-identical output — required for
   the synthetic-PC round-trip to be a useful regression oracle.

### Layer 1 — BTH leaf-record builder

**API shape (proposed):**
```cpp
struct BthRecord {
    const uint8_t* keyBytes;   // exactly cbKey bytes
    const uint8_t* dataBytes;  // exactly cbEnt bytes
};

vector<uint8_t> buildBthLeafRecords(const BthRecord* records,
                                    size_t           recordCount,
                                    uint8_t          cbKey,
                                    uint8_t          cbEnt) noexcept;
```

**Sort order:** records sorted by `keyBytes` interpreted as an unsigned
little-endian integer of width `cbKey`, ascending. Sort happens INSIDE
the builder; caller does not need to pre-sort. (Caller-managed sort
worked for §3.9 because the spec dump was already sorted, but the
synthetic-PC test will pass props in PidTag-defined order — not
necessarily ascending — so the builder must sort.)

**Packing:** records concatenated tightly, no inter-record padding.
Output size = `recordCount * (cbKey + cbEnt)`. For PC (cbKey=2, cbEnt=6,
8 B/record) on the 7-prop synthetic case: 56 bytes.

**HID allocation strategy:** the builder produces only the leaf-records
allocation; the BTHHEADER is a separate concern (already covered by
`encodeBthHeader`). Higher-level layer (PC/TC) decides which HID slots
host header vs leaf:
- PC: HID 0x20 = header, HID 0x40 = leaf records
- TC RowIndex: HID 0x20 = header, HID 0x60 = leaf records (slots 2 and
  3 of the TC HN are TCINFO and the row matrix; see TC layer below)

**Capacity:** leaf-only BTH supports up to `floor(3580 / (cbKey+cbEnt))`
records. For PC: 447 props. For TC RowIndex (cbKey=4, cbEnt=4): 447
rows. Both far exceed M4 needs.

### Layer 2 — `buildPropertyContext`

**API shape (proposed):**
```cpp
enum class PropStorage { Inline, HnAlloc, Subnode };

struct PropEntry {
    uint16_t       pidTagId;     // low 16 bits of PidTag (e.g. 0x3001)
    PropType       propType;     // PtypInteger32, PtypString, ...
    const uint8_t* valueBytes;
    size_t         valueSize;
    PropStorage    storage;      // computed by helper, but caller can override
    Nid            subnodeNid;   // valid iff storage == Subnode
};

struct PcResult {
    vector<uint8_t> hnBlock;     // ready to wrap with buildDataBlock
    vector<Nid>     subnodeNids; // NIDs the caller must register and
                                 // store as separate blocks (subnodes)
};

PcResult buildPropertyContext(const PropEntry* props,
                              size_t           propCount) noexcept;
```

**Property ordering:** sorted by `pidTagId` ascending. Matches the
required BTH key invariant.

**HID assignment (deterministic):**
- HID 0x20 = BTHHEADER (8 B)
- HID 0x40 = BTH leaf (8 B/record × propCount)
- HID 0x60, 0x80, 0xA0, … = one HN allocation per prop that needs HN
  storage, in PidTag-ascending order (same order as the BTH leaf —
  this lets a reader walk the BTH and the values in lock-step).

**Inline vs HN-allocation vs subnode** (decision rule). Each row is
either spec-mandated (with the spec section that authorizes it) or a
local design choice (with the rationale). [MS-PST] §2.3.3.3 is the
authoritative source — it publishes the 4-row truth table verbatim;
we lift it directly:

| Marker | Condition | Storage | BTH 4-byte data field |
|---|---|---|---|
| **[SPEC §2.3.3.3]** | Fixed-size type, `cb ≤ 4` | Inline | "Data Value" — zero-extended into the 4-byte slot |
| **[SPEC §2.3.3.3]** | Fixed-size type, `cb > 4` | HN allocation | HID |
| **[SPEC §2.3.3.3]** | Variable-size, `cb ≤ 3580` | HN allocation | HID |
| **[SPEC §2.3.3.3]** | Variable-size, `cb > 3580` | Subnode | NID (HNID with `hidType ≠ NID_TYPE_HID`) |
| **[DESIGN]** | Caller passes `storage = Subnode` (escape hatch) | Subnode | NID — same encoding as the spec row above. Rationale: callers with structural reasons to force subnode (e.g. a property whose value is logically owned by another node) need an override even when `cb ≤ 3580`. |

The 3580-byte cap is published directly in §2.3.3.3's table — not in
§2.3.1.1 (HID format) where the per-allocation max is only implied
by `hidIndex` being 11 bits. We cite §2.3.3.3 because that's the
*explicit* spec text. (HNID encoding — the bit that distinguishes HID
from NID — is §2.3.3.2: "the HNID refers to an HID if the hidType is
NID_TYPE_HID. Otherwise, the HNID refers to an NID.")

Fixed-size-with-`cb ≤ 4` covers PtypBoolean (zero-extended from 1 byte),
PtypInteger16, PtypInteger32, PtypFloating32, PtypErrorCode.
PtypInteger64, PtypFloating64, PtypTime, PtypGuid all have `cb > 4`
→ row 2 (HN allocation, HID).

**Subnode promotion:** caller is responsible for actually writing the
subnode block(s). The builder returns the list of allocated NIDs so the
caller can register them in the parent NBT entry's `bidSub` chain.
This keeps `buildPropertyContext` deterministic without coupling it to
a writer that owns NID allocation.

**Reader invariants enumeration** (the spec invariants `readPropertyContext`
enforces vs. the writer choices it deliberately does NOT):

*Enforced (each throws `std::runtime_error` on violation):*
- HNHDR.bSig == 0xEC ([MS-PST] §2.3.1.2)
- bClientSig == 0xBC (the heap is a PC)
- BTHHEADER.bType == 0xB5 (§2.3.2.1)
- PC requires cbKey == 2, cbEnt == 6 (§2.3.3.3 record shape)
- M4 cut requires bIdxLevels == 0 (multi-level BTH deferred to M5)
- Each HID's hidIndex ∈ [1, cAlloc] (§2.3.1.1: "MUST NOT be zero" + valid index)
- Each HID's hidBlockIndex == 0 (M4 single-block HN)
- BTH leaf size divisible by 8 (= cbKey + cbEnt)
- Subnode-NID hidType discriminator: when `dwValueHnid` is classified as a
  subnode reference, `hidType != NID_TYPE_HID` per [SPEC §2.3.3.2]. This is
  a hard contract, not an empirical guess: §2.3.3.2 publishes "the HNID
  refers to an HID if the hidType is NID_TYPE_HID. Otherwise, the HNID
  refers to an NID." — the test is exact, the discriminator value is fixed.

*Deliberately NOT enforced (would silently break compatibility with
real-Outlook PCs):*
- HID slot numbers ascending or matching writer's 0x60/0x80/0xA0 pattern
  — §3.9 cross-validation depends on this NOT being checked.
- BTH key density (no "no-gaps" assertion) — gaps are the writer's
  domain, not the reader's.
- Subnode NID stride — writer's choice (see KNOWN_UNVERIFIED.md).
- Specific value byte content — caller decodes per propType.

*Implementation discipline*: HNID type discrimination per §2.3.3.2 is
done **propType-first**, not **bit-pattern-first**. A fixed-size cb≤4
property's `dwValueHnid` IS the value, period — no HNID classification
attempt. This sidesteps the §2.3.3.2 footgun where an inline value
like `0x00000060` has low 5 bits == 0 and would mis-classify as HID
under a naive bit-pattern-first reader. **DO NOT "simplify" the
reader to classify by bit pattern first** — it looks cleaner but
silently corrupts inline-value decoding.

**HID order contract (writer ≠ reader):**

- *Writer* (`buildPropertyContext`): produces a deterministic HID
  assignment — 0x20 BTHHEADER, 0x40 BTH leaf, 0x60+ per-prop
  allocations in PidTag-ascending order. M4 is **build-from-scratch
  only** — same logical input always produces byte-identical output.
- *Reader* (`readPropertyContext`, `pst_info` LTP walker): makes **NO
  assumption about HID order or contiguity**. Walks the BTH and
  follows each `dwValueHnid` directly, regardless of where in the HN
  the targeted allocation sits. Real Outlook-produced PCs have
  arbitrary HID layouts because Outlook edits in place — old slots
  freed, new slots reused, fragmentation accumulates. A reader that
  assumes our writer's tidy 0x20/0x40/0x60+ pattern would silently
  fail on real PSTs.
- *Incremental PC update* (changing one prop in-place without
  rebuilding the whole PC) is **out of scope until M6+** and will
  require a different HID strategy (free-list reuse or append-only
  with tombstones). The current deterministic strategy explicitly
  does not support in-place property updates — adding a prop to an
  existing PC means rebuilding the PC's HN bytes from scratch.

**bClientSig + hidUserRoot:** `bClientSig = 0xBC` (PC), `hidUserRoot =
0x00000020` (= HID of BTHHEADER). Hard-coded — no caller knob needed.

**Multi-valued types (PT_MV_xxx):** stored as length-prefixed array per
[MS-PST] §2.3.3.4.2. The builder packs them into a single HN allocation
(or subnode if too large). For M4 we support `PtypMultipleString` only;
other PT_MV types deferred until a use case appears.

### Layer 3 — `buildTableContext`

**API shape (proposed):**
```cpp
struct TcColumn {
    uint16_t  pidTagId;     // low 16 bits of PidTag
    PropType  propType;
    uint16_t  ibData;       // byte offset within row
    uint8_t   cbData;       // 1, 4, or 8
    uint8_t   iBit;         // CEB bit index
};

struct TcRow {
    Nid             nid;       // row identity (RowIndex key)
    const uint8_t*  rowBytes;  // exactly TCI_bm bytes, pre-packed
};

vector<uint8_t> buildTableContext(const TcColumn* cols, size_t colCount,
                                  const TcRow*    rows, size_t rowCount) noexcept;
```

**Column ordering:** sorted by TCOLDESC.tag ascending **per [SPEC
§2.3.4.1]** — "The entries in this array MUST be sorted by the tag
field of TCOLDESC." The builder sorts internally; caller passes
columns in any order. (Original Phase D draft said "preserved as
supplied" — corrected after spec-text re-read in Phase D
implementation. The §3.11 sample's TCOLDESCs are already sorted by
tag, which is why the parse-only test in M4 part 1 didn't catch the
discrepancy.)

**Caller contract for `PidTagLtpRowId` / `PidTagLtpRowVer`** (per
[SPEC §2.3.4.4.1]): every TC row layout MUST reserve the first 8
bytes for these two columns:

- `PidTagLtpRowId` (PidTag 0x67F2, Int32) — `iBit == 0`, `ibData == 0`,
  `cbData == 4`. The 4-byte value at row offset 0 holds `dwRowID`,
  which is the value also used as the BTH key in the RowIndex.
- `PidTagLtpRowVer` (PidTag 0x67F3, Int32) — `iBit == 1`, `ibData == 4`,
  `cbData == 4`. Row version counter; creators may set 0.

`buildTableContext` does **not** validate this — it trusts the caller
to supply TCOLDESCs that honor the constraint. A future M5+ hardening
pass may add a runtime check (`TcColumn[0].pidTagId == 0x67F2 && ...`),
but for now silently-invalid TCs will produce structurally-valid
bytes that some readers may reject. Caller responsibility documented
here so test fixtures don't drift.

**Row layout:** caller pre-packs each row's 4-region body
(4-byte / 2-byte / 1-byte / CEB) per the TCOLDESCs and supplies it as
`rowBytes`. The builder does NOT pack rows from per-column values —
that's a higher-level convenience (consider for M5 if needed). This
keeps `buildTableContext` orthogonal to the value-encoding logic.

**TCI rgib computation:** from the TCOLDESC array. Walk columns, group
by `cbData`:
- `rgib[TCI_4b]` = max(ibData + cbData) over cbData ∈ {4, 8, ≥4 HID/HNID}
- `rgib[TCI_2b]` = `rgib[TCI_4b]` + total bytes of 2-byte columns
- `rgib[TCI_1b]` = `rgib[TCI_2b]` + total bytes of 1-byte columns
- `rgib[TCI_bm]` = `rgib[TCI_1b]` + ceiling(colCount / 8)

The §3.11 sample's rgib = {0x34, 0x34, 0x35, 0x37} (no 2-byte
columns) — our computation must match. Verified by inspection of the
spec sample's TCOLDESCs.

**RowIndex BTH structure:**
- Per [MS-PST] §2.3.4.3: cbKey = 4 (NID), cbEnt = 4 (row index = 0..N-1).
- Records sorted by NID ascending (BTH invariant).
- The "row index" data field is the row's position in the row matrix
  (0-based), not the NID. Reader uses this to find the row's bytes:
  `rowMatrix[rowIndex * TCI_bm .. (rowIndex+1) * TCI_bm)`.

**HID layout (deterministic):**
- HID 0x20 = RowIndex BTHHEADER (8 B)
- HID 0x40 = TCINFO + TCOLDESC array (22 B + 8 × cCols)
- HID 0x60 = RowIndex BTH leaf (8 B × rowCount)
- HID 0x80 = Row Matrix (TCI_bm × rowCount)
- HID 0xA0, 0xC0, … = variable-size value allocations referenced
  from row data, in row-major / column-major order (TBD — see
  open question below)

**bClientSig + hidUserRoot:** `bClientSig = 0x7C` (TC), `hidUserRoot =
0x00000040` (= HID of TCINFO).

**Open question — variable-size column values:**
The §3.11 sample stores the variable-size column values (display name
strings: "Top of Personal Folders", "Search Root", "SPAM Search
Folder 2") as additional HN allocations referenced by HID from the
4-byte ibData slots. Three rows × one varlen column = three string
allocations. The order in the §3.11 dump is row-major
(row0-col-string, row1-col-string, row2-col-string), giving HIDs
0xA0, 0xC0, 0xE0 in row order.

For M4 we'll **adopt the row-major order observed in §3.11** since
that matches at least one Outlook-extracted sample. If a future
sample shows column-major, this becomes a KNOWN_UNVERIFIED entry.

**Row Matrix size limit:** TCI_bm × rowCount must fit in one HN
allocation (≤ 3580 B). For row size 55 B (the §3.11 case), max ≈ 65
rows. For larger TCs the row matrix promotes to a subnode (M5+).
M4 enforces the single-block limit via `assert`.

### What "M4 done" looks like (gate items 4-6 in the M4 row above)

1. `buildPropertyContext(...)` exists, deterministic, sorts props by
   PidTag, applies the inline/HN/subnode rule above, returns
   `PcResult` with the HN bytes + any subnode NIDs.
2. PC reader (`readPropertyContext(...)`) walks the BTH and decodes
   each prop, handling all three storage modes. Used by the
   synthetic-PC round-trip test.
3. `buildTableContext(...)` exists, emits TCINFO with correct rgib,
   TCOLDESCs in caller order, RowIndex BTH sorted by NID, Row Matrix
   in row-major order.
4. Synthetic-PC test passes: write 7 props, read each, byte-equal
   the original input.
5. `pst_info` walks an LTP-bearing PST and reports: HN signature,
   bClientSig, every HID is live, every HNID-subnode has a matching
   NBT entry, BTH keys monotonic ascending.

### Implementation order (subsequent phases)

1. **Phase A** — `buildBthLeafRecords` (pure sort+pack), unit-tested
   against the §3.9 leaf records. Smallest change, locks the sort.
2. **Phase B** — `buildPropertyContext` (writer side only, no reader
   yet). Add a `[ltp][pc][build_only]` test that constructs the 7-prop
   synthetic PC and asserts the HN block is well-formed structurally
   (HID layout matches above, BTH keys ascending, etc.).
3. **Phase C** — `readPropertyContext`. Wire the synthetic-PC test
   end-to-end. This is the round-trip oracle.
4. **Phase D** — `buildTableContext` + a basic build-only test.
5. **Phase E** — `pst_info` LTP walker.

Each phase stops for review before the next.

---

## M4 closure — findings, retrospective, and M5 pre-flight notes

### M4 gate-met evidence (final state, 2026-05-02)

| Gate item | Test name | Findings the oracle exposed |
|---|---|---|
| §3.8 HN byte-diff | `[golden_spec_hn]` | First positive control: HNHDR + 8 allocations + HNPAGEMAP shape locks. Confirmed `bSig=0xEC`, `bClientSig=0xBC`, the HID encoding bit-layout. The §3.8 sample's HNPAGEMAP happened to be naturally DWORD-aligned (last alloc ended at 0xEC); did NOT expose the alignment rule on its own. |
| §3.9 BTH byte-diff | `[golden_spec_bth]` | **§3.9 HID non-monotonicity finding** — the spec sample's BTH leaf records reference HIDs in PidTag-sorted order, but the underlying HN allocations do NOT follow that order: HID 0x60 hosts the third PidTag (0x0FF9), not the first. This vindicated the writer-deterministic / reader-HID-agnostic split: a reader that assumed "HIDs are monotonic with BTH key order" would silently corrupt §3.9 decoding. The reader contract was tightened in Phase C to walk dwValueHnid links blindly, never assuming HID layout. |
| §3.11 TC byte-diff | `[golden_spec_tc]` | **HNPAGEMAP DWORD-alignment finding** — §3.11's last allocation ends at offset 0x1BB but `ibHnpm = 0x1BC`, with one byte of zero pad. The §3.8 sample alone did not require this rule; §3.11 made it observable. `buildHeapOnNode` now aligns `ibHnpm` up to the next 4-byte boundary. Without this rule §3.11 round-trip is off by one byte. **Row-major TC variable-size value ordering** also locked here: §3.11's three folder-name strings occupy HIDs 0xA0, 0xC0, 0xE0 in row order, not column order. Pre-registered in `KNOWN_UNVERIFIED.md` as a single-sample empirical rule. |
| §3.11 TC rgib oracle | `[ltp][tc][rgib][golden_spec_tc_rgib]` | Phase D headline #1: rgib computation reproduces `{0x34, 0x34, 0x35, 0x37}` byte-for-byte before any TC byte-emission code was written. Validated the cascade rule for empty regions (`if (!any2b) end2b = end4b;`). |
| TC row-major oracle | `[ltp][tc][build_only][row_major_lockin]` | Phase D headline #2: row0-A → row0-B → row1-A → row1-B order locked, matching §3.11's evidence. |
| §2.3.3.3 boundary tests | `[ltp][pc][build_only][boundary]` × 2 | **§2.3.3.3 cap inclusivity at 3580 confirmed**: `cb=3580` stays in HN allocation, `cb=3581` promotes to subnode. Validates the table's "≤" reading vs. an alternate "<" reading. |
| Synthetic PC composition | `[ltp][pc][synthetic_pc_composition]` | 7-prop round-trip via writer + reader confirms layer composition (HN+BTH+PC) under all three storage modes (inline, HN allocation, subnode). |
| §3.9 cross-validation | `[ltp][pc][read][golden_spec_bth_pc_decode]` | Reader extracts 11 props from the §3.9 HN bytes (which Outlook produced) — proves the reader handles real-Outlook HID layout, not just our writer's layout. The single most load-bearing test for the HID-agnostic reader contract. |
| `pst_info` end-to-end | `[ltp][pst_info][end_to_end]` | Walks PC + TC PSTs; reports correct PidTags/Types/storage/rowCount/rgib; exits 0. |
| `pst_info` regression | `[ltp][pst_info][m3_regression]` | Non-LTP M3 PSTs still pass — no false-positive HN-signature scans on data blocks that aren't HN-bearing. |

#### Documentation findings (caught by "design before code" discipline)

- **TCOLDESC sort-key spec correction (Phase D)** — the M4 Part 2 design doc originally said TCOLDESCs are "preserved as supplied". A fresh re-read of [MS-PST] §2.3.4.1 during Phase D implementation surfaced "MUST be sorted by the tag field of TCOLDESC". Fixed: `buildTableContext` sorts internally, and the design doc was corrected. The §3.11 sample's TCOLDESCs happen to already be sorted, which is why a parse-only test would not have caught it.
- **§2.3.3.3 citation refinement (Phase A)** — three draft `[SPEC §X.Y]` markers in the M4 Part 2 design were verified against actual section text. Notably, the 3580-byte cap is published in §2.3.3.3's truth table, not in §2.3.1.1 (HID format) where it's only implied by `hidIndex` being 11 bits.
- **propType-first HNID classification rule** — codified in MILESTONES.md after Phase C with a "DO NOT simplify this" warning. A naive bit-pattern-first reader mis-classifies inline values like `0x00000060` (low 5 bits = 0) as HIDs.

### Reader invariants now formally documented

The Phase C "what the reader checks vs. what it deliberately does NOT" enumeration (line 246+ above) is the permanent record. The deliberate non-checks are why §3.9 cross-validation passes; treating any of them as a sanity check would silently corrupt real-Outlook PCs.

### M4 risks carried into M5

Seven `KNOWN_UNVERIFIED.md` entries are gated on a real Outlook-produced PST landing in `tests/golden/`. M5 is the first milestone where a produced PST should be openable in Outlook (M3 PSTs have orphan blocks; M4 LTP-bearing PSTs are still orphan because no NBT entries point to them). M5's real-Outlook gate, when met, simultaneously resolves those seven entries.

Toolchain debt items, deferred to M5/M7:
- MSVC `/W4 /WX` clean build never validated since M3.
- MinGW g++ 6.3 dates from 2016. C++17 features used in M4 (string_view, structured bindings) revealed compatibility issues — `string_view` was removed in test code in favor of `const char*` + length.
- `%zu` format-string warnings in `pst_info.cpp` for `size_t` printf calls.
- The §2 "ASCII-only test names" discipline holds throughout M4; no § characters leaked into Catch2 tags.

### M4 retrospective (one paragraph)

M4 took the spec-sample-byte-diff discipline that caught wSig and the HNPAGEMAP DWORD-alignment rule and added a second oracle category — **cross-validation against §3.9** — to test that the reader works on bytes the writer never produced. Synthetic round-trip alone could not have caught the HID-agnostic-reader contract because the writer's HIDs were always tidy; only feeding the reader real-Outlook bytes (via §3.9, which has non-monotonic HIDs) made the contract observable. "Design before code" caught two documentation defects (TCOLDESC sort-key wrong, three citation markers wrong) before they hardened into implementation bugs. The dominant residual risk is concentrated in seven `KNOWN_UNVERIFIED.md` entries that all gate on the same condition — getting one real Outlook PST into `tests/golden/`. That concentration is itself a finding: the seven entries are not seven independent gambles but one downstream gate item, and M5's real-Outlook gate is therefore unusually high-leverage.

---

## M5 — pre-flight notes (do not start implementation)

### M5 scope (working draft)

- NBT navigation: writer assigns NIDs deterministically; reader resolves via NBT walk (no caller knowledge of NID layout).
- NID registration: every block produced by M3/M4 builders is reachable from a parent NID in the NBT.
- Node-graph wiring: PC + TC nodes have NBT entries; subnode trees (SLBLOCK/SIBLOCK from M3) are wired into parent NBT entries via `bidSub`.
- NBT intermediate-page pagination (parallel to M3's BBT root work).
- Mandatory NIDs from §2.4.8 — minimum set required for Outlook to open the PST.

M5 is the first milestone where Outlook should be able to attempt opening produced PSTs without "orphan blocks" warnings — making real-Outlook validation viable for the first time.

### M5 oracle inventory

Three categories: **transcribable** (hex dump → byte-diff oracle), **decode reference** (semantic guidance, no new bytes to transcribe), and **architectural** (diagram only).

| Section | Title | URL | Category | Notes |
|---|---|---|---|---|
| **§3.1** | Sample Node Database (NDB) | `learn.microsoft.com/.../ms-pst/903241a7-8e4d-4ff6-93e4-78a3d74bd8dc` | Architectural diagram | Image-only (Figure 20). Shows 2-level NBT topology, top-level node with bidData + bidSub, subnode SIBLOCK→SLBLOCK fanout, second top-level node with XBLOCK data tree. Useful for design verification, not byte-diff. |
| **§3.3** | Sample Intermediate BT Page | `learn.microsoft.com/.../ms-pst/ef7837e0-22be-4da5-9c5f-1c79db6532f4` | **Transcribable** (512 B) | `cLevel=1, cEnt=3, cbEnt=0x18, cEntMax=0x14`. Format is shared between intermediate NBT and BBT pages, so this single sample exercises both BT-intermediate writers. PAGETRAILER bytes published; CRC self-consistency check applies (parallel to §3.5 / §3.7). **Transcribe to `tests/golden/spec_sample_bt_intermediate.{hex,bin}` in M5 pre-flight Part 2.** |
| §3.10 | Sample Message Store | `learn.microsoft.com/.../ms-pst/8fa17657-df23-466d-b09b-29742a745246` | Decode reference | Per spec text: *"the binary data used in the last two examples (HN, BTH) is actually that of the message store PC of a PST"*. The §3.8 HN bytes ARE the message store PC bytes — already transcribed. §3.10 publishes the decoded property list (NID 0x21 = NID_MESSAGE_STORE, 9 properties: PidTagReplVersionhistory, PidTagReplFlags, PidTagRecordKey, PidTagDisplayName="UNICODE1", PidTagValidFolderMask=0x89, PidTagIpmSubTreeEntryId, PidTagIpmWastebasketEntryId, PidTagFinderEntryId, PidTagPstPassword=0). Use this list to verify M5's reader against the §3.8 bytes — same byte oracle, deeper semantic check. |
| §3.12 | Sample Folder Object | `learn.microsoft.com/.../ms-pst/59235d49-bd76-4759-b26c-9769e97c4106` | Decode reference (per earlier inspection in M4 design phase) | Decoded content of Root Folder PC + hierarchy/contents/FAI tables. Names 3 sub-folders ("Top of Personal Folders", "Search Root", "SPAM Search Folder 2") that match §3.11 TC's row strings — confirming §3.11's TC IS the Root Folder's hierarchy table. Same pattern as §3.10: decode reference for previously-transcribed bytes. |
| §3.13 | Sample Message Object | `learn.microsoft.com/.../ms-pst/5ee9a00a-858b-47db-95b3-f91518640ea7` | Decode reference (M6) | Out of scope for M5; mark for M6. |

**Single new transcription required for M5**: §3.3 Sample Intermediate BT Page (512 B). The §3.10 / §3.12 decode references unlock semantic verification of bytes already on disk.

### M5 design choices to specify before implementation (Phase A of M5)

To be authored as a "M5 Part 2 design doc" parallel to M4's, before any M5 code lands:

- NID allocation strategy (deterministic, by node type, with what stride?)
- NBT page-ordering rules
- Node-graph wiring contract: which builders own NID assignment, which take NIDs as inputs (parallel to M4's "subnode NID is caller-supplied" rule)
- Reader contract: NBT-walker is order-agnostic and doesn't assume our deterministic layout — same discipline as the HID-agnostic PC reader
- Each design rule marked `[SPEC §X.Y]` (verified against actual section text, never fabricated) or `[DESIGN]` (with rationale)

### M5 oracle transcribed (2026-05-02, M5 pre-flight)

| Sample | File | Size | CRC self-consistency | Notes |
|---|---|---|---|---|
| §3.3 BT intermediate | `tests/golden/spec_sample_bt_intermediate.bin` | 512 B | **Positive control: full byte-for-byte match including stored CRC.** `crc32(bytes[0..496))` = `0x02E8B164` = stored dwCRC. | This sample is specifically an intermediate **NBT** page (`ptype=0x81`); the format is shared with intermediate BBT per the spec text. cLevel=1, cEnt=3, cbEnt=0x18, cEntMax=0x14. bid=0x206, wSig=0x8006. Joins §3.2 / §3.4 / §3.6 as the fourth positive-control oracle in the suite. |

The §3.3 outcome means the spec-sample CRC anomaly count remains **2-of-5**: §3.5 (BBT leaf) and §3.7 (SLBLOCK) are the only two anomalies among samples that have a stored dwCRC to compare against. §3.2 (header), §3.3 (BT intermediate), §3.4 (NBT leaf), §3.6 (XBLOCK) are all clean positive controls. The §3.6 XBLOCK clean-control + §3.3 clean-control evidence further reinforces that our CRC code is correct and the §3.5/§3.7 stored values are doc artifacts, not algorithm divergence.

---

## M5 — NBT navigation and node graph: design

This doc pins design choices BEFORE implementation so the test
specification is stable. Same discipline as M4 Part 2.

References (verified against actual section text via WebFetch
2026-05-02 — citations elsewhere in this doc were wrong three
times during M4 design; treat all `[SPEC §X.Y]` markers as
re-verifiable, not authoritative until checked):

- [MS-PST] §1.3.1.1 *Encoding Algorithm* (cyclic vs permutative — for context, not directly used here)
- [MS-PST] §2.2.2.7 *Pages* — page formats (BT page, AMap page, etc.)
- [MS-PST] §2.2.2.7.7 *BTPAGE* — common BT page format for both NBT and BBT
- [MS-PST] §2.2.2.7.7.1 *BTPAGE structure* footer (cEnt / cEntMax / cbEnt / cLevel)
- [MS-PST] §2.2.2.7.7.2 *BTENTRY* — intermediate-level entries (24 B: btkey 8 + BREF 16)
- [MS-PST] §2.2.2.7.7.3 *NBTENTRY* — leaf NBT entries (32 B: nid 4 + pad 4 + bidData 8 + bidSub 8 + nidParent 4 + dwPad 4)
- [MS-PST] §2.4.1 *Special internal NIDs* — fixed NIDs that every PST must contain (NID_MESSAGE_STORE = 0x21, NID_NAME_TO_ID_MAP = 0x61, NID_ROOT_FOLDER = 0x122, ...)
- [MS-PST] §2.4.8 *Mandatory nodes* (referenced in M5 description; verify section number when implementation phase begins)

### Ground rules (apply throughout M5)

1. **NBT and BBT share the BTPAGE format**, so the §3.3 oracle byte-diffs both writers. The two diverge only in (a) `ptype` (0x81 vs 0x80) and (b) leaf-record layout. M5 reuses one BTPAGE writer for both.
2. **Page-level pagination** parallels M3's BBT root work: when leaf entries don't fit in one page, split to an intermediate level. Single-page case is the M5 starting point; intermediate-level support is the M5 stretch goal validated by §3.3.
3. **NIDs are write-once in M5.** No NID re-use, no NBT entry deletion, no in-place node edit. (Same simplification as M4's "build-from-scratch only" PC writer — those features arrive in M7+.)

### NID allocation strategy

| Marker | Decision | Rationale |
|---|---|---|
| **[SPEC §2.2.2.1]** | NID is a 32-bit value: `nidType` in low 5 bits, `nidIndex` in high 27 bits. Equality compares the full 32 bits; nidType + nidIndex must both match. **Encoding**: `NID = (nidIndex << 5) \| (nidType & 0x1F)`. | Spec-pinned; not a design choice. Verified against the spec page 2026-05-02. |
| **[SPEC §2.4.1]** | Reserved NIDs (verbatim from spec — verified via WebFetch on 2026-05-02): see "Reserved NIDs" table below. The allocator pre-registers all 14 of these as taken. | Hard contract; reproduce exactly. |
| **[DESIGN]** | Non-reserved NIDs allocated by **per-nidType monotonic counter starting at nidIndex=1**, advancing by 1 per allocation (= NID += 0x20 with nidType held constant). Each nidType has its own counter. Counter SKIPs any pre-registered NID values rather than colliding. | Smallest legal stride; deterministic; collision-free by construction. Same arithmetic as the M4 subnode-NID rule. |
| **[DESIGN]** | Subnode NIDs allocated within a parent node use the M4 stride (`+= 0x20`, see KNOWN_UNVERIFIED.md "subnode-NID allocation stride"). The M5 allocator service is also the source of subnode NIDs when the caller doesn't supply its own — preserves global uniqueness. | Continuity with M4. |
| **[SPEC §2.4.1] + [DESIGN]** | The NID space is global per PST file: every NID (reserved + monotonic-allocated + subnode) MUST be unique across the file. M5's allocator owns uniqueness; the PC/TC builders trust the caller-supplied NIDs are unique. | Hard invariant. The allocator is the single point of truth. |
| **[DESIGN]** | Duplicate-NID handling: when caller pre-registers a NID and the per-nidType counter would later produce that same NID, the counter **skips past** rather than erroring. Erroring would force callers to pre-compute counter advances. | Caller convenience; no correctness cost. |
| **[DESIGN]** | Reserved-NID handling: explicit caller request for a reserved NID is **rejected** (throws `std::runtime_error`). Reserved NIDs MUST be allocated only via the dedicated `reservedNidFor(...)` lookup, which returns the spec-mandated value. | Reserved NIDs have spec-defined semantics; an unintended overwrite silently produces a malformed PST. |

#### Reserved NIDs ([SPEC §2.4.1], verbatim — verified 2026-05-02)

| NID | nidType | nidIndex | Friendly name | Meaning |
|---|---|---|---|---|
| `0x21`  | 0x01 INTERNAL       | 0x01 | NID_MESSAGE_STORE              | Message store node ([SPEC §2.4.3]). Confirmed by §3.10. |
| `0x61`  | 0x01 INTERNAL       | 0x03 | NID_NAME_TO_ID_MAP             | Named Properties Map ([SPEC §2.4.7]). |
| `0xA1`  | 0x01 INTERNAL       | 0x05 | NID_NORMAL_FOLDER_TEMPLATE     | Special template node for an empty Folder object. |
| `0xC1`  | 0x01 INTERNAL       | 0x06 | NID_SEARCH_FOLDER_TEMPLATE     | Special template node for an empty search Folder object. |
| `0x122` | 0x02 NORMAL_FOLDER  | 0x09 | NID_ROOT_FOLDER                | Root Mailbox Folder object of PST. |
| `0x1E1` | 0x01 INTERNAL       | 0x0F | NID_SEARCH_MANAGEMENT_QUEUE    | Queue of Pending Search-related updates. |
| `0x201` | 0x01 INTERNAL       | 0x10 | NID_SEARCH_ACTIVITY_LIST       | Folder object NIDs with active Search activity. |
| `0x241` | 0x01 INTERNAL       | 0x12 | NID_RESERVED1                  | Reserved. |
| `0x261` | 0x01 INTERNAL       | 0x13 | NID_SEARCH_DOMAIN_OBJECT       | Global list of search-criteria-referenced folders. |
| `0x281` | 0x01 INTERNAL       | 0x14 | NID_SEARCH_GATHERER_QUEUE      | Search Gatherer Queue ([SPEC §2.4.8.5.1]). |
| `0x2A1` | 0x01 INTERNAL       | 0x15 | NID_SEARCH_GATHERER_DESCRIPTOR | Search Gatherer Descriptor ([SPEC §2.4.8.5.2]). |
| `0x2E1` | 0x01 INTERNAL       | 0x17 | NID_RESERVED2                  | Reserved. |
| `0x301` | 0x01 INTERNAL       | 0x18 | NID_RESERVED3                  | Reserved. |
| `0x321` | 0x01 INTERNAL       | 0x19 | NID_SEARCH_GATHERER_FOLDER_QUEUE | Search Gatherer Folder Queue ([SPEC §2.4.8.5.3]). |

**Phase A finding (spec-text quirk)**: §2.4.1's title is "Special Internal NIDs" and the first paragraph says "this section focuses on a special NID_TYPE: NID_TYPE_INTERNAL (0x01)". But **NID_ROOT_FOLDER = 0x122 has nidType = 0x02 = NID_TYPE_NORMAL_FOLDER**, not INTERNAL. The "Internal" in the section title means "internal to the implementation" (i.e. spec-mandated), NOT literally `nidType == NID_TYPE_INTERNAL`. NID_ROOT_FOLDER is the only exception in the table.

#### nidType values ([SPEC §2.2.2.1], verbatim — verified 2026-05-02)

| nidType | Friendly name |
|---|---|
| 0x00 | NID_TYPE_HID |
| 0x01 | NID_TYPE_INTERNAL |
| 0x02 | NID_TYPE_NORMAL_FOLDER |
| 0x03 | NID_TYPE_SEARCH_FOLDER |
| 0x04 | NID_TYPE_NORMAL_MESSAGE |
| 0x05 | NID_TYPE_ATTACHMENT |
| 0x06 | NID_TYPE_SEARCH_UPDATE_QUEUE |
| 0x07 | NID_TYPE_SEARCH_CRITERIA_OBJECT |
| 0x08 | NID_TYPE_ASSOC_MESSAGE |
| 0x0A | NID_TYPE_CONTENTS_TABLE_INDEX |
| 0x0B | NID_TYPE_RECEIVE_FOLDER_TABLE |
| 0x0C | NID_TYPE_OUTGOING_QUEUE_TABLE |
| 0x0D | NID_TYPE_HIERARCHY_TABLE |
| 0x0E | NID_TYPE_CONTENTS_TABLE |
| 0x0F | NID_TYPE_ASSOC_CONTENTS_TABLE |
| 0x10 | NID_TYPE_SEARCH_CONTENTS_TABLE |
| 0x11 | NID_TYPE_ATTACHMENT_TABLE |
| 0x12 | NID_TYPE_RECIPIENT_TABLE |
| 0x13 | NID_TYPE_SEARCH_TABLE_INDEX |
| 0x1F | NID_TYPE_LTP |

Gaps at 0x09 and 0x14..0x1E are unassigned by the spec. The allocator validates that any caller-supplied nidType is one of the 19 defined values above.

### NBT page layout and ordering rules

| Marker | Decision | Rationale |
|---|---|---|
| **[SPEC §2.2.2.7.7]** | NBTENTRY records are sorted by NID ascending, treated as unsigned 32-bit. Same rule applies inside intermediate BTENTRY records (sorted by btkey, which IS the NID for NBT). | Spec-pinned BTPAGE invariant. |
| **[SPEC §2.2.2.7.7.1]** | BTPAGE footer: `cEnt` records, `cEntMax` capacity, `cbEnt` per-record size (0x20 for NBTENTRY leaf, 0x18 for BTENTRY intermediate), `cLevel` (0=leaf, ≥1=intermediate). dwPadding (4 B) follows. | Spec-pinned. |
| **[SPEC §2.2.2.7.1]** | PAGETRAILER (16 B) at end: ptype, ptypeRepeat, wSig, dwCRC, bid. dwCRC scope is `crc32(bytes[0..496))` (verified by §3.2/§3.3/§3.4/§3.6). | Spec-pinned. M3 already implements; reuse. |
| **[DESIGN]** | Leaf-page split when entries > cEntMax. Initial cEntMax for NBT leaf: 0x0F (= 15, matches §3.4). For BBT leaf: spec varies by sample — choose smallest legal value that holds expected entry count. Intermediate page cEntMax: 0x14 (= 20, matches §3.3). | Empirical from §3.4 / §3.3 oracles. Single-sample so each is a KNOWN_UNVERIFIED candidate (see Open Questions below). |
| **[DESIGN]** | When a leaf splits, intermediate level created with `cLevel=1`. M5 supports cLevel ≤ 1 only; multi-level (cLevel ≥ 2) deferred to M7+ when message volumes justify it. Single-page case is also legal — `cLevel=0` and `cEnt ≤ cEntMax`. | Match expected M5 PST sizes (1 PC + 1 TC = 2 NBT entries + reserved NIDs ≈ a dozen entries, fits one page). |

### Node-graph wiring contract

| Marker | Decision | Rationale |
|---|---|---|
| **[DESIGN]** | NID assignment is owned by `M5Allocator` (or equivalent — name TBD). PC and TC builders take NIDs as inputs (not allocate them) — same caller contract as M4's PC subnode NIDs. | Single point of truth for NID uniqueness; testable in isolation. |
| **[SPEC §2.2.2.7.7.3]** | NBTENTRY layout: `nid` (4 B) + 4 B pad + `bidData` (8 B) + `bidSub` (8 B) + `nidParent` (4 B) + 4 B pad = 32 B. `bidSub=0` indicates no subnode tree. | Spec-pinned. Verified against §3.4 oracle. |
| **[DESIGN]** | M4's PC `PcResult.subnodeNids` list is consumed by the wiring layer: each subnode NID gets its own NBTENTRY pointing to its block, AND the parent PC node's NBTENTRY's `bidSub` gets a SLBLOCK BID (not the subnode block BID directly) when ≥ 1 subnode exists. (When only 1 subnode, `bidSub` may point directly to the data block per §2.2.2.8.3.1 — verify behavior in §3.1 NDB diagram and during implementation. Mark this as KNOWN_UNVERIFIED until confirmed.) | Implements the bidSub chain promised by M4. |
| **[DESIGN]** | M4 `buildTableContext` does not currently promote oversized RowMatrix to a subnode. M5 wiring layer DOES allow this promotion when needed; until then, TCs are limited to single-block RowMatrix (≤ 3580 / TCI_bm rows). | Defer scaling work until a TC actually exceeds the cap. |

### Reader contract (parallel to M4's HID-agnostic PC reader)

| Marker | Decision | Rationale |
|---|---|---|
| **[INVARIANT]** | NBT walker is **NID-order-agnostic**: walks NBT pages without assuming any particular allocation stride or contiguity. Real Outlook PSTs are write-once-with-edits, so their NID layouts are arbitrary. | The §3.9 lesson applied to NBT. A reader that assumed M5's tidy `+= 0x20` stride would silently fail on real PSTs. |
| **[INVARIANT]** | Intermediate BT-page walker treats `cLevel` as authoritative and recurses regardless of cEntMax / cbEnt actual values, as long as cbEnt agrees with the level's record-type expectation (NBTENTRY 0x20 at cLevel=0, BTENTRY 0x18 at cLevel ≥ 1). | Same defensive-write / permissive-read split as M4. |
| **[INVARIANT]** | NID lookups: reader resolves a NID by binary-search descending the BT, NEVER by computing a deterministic offset from the NID value. | Hard contract; same as PC HID resolution via HNPAGEMAP. |
| **[ENFORCED]** | Reader throws `std::runtime_error` on: ptype/ptypeRepeat mismatch, dwCRC mismatch (when reader is in strict mode), BTPAGE.cbEnt not in {0x18, 0x20}, BTENTRY.btkey not strictly ascending, NBTENTRY.nidParent referring to a NID that doesn't exist. | Catches structural corruption early. |
| **[NOT ENFORCED]** | NID stride patterns, NBTENTRY alignment within a page, ordering of intermediate/leaf pages on disk, specific BID values for NBT pages, presence of dwPadding equal to 0 (it CAN be non-zero per §2.2.2.7.7.1's "unused space can contain any value"). | Real-Outlook compatibility; same discipline as PC reader. |

### Real-Outlook validation plan (NEW — converts deferred gate into a runnable procedure)

Seven KNOWN_UNVERIFIED entries gate on landing one real Outlook-produced PST. M5 is the milestone where Outlook should accept produced files; the real-Outlook validation gate is therefore part of M5's definition of done. This section specifies HOW that validation runs when a sample is in hand.

#### Sample acquisition (target shapes)

| Priority | Sample shape | Why it matters |
|---|---|---|
| **Required** | One M4-LTP-bearing PST: at minimum a PC + a TC. Any Outlook-produced .pst opened, examined briefly, then saved-as. | Exercises HNPAGEMAP alignment, row-major TC ordering, real PC HID layouts, real BBT-leaf and SLBLOCK CRCs. |
| **Ideal** | A PST with a multi-subnode PC (multiple oversized properties on one node). | Only sample shape that exercises the subnode-NID stride entry. Could be constructed by attaching ≥ 2 large binaries to one message. |
| **Ideal** | A PST containing an empty PC (a folder/message with no custom properties beyond the spec's mandatory ones, ideally with the user-editable subset cleared). | Only sample shape that exercises the empty-PC `hidRoot==0` sentinel entry. |
| **Ideal** | A PST containing PtypBoolean property values (e.g. `PR_HASATTACH`, `PR_RTF_IN_SYNC`, message-flag derivatives). | Confirms zero-extension layout vs. 4-byte BOOL. Easy to produce — most non-empty messages carry these. |

#### Extraction targets per sample

For each .pst in `tests/golden/real_outlook_*.pst`:

1. **BBT leaf pages** — extract one. Compute `crc32(page[0..496))` against the stored dwCRC. Outcome decides §3.5 anomaly status.
2. **SLBLOCK** — extract one. Compute the same. Decides §3.7 anomaly status.
3. **HN data block with HNPAGEMAP** — extract. Verify `ibHnpm` is at a 4-byte boundary. If not, our DWORD-alignment rule is wrong; if yes, it's confirmed for real-Outlook output.
4. **TC with variable-size column values across multiple rows** — extract HID assignment order. Decides row-major vs column-major for §3.11.
5. **Boolean property byte layout** — find one, examine the 4-byte slot. Decides PtypBoolean encoding.
6. **Multi-subnode PC** (if available) — extract NID stride between consecutive subnodes. Decides `+= 0x20` vs alternative.
7. **Empty PC** (if available) — extract BTHHEADER.hidRoot encoding. Decides sentinel-0 interpretation.
8. **Real PC HID layout** — extract the HID assignment for an arbitrary PC. Compare to our writer's tidy 0x20/0x40/0x60+ pattern. Difference is expected; this is the test for the HID-agnostic-reader contract under non-synthetic conditions.

#### Validation order

1. **CRC-first** — compute and compare dwCRC values across BBT leaf + SLBLOCK + page samples. Lowest-effort check; immediate yes/no on the §3.5/§3.7 anomaly hypothesis.
2. **Structural rules next** — HNPAGEMAP DWORD-alignment, row-major TC, Boolean inline encoding. Each is a yes/no comparison against the rule we wrote into the M4 writer.
3. **Semantic rules last** — does our reader extract the same property values that pst_info / libpff / mfcmapi extract? This is the highest-effort check and the strongest evidence of compatibility.

#### Result recording

Each KNOWN_UNVERIFIED.md entry whose gate is met flips to one of:

- **Verified** — entry can be removed (or kept as a "verified at <date> against <sample>" historical record).
- **Tolerated** — Outlook varies; our writer's choice is one of several Outlook-acceptable patterns; reader handles all.
- **Disagree** — Outlook does it differently from our writer. Triage: change the writer, or document a tolerated divergence.

New findings (e.g. a structural pattern not previously catalogued) get fresh KNOWN_UNVERIFIED entries.

#### Acquisition path for future contributors

If no Windows + Outlook machine is available locally:

- **Outlook desktop** is the primary creator. Outlook 2016+ on Windows produces Unicode PSTs (wVer=23) by default. Free trials of Microsoft 365 include Outlook.
- **Outlook for Mac** does NOT write PSTs natively (uses .olm). Won't produce a useful sample.
- **Third-party tools**: Some MAPI tools (mfcmapi, OutlookSpy) can emit PSTs via Outlook's MAPI. They're equivalent to Outlook output for compatibility purposes.
- **Public PST corpora**: TREC enron archive contains real PSTs but predates Unicode-PST format (mostly ANSI-PST = wVer=14, a different format). Avoid for M5 validation.

### Open questions (not yet KNOWN_UNVERIFIED entries — pre-implementation TODOs)

These are questions to resolve BEFORE M5 Phase A code lands. None blocks pre-flight; all need answers before generative code.

1. **NBT leaf cEntMax**: §3.4 sample uses cEntMax=0x0F. Single sample; could be Outlook's choice for a specific PST shape, not a fixed default. Pre-Phase-A: read §2.2.2.7.7 carefully for any prescribed value. If still ambiguous, log as KNOWN_UNVERIFIED at code time.
2. **BBT leaf cEntMax**: cf §3.5 sample cEntMax. We have not yet examined this in M3 (the M3 BBT writer may have used a different value). Pre-Phase-A: audit M3 code for the chosen cEntMax and align with §3.5.
3. **`bidSub` direct-data-block vs SLBLOCK**: when a node has exactly one subnode, does `bidSub` point to the subnode's data block directly, or wrap it in a 1-entry SLBLOCK? Pre-Phase-A: re-read §2.2.2.8.3.1 and §3.1 NDB diagram.
4. **Mandatory NID enumeration**: §2.4.1 lists special internal NIDs; §2.4.8 (per the M5 description in user prompt) lists mandatory nodes. These may overlap. Pre-Phase-A: produce a definitive list and pin it as a `[SPEC §x.y]` table in this doc.
5. **§3.3 sample is intermediate NBT specifically** (`ptype=0x81`). Format is shared with intermediate BBT, but our `[golden_spec_bt_intermediate]` test should byte-diff against the NBT shape only — and the BBT-shape test would need a separate sample (which the spec does not publish). Pre-Phase-A: decide whether to (a) skip explicit BBT-intermediate byte-diff (rely on shared-code coverage) or (b) construct a synthetic BBT-intermediate test.

### Implementation phasing (subsequent — Phase A authorization is a separate prompt)

Suggested split, not yet authorized:
1. **Phase A** — `M5Allocator` + NID/BID counters, deterministic. Unit-tested with NID/nidType invariants.
2. **Phase B** — BTPAGE writer (parametric over cbEnt, cLevel, ptype). Byte-diff oracle: §3.4 NBT leaf already passes via M3 path; verify the unified writer matches; add §3.3 intermediate as new oracle.
3. **Phase C** — NBT walker (reader). Cross-validate against §3.3 + §3.4 (real-Outlook bytes); enforce reader invariants enumerated above.
4. **Phase D** — End-to-end PST: PC + TC + NBT entries + BBT + AMap. `pst_info` reports zero orphan blocks.
5. **Phase E** — Semantic decode tests + real-Outlook PST acquisition + KNOWN_UNVERIFIED audit + (if PST acquired) the seven-entry validation pass.

Each phase stops for review.

---

## M5 toolchain-debt action plan (decided 2026-05-02 in M5 pre-flight)

Each item carries a status: **fixed-now** (resolved during pre-flight) / **deferred-with-rationale** (explicitly chosen to defer, rationale captured) / **scheduled-for-phase-X** (will be addressed mid-M5).

### MSVC `/W4 /WX` clean build — **deferred-with-rationale to M5 Phase D**

- Status: never validated since M3 flagged it.
- Why now is wrong: pre-flight is not the moment to run an MSVC build because the toolchain switch creates noise that masks M5 implementation issues. Cleaner to fix M5 layer-by-layer first under MinGW (where cycle time is fastest), then in Phase D run MSVC once over the whole M5 surface and triage as one batch. Phase D is also where the end-to-end PST is built — natural integration moment.
- Why deferral is acceptable: the user prompt's note "M5 introduces NBT pointer arithmetic — the layer most likely to surface signed/unsigned and narrowing issues that GCC 6.3 misses but MSVC catches" is correct, BUT the alternative — running MSVC against an unfinished M5 — would surface the same issues mixed with implementation churn. Concentrating MSVC work in Phase D batches the remediation.
- Concrete plan in Phase D: dual-build CI step that emits MSVC and MinGW binaries from the same source. Currently we build only MinGW.

### MinGW upgrade (g++ 6.3 → modern) — **deferred-with-rationale to M7 (Hardening)**

- Status: g++ 6.3 (2016) is the local toolchain. M4 surfaced one direct issue (`std::string_view` removed from test code).
- Why defer: upgrading mid-milestone risks introducing build-system noise that's indistinguishable from logic errors. The M4 string_view workaround was localized and didn't compromise correctness. M7 is the natural toolchain-modernization milestone (`/W4 /WX`, fuzz testing, release artifacts).
- Why deferral is acceptable: the M5 layer (NBT pointer arithmetic) does not require C++20 features. C++17 is enough.
- Concrete plan in M7: bump to g++ 13.x (Windows: MSYS2 UCRT64 distribution) AND establish that as the project's minimum supported MinGW. Document in CMakeLists.txt.

### `%zu` and 64-bit format warnings in `pst_info.cpp` — **fixed-now**

- Status: resolved. Initial fix (cast `size_t` to `unsigned long`, use `%lu`) revealed a downstream issue: the test target compiles `pst_info.cpp` under `-Wall -Wextra -Wpedantic` (the binary target does NOT), and `-Wpedantic` rejects MinGW-specific 64-bit format specifiers (`%llX`, and PRIx64 which expands to `I64X` on Windows MinGW).
- Final fix: split each 64-bit value into two 32-bit halves and print as `0x%08X%08X`. Portable, pedantic-clean, equivalent output. Applied at lines ~550, ~570, ~592 in `tools/pst_info.cpp`.
- Result: both `pst_info.exe` and `pstwriter_tests.exe` build with zero format warnings.

### ctest `§` regex audit — **fixed-now (verified clean)**

- Status: verified by inspection of `tests/test_*.cpp` — no Catch2 tag contains `§`. The M4 discipline of ASCII-only test names was followed throughout. M5 will inherit the same discipline.
- Rationale: a 1-grep verification, done now.

### Discipline note for M5

Per-phase commits resume in M5 (M4 single-commit fold was a closure exception, not a permanent pattern). Bisect-friendly history starts here.

### Phase D mandatory-nodes deferral to M6 (decided 2026-05-02)

**[SPEC sec 2.7.1] enumerates 27 mandatory nodes** that "MUST be present in a PST" — every PC and TC needed for a minimum-conforming PST: message store + root folder + IPM subtree + deleted items + search-related folders + various hierarchy/contents/FAI tables + recipient/attachment template tables + name-to-id map.

**Phase D scope is the M5 PLUMBING demonstration**, not the full §2.7.1 set:
- Phase D builds 1 PC + 1 TC (M4 fixtures) wired via NBT/BBT.
- Phase D's "no orphan blocks" gate means every block produced by the writer has an NBT entry; pst_info reports `BBT walked: 2 entries, 2 block CRCs verified, 0 mismatches` and the NBT walk yields exactly the expected 2 NIDs.
- The full 27-node mandatory set is **deferred to M6 (Messaging Core)** because each entry requires:
  - For PCs: full messaging-layer schema (PR_DISPLAY_NAME, PR_RECORD_KEY, PR_PARENT_FID, etc.) per [MS-OXPROPS] / [MS-OXCMSG].
  - For TCs: TCOLDESC arrays matching folder-template-table / hierarchy-table / contents-table / etc. spec tables.
  - Cross-node references: PR_IPM_SUBTREE_ENTRYID inside the message store PC must point to NID 0x8022; root folder's hierarchy table must list IPM subtree as a sub-folder; etc.
- Outlook will reject a PST missing these (per spec). M5 Phase E's real-Outlook gate is therefore **not** met by Phase D alone; it requires M6 to land.

**What this means for the M5 milestone status**:
- Phase D unlocks `[m5][end_to_end][m5_gate]` (pst_info ALL CHECKS PASSED on a non-orphan PST).
- M5's headline "REAL-OUTLOOK GATE" effectively rolls forward to M6 (= the milestone where Outlook can attempt to open produced files) — same logical gate, different milestone label after the deferral.
- The seven KNOWN_UNVERIFIED entries gated on a real-Outlook PST stay open through M6.

This deferral is intentional and documented to keep Phase D's scope tight. Phase D demonstrates that the M5 NID/BID/NBT/BBT plumbing is correct given M4-level node bytes; M6 demonstrates that the messaging-layer schema can be assembled on top.

---

## M5 closure — findings, retrospective, and M6 pre-flight notes

### M5 gate-met evidence (final state, 2026-05-02)

| Gate item | Test name(s) | What the oracle proved |
|---|---|---|
| §3.3 BT intermediate byte-diff | `[golden_spec_bt_intermediate]` | **Positive control: full byte-for-byte match including stored CRC.** Establishes that `buildBtIntermediate` reproduces real-format BTPAGE bytes when ptype=NBT. CRC = `0x02E8B164` = `crc32(bytes[0..496))`. |
| BTPAGE writer sorts entries | `[m5][reversed_input]` × 2 | Writer sorts internally for both BTENTRY (by btkey) and NBTENTRY (by NID). Catches the no-op-pass-through failure mode that pre-sorted spec inputs would mask — same lesson as the wSig-bug regression. |
| NBT/BBT shared format | `[m5][shared_format]` | Intermediate NBT (ptype=0x81) and BBT (ptype=0x80) bytes differ at exactly 2 positions (0x1F0/0x1F1). dwCRC byte-identical (proves CRC scope ends at 0x1F0). One writer covers both, parameterized by ptype. |
| NBT pagination | `[m5][nbt][pagination]` × 4 | Single-leaf (≤15 entries) + multi-leaf (16-300 entries with intermediate). Reader descends through intermediate to resolve cross-leaf NIDs. Tree-depth-2 throws (deferred to M7+). |
| NID assignment determinism | `[m5][allocator]` × 14 cases / 1236 assertions | All 14 reserved §2.4.1 NIDs pre-populated; per-nidType counter starting at idx=1 with skip-past-reserved; cross-nidType independence; reservedNidFor() returns spec values. |
| **NID-order-agnostic reader** | `[m5][nbt_reader][non_monotonic_nids]` | **LOAD-BEARING.** Reader resolves NIDs `{0x21, 0x100, 0x40, 0x800, 0x122, 0x60, 0x205}` in a leaf — values that M5Allocator would never produce in this order. Equivalent of M4 §3.9 cross-validation for the PC HID-agnostic reader. |
| End-to-end PST | `[m5][end_to_end][m5_gate]` | M5Allocator → M4 PC + TC → buildDataBlock → writeM5Pst → pst_info ALL CHECKS PASSED. NBT walks via nbtForEach yields exactly the expected 2 NIDs (0x22 NormalFolder + 0x2D HierarchyTable). |
| §3.10 PC semantic decode | `[semantic_decode_3_10]` | All 9 §3.10-named properties found in §3.8 HN bytes with matching types. Inline values match (`PidTagValidFolderMask` = 0x89, `PidTagPstPassword` = 0). EntryID values' last-4-bytes = expected NIDs (`PidTagIpmSubTreeEntryId` → 0x8022, etc.). DisplayName UTF-16-LE = "UNICODE1". RecordKey GUID matches. |
| §3.12 TC semantic decode | `[semantic_decode_3_12]` | All 3 folder name strings present in §3.11 TC bytes ("Top of Personal Folders", "Search Root", "SPAM Search Folder 2"). Three RowIndex BTH RowIDs (0x2223, 0x8022, 0x8042) found. |
| Spec invariant rejection | `[m5][nbt_reader][negative]` × 7 | Reader throws on bad ptype, ptype/ptypeRepeat mismatch, cEnt > cEntMax, wrong cbEnt for cLevel, cLevel > 1 (M5 cap), CRC corruption (strict mode), IB past EOF. |

#### Findings during M5

- **§3.3 positive-control discovery (M5 pre-flight)**: §3.3's stored dwCRC matches `crc32(bytes[0..496))` exactly. Joins §3.2/§3.4/§3.6 as the fourth positive-control oracle. Anomaly count stays 2-of-5 (§3.5/§3.7 still hand-edited).
- **cEntMax is derivable, not empirical (Phase B)**: cEntMax = `floor(488 / cbEnt)` always — both §3.4 (=15) and §3.3 (=20) match this formula exactly. No KNOWN_UNVERIFIED entry needed.
- **§3.10 enumerates 9 properties; §3.9 BTH actually has 11 (Phase E)**: PidTags 0x6633 (PtypBoolean=1) and 0x66FA (PtypInteger32=0x000E000D) are present in real Outlook bytes but not surfaced in §3.10's prose. Documented in test comments. Our reader correctly finds all 11.
- **§2.7.1 deferral (Phase D)**: spec lists 27 mandatory nodes; Phase D builds 2 (M4 PC + TC fixtures). Full set requires messaging-layer schema work and is deferred to M6. Real-Outlook gate moved to M6 alongside.
- **§2.4.1 "Internal" misnomer (Phase A)**: section title says "Special Internal NIDs" but `NID_ROOT_FOLDER` (0x122) has nidType=NormalFolder, not Internal. Documented; allocator handles correctly.
- **ROOT.brefNbt offset bug caught by reader (Phase D)**: first attempt used 0xCC, the correct offset is 0xD8 (= 0xB4 + 36). The reader's bounds check (`page IB extends past EOF`) caught it cleanly in 1 iteration — vindicates the defensive enforcement-of-spec-invariants discipline.

#### Documentation findings during M5

- **§2.2.2.7.7.1 cbEnt table typo**: lists `cLevel = "Less than 0"` for BTENTRY; cLevel is unsigned 1-byte so this is impossible. Treated as typo (should be "Greater than 0").
- **§2.7.1 typos**: row 9 says `NID_TYPE_HIERRCHY_TABLE` (missing 'A'); 4 rows use "IPM SuBTree" (mid-word capital T). Cosmetic.
- **§3.10 omissions**: 2 PidTags (0x6633, 0x66FA) present in §3.9 BTH but not in §3.10's prose dump. Either §3.10 is incomplete or these are implementation-specific.
- **Spec citation accuracy**: 4 prior incorrect drafts before M5; all M5 citations were re-verified via WebFetch on 2026-05-02. The pattern of treating spec-marker drafts as freshly re-verifiable rather than authoritative caught the §2.4.8 vs §2.7.1 distinction.

### M5 retrospective (one paragraph)

M5 took the byte-diff-oracle + cross-validation discipline from M4 and applied it to a NEW layer (NBT) for which the spec publishes only ONE byte sample (§3.3, intermediate page). The single byte-diff oracle was supplemented by **synthetic non-monotonic-NID cross-validation** (Phase C `[non_monotonic_nids]`) — equivalent of the M4 §3.9 finding, applied prospectively to NBT before the first reader existed. "Design before code" caught the §2.4.1 "Internal" misnomer (NID_ROOT_FOLDER's nidType = NormalFolder, not Internal) and the §2.7.1 deferral decision (27 mandatory nodes is M6 work, not Phase D plumbing) — both surfaced from spec text re-verification rather than implementation surprises. The dominant residual risk transfers cleanly to M6: 7 M4-era KNOWN_UNVERIFIED entries + 1 M5 entry (intermediate-BBT format), all gated on real-Outlook PSTs that are now M6's responsibility.

---

## M6 — Messaging Core: pre-flight notes

### M6 scope (working draft)

M6 lands the messaging-layer schema on top of M5's NBT/BBT plumbing:

- **Message store PC** at NID_MESSAGE_STORE (0x21) with §3.10's 9 named properties.
- **Root Folder PC + 3 sibling tables** at NIDs 0x122 / 0x12D / 0x12E / 0x12F per §3.12.
- **Name-to-ID map** at NID_NAME_TO_ID_MAP (0x61) per §2.4.7.
- **IPM Subtree, Search Folders, Deleted Items**: 3 folder objects × 4 nodes each = 12 nodes (NIDs 0x8022..0x806F).
- **Search-related infrastructure**: NID_SEARCH_MANAGEMENT_QUEUE, NID_SEARCH_ACTIVITY_LIST, the 5 template-table TCs.
- **Spam search folder PC** at NID 0x2223.
- Total: the §2.7.1 list of 27 mandatory nodes, fully populated.

M6 is the milestone where Outlook should be able to **open** the produced PST — making real-Outlook validation actually meaningful for the first time. The 7 M4-era KNOWN_UNVERIFIED entries + 1 M5 entry all resolve at M6's real-Outlook gate.

### M6 oracle inventory

| Source | URL | Category | Notes |
|---|---|---|---|
| **[MS-OXPROPS]** Master Property List | `learn.microsoft.com/.../ms-oxprops/f6ab1613-aefe-447d-a49c-18217230b148` | Reference catalog | Canonical names + PidTag values + property types for every property used in messaging. M6 pinning each PC's schema requires looking up ~20-50 PidTags here. PidTag prefix = property identified by 16-bit ID (most common); PidLid prefix = property identified by 32-bit value + property set; PidName prefix = string-named property. |
| **[MS-OXCMSG]** Message and Attachment Object Protocol | `learn.microsoft.com/.../ms-oxcmsg/7fd7ec40-deec-4c06-9493-1bc06b349682` | Reference (M6 + M7) | ROP-level (Remote Operation) protocol for message/attachment objects. Defines Message-object property schema, Recipient-table layout, Attachment-table layout. M6 uses for Recipient/Attachment template TCs (§2.7.1 NIDs 0x671/0x692). M7 uses for actual message + attachment writing. |
| **[MS-PST] §3.10** Sample Message Store | (already transcribed; §3.8 HN bytes) | Decode reference | The 9 properties verified by M5 Phase E `[semantic_decode_3_10]`. M6 writes a new message-store PC with these 9 (or more) properties; semantic_decode test will round-trip against §3.10's pinned values. |
| **[MS-PST] §3.12** Sample Folder Object | (already transcribed; §3.11 TC bytes for the hierarchy table) | Decode reference | 4 constituents per Folder: PC + Hierarchy TC + Contents TC + FAI Contents TC. M6 builds the Root Folder PC (4 props per §3.12) + the 3 sibling TCs. The full 13-column hierarchy TC schema is published verbatim in §3.12 — M6 must reproduce. |
| **[MS-OXCFOLD]** Folder Object Protocol | `learn.microsoft.com/en-us/openspecs/exchange_server_protocols/ms-oxcfold/...` (verify URL when M6 starts) | Reference (M6) | Folder-object semantic operations; PR_PARENT_FID / PR_PARENT_ENTRYID conventions. |

**No new §3.x byte-diff oracles**: M6 introduces no new spec-published byte samples beyond what M5 transcribed. Validation is via composition (§3.10/§3.12 decode references against M6-built bytes) + real-Outlook gate.

### M6 design choices to specify before implementation (Phase A of M6)

To be authored as an "M6 Part 2 design doc" before any M6 code lands:

- **Schema strategy**: hard-coded per §2.7.1 vs config-driven from a property catalog?
- **EntryID encoding**: how does our writer produce 24-byte EntryIDs (per §3.10 sample)? They contain a GUID + NID; the GUID must match the message store's `PidTagRecordKey`.
- **PR_PARENT_FID / PR_PARENT_ENTRYID wiring**: §3.12 says "the parent NID of the Root Folder points to itself" — confirm by spec text re-read in M6 pre-flight.
- **Empty-table semantics**: §3.12 has Contents/FAI tables with 0 rows but full column definitions. Does M5's TC writer (M4 carryover) produce this state? If not, may need a small extension.
- Each design rule marked `[SPEC §X.Y]` (verified) or `[DESIGN]` with rationale.

### M6 real-Outlook acquisition retry

Phase E's M5 closure does not unblock the seven KNOWN_UNVERIFIED entries (M5's plumbing is correct given M4 fixtures, but those fixtures aren't a complete PST). **M6 is the natural retry moment**:

- M6 produces files Outlook should actually accept (per §2.7.1 27-mandatory-node compliance).
- Real-Outlook validation becomes meaningful — opening the M6 PST in Outlook either succeeds (resolving all 8 KNOWN_UNVERIFIED entries) or surfaces specific divergences worth investigating.
- Local Outlook is still installed (per Phase A pre-flight finding); the user-facing 2-minute Account Settings → Add Data File path remains the simplest acquisition route.

### M6 toolchain debt

Same items as M5 pre-flight (deferred):
- MSVC `/W4 /WX` clean build still pending (deferred to M5 Phase D originally; M5 Phase D ran but did not validate MSVC; M6 Phase D is the new natural moment, OR before M6 starts).
- MinGW upgrade (g++ 6.3 → modern) deferred to M7 (Hardening).

---

## Real-Outlook PST acquisition status (M5 pre-flight, 2026-05-02)

**Status: not-attempted-this-pre-flight; concrete path documented.**

### Local-machine probe results

- Outlook IS installed locally (evidence: `%LOCALAPPDATA%\Microsoft\Outlook\` exists with content, including signed-in account caches).
- **No `.pst` files exist on disk.** Two `.ost` files were found:
  - `hemant.singh2@s.amity.edu.ost` (16 MB)
  - `hs20hemant@outlook.com.ost` (84 MB)
- Important: `.ost` is **NOT** the same format as `.pst`. `.ost` is the Exchange offline-cache format. `.pst` is the personal-folders format we are targeting. The two share NDB-layer concepts but differ in the messaging-layer headers and have different magic bytes / file extensions; an `.ost` is not a substitute for a `.pst` in our validation gate.

### Why not attempted in pre-flight

Acquiring a real `.pst` requires user-facing interaction with Outlook (e.g., **File → Open & Export → Import/Export → Export to a file → Outlook Data File (.pst)**, or creating a new local Personal Folders archive). That touches the user's signed-in mailboxes, which is out of scope for an autonomous pre-flight pass. Pre-flight should stop short of operations that read or modify the user's email content.

### Attempted again at M5 Phase A — blocked

When the user explicitly delegated PST acquisition ("no pst file created from my side by outlook, figure out best from your side"), the agent attempted PowerShell + Outlook COM automation (`New-Object -ComObject Outlook.Application`) to create a local PST without user GUI interaction. The sandbox correctly **denied permission** with this rationale: instantiating Outlook via COM accesses the user's signed-in mailboxes (Outlook auto-syncs on launch), and "figure out best from your side" is not specific authorization for email-client automation.

**Outcome:** no autonomous path is available without escalating beyond Phase A scope. The acquisition path documented in the previous subsection (user-driven Outlook → Add Data File) remains the only viable route. The user can either:
- Run the documented Outlook menu sequence themselves (2-minute action), or
- Explicitly authorize Bash COM-automation permission for a follow-up turn, accepting that Outlook will briefly launch and auto-sync against signed-in mailboxes during PST creation.

This block is not Phase A's problem. M5 Phase B/C/D do not need a real-Outlook PST — they need only the §3.3 spec oracle (already on disk). The real-Outlook gate becomes load-bearing at M5 Phase E. There is time before that point for the user to decide whether to run the menu sequence, or for the agent to revisit with explicit permission.

### Acquisition path for the user (when ready)

The fastest sample-acquisition path on this machine:

1. **Outlook → File → Account Settings → Data Files → Add** — create a new local `.pst`. This produces a near-empty PST with the minimum mandatory NIDs (message store, root folder, search root, deleted-items, etc.). **This is the highest-value first sample**: it directly exercises the seven-entries-one-gate validation.
2. Optionally drag a few messages into the new local PST to populate the LTP layer with non-trivial PCs and TCs.
3. Save to `tests/golden/real_outlook_minimum.pst`.
4. Add `tests/golden/real_outlook_*.pst` to `.gitignore` if licensing/privacy restricts redistribution. (Outlook-produced PSTs may contain proprietary metadata — treat as private by default.)
5. Run `build/gcc/bin/pst_info.exe tests/golden/real_outlook_minimum.pst` and capture output verbatim.

### Validation pass to run once a sample is in hand

The "Real-Outlook validation plan" subsection in M5's design doc above specifies the validation order (CRC-first, structural rules, semantic rules) and result-recording convention. M5 Phase E is the natural moment to run that pass.

### Known blockers if the user does not have Outlook

- **Outlook for Mac** — does NOT write `.pst` natively; it uses `.olm`. Not viable.
- **Linux / no-Office-license** — Microsoft 365 Outlook trial requires a Windows host; could be run in a Windows VM. mfcmapi / OutlookSpy depend on a working Outlook profile.
- **Public PST corpora (TREC enron etc.)** — predates Unicode-PST format (most are ANSI-PST = wVer=14). Not viable for Unicode-PST validation.

This block is therefore primarily user-time-cost on a Windows + Outlook host that this machine already has.

---

## M6 Part 2 — Messaging Core builder design (Phase A, 2026-05-04)

This doc pins design choices BEFORE M6 implementation lands so test
specification is stable. Same discipline as M4 Part 2 / M5 design.

M5 closure left the project here: the M5 PST writer plumbs NBT/BBT
correctly given M4 PC + TC fixtures, but Outlook will reject the
output as "not a complete PST" because the [SPEC §2.7.1] mandatory-node
set is not populated. M6 lands the messaging-layer schema on top of M5
plumbing so the produced PST should actually open in Outlook for the
first time. Eight KNOWN_UNVERIFIED entries (7 M4-era + 1 M5 pre-flight)
all gate on the moment Outlook accepts an M6-produced PST.

### References (verified during this Phase A — re-verifiable, not authoritative until checked)

Re-verified via WebFetch on 2026-05-04:

- [MS-PST] §3.10 *Sample Message Store* — `learn.microsoft.com/.../ms-pst/8fa17657-df23-466d-b09b-29742a745246` (✓ fetched cleanly; 9-property dump matches existing `[semantic_decode_3_10]` test)
- [MS-PST] §3.12 *Sample Folder Object* — `learn.microsoft.com/.../ms-pst/59235d49-bd76-4759-b26c-9769e97c4106` (✓ fetched cleanly; 4-prop Root Folder PC + 13-col Hierarchy TC + 27-col Contents TC + 17-col FAI Contents TC confirmed)

Verified during M5 closure (2026-05-02), trusted forward:
- [MS-PST] §2.4.1 *Special Internal NIDs* — 14 reserved NIDs in `M5Allocator`
- [MS-PST] §2.2.2.1 *NID layout* — `nidType` low 5 bits, `nidIndex` high 27
- [MS-PST] §2.3.x *LTP layer* — HN, BTH, PC, TC formats (M4)

**Cited but URL not yet re-verified — placed in pre-Phase-B verification queue:**
- [MS-PST] §2.7.1 *Mandatory Nodes* — cited in M5 closure and existing `[semantic_decode_3_10]` test comments. The NID values 0x8022 (IPM SubTree), 0x8042 (Search Folders / Finder), 0x8062 (Deleted Items / Wastebasket) are referenced by §3.10's EntryIDs (verified) and so the existence of these NIDs is established empirically; the section URL itself was wrong on first attempt during this Phase A (404). **Do not write `[SPEC §2.7.1]` markers in M6 code without re-verifying URL + content first.**
- [MS-PST] §2.4.7 *Named Property Map* (NID 0x61) — schema not yet seen by this session.
- [MS-PST] §2.4.8.5.x — search-related TC schemas (referenced by M5Allocator's `SearchGathererQueue`/`Descriptor`/`FolderQueue` reserved NIDs).

External references (not fetched in Phase A; pre-Phase-B verification queue):
- [MS-OXPROPS] *Master Property List* — `learn.microsoft.com/.../ms-oxprops/f6ab1613-aefe-447d-a49c-18217230b148` — canonical PidTag names, IDs, types.
- [MS-OXCMSG] *Message and Attachment Object Protocol* — used in M7 for actual messages; M6 uses it only for §2.7.1's Recipient/Attachment template TCs.
- [MS-OXCFOLD] *Folder Object Protocol* — `learn.microsoft.com/.../ms-oxcfold/...` — confirms `PR_PARENT_FID` / `PR_PARENT_ENTRYID` semantics. M6 references for sub-folder wiring verification.
- [MS-OXCDATA] — EntryID structure semantics (`rgbFlags` bytes 0..3).

### Ground rules (apply throughout M6)

1. **Build-from-scratch only.** Same simplification as M4 PC writer / M5 NBT plumbing. In-place property updates, message edits, etc. are M7+ scope.
2. **Hard-coded per-NID-type schemas.** See Decision 1 below — config-driven property catalogs are M7 hardening work.
3. **Outlook-acceptance gate is M6's headline.** When an M6 PST opens cleanly in Outlook, the 7 M4-era + 1 M5 KNOWN_UNVERIFIED entries all resolve at once. M6 closure should run that gate end-to-end.
4. **Spec-citation discipline (M5 carryover):** every `[SPEC §X.Y]` marker added in M6 code must point at a section whose URL was re-fetched in this session or a chained M6 phase. Anything else gets `[SPEC §X.Y, RE-VERIFY]`.

### Decision 1 — Schema strategy: hard-coded per node type

| Marker | Decision | Rationale |
|---|---|---|
| **[DESIGN]** | M6 hard-codes the per-NID-type PC/TC schemas as standalone builders: `buildMessageStorePc(...)`, `buildRootFolderPc(...)`, `buildFolderHierarchyTc(...)`, `buildFolderContentsTc(...)`, `buildFolderFaiContentsTc(...)`, `buildSubfolderPc(...)`, `buildNameToIdMapPc(...)`, etc. Each builder takes the minimum dynamic data the schema needs (display name UTF-16-LE bytes, ProviderUID GUID, sub-folder NID list) and emits an HN body via M4's `buildPropertyContext` / `buildTableContext`. | Deterministic byte output is required for §3.10/§3.12 byte-diff oracles. A property-catalog abstraction adds pre-Phase-B validation surface area without test gain at this milestone. M7 hardening is the natural moment for a `PropertyCatalog` registry once the M6 schemas have stabilised. |
| **[DESIGN]** | Each builder is **byte-deterministic**: same logical input ⇒ byte-identical output. PidTags sorted ascending, EntryIDs encoded canonically (Decision 2), strings caller-supplied as UTF-16-LE. | Same M4 deterministic-builder rule. Required for the M6 byte-diff oracles. |

Alternative considered: A `PropertyCatalog` registry mapping `(NID-type, schema-version) → schema descriptor`. Defer to M7+.

### Decision 2 — EntryID encoding (24 bytes, 3-region layout)

[VERIFIED via §3.10 fetch, 2026-05-04] EntryID format reverse-engineered from §3.10's three EntryID values:

```
[bytes  0..  3]   rgbFlags    — 4 zero bytes in §3.10 (all three EntryIDs)
[bytes  4.. 19]   ProviderUID — 16-byte GUID (= message store's PidTagRecordKey verbatim)
[bytes 20.. 23]   entryNid    — 4-byte NID, little-endian
```

§3.10 evidence (verbatim from spec sample):
- `PidTagRecordKey` = `22 9D B5 0A DC D9 94 43 85 DE 90 AE B0 7D 12 70`
- `PidTagIpmSubTreeEntryId` = `00 00 00 00 ‖ 22 9D B5 0A DC D9 94 43 85 DE 90 AE B0 7D 12 70 ‖ 22 80 00 00` ← `entryNid = 0x00008022` (IPM SubTree)
- `PidTagIpmWastebasketEntryId` last 4 bytes: `62 80 00 00` ← `entryNid = 0x00008062` (Wastebasket / Deleted Items)
- `PidTagFinderEntryId` last 4 bytes: `42 80 00 00` ← `entryNid = 0x00008042` (Finder / Search Folders)

| Marker | Decision | Rationale |
|---|---|---|
| **[VERIFIED §3.10]** | EntryID byte layout = `rgbFlags(4) ‖ ProviderUID(16) ‖ entryNid(4)`. Fixed, 24 bytes total. | §3.10's three EntryIDs all match this shape exactly. |
| **[DESIGN]** | `rgbFlags = {0, 0, 0, 0}` for all M6-generated EntryIDs. The §3.10 sample shows all-zero for all three EntryIDs. | Conservative: matches the spec sample. May change if a real-Outlook PST surfaces non-zero rgbFlags ([MS-OXCDATA] semantics — pre-Phase-B verification). |
| **[DESIGN]** | `ProviderUID` is a 16-byte GUID **caller-supplied** at PST-creation time. The same GUID populates the message-store PC's `PidTagRecordKey` AND every EntryID inside that PST. The library exposes a default helper that generates a v4 random GUID via `std::random_device`; callers may override (e.g. for deterministic test fixtures). | Caller responsibility, same pattern as M5's caller-supplied NIDs. Default helper avoids forcing every caller to vendor a GUID library. The cross-EntryID-consistency invariant (every EntryID's UID == message-store's PidTagRecordKey) is enforced by routing all EntryID construction through one builder. |
| **[INVARIANT]** | M6 has exactly ONE ProviderUID per PST — the message store's `PidTagRecordKey`. Every EntryID embeds it. Reader (semantic-decode test) verifies the cross-prop invariant. | §3.10 evidence: same 16 bytes appear in `PidTagRecordKey` AND inside all 3 EntryIDs. This must not drift in M6 output. |

**Open question — pre-Phase-B**: does Outlook's `EntryID.rgbFlags` carry message-class semantics in some scenarios? [MS-OXCDATA] is the authoritative source. If yes, M6's all-zero default may need to vary by NID-type. Verify before Phase B.

### Decision 3 — `nidParent` wiring (NBT entry's parent-NID field)

[VERIFIED via §3.12 fetch, 2026-05-04]: *"the parent NID of the Root Folder points to itself."* §3.12 sample shows Root Folder NID = 0x122, Parent NID = 0x00000122.

Also from §3.12: the three sibling tables (NIDs 0x12D Hierarchy / 0x12E Contents / 0x12F FAI Contents) all have `Parent NID: 0x00000000` — they are NOT logical children of the Root Folder PC at the NBT level; they are sibling NBT entries with the same `nidIndex` (0x09) but different `nidType`.

| Marker | Decision | Rationale |
|---|---|---|
| **[VERIFIED §3.12]** | Root Folder's NBT entry uses `nidParent = NID_ROOT_FOLDER (0x122)` — self-reference. | §3.12 prose + dump. |
| **[DESIGN]** | Sub-folder PCs (NIDs 0x8022 IPM Subtree, 0x8042 Finder, 0x8062 Wastebasket — all with `nidType = NORMAL_FOLDER` per the §3.12 hierarchy-TC's RowIndex evidence) use `nidParent = NID_ROOT_FOLDER (0x122)`. | Standard parent-child folder relationship. (RE-VERIFY before Phase B by inspecting §3.12 prose for sub-folder NBT entries — §3.12 publishes only the Root Folder's NBT entry, so sub-folder `nidParent` is inferred, not directly observed.) |
| **[VERIFIED §3.12]** | Per-folder "sibling" tables (Hierarchy / Contents / FAI Contents at `nidType` ∈ {0x0D, 0x0E, 0x0F} sharing the same `nidIndex` as the folder PC) have `nidParent = 0x00000000`. They are not "children" of the folder PC in the NBT sense. | §3.12 sample's NBT entries for 0x12D / 0x12E / 0x12F all show `Parent NID: 0x00000000`. |

**Implication for `M5Node` struct** (from `writer.hpp`): the existing field `Nid nidParent {};` already accommodates this — M6 just supplies the right value per node-kind. No structural change required.

### Decision 4 — Empty-table semantics (Contents TC + FAI Contents TC with 0 rows)

[VERIFIED via §3.12 fetch, 2026-05-04]: Root Folder's Contents TC (NID 0x12E, 27 columns) and FAI Contents TC (NID 0x12F, 17 columns) both publish full TCOLDESC arrays but ZERO rows ("Row Matrix Data Not Present (0 Rows)"). RowIndex BTH HID = 0x20 in both — i.e., RowIndex BTHHEADER is present but its leaf is empty.

| Marker | Decision | Rationale |
|---|---|---|
| **[VERIFIED §3.12]** | Empty TC layout: TCINFO + sorted TCOLDESC array at HID 0x40, RowIndex BTHHEADER at HID 0x20 (`hidRoot = 0` / `bIdxLevels = 0`), no Row Matrix HN allocation. | §3.12's Contents/FAI dumps. |
| **[PRE-PHASE-B AUDIT]** | Verify M4's `buildTableContext(...)` (already shipped) produces this exact byte shape when called with `rowCount = 0`. If it allocates an empty Row Matrix slot, or sets `hidRoot` to a non-zero value, fix in Phase B. | The existing `[golden_spec_tc]` test exercises the §3.11 sample which has 3 rows; the 0-row case is under-tested. |
| **[DESIGN]** | M6's `buildFolderContentsTc(...)` and `buildFolderFaiContentsTc(...)` always emit empty TCs (0 rows) — actual message rows arrive in M7. | M6 scope is "Outlook can open the PST"; messages are M7. |
| **[CROSSCHECK with M4 KNOWN_UNVERIFIED]** | The "empty-PC `hidRoot==0` convention" entry (M4 PC reader) and the empty-TC RowIndex case here are the same sentinel pattern — `hidIndex == 0` → empty BTH. M4's reader handles this (`readPropertyContext` returns 0 records); M6 builder must EMIT this. | Reader / writer symmetry. |

### Mandatory nodes — §2.7.1 verbatim (re-fetched 2026-05-04, URL `learn.microsoft.com/.../ms-pst/661f9921-54ff-4768-b98c-91954312af52`)

Spec text quoted verbatim: *"The following table lists the absolute minimum list of nodes that MUST be present in a PST. Implementations SHOULD consider the PST invalid if any of the nodes are missing or are incorrectly formed. The NIDs in bold are fixed NID values, where the others are sample NIDs that can be any valid NID value for its respective NID_TYPE."*

**Total: 27 nodes** (confirms M5 closure's "27 mandatory nodes" claim).

| # | NID | NID_TYPE | Special NID / Friendly | Object | Minimal state | M6 builder |
|---|---|---|---|---|---|---|
|  1 | `0x0021` | INTERNAL              | NID_MESSAGE_STORE                  | PC   | Schema Props | `buildMessageStorePc(...)` |
|  2 | `0x0061` | INTERNAL              | NID_NAME_TO_ID_MAP                 | PC   | Empty        | `buildNameToIdMapPc(...)` |
|  3 | `0x0122` | NORMAL_FOLDER         | NID_ROOT_FOLDER                    | PC   | Schema Props | `buildRootFolderPc(...)` |
|  4 | `0x012D` | HIERARCHY_TABLE       | <Root Folder>                      | TC   | **2 Rows**   | `buildFolderHierarchyTc(...)` |
|  5 | `0x012E` | CONTENTS_TABLE        | <Root Folder>                      | TC   | Columns Only | `buildFolderContentsTc(...)` |
|  6 | `0x012F` | ASSOC_CONTENTS_TABLE  | <Root Folder>                      | TC   | Columns Only | `buildFolderFaiContentsTc(...)` |
|  7 | `0x01E1` | INTERNAL              | NID_SEARCH_MANAGEMENT_QUEUE        | node | Empty        | `buildEmptyNode(...)` (see Decision 5) |
|  8 | `0x0201` | INTERNAL              | NID_SEARCH_ACTIVITY_LIST           | node | Empty        | `buildEmptyNode(...)` |
|  9 | `0x060D` | HIERARCHY_TABLE       | NID_HIERARCHY_TABLE_TEMPLATE       | TC   | Columns Only | `buildHierarchyTemplateTc(...)` |
| 10 | `0x060E` | CONTENTS_TABLE        | NID_CONTENTS_TABLE_TEMPLATE        | TC   | Columns Only | `buildContentsTemplateTc(...)` |
| 11 | `0x060F` | ASSOC_CONTENTS_TABLE  | NID_ASSOC_CONTENTS_TABLE_TEMPLATE  | TC   | Columns Only | `buildFaiContentsTemplateTc(...)` |
| 12 | `0x0610` | SEARCH_CONTENTS_TABLE | NID_SEARCH_CONTENTS_TABLE_TEMPLATE | TC   | Columns Only | `buildSearchContentsTemplateTc(...)` |
| 13 | `0x0671` | ATTACHMENT_TABLE      | NID_ATTACHMENT_TABLE               | TC   | Columns Only | `buildAttachmentTemplateTc(...)` |
| 14 | `0x0692` | RECIPIENT_TABLE       | NID_RECIPIENT_TABLE                | TC   | Columns Only | `buildRecipientTemplateTc(...)` |
| 15 | `0x2223` | **SEARCH_FOLDER**     | <Spam search Folder>               | PC   | Schema Props | `buildSearchFolderPc(...)` |
| 16 | `0x8022` | NORMAL_FOLDER         | <IPM SuBTree>                      | PC   | Schema Props | `buildSubfolderPc(...)` |
| 17 | `0x802D` | HIERARCHY_TABLE       | <IPM SuBTree>                      | TC   | **2 Rows**   | `buildFolderHierarchyTc(...)` |
| 18 | `0x802E` | CONTENTS_TABLE        | <IPM SuBTree>                      | TC   | Columns Only | `buildFolderContentsTc(...)` |
| 19 | `0x802F` | ASSOC_CONTENTS_TABLE  | <IPM SuBTree>                      | TC   | Columns Only | `buildFolderFaiContentsTc(...)` |
| 20 | `0x8042` | NORMAL_FOLDER         | <Search Folder objects> (Finder)   | PC   | Schema Props | `buildSubfolderPc(...)` |
| 21 | `0x804D` | HIERARCHY_TABLE       | <Search Folder objects>            | TC   | Columns Only | `buildFolderHierarchyTc(...)` |
| 22 | `0x804E` | CONTENTS_TABLE        | <Search Folder objects>            | TC   | Columns Only | `buildFolderContentsTc(...)` |
| 23 | `0x804F` | ASSOC_CONTENTS_TABLE  | <Search Folder objects>            | TC   | Columns Only | `buildFolderFaiContentsTc(...)` |
| 24 | `0x8062` | NORMAL_FOLDER         | <Deleted Items>                    | PC   | Schema Props | `buildSubfolderPc(...)` |
| 25 | `0x806D` | HIERARCHY_TABLE       | <Deleted Items>                    | TC   | Columns Only | `buildFolderHierarchyTc(...)` |
| 26 | `0x806E` | CONTENTS_TABLE        | <Deleted Items>                    | TC   | Columns Only | `buildFolderContentsTc(...)` |
| 27 | `0x806F` | ASSOC_CONTENTS_TABLE  | <Deleted Items>                    | TC   | Columns Only | `buildFolderFaiContentsTc(...)` |

**Sibling-NID stride (verified from §2.7.1 verbatim)**: per-folder `Hierarchy / Contents / FAI Contents` tables share the **same `nidIndex`** as the folder PC, with `nidType` ∈ {0x0D, 0x0E, 0x0F} respectively. Worked example: Root Folder PC at NID 0x0122 has `nidIndex = 0x09`; its tables are at 0x012D / 0x012E / 0x012F (same `nidIndex 0x09`, `nidType` 0x0D / 0x0E / 0x0F). Same pattern at 0x8022/2D/2E/2F, 0x8042/4D/4E/4F, 0x8062/6D/6E/6F.

**Spec typo observed**: §2.7.1 row for 0x060D writes `NID_TYPE_HIERRCHY_TABLE` (missing 'A'). Same typo flagged in M5 closure §2.7.1 typo notes — confirms we're reading the same source.

#### Findings on re-verifying §2.7.1 (correct prior-design errors)

1. **Sibling-table NIDs were wrong in initial Phase A draft**: had 0x8023/0x8024/0x8025 for IPM Subtree tables; correct values are 0x802D / 0x802E / 0x802F. Same correction applied for Search Folders (0x804D/E/F not 0x8043/4/5) and Deleted Items (0x806D/E/F not 0x8063/4/5). The general rule is `nidType` 0x0D/0x0E/0x0F, NOT successive `nidIndex` values.
2. **Spam Search Folder 0x2223 is a SEARCH_FOLDER (nidType 0x03)**, not NORMAL_FOLDER. Different PC schema from the regular sub-folders.
3. **Templates were missed entirely** in the initial draft: 0x060D / 0x060E / 0x060F / 0x0610 are 4 standalone "Columns Only" TCs not associated with any folder. They define the schemas other tables inherit. M6 must emit them with the spec-mandated column sets.
4. **Recipient and Attachment templates** (0x0692, 0x0671) are also independent "Columns Only" TCs. Schemas come from [MS-OXCMSG]. They live in the PST even before any messages exist.
5. **Two "node" objects** (0x01E1, 0x0201): not PCs, not TCs. Object type literally `node` per §2.7.1. Likely just a bare data block holding minimal payload (4-byte zero, or empty). Pre-Phase-B research: extract one from a real Outlook PST.
6. **Root Folder Hierarchy "minimal" = 2 Rows**, but §3.12 sample has 3 rows (incl. 0x2223 spam). M6 default emits 3 rows to keep 0x2223 reachable from Root, since 0x2223 itself is mandatory and orphaning it would fail spec invariant "every NID is reachable".
7. **IPM Subtree Hierarchy "minimal" = 2 Rows** but §2.7.1 doesn't list any IPM-Subtree-children NIDs. Pre-Phase-B finding: either (a) the 2-row minimum is enforced loosely by Outlook and 0 rows is tolerated, or (b) Outlook auto-creates standard mailbox folders (Inbox, Drafts, etc.) on first-open. Confirm at the M6 real-Outlook gate; if Outlook rejects an empty IPM Subtree Hierarchy, M6 emits 2 dummy entries + their PC + 3 sibling tables each (8 extra NIDs not in §2.7.1).
8. **Reserved NIDs not in §2.7.1**: of `M5Allocator`'s 14 reserved NIDs (§2.4.1), only 5 are also §2.7.1-mandatory: `0x0021`, `0x0061`, `0x0122`, `0x01E1`, `0x0201`. The other 9 (0xA1 normal-folder template, 0xC1 search-folder template, 0x241/0x261/0x281/0x2A1/0x2E1/0x301/0x321) are reserved-but-optional. M6 writes only the 5 mandatory ones; M5Allocator continues to pre-register all 14 to prevent accidental reuse.

### Per-PC schemas (Phase B + D builder targets)

#### M6.1 — Message Store PC (NID 0x21)

[VERIFIED §3.10] 9 named properties. Existing test `[semantic_decode_3_10]` has the byte-diff specification.

| PidTag | Name | Type | M6 value strategy |
|---|---|---|---|
| `0x0E340102` | PidTagReplVersionhistory | PtypBinary (24 B) | Generated: `01 00 00 00 ‖ ProviderUID(16) ‖ 01 00 00 00`. The §3.10 sample's 24-byte value decomposes as `01 00 00 00` + the 16-byte ProviderUID + `01 00 00 00`. |
| `0x0E380003` | PidTagReplFlags          | PtypInteger32     | `0` |
| `0x0FF90102` | PidTagRecordKey          | PtypBinary (16 B) | The ProviderUID GUID itself |
| `0x3001001F` | PidTagDisplayName        | PtypString        | Caller-supplied UTF-16-LE bytes; empty-string default if unspecified |
| `0x35DF0003` | PidTagValidFolderMask    | PtypInteger32     | `0x89` (matches §3.10; bit set: SubTree/Finder/Wastebasket EntryIDs valid; pre-Phase-B re-derive bitmask semantics from [MS-OXCSTOR]) |
| `0x35E00102` | PidTagIpmSubTreeEntryId  | PtypBinary (24 B) | EntryID with `entryNid = 0x8022` |
| `0x35E30102` | PidTagIpmWastebasketEntryId | PtypBinary (24 B) | EntryID with `entryNid = 0x8062` |
| `0x35E70102` | PidTagFinderEntryId      | PtypBinary (24 B) | EntryID with `entryNid = 0x8042` |
| `0x67FF0003` | PidTagPstPassword        | PtypInteger32     | `0` (no password) |

**Plus 2 extras observed in §3.9 BTH bytes but omitted from §3.10 prose** (per M5 closure finding):
| `0x6633000B` | (PidTag 0x6633, type Boolean)        | PtypBoolean   | `1` (matches §3.9 evidence) |
| `0x66FA0003` | (PidTag 0x66FA, type Integer32)      | PtypInteger32 | `0x000E000D` (matches §3.9 evidence) |

Total: 11 properties. Pre-Phase-B: identify canonical PidTag names for 0x6633 / 0x66FA from [MS-OXPROPS]. Both are in the `0x6600..0x67FF` PST-internal range so [MS-OXCSTOR] / [MS-PST] is the authoritative source, not [MS-OXPROPS].

#### M6.2 — Root Folder PC (NID 0x122)

[VERIFIED §3.12] 4 properties:

| PidTag | Name | Type | M6 value strategy |
|---|---|---|---|
| `0x3001001F` | PidTagDisplayName        | PtypString    | UTF-16-LE bytes; §3.12 sample shows it but value is omitted in the prose dump (likely empty) — caller-supplied or empty-default |
| `0x36020003` | PidTagContentCount       | PtypInteger32 | `0` |
| `0x36030003` | PidTagContentUnreadCount | PtypInteger32 | `0` |
| `0x360A000B` | PidTagSubfolders         | PtypBoolean   | `1` (Root Folder has 3 sub-folders per §3.12) |

#### M6.3 — Sub-folder PCs (NIDs 0x8022 / 0x8042 / 0x8062)

Same 4-property shape as Root Folder PC. Differ only in:
- DisplayName UTF-16-LE bytes ("Top of Personal Folders" / "Search Root" / "Wastebasket" or similar)
- Subfolders Boolean (false unless the sub-folder has its own children)
- ContentCount / ContentUnreadCount (0 in M6; M7+ updates as messages land)

#### M6.4 — Search Folder PC (NID 0x2223 — Spam Search Folder)

`nidType = NID_TYPE_SEARCH_FOLDER (0x03)`, NOT `NORMAL_FOLDER`. Schema is per §2.7.1's "PC / Schema Props" — exact property list not pinned in §2.7.1 itself; pre-Phase-B research item.

Pre-Phase-B: extract a real Outlook PST and dump NID 0x2223's PC. Likely a superset of the standard folder PC schema (DisplayName, ContentCount, etc.) plus search-specific properties (PR_SEARCH_KEY, search criteria props per [MS-OXOSRCH]).

#### M6.5 — Name-To-Id Map PC (NID 0x0061)

[VERIFIED §2.4.7, fetched 2026-05-04, URL `learn.microsoft.com/.../ms-pst/e17e195d-0454-4b9b-b398-c9127a26a678`]

Spec text (verbatim): *"The mapping between NPIDs and property names is done using a special Name-to-ID-Map in the PST, with a special NID of NID_NAME_TO_ID_MAP (0x61). There is one Name-to-ID-Map per PST. From an implementation point of view, the Name-to-ID-Map is a standard PC with some special properties. Specifically, the properties in the PC do not refer to real property identifiers, but instead point to specific data sections of the Name-to-ID-Map."*

NPMAP components per §2.4.7: Entry Stream, GUID Stream, String Stream, hash table.

§2.7.1 minimum state for NID 0x0061 = **"Empty"** — the PC is present but no named properties are registered.

For an empty Name-to-ID Map, the PC contains all 4 streams as zero-length PtypBinary properties (the stream PidTags are spec-mandated; the hash-table property is also present but contains the empty hash).

Exact PidTag IDs for the 4 stream properties are in §2.4.7's sub-sections (§2.4.7.1 Entry Stream, §2.4.7.2 GUID Stream, §2.4.7.3 String Stream, §2.4.7.4 hash table) — pre-Phase-B fetch each sub-page and pin the PidTag values + property types. Initial guess (to confirm):
- `PidTagNameidStreamEntry`  (0x00030102 PtypBinary) — Entry Stream
- `PidTagNameidStreamGuid`   (0x00020102 PtypBinary) — GUID Stream
- `PidTagNameidStreamString` (0x00040102 PtypBinary) — String Stream
- One additional property holding the hash-table data (PidTag TBD).

### Decision 5 — "node" object type for NIDs 0x01E1 / 0x0201 (Search Management Queue / Activity List)

§2.7.1 lists these two NIDs with `Object = "node"` (lowercase, neither PC nor TC) and `Minimal state = "Empty"`. They are NOT heap-on-node structures.

| Marker | Decision | Rationale |
|---|---|---|
| **[SPEC §2.7.1]** | NIDs 0x01E1 and 0x0201 are mandatory but their object type is bare `node` — a data block with no LTP-layer heap structure. | §2.7.1 verbatim. |
| **[DESIGN]** | M6 emits each as a single small data block (e.g. 4 bytes of zeros) registered in the BBT, with an NBT entry pointing to it. The BBT entry's `cb` may be as small as 1 byte; we'll use 4 (DWORD-sized) for alignment safety. `bidSub = 0` (no subnode tree). | "Empty" state per §2.7.1; no published evidence of required content. Pre-Phase-B: extract these blocks from a real Outlook PST to verify byte content and minimum size. |
| **[DESIGN]** | `buildEmptyNode(...)` helper produces the data block + NBTENTRY pair. Used for both 0x01E1 and 0x0201. | Single helper, parameterized by NID, keeps the wiring path uniform with PC/TC nodes. |

Pre-Phase-B research item: the M5 `[m5][end_to_end][m5_gate]` test exercises 2 nodes (the M4 PC + TC fixtures); extending to ~25 nodes is unproven. Confirm M5's `writeM5Pst(...)` BBT/NBT pagination handles 27 NIDs without exceeding the M5 single-intermediate-page cap (300 nodes; 27 fits comfortably). Audit before Phase D.

### Per-TC schemas (Phase C builder targets)

#### M6.6 — Folder Hierarchy TC (per folder, e.g. NID 0x12D for Root)

[VERIFIED §3.12] 13 columns sorted by PidTag ascending (TCOLDESC.tag invariant):

| PidTag        | Name                            | ibData | cbData | iBit |
|---------------|----------------------------------|--------|--------|------|
| `0x0E300003`  | PidTagReplItemid                 | 20     | 4      | 6    |
| `0x0E330014`  | PidTagReplChangenum              | 24     | 8      | 7    |
| `0x0E340102`  | PidTagReplVersionhistory         | 32     | 4      | 8    |
| `0x0E380003`  | PidTagReplFlags                  | 36     | 4      | 9    |
| `0x3001001F`  | PidTagDisplayName_W              | 8      | 4      | 2    |
| `0x36020003`  | PidTagContentCount               | 12     | 4      | 3    |
| `0x36030003`  | PidTagContentUnreadCount         | 16     | 4      | 4    |
| `0x360A000B`  | PidTagSubfolders                 | 52     | 1      | 5    |
| `0x3613001F`  | PidTagContainerClass_W           | 40     | 4      | 10   |
| `0x66350003`  | (PidTagPstHiddenCount)           | 44     | 4      | 11   |
| `0x66360003`  | (PidTagPstHiddenUnread)          | 48     | 4      | 12   |
| `0x67F20003`  | PidTagLtpRowId                   | 0      | 4      | 0    |
| `0x67F30003`  | PidTagLtpRowVer                  | 4      | 4      | 1    |

Row Matrix: one row per sub-folder. For Root Folder Hierarchy this is 3 rows (matching §3.12). For sub-folder Hierarchy TCs in M6: 0 rows (no nested sub-folders in M6 scope). For an M6 minimum PST with 3 sub-folders, only Root's Hierarchy is non-empty.

#### M6.7 — Folder Contents TC (per folder, e.g. NID 0x12E for Root)

[VERIFIED §3.12] 27 columns. Full TCOLDESC table reproducible from §3.12; transcribe in Phase C against the §3.12 fetched bytes. Row count: 0 for every folder in M6.

#### M6.8 — Folder FAI Contents TC (per folder, e.g. NID 0x12F for Root)

[VERIFIED §3.12] 17 columns. Same pattern. Row count: 0.

#### M6.9 — Template TCs (NIDs 0x060D / 0x060E / 0x060F / 0x0610)

[SPEC §2.7.1] Four standalone "Columns Only" TCs that define the schemas other tables inherit from:
- **0x060D** `NID_HIERARCHY_TABLE_TEMPLATE` — same 13-column schema as per-folder Hierarchy TCs (M6.6); differ only in NID. Row count: 0.
- **0x060E** `NID_CONTENTS_TABLE_TEMPLATE` — same 27-column schema as per-folder Contents TCs (M6.7). Row count: 0.
- **0x060F** `NID_ASSOC_CONTENTS_TABLE_TEMPLATE` — same 17-column schema as per-folder FAI Contents TCs (M6.8). Row count: 0.
- **0x0610** `NID_SEARCH_CONTENTS_TABLE_TEMPLATE` — search-results contents TC schema. Pre-Phase-B: fetch the search-folder TC schema from [MS-OXOSRCH] / [MS-PST] §2.4.8.6 (verify section number); it likely shares the Contents schema (M6.7) with extra search-criteria columns.

[DESIGN] Builders for the first three reuse `buildFolderHierarchyTc` / `buildFolderContentsTc` / `buildFolderFaiContentsTc` with `rowCount = 0` and a fixed NID. The 4th (0x0610) needs its own builder once the search schema is verified.

#### M6.10 — Recipient Table TC (NID 0x0692)

[SPEC §2.7.1] "Columns Only" TC. Schema from [MS-OXCMSG] §2.6.1.x or [MS-PST] §2.4.8.x (verify exact location pre-Phase-B). Row count: 0 in M6.

The Recipient Table holds per-message recipient rows. Even with zero messages in M6, the template must be present for Outlook compatibility.

Pre-Phase-B: pin the column schema (PidTagDisplayName_W, PidTagEmailAddress_W, PidTagAddrType_W, PidTagRecipientType, PidTagLtpRowId, PidTagLtpRowVer, ...).

#### M6.11 — Attachment Table TC (NID 0x0671)

[SPEC §2.7.1] "Columns Only" TC. Schema from [MS-OXCMSG] §2.7.x. Row count: 0 in M6.

Per-message attachment rows. Pre-Phase-B: pin column schema (PidTagAttachNumber, PidTagAttachMethod, PidTagAttachLongFilename_W, PidTagAttachSize, PidTagLtpRowId, PidTagLtpRowVer, ...).

### Reader contract (M5 carryover, expanded)

| Marker | Decision | Rationale |
|---|---|---|
| **[INVARIANT]** | M6 reader / semantic-decode tests are **propType-driven**, **HID-agnostic**, **NID-agnostic** (M4 + M5 carryover). No assumptions about HID layout, NID stride, or table-row ordering. | Real-Outlook PSTs are write-once-with-edits; tidy writer-side patterns don't hold. |
| **[NEW]** | EntryID cross-prop invariant: every EntryID inside a PST embeds a 16-byte ProviderUID at offset 4..19, and that GUID equals the message-store PC's `PidTagRecordKey`. The semantic-decode test verifies this across all M6-emitted EntryIDs. | Caught a bug if M6 ever generates inconsistent ProviderUIDs across a single PST. |
| **[NEW]** | Folder-graph invariant: every NID referenced by an EntryID inside a PC must exist in the NBT. (E.g., message store's `PidTagIpmSubTreeEntryId.entryNid = 0x8022` ⇒ NBT must have an entry for 0x8022.) The semantic-decode test resolves every EntryID's `entryNid` against the NBT. | Catches "orphan EntryID" — EntryID points to a NID with no node behind it. |
| **[NEW]** | Hierarchy-row invariant: every row in a folder's Hierarchy TC has `PidTagLtpRowId == sub-folder's NID`. Semantic-decode test cross-references against the NBT: every Hierarchy row's RowID must be a NID present in NBT, AND its NBT entry's `nidParent` must be the parent folder's NID. | Catches divergence between the table-driven folder graph (Hierarchy TCs) and the NBT-driven node graph. Both must agree. |

### Gate items: "M6 done"

1. `buildMessageStorePc(...)` exists. `[m6][message_store_round_trip]` test feeds §3.10's logical inputs (display name "UNICODE1", PidTagRecordKey GUID `22 9D B5 0A...12 70`, ValidFolderMask 0x89, three NIDs 0x8022/0x8042/0x8062), takes the resulting HN bytes through `readPropertyContext`, and verifies all 11 properties decode to the §3.10-published values. **Byte-diff against §3.8 sample is NOT a goal**: §3.8's HN allocation order (sizes 16, 16, 24, 24, 24, 24) doesn't match our M4 PC writer's PidTag-ascending HID assignment (which would emit 24, 16, 16, 24, 24, 24 for the same 6 HN-stored props). Real Outlook allocated §3.8's slots in some history-driven order — same lesson as M4 §3.9 cross-validation. Round-trip semantic equivalence is the realistic gate; the existing M5 `[semantic_decode_3_10]` test locks the read-side decode.
2. `buildRootFolderPc(...)` exists; emits the §3.12 4-property PC bytes deterministically.
3. `buildSubfolderPc(...)` (NORMAL_FOLDER) and `buildSearchFolderPc(...)` (SEARCH_FOLDER for NID 0x2223) exist.
4. `buildFolderHierarchyTc(...)` / `buildFolderContentsTc(...)` / `buildFolderFaiContentsTc(...)` exist; `[m6][hierarchy_tc_3_12]` byte-diff test reproduces §3.12's Hierarchy TC for the 3-sub-folder case. Empty-row TC byte shape verified against §3.12 Contents/FAI.
5. `buildNameToIdMapPc(...)` exists; emits §2.4.7 NameToId schema with empty stream values (M6 default).
6. Template-TC builders for `0x060D / 0x060E / 0x060F / 0x0610` exist (most reuse Hierarchy/Contents/FAI builders with empty rows). Search-Contents template (0x0610) has a Phase-B-pinned schema from [MS-OXOSRCH].
7. `buildRecipientTemplateTc(...)` and `buildAttachmentTemplateTc(...)` exist with [MS-OXCMSG] schemas, 0 rows.
8. `buildEmptyNode(...)` helper exists; emits the bare-data-block + NBTENTRY pair for NIDs 0x01E1 and 0x0201.
9. End-to-end M6 PST writer (extension of `writeM5Pst` → new `writeM6Pst`) assembles **all 27 §2.7.1 mandatory nodes** into one PST. `pst_info` reports `ALL CHECKS PASSED`. Folder-graph invariants (M6 reader contract) verified by automated tests. NBT walk yields exactly the expected 27 NIDs.
10. **Real-Outlook gate: an M6-produced PST opens cleanly in a Windows Outlook installation** without prompting "Outlook detected a problem with this Outlook Data File" or `scanpst` warnings. The 7 M4-era + 1 M5 KNOWN_UNVERIFIED entries are walked at this point; each flips to **Verified** / **Tolerated** / **Disagree** per the M5 closure validation procedure.

### Implementation phasing (subsequent — Phase B authorization is a separate prompt)

1. **Phase A** — this design doc + pre-Phase-B verification queue resolved (re-verify §2.7.1, §2.4.7, §2.4.8.5.x, [MS-OXCDATA] EntryID flags, [MS-OXPROPS] PidTag canonical names for 0x6633 / 0x66FA / 0x6635 / 0x6636).
2. **Phase B** — Per-PC schema builders: `buildMessageStorePc(...)`, `buildRootFolderPc(...)`, `buildSubfolderPc(...)`, `buildNameToIdMapPc(...)`. Each with a build-only test asserting structural HN correctness; `buildMessageStorePc` additionally byte-diffs against §3.10 / §3.8 HN bytes. EntryID encoder lives here as a private helper used by all PC builders.
3. **Phase C** — Per-TC schema builders: `buildFolderHierarchyTc(...)`, `buildFolderContentsTc(...)`, `buildFolderFaiContentsTc(...)`. Phase B+C unblock byte-diff oracles against §3.12.
4. **Phase D** — End-to-end M6 PST: extend `writeM5Pst` (or new `writeM6Pst`) to assemble the §2.7.1 mandatory-node set with proper NBT wiring. `pst_info` ALL CHECKS PASSED on a fully-mandatory-node-populated PST. Folder-graph invariant tests pass.
5. **Phase E** — Real-Outlook gate. Acquire an M6-compatible target sample (the M5-deferred Outlook → Add Data File path). Run the M5-defined "Real-Outlook validation plan" against the M6 PST AND against an Outlook-produced reference PST. Walk the 8 KNOWN_UNVERIFIED entries; record outcomes; close M6.

Each phase stops for review. Per-phase commits resume (M5 discipline).

### Pre-Phase-B verification queue (open questions to close before Phase B starts)

**Resolved during Phase A (2026-05-04):**
- ~~§2.7.1 *Mandatory Nodes*~~ — re-fetched, 27 nodes verbatim above.
- ~~§2.4.7 *Named Property Map*~~ — re-fetched (intro + 4-component layout); sub-page schemas (§2.4.7.1+) deferred to Phase B authoring.
- ~~M4 `buildTableContext` 0-row audit~~ — code review at [src/ltp.cpp:522-694](src/ltp.cpp#L522-L694) confirms `hnidRows = 0` and `RowIndex BTHHEADER.hidRoot = 0` when `rowCount = 0` (matches §3.12 evidence). **Caveat**: writer also emits two zero-sized HN allocations for HID 0x60 (RowIndex BTH leaf) and HID 0x80 (Row Matrix) regardless of rowCount. §3.12's prose dump for Contents/FAI (which have 0 rows) does NOT explicitly list HIDs 0x60 / 0x80. **Phase B fix-it candidate**: change writer to emit only HIDs 0x20 + 0x40 when `rowCount = 0` (skip the two zero-sized allocations). Risk: may break some intermediate readers that expect 4 allocations always; defer until M6 real-Outlook gate decides.

**Open — must close before Phase B:**

1. **§2.4.7.1+ stream sub-pages** — fetch each (Entry / GUID / String / hash) and pin the 4 PidTag IDs that comprise the Name-to-ID Map PC. Initial guesses listed in M6.5 are unverified.
2. **§2.4.8.x or [MS-OXOSRCH] Search-Contents template (0x0610) schema** — fetch and pin column list for `NID_SEARCH_CONTENTS_TABLE_TEMPLATE`.
3. **[MS-OXCMSG] Recipient (0x0692) and Attachment (0x0671) Table schemas** — fetch the column lists each Recipient / Attachment row contains. Match the M6.10 / M6.11 builder targets.
4. **NIDs 0x01E1 / 0x0201 "node" object content** — extract from a real Outlook PST; confirm minimum byte content (probably 4 zero bytes) and BBT entry shape. May not be derivable from spec text alone — may have to be a real-Outlook-gated empirical finding.
5. **[MS-OXCDATA] EntryID `rgbFlags`** — confirm all-zero is canonical or determine when non-zero is required.
6. **[MS-OXPROPS] PidTag canonical names** for 0x6633 (Boolean), 0x66FA (Int32) observed in §3.9 BTH bytes; 0x6635 / 0x6636 used in §3.12 Hierarchy TC.
7. **§3.12 sub-folder `nidParent` values** — §3.12 publishes only Root Folder's NBT entry; sub-folder `nidParent = 0x122` is inferred. Acquire a real-Outlook PST and confirm.
8. **`PidTagValidFolderMask` bitmask semantics** — §3.10 sample value `0x89` should decompose into named bits per [MS-OXCSTOR]. Verify before hard-coding `0x89` as M6 default.
9. **M4 `buildPropertyContext` Boolean encoding** — confirm `PidTagSubfolders` (PtypBoolean) emits `0x01 ‖ 00 00 00` per the M4 KNOWN_UNVERIFIED entry. Test against §3.12 Root Folder PC bytes (which are NOT yet on disk as a separate fixture).
10. **GUID generation strategy** — pick (a) caller-supplied / (b) `std::random_device` default / (c) deterministic-from-seed hybrid. Default to (a) + helper for (b).
11. **NID 0x2223 Search Folder PC schema** — extract from a real Outlook PST or re-read §2.4.8.x; pin property list.
12. **IPM Subtree Hierarchy "2 Rows" requirement** — §2.7.1 mandates 2 rows but doesn't list the children NIDs. Decide before Phase D: (a) emit 0 rows and trust Outlook tolerance, (b) auto-create 2 dummy NIDs not in §2.7.1 with their own PC + 3 sibling tables each (= 8 extra NIDs). Prefer (a) and validate at the real-Outlook gate; fall back to (b) if Outlook rejects.

### M6 risks carried forward

| Risk | Severity | Mitigation |
|---|---|---|
| Outlook rejects the M6 PST despite §2.7.1 nodes being populated | High | Real-Outlook gate is Phase E; if it fails, byte-diff against an Outlook-produced reference PST per the M5 closure validation procedure. |
| §2.7.1 enumerates more nodes than the §3.10 + §3.12 evidence suggests | Medium | Pre-Phase-B verification queue item 1; if extra nodes are needed, expand Phase D scope before declaring M6 done. |
| `buildTableContext` 0-row case produces wrong bytes | Low | Pre-Phase-B audit (queue item 8) catches this before Phase C. |
| EntryID `rgbFlags` semantics surface a non-zero requirement | Low | Pre-Phase-B verification queue item 4 catches this; M6 default is all-zero matching §3.10 evidence. |
| ProviderUID GUID strategy diverges from Outlook's expected behavior | Low | §3.10 evidence shows ANY 16-byte GUID is acceptable as long as it's used consistently across all EntryIDs in the PST. M6 enforces the consistency invariant. |

### M6 toolchain debt

Same items as M5 pre-flight:
- MSVC `/W4 /WX` clean build still pending (M5 Phase D ran but did not validate MSVC). M6 Phase D is the new natural moment, OR before M6 Phase B starts (preferred — catches signed/unsigned issues in new code at write time, not at end-of-milestone).
- MinGW upgrade (g++ 6.3 → modern) deferred to M7 (Hardening).



---

## M6 closure — Real-Outlook validation pass (backup.pst, 2026-05-04)

**Context**: a real Outlook-produced PST (`backup.pst`, 2.3 MB Unicode wVer=23, 243 BBT entries, 1 intermediate BBT page) was used as ground truth to resolve the 8 KNOWN_UNVERIFIED entries accumulated since M3. **NOTE**: this is parallel to but does NOT satisfy the M6 Phase E gate ("real Outlook opens m6_full_pst.pst"). Phase E remains pending until a Windows + Outlook environment is available.

### Step 1 — pst_info walkability

Header: dwMagic=`!BDN`, wVer=23 (Unicode), wMagicClient=`SM`, both header CRCs match.

Initial result: **`[FAIL]` — 50/243 BBT block CRCs verified, 193 mismatches.** This was a STOP signal: our reader rejected what Outlook produces. Investigation followed before resolving any KNOWN_UNVERIFIED entry.

### Step 1b — root-cause: real CRC scope bug

[src/block.cpp:103](src/block.cpp#L103) computed `dwCRC = crc32(buf, trailerOff)` (= cb + alignment-padding). [MS-PST] §2.2.2.8.1 verbatim: *"dwCRC: 32-bit CRC of the **cb bytes** of raw data"*. Scope must be `crc32(buf, cb)` — padding excluded.

Empirical confirmation via 5-block sample from backup.pst: 5/5 blocks match `cb`-only scope; 0/5 match `cb+padding` scope.

**Why our internal tests didn't catch it**:
- `[golden_spec_data_tree]` §3.6 XBLOCK has cb=432 = totalSize-16 (no padding). Both scopes coincidentally produce identical CRCs.
- All other internal tests use *self-consistent* CRCs (writer + reader use same buggy scope). Self-consistency masks the bug.
- §3.7 SLBLOCK / §3.5 BBT-leaf were the canaries — both flagged as KNOWN_UNVERIFIED with hypothesis "spec sample hand-edited". The real explanation: spec was correct; our writer was wrong.

**Fix applied** to [src/block.cpp](src/block.cpp), [tools/pst_info.cpp](tools/pst_info.cpp), and 6 test assertions. Diff is 1 line per call site (`trailerOff` → `cb` / `bodyBytes` / `cbPayload` / `t.cb`).

### Step 1c — post-fix re-check

- All 137 internal tests pass.
- §3.7 SLBLOCK byte-for-byte test now does FULL block (incl. dwCRC + bid in trailer) and passes.
- §3.6 XBLOCK byte-for-byte still passes (no behavior change for naturally-aligned blocks).
- M6 end-to-end (writeM6Pst) test still passes; pst_info on m6_full_pst.pst still ALL CHECKS PASSED.
- pst_info on backup.pst: BBT walked **243 entries, 243 block CRCs verified, 0 mismatches**. Remaining failures (14) are reader-side limitations (multi-block HN, hidIndex==0), not writer or CRC issues.

### Step 2 — Eight-entry resolution table

| Entry | Status | Evidence |
|---|---|---|
| §3.5 BBT-leaf dwCRC anomaly  | **VERIFIED** | 14/14 BBT leaves CRC-match after CRC fix. Anomaly was OUR bug. |
| §3.7 SLBLOCK dwCRC anomaly   | **VERIFIED** | Same root cause as §3.5. Full byte-for-byte against §3.7 now passes. |
| HNPAGEMAP DWORD-alignment    | **TOLERATED** | 48/48 sampled HNs are WORD-aligned; only 13/48 happen to be DWORD-aligned. Outlook uses WORD; our writer over-aligns to DWORD. Both produce structurally valid HNs. M4 reader tolerates real Outlook output. Optimization opportunity for M7+. |
| Row-major TC varlen ordering | **VERIFIED** | 2/2 multi-row TCs with varlen cols are strictly row-major. |
| Subnode NID stride +0x20     | **VERIFIED** | 12 multi-entry SLBLOCKs sampled; 90/90 within-nidType strides are +0x20. |
| Empty-PC hidRoot=0 sentinel  | **UNTESTED** | 0 empty PCs in 21 single-block PCs scanned. Real PSTs rarely contain truly-empty PCs. Would need a freshly-created PST with no activity. |
| PtypBoolean inline encoding  | **VERIFIED** | 44/44 zero-extended (upper 3 bytes = 0). Low byte: 32 false, 12 true. |
| M5 intermediate-BBT format   | **VERIFIED** | 1 intermediate BBT page (ptype=0x80, cLevel=1) in backup.pst. CRC matches under same scope as intermediate NBT. Format-shared claim from §3.3 holds. |

**Disagree count: 0.** No bug-fix work needed beyond the already-applied CRC scope fix.

**Untested count: 1** (empty-PC sentinel). To resolve: acquire a freshly-created Outlook PST (Outlook → File → New → Outlook Data File) with no folders or activity beyond the §2.7.1 minimum.

### Step 3 — KNOWN_UNVERIFIED.md updated

See [KNOWN_UNVERIFIED.md](KNOWN_UNVERIFIED.md) "Real-Outlook validation pass (backup.pst, 2026-05-04)" section for per-entry evidence.

### M6 Phase E status

This pass does NOT satisfy M6 Phase E. Phase E specifically requires:
- Open `m6_full_pst.pst` in classic Outlook on Windows
- Outlook accepts it without "detected a problem with this Outlook Data File" or scanpst warnings
- The 8 best-guess M6.5–M6.11 schemas (Search Folder PC, NameToIdMap empty state, Recipient/Attachment/SearchContents templates, bare-node payload, IPM Subtree Hierarchy 2-row vs 0-row) all surface real-Outlook tolerance / rejection

Phase E remains pending. The CRC scope fix landed today substantially de-risks Phase E (Outlook would have rejected every M6 PST we produced before today due to wrong block CRCs).

### MSVC `/W4 /WX` cleanup status

**Still pending.** Has not been addressed during M5 Phase D, M6 Phase B, M6 Phase D, or this validation pass. M5 closure deferred it; M6 Phase A reaffirmed the deferral. Outstanding for M7 (Hardening) at the latest.

### Retrospective (one paragraph)

The validation pass discovered a 1.5-year-old CRC scope bug that had been hiding in plain sight, masked by test self-consistency. Three KNOWN_UNVERIFIED entries that had been logged with the hypothesis "the spec must be hand-edited" (§3.5, §3.7 anomalies + §3.6 working as the lone positive control) all flipped from anomaly to verified once the root cause was identified — the spec was right; our writer had been wrong since M3. Empirical validation against a 2.3-MB real Outlook PST resolved 6 entries cleanly, surfaced 1 over-alignment (HNPAGEMAP) as a benign tolerance, and left only 1 untested (empty-PC sentinel, requires a different sample shape). The pattern that pre-registered single-sample assumptions at inference time (per the M3-era discipline) paid off: every entry was already framed as a yes/no test against external evidence, so the resolution took ~2 hours of probe code + spec re-reads rather than a milestone of speculative refactoring.

### CRC-scope bug retrospective (added 2026-05-04, post-fix)

**Affected scope: M2 through M6** produced PSTs with wrong block dwCRC
values whenever a block's payload size wasn't naturally aligned to
`64*n - 16`. That covers the great majority of blocks in any non-trivial
PST. Every PST we shipped before commit `5c4a5c6` would have been
rejected by Outlook's CRC verification at open time.

**Critical caveat about ALL CHECKS PASSED results before commit `5c4a5c6`**:
they were self-consistent confirmations of a shared writer/reader bug,
NOT validation against external ground truth. pst_info computed the
same wrong scope as the writer (`crc32(blk, totalCb - kBlockTrailerSize)`),
so writer-produced blocks always self-verified. The bug only surfaced
when verifying a block produced by an *independent* writer (Outlook's),
which is what the backup.pst real-Outlook validation finally did.

**Why this matters for M7-M9 design**:

The writer/reader independence contract — the implicit promise that the
reader provides an independent verification path against the writer —
**failed**, because both shared the same buggy CRC primitive. The
contract was structural fiction, not enforced separation.

**Shared primitives are shared risk.** Concrete examples that may surface
during M7-M9:
- Any byte-encoding helper called by both writer and reader (e.g., RTF
  compression, internet-message-headers serialization).
- Any value-conversion function (Graph time → FILETIME, ASCII → UTF-16-LE,
  Graph attachment-method enum → PidTagAttachMethod).
- Any inline-vs-HN-vs-subnode storage decision used by both PC builder
  and PC reader.

**Pattern to apply during M7-M9**:
1. When adding any new shared primitive, write at least one test that
   exercises it via a path *different* from the writer/reader pair —
   e.g., compare against published spec sample bytes, or against a
   trace from a third-party reader (libpff, mfcmapi).
2. Vary test conditions across positive controls. Do not rely on a
   single sample to prove a class of correctness; § conditions matter.
3. If a test you control disagrees with another test you control, do
   not conclude "the spec must be wrong" — investigate your own
   primitives first.
4. Treat any "this should work but doesn't, must be hand-edited"
   hypothesis as a warning sign. Validate the primitive before
   accepting the hypothesis.

**Concrete remediation list (open items as of M6 closure)**:

| Item | Status | Notes |
|---|---|---|
| All M2-M6 internal tests pass under correct CRC scope | ✓ done | 137/137 in commit `5c4a5c6` |
| pst_info on backup.pst matches all 243 block CRCs | ✓ done | post-fix |
| pst_info on m6_full_pst.pst still ALL CHECKS PASSED | ✓ done | post-fix |
| MSVC `/W4 /WX` clean build | pending | Track 2 of pre-M7 cleanup |
| Phase E real-Outlook validation of m6_full_pst.pst | pending | requires Windows + Outlook |
| Empty-PC `hidRoot=0` sentinel verification | pending (UNTESTED) | requires fresh-Outlook PST |

The CRC-scope finding elevated the urgency of MSVC `/W4 /WX` cleanup —
deferred since M3 because "MinGW is sufficient", but MinGW's looser
warnings missed the same class of issue that hid the CRC bug for 6+
milestones. Going forward, both MinGW and MSVC must be green as gate
items for every milestone.

### Track 2 — MSVC `/W4 /WX` cleanup: DEFERRED to M10 (Production hardening), 2026-05-04

**Decision**: defer MSVC dual-toolchain cleanup to **M10 (Production
hardening)**.

**This is the SIXTH deferral** of MSVC cleanup (M3, M4, M5, M6,
pre-M7 cleanup pre-attempt, now to M10). The deferral chain has been
recognized as a recurring pattern. **Future deferrals beyond M10
require explicit user reauthorization** rather than continuing
implicitly. M10 = re-attempt is not optional; if M10 arrives and
MSVC is still not installable, escalate to manual user action rather
than further defer.

**Rationale recorded for this round**:

- **Time pressure on Aspose.Email replacement deliverable** is the
  dominant constraint. M7-M9 (mail / contacts / calendar) is the
  full deliverable scope; another ~hour blocking M7 entry on a
  bounded-yield cleanup is the wrong trade-off.
- **MinGW with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion
  -Werror` provides primary warning coverage**. 137/137 tests pass
  under that gate. Marginal yield from MSVC is bounded: a few
  format-string mismatches, finer signedness checks, a few
  uninitialized-variable patterns. Real but ~5-30 warnings,
  mechanical.
- **Validation effort prioritized toward real-Outlook gate work
  when access becomes available**. The CRC-scope bug
  (commit `5c4a5c6`) — the canonical example of the kind of bug
  MSVC strictness exists to catch — was actually caught by EXTERNAL
  ground-truth validation (backup.pst, real Outlook PST), not by
  any toolchain warning. Real-Outlook validation is the stronger
  oracle for this project's failure modes.
- **VS Build Tools 2022 install via `winget` reported success but
  did not materialize the toolchain** (`vcvars64.bat` absent,
  `vswhere` returns empty registration). Most likely cause: the
  multi-GB workload install required an interactive elevation that
  didn't auto-resolve under non-interactive shell context. Per
  project-wide rule ("if installation requires user action, stop
  and report — do not attempt workarounds"), the install was halted.

**Risk acknowledged**:

- Shipping code that has not been compiled under MSVC. Future
  engineers using Visual Studio will likely encounter warnings on
  first build.
- **Estimated cleanup effort when eventually performed: 0.5 to 1 day.**
  Drift accumulates with each milestone of additional code (M7-M9
  add ~3-5x more source); the ratio of cleanup-time-to-codebase-size
  may grow more than linearly if the warning categories diverge
  across new code patterns.
- A class of bugs MSVC catches and MinGW does not (some
  uninitialized-variable patterns, MSVC-specific narrowing checks)
  may sit undetected in M7-M9 code until M10 cleanup. These would
  not be runtime-fatal under MinGW but could be on MSVC.

**Compensating commitment**:

Each M7-M9 milestone closure runs:
1. **`pst_info` structural validation** against the milestone's
   produced PST (m7_full_pst.pst, m8_contacts.pst, m9_calendar.pst,
   etc.) — block CRCs, page CRCs, BBT/NBT walks, HN body inspection.
2. **(When Outlook access available) opens-in-Outlook test**
   against the same PST.

This is the safety net that caught the CRC scope bug. It is not a
substitute for MSVC strictness on warning-class bugs, but it is
substantially stronger on logic-class bugs.

**Deferral history (for accountability)**:

| Milestone boundary | Decision | Rationale |
|---|---|---|
| End of M3 | Deferred | "fix later, focus on block writer" |
| End of M4 | Deferred | "M5 priority, fix in M5 toolchain debt action plan" |
| End of M5 (Phase D) | Deferred | "concentrate MSVC remediation in batch at M5 closure" |
| End of M6 (Phase D) | Deferred | "M6 deliverable pressure, M7 Phase D will retry" |
| Pre-M7 cleanup attempt (today) | Attempted, failed silently | winget reported success but toolchain didn't materialize |
| **Now** | **Deferred to M10** | Aspose deliverable pressure; explicit acknowledgment that the chain has gone six deferrals deep |

**MinGW status (unchanged)**: 137/137 tests pass under
`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`. That gate
remains the standing toolchain check until MSVC is added at M10.

**Failed install evidence kept for next attempt** (not committed):
- `.tmp/winget_vsbt.log` — full winget output showing
  "Successfully installed" despite incomplete materialization.
- `vswhere.exe` was deployed (Installer infrastructure exists), but
  the BuildTools workload itself was not installed.
- Suggests the manual GUI install at https://visualstudio.microsoft.com/visual-cpp-build-tools/
  will be the M10 retry path rather than another `winget --passive` attempt.

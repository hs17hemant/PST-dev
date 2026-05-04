# KNOWN_UNVERIFIED.md

Log of guesses we made during pstwriter development that **real Outlook
would resolve**. When `tests/golden/empty.pst` (or any equivalent
ground-truth file) is finally produced by Outlook, this is the diff list
to check first.

## Real-Outlook validation pass (backup.pst, 2026-05-04)

Resolved against a real Outlook-produced PST (2.3 MB Unicode, wVer=23,
243 BBT entries, 1 intermediate BBT page). 6 of 8 entries Verified, 1
Tolerated, 1 Untested, 0 Disagree.

| Entry | Status | Evidence summary |
|---|---|---|
| §3.5 BBT-leaf dwCRC anomaly  | **VERIFIED** | 14/14 BBT leaves CRC-match after the M3-era CRC scope bug was fixed (commit pending). Anomaly was OUR writer using `crc32(blk, trailerOff)` instead of `crc32(blk, cb)` per [SPEC §2.2.2.8.1]. §3.5 sample was correct all along. |
| §3.7 SLBLOCK dwCRC anomaly   | **VERIFIED** | Same root cause as §3.5. After fix, `[golden_spec_slblock]` does full byte-for-byte (incl. dwCRC) and passes. §3.7 sample was correct all along. |
| HNPAGEMAP DWORD-alignment    | **TOLERATED** | Sample of 48 single-block HNs in backup.pst: ibHnpm WORD-aligned in 48/48, DWORD-aligned in only 13/48. Real Outlook uses WORD (2-byte) alignment, NOT DWORD. Our writer over-aligns to DWORD — produces structurally valid HNs (DWORD-aligned ⇒ also WORD-aligned), just with 1-3 bytes of extra padding per HN. M4 reader accepts WORD-aligned ibHnpm without issue (no alignment-related failures in pst_info backup.pst walk). Optimization opportunity for M7+. |
| Row-major TC varlen ordering | **VERIFIED** | 2/2 multi-row TCs with variable-size columns in backup.pst lay out HN allocations in strict row-major order (all of row N's HIDs > all of row N-1's HIDs). |
| Subnode NID stride +0x20     | **VERIFIED** | 12 multi-entry SLBLOCKs sampled. Within-nidType stride: +0x20 in 90/90 cases. Cross-nidType strides (~0x7000-0x8500, 11 cases) are not "strides" in the M4 sense — they're nidType boundary changes where the low 5 bits flip. Inside any single nidType counter, +0x20 (= idx +=1) is the canonical stride. |
| Empty-PC hidRoot=0 sentinel  | **UNTESTED** | 0 empty PCs found in 21 single-block PCs scanned. Real-Outlook PSTs rarely have totally-empty PCs (every PC carries at least DisplayName + a few mandatory props). Would need a freshly-created Outlook PST with no message activity to surface this case. Reader-side compatibility verified: M4 `readPropertyContext` correctly returns `[]` for hidIndex==0 inputs (synthetic round-trip). |
| PtypBoolean inline encoding  | **VERIFIED** | 44/44 PtypBoolean properties zero-extended (upper 3 bytes = 0). Low byte distribution: 32 false, 12 true. Matches our M4 writer's output exactly. |
| M5 intermediate-BBT format   | **VERIFIED** | backup.pst has exactly 1 intermediate BBT page (ptype=0x80, cLevel=1). Page CRC matches under same scope as intermediate NBT. The "format shared with NBT-intermediate" claim from §3.3 holds for real Outlook PSTs. |

**No Disagree resolutions.** All 6 Verified entries can be promoted to
"locked" status. The 1 Tolerated and 1 Untested entries remain open,
both with documented next steps.

### Significant finding from this pass

A **real CRC-scope bug** was discovered before the entry-resolution pass began. [src/block.cpp](src/block.cpp) computed `dwCRC = crc32(buf, trailerOff)` (over payload + alignment-padding); [SPEC §2.2.2.8.1] says scope is `cb` bytes only. All M2/M3/M5/M6 PSTs we produced had wrong block dwCRC values for any payload not naturally 64-byte-aligned. Internal byte-diff oracles (§3.6) didn't catch it because §3.6 happens to have cb == trailerOff (no padding). Fix applied to [src/block.cpp](src/block.cpp), [tools/pst_info.cpp](tools/pst_info.cpp), and 6 test assertions.

After fix:
- pst_info on backup.pst: 50/243 → **243/243** block CRCs verified
- `[golden_spec_slblock]` flipped from "self-consistent CRC under wrong scope" to **full byte-for-byte against §3.7 including dwCRC**
- All 137 internal tests pass
- M6 end-to-end (writeM6Pst) still produces structurally-valid PSTs



Each row format:
- **Where**: file:line where the guess lives.
- **What we wrote**: the chosen value or behaviour.
- **Alternatives**: other plausi ble values from the spec or from libpff/mfcmapi.
- **Why we picked it**: brief reason.
- **Catches it**: which existing test/oracle would fail if we're wrong.

## M2 — empty 5-page skeleton

| Where | What we wrote | Alternatives | Why we picked it | Catches it |
|---|---|---|---|---|
| [src/writer.cpp:30](src/writer.cpp#L30) | `bidNextP = Bid::makeInternal(4)` (= 0x13) | `0x10`, `0x14`, any value > used BIDs | First unused internal BID after AMap/NBT/BBT got 0x07/0x0B/0x0F | Real Outlook open / scanpst |
| [src/writer.cpp:31](src/writer.cpp#L31) | `bidNextB = Bid::makeData(1)` (= 0x04) | `0x10`, `0x100`, etc. | No blocks allocated yet; smallest legal data BID | Outlook open |
| [src/writer.cpp:34](src/writer.cpp#L34) | `dwUnique = 1` | random GUID-style value, monotonic timestamp | Spec only requires non-zero monotonic; 1 is legal | Outlook open |
| [src/ndb.cpp `writePageTrailer`](src/ndb.cpp) | AMap PAGETRAILER `bid` = page's own BID (= 0x07) | `0` (per SPEC_BRIEF) | SPEC_GROUND_TRUTH default + libpff agrees | Outlook open / scanpst /AMap-walk |
| [src/writer.cpp:50](src/writer.cpp#L50) | `cbAMapFree = 251392` (AMap-coverage minus allocated) | `0` (real-file-free); `253952 - bytesUsed` rounded | SPEC_GROUND_TRUTH says "total free bytes advertised by AMaps"; this matches | Outlook open / Outlook computes its own free-space metrics |
| [src/writer.cpp:36](src/writer.cpp#L36) | NBT/BBT/AMap pages use `Bid::makeInternal()` | `Bid::makeData()` (no internal flag) | Pages are always "internal" per BID flag semantics | Outlook open |
| [include/pstwriter/types.hpp:117-126](include/pstwriter/types.hpp) | `kHdrCrcPartialLen = 471`, `kHdrCrcFullLen = 516` | 464 / 484 (SPEC_BRIEF originally), 471 / 484 (libpff) | SPEC_GROUND_TRUTH §3.2 sample CRCs reproduce exactly with these lengths | `[golden_spec_header]` test (PASSING) |
| [src/ndb.cpp `serializeHeader`](src/ndb.cpp) | rgbFM[128] and rgbFP[128] = all 0xFF | all 0x00 | SPEC_GROUND_TRUTH + spec sample shows 0xFF | `[golden_spec_header]` test (PASSING) |
| [src/ndb.cpp `serializeHeader`](src/ndb.cpp) | `dwReserved1 = 0`, `dwReserved2 = 0`, `bidUnused = 0` for fresh PST | non-zero scratch values like the §3.2 sample shows | Spec says "reserved, set to 0 by creators" | None directly; harmless |

## M3 — block writer

| Where | What we wrote | Alternatives | Why we picked it | Catches it |
|---|---|---|---|---|
| [include/pstwriter/ndb.hpp `computeBlockSig`](include/pstwriter/ndb.hpp) | wSig formula = 32-bit truncate, then `(mix>>16) ^ (mix&0xFFFF)` | (a) naive `(ib^bid) & 0xFFFF` — wrong; produces `0x0046` for §3.5 (b) 64-bit 4-way fold `(mix>>48)^(mix>>32)^(mix>>16)^mix` (c) libpff's 3-way fold | Matches §3.5 sample's wSig=0x00D6 exactly. The 4 variants only diverge if file > 4 GB; below that all of (a-corrected, b, c, ours) agree | `[ndb][block][sig]` test pins `(0x246, 0x900200) → 0x00D6` |
| [src/writer.cpp `buildAndWriteBlocksPst`](src/writer.cpp) | Block layout order: `HEADER → pad → AMap → blocks → BBT leaves → BBT root → empty NBT leaf → EOF` | (a) `… → blocks after BBT/NBT` (b) `… → NBT before BBT` | Spec doesn't mandate; libpff/Outlook commonly put blocks adjacent to the AMap | Outlook open. pst_info is order-agnostic |
| [src/writer.cpp:writeBlocksPst](src/writer.cpp) | Block BIDs allocated as `Bid::makeData(i+1)` for i in 0..N-1 (= 0x04, 0x08, …) | start higher (0x10, 0x100), randomize, gap-fill | Smallest legal data BIDs, monotonic, predictable for tests. Real Outlook uses larger starting BIDs | Outlook open |
| [src/writer.cpp:writeBlocksPst](src/writer.cpp) | BBT leaf/root page BIDs allocated as `Bid::makeInternal(2..)` after the AMap's 0x07 | use distinct ranges per page kind, randomize | Sequential. Spec only requires uniqueness | Outlook open |
| [src/writer.cpp:writeXBlockPst](src/writer.cpp) | XBLOCK BID = `Bid::makeInternal(N+1)` where N = data block count | any unused internal BID | Cheap monotonic allocation | Outlook open |
| [src/writer.cpp:writeBlocksPst](src/writer.cpp) | NBT remains empty in M3 (no NBTENTRYs registered) | one synthetic NID per block | M3 scope is BBT/block format; NID/node mapping arrives in M5. Outlook will reject this as "orphan blocks" but pst_info doesn't care | Outlook open (will fail) |
| [src/block.cpp `appendBlockTrailer` / `buildInternalBlock`](src/block.cpp) | BLOCKTRAILER.cb for internal blocks = body size (header 8 + entries × cEnt), excluding padding/trailer | include trailer-pad in cb | Spec §1.3.2.5: "cb is the bytes of raw data NOT INCLUDING the BLOCKTRAILER". Internal blocks have no padding-as-data semantic, so the natural cb is the structured body size | scanpst / Outlook |
| [src/block.cpp](src/block.cpp) | Internal blocks (XBLOCK/XX/SL/SI) are NOT encrypted; only data blocks are encrypted | encrypt internals too | Spec §1.3.1.5 says encryption applies to "data blocks". libpff agrees | scanpst |
| [tests/test_block.cpp `[golden_spec_bbt_leaf]`](tests/test_block.cpp) | The §3.5 BBT-leaf sample test does NOT compare the trailer's `dwCRC` byte-for-byte | strict byte-for-byte comparison | The spec's published `dwCRC=0xA1F6A02F` does NOT match `crc32(its-own-first-496-bytes)` under our PST CRC-32 (verified by §3.2 header end-to-end). The §3.5 sample also has BREF entries with 64-byte-misaligned IBs (e.g. ib=0x20B) — strongly suggests Microsoft hand-edited the dump for illustration without regenerating the CRC. Our test verifies bytes [0..499] (page body + ptype/ptypeRepeat/wSig), the wSig landmark (0x00D6), the bid (0x246), and self-consistent dwCRC | None — until a real Outlook BBT page lands in `tests/golden/`, this is the gap |

### Spec-sample dwCRC anomalies (§3.5 BBT leaf and §3.7 SLBLOCK) — RESOLVED 2026-05-04

**Status: RESOLVED. Original hypothesis was WRONG. Root cause was a CRC
scope bug in our writer/reader, fixed in commit `5c4a5c6`.**

[MS-PST] published spec samples at:
- §3.5 *Sample Leaf BBT Page* — stored dwCRC `0xA1F6A02F`
- §3.7 *Sample SLBLOCK* — stored dwCRC `0xD9D45E50`

were both correct all along. Our writer was computing `dwCRC = crc32(buf,
trailerOff)` (where `trailerOff = totalSize - 16` = cb + 64-byte alignment
padding) instead of `crc32(buf, cb)` per [MS-PST §2.2.2.8.1] verbatim:
*"dwCRC: 32-bit CRC of the **cb bytes** of raw data"*.

**Resolution evidence**:
- After commit `5c4a5c6` fixed [src/block.cpp](src/block.cpp), the §3.7 SLBLOCK byte-diff test was upgraded from "[0..52) + self-consistent CRC under wrong scope" to **full byte-for-byte against §3.7's published 64 bytes including stored dwCRC + bid in trailer**, and it passes.
- pst_info on backup.pst (real Outlook PST): 50/243 → **243/243** block CRCs verified after fix.
- §3.6 XBLOCK still passes: it had `cb = totalSize - 16 = 432` (no padding), so both the buggy and correct CRC scopes produce identical output. This coincidence is why §3.6 was a positive control under the wrong scope.

**Why the original hypothesis was wrong**:

The reasoning chain that "§3.6 XBLOCK CRC matches → therefore our CRC code is correct → therefore §3.5/§3.7 must be hand-edited" had a hidden assumption: that §3.6 exercised every CRC code path. It didn't. §3.6 happened to have no alignment padding, so the buggy scope and correct scope produced bitwise-identical CRCs. The bug was invisible under §3.6's specific test conditions.

The "BREF entries with 64-byte-misaligned IBs (e.g. ib=0x20B)" observation in §3.5 was correct evidence of *something*, but we drew the wrong conclusion. It was evidence that §3.5 was a partial dump (BBTENTRY records were edited to fit on a sample page), not evidence that the CRC was hand-edited. The CRC was real.

**Tests that now lock the §3.x oracles in** (all PASSING):
- `[golden_spec_header]` — §3.2, full byte-for-byte
- `[golden_spec_nbt_leaf]` — §3.4, parse + CRC + wSig
- `[golden_spec_bbt_leaf]` — §3.5, body + wSig + self-consistent CRC (NOTE: §3.5 still excludes byte-for-byte trailer comparison because of the misaligned BREF IBs — those WERE genuine spec-edits, separate from the CRC issue)
- `[golden_spec_data_tree]` — §3.6, full byte-for-byte XBLOCK round-trip
- `[golden_spec_slblock]` — §3.7, **full byte-for-byte including dwCRC + bid trailer** (upgraded post-fix)

### Methodology lesson (2026-05-04, learned from this resolution)

**A single positive control under one set of conditions does not rule out
a class of bugs; it rules out a class of bugs under those specific
conditions.**

§3.6 XBLOCK happened to have `cb = totalSize - 16` with no alignment
padding, so the buggy CRC scope `crc32(buf, trailerOff)` coincidentally
produced the same value as the correct scope `crc32(buf, cb)`. §3.5
BBT-leaf and §3.7 SLBLOCK had non-zero padding and exposed the bug — but
the wrong hypothesis ("Microsoft hand-edited illustrative samples") hid
the root cause for **6+ milestones** (M3 through M6) and shipped a
broken writer past every internal test gate.

**Lesson for future positive-control tests**:
- Vary test conditions across positive controls. Do not rely on a single
  spec sample as evidence that a class of behavior is correct.
- If two methods produce matching output under condition X, but you
  haven't tested under condition Y, you have NOT proven correctness —
  you've proven they happen to agree under X.
- When a test you control (§3.5/§3.7) disagrees with a test you also
  control (§3.6), the more parsimonious explanation than "the spec is
  wrong" is "I'm wrong, and the disagreement is the signal".
- Self-consistency between writer and reader is NOT validation. If both
  share the same buggy primitive, internal round-trip tests pass while
  external compatibility fails. **Shared primitives are shared risk**;
  every cross-validation oracle should ideally use a fully-independent
  computation path.

**Concrete debt incurred by this lesson**: M2 through M6 produced PSTs
with wrong block dwCRCs whenever payload size wasn't naturally 64-byte
aligned. ALL CHECKS PASSED results before commit `5c4a5c6` were
self-consistent confirmations of a shared bug, not validation against
external ground truth.

**Application during M7-M9**: When adding new builders, watch for shared
primitives that might propagate the same wrong assumption to writer and
reader simultaneously. When in doubt, vary test conditions across
positive controls. Treat any "this spec sample must be wrong" hypothesis
as a red flag — verify the writer's own primitives first.

## M5 audit (2026-05-02, M5 closure)

M5 introduced ONE new KNOWN_UNVERIFIED entry (intermediate-BBT format,
pre-flight). The seven M4-era entries remain open and now gate on M6
(not M5 Phase E — see MILESTONES.md "Phase D mandatory-nodes deferral"
subsection: M5 produces PSTs that Outlook will reject because the
§2.7.1 mandatory-nodes set is incomplete; M6 is the milestone where
Outlook can actually attempt to open produced PSTs).

| Entry | M5 status | Now gated on |
|---|---|---|
| §3.5 BBT-leaf dwCRC anomaly       | Open (since M3) | M6 real-Outlook |
| §3.7 SLBLOCK dwCRC anomaly        | Open (since M3) | M6 real-Outlook |
| HNPAGEMAP DWORD-alignment         | Open (since M4) | M6 real-Outlook |
| Row-major TC variable-size order  | Open (since M4) | M6 real-Outlook |
| PtypBoolean inline encoding       | Open (since M4) | M6 real-Outlook |
| Subnode-NID stride (`+= 0x20`)    | Open (since M4) | M6 real-Outlook |
| Empty-PC `hidRoot==0` convention  | Open (since M4) | M6 real-Outlook |
| **M5 intermediate-BBT format**    | **Open (NEW, pre-flight)** | M6 real-Outlook (extract one BBT-intermediate page) |

**No new entries added during Phases A-E** beyond the pre-flight
intermediate-BBT entry. Phase A (allocator), B (BTPAGE writer),
C (NBT reader), D (end-to-end PST), E (semantic decodes) all produced
no single-sample empirical findings — every M5 design choice was
either a hard SPEC-pinned rule or a [DESIGN] decision documented in
MILESTONES.md.

The eight-entries-one-gate concentration (M5's pre-flight finding
about M4 entries) extends cleanly: all 8 resolve at M6's real-Outlook
gate. M6 is therefore an unusually high-leverage milestone for the
project's empirical-finding accumulation.

## M4 audit (2026-05-02, M4 closure)

Consolidated resolution status for every M4 entry below. **Seven entries
all gate on the same condition** — landing one real Outlook-produced PST
in `tests/golden/`. That concentration is itself an M4 finding: it
collapses seven independent unknowns into one downstream gate item, which
M5 is the natural place to clear (M5 is the first milestone that produces
PSTs Outlook should actually accept).

| Entry | Resolution status | Real-Outlook gate? |
|---|---|---|
| §3.5 BBT-leaf dwCRC anomaly | **Open**. Strong indirect evidence (§3.2 + §3.6 CRCs match) that our CRC code is correct and the spec sample is hand-edited. | Yes — confirmed when one real BBT page byte-diffs cleanly. |
| §3.7 SLBLOCK dwCRC anomaly | **Open**. Same pattern as §3.5; same indirect evidence chain. | Yes — confirmed when one real SLBLOCK byte-diffs cleanly. |
| HNPAGEMAP DWORD-alignment | **Open**. Production code (`buildHeapOnNode`) exercises the rule correctly; round-trips §3.8 (naturally aligned) AND §3.11 (1 byte of pad). Internally consistent across both samples. | Yes — confirmed when a real-Outlook HN with a non-naturally-DWORD-aligned final allocation byte-diffs cleanly. |
| Row-major TC variable-size value ordering | **Open**. Regression oracle in place via `[golden_spec_tc]`; reader is HID-agnostic so cannot regress on alternate Outlook orderings. | Yes — confirmed when a real-Outlook TC with multiple varlen rows byte-diffs cleanly (or revised to "Outlook varies" if it doesn't). |
| PtypBoolean inline encoding (zero-extended) | **Open**. Synthetic round-trip exercises the writer choice; reader decodes per `propType` so already tolerates either reading. | Yes — confirmed when a real-Outlook PC with a Boolean property byte-diffs cleanly. |
| Subnode-NID allocation stride (`+= 0x20`) | **Open**. Single-subnode synthetic test does not exercise stride at all; the rule is unobservable until M5+ produces multi-subnode PCs. | Yes — confirmed when a real-Outlook PC with multiple subnode-promoted properties is available. |
| Empty-PC `hidRoot == 0` reader convention | **Open**. Reasoned interpretation of [MS-PST] §2.3.1.1 + §2.3.2.1; reader handles both interpretation (A) and (B) gracefully. | Yes — confirmed when a real-Outlook empty PC is produced. |
| Subnode-NID `hidType != NID_TYPE_HID` discriminator | **Resolved — promoted to MILESTONES.md as a hard reader-invariant** (see "Reader invariants enumeration"). [SPEC §2.3.3.2] publishes the rule explicitly; not an empirical guess. | N/A — was never an empirical entry. |

**Why this concentration is the M5 high-leverage gate**: seven KNOWN_UNVERIFIED items resolve simultaneously the moment one real Outlook PST lands. Until then, the writer's internal consistency is testable but its agreement with Outlook's actual encoding is not. The defensive discipline of pre-registering each entry at inference time (rather than at contradiction time) means each of the seven is ready to move to "Verified" or "Disagree — fix as follows" without re-investigation.

## M4 — LTP layer spec samples (transcribed 2026-05-02)

| Sample | File | Size | CRC self-consistency | Notes |
|---|---|---|---|---|
| §3.8 HN | `tests/golden/spec_sample_hn.bin` | 258 B | **N/A** — spec dump shows HN structured body only, no BLOCKTRAILER | Single positive control: HNHDR + 8 allocs + HNPAGEMAP shape will be the byte-diff oracle for `buildHeapOnNode(...)` once it exists. |
| §3.9 BTH | `tests/golden/spec_sample_bth.bin` | 258 B | **N/A** — same physical block as §3.8 | Per spec text: "this example uses the same binary dump from the last example to further examine the inner BTH structure". The BTH lives at HID 0x40 inside the §3.8 HN. We keep both files for naming clarity but they are byte-identical (`cmp` confirms). |
| §3.11 TC | `tests/golden/spec_sample_tc.bin` | 464 B | **N/A** — spec dump shows TC/HN body only, no BLOCKTRAILER | Will be the byte-diff oracle for `buildTableContext(...)`. Encodes 13 columns + 2-row Row Matrix + RowIndex BTH + variable-size string allocations + HNPAGEMAP. The toughest M4 oracle. |

**Why CRC self-consistency is N/A for these three:** the [MS-PST] §3.8 /
§3.9 / §3.11 spec pages publish only the structured HN body (HNHDR +
heap allocations + HNPAGEMAP). They do NOT include the surrounding
BLOCKTRAILER, so there is no stored `dwCRC` to compare against. Our
M4 round-trip tests must therefore compare structured bytes only and
attach a real BLOCKTRAILER computed by our `buildDataBlock(...)` /
`appendBlockTrailer(...)` path (which is already validated end-to-end
by the §3.6 XBLOCK oracle).

This is **different** from the §3.5 / §3.7 anomaly above, where the
spec DID publish a stored dwCRC and that stored value disagrees with
our computation. For §3.8 / §3.9 / §3.11 there's nothing to disagree
with.

Tests that lock the M4 §3.x oracles in:
- `[golden_spec_hn]` — §3.8, full byte-for-byte HN round-trip via `buildHeapOnNode(...)` (PASSING)
- `[golden_spec_bth]` — §3.9, full byte-for-byte HN-with-BTH round-trip via `buildHeapOnNode(...)` + `encodeBthHeader(...)` (PASSING)
- `[golden_spec_tc]` — §3.11, full byte-for-byte TC HN round-trip via `buildHeapOnNode(...)` with `bClientSig=0x7C` (PASSING)
- `[synthetic_pc_composition]` — synthetic-PC layer-composition oracle (SKIPPED, awaiting `buildPropertyContext(...)` + reader path)

### M4 TC variable-size value ordering: row-major (pre-registered)

**Status: pre-registered before implementation, basis is a single
spec sample. Mark as confirmed when a real Outlook TC corroborates,
or revise the writer if a counter-example arrives.**

The §3.11 *Sample TC* dump stores variable-size column values
(string/binary referenced from row cells) in **row-major** order:
row 0's varlen values appear first (in column order), then row 1's,
then row 2's. Specifically, the three folder-name strings ("Top of
Personal Folders", "Search Root", "SPAM Search Folder 2") occupy
HIDs 0xA0, 0xC0, 0xE0 in that row order, not column-then-row.

This is a single observation. Outlook may also write column-major
("all of column X's varlen values, then column Y's") — the spec text
in §2.3.4.4 does not mandate either ordering, only that each row
cell's `ibData` slot points (via HID) to the correct allocation.

**Decision for `buildTableContext`** (M4 first cut): write
**row-major** to match the §3.11 evidence. This makes our output
byte-equal §3.11 and any other row-major-extracted TC. If a real
Outlook PST surfaces with column-major TCs, we have two paths:

1. Add a `TcOrdering` knob to `buildTableContext` (cheap; reader
   doesn't care because it follows HIDs, not order).
2. Promote this entry to a confirmed quirk: "Outlook varies; row-
   major is the more common pattern; we emit row-major."

**Reader-side invariant (locked now):** the LTP reader path
(`readPropertyContext` and the M5 TC reader) MUST be HID-driven, not
order-driven. It walks each row, reads the cell's `ibData` field,
treats that as an HID, and resolves the HID against HNPAGEMAP — never
assuming "the i-th varlen value lives at HID 0xA0 + 0x20 × i" or
similar. This ensures we stay compatible with any ordering Outlook
chooses, even if our writer commits to one.

**Confirmation gate:** once a real Outlook-produced TC is available
in `tests/golden/`, byte-diff it against `buildTableContext(...)`
output for the same logical content. If row order matches, this
entry can be deleted. If it doesn't, expand to "Outlook varies" and
add the knob.

### M4 PC writer: PtypBoolean inline encoding (zero-extended to 4 bytes)

**Status: pre-registered before real-Outlook validation.**

[MS-PST] §2.3.3.3's storage table publishes the rule "fixed-size,
cb ≤ 4 → Data Value (inline)" but does not pin the byte layout for
the 1-byte Boolean case. Two readings:

- **(A)** Boolean stored as 1 raw byte in the low byte of the 4-byte
  dwValueHnid slot, upper 3 bytes zero. Standard interpretation;
  matches libpff. Our writer does this.
- **(B)** Boolean stored as a 4-byte BOOL (`!= 0` is true), so any
  bit pattern with low byte ∈ {0, 1} is canonical and the upper
  three bytes are also valid as full-zero. Functionally equivalent
  to (A) but allows the upper bytes to be non-zero garbage.

**Decision for `buildPropertyContext`** (Phase B): emit (A) — write
1 raw byte at offset 0 of the 4-byte slot, upper 3 bytes zero. If
Outlook produces (B)-style garbage in the upper bytes for some
samples, our reader (Phase C, HID-agnostic, decodes per propType not
per bit pattern) will still extract the right value.

**Confirmation gate**: byte-diff a real Outlook PC with a Boolean
property against `buildPropertyContext` output. If only the upper
bytes differ, this entry can be downgraded to "tolerated" or removed.

### M4 PC reader: empty-PC `hidRoot == 0` convention

**Status: pre-registered before real-Outlook empty-PC observation.**

`readPropertyContext` interprets `BTHHEADER.hidRoot == 0` (specifically:
`hidIndex == 0` in the encoded HID) as "empty BTH, return zero
records" rather than throwing. The spec [MS-PST] §2.3.2.1 doesn't pin
how empty BTHs are encoded; alternative interpretations Outlook might
use:

- (A) Sentinel `hidIndex == 0` → empty (our reader's choice). Symmetric
  with §2.3.1.1's "hidIndex MUST NOT be zero" — zero literally cannot
  point to a real allocation, so reading it as "empty" is the only
  legal interpretation.
- (B) `hidRoot` points to a 0-byte allocation (e.g. HID 0x40 with
  rgibAlloc[0] == rgibAlloc[1]). Zero records, but the BTHHEADER
  itself is fully populated.
- (C) Some sentinel value like `0xFFFFFFFF` in `hidRoot`. Less likely
  (would conflict with §2.3.1.1's bit fields) but possible.

**Decision for `readPropertyContext`** (Phase C): accept (A) cleanly —
returns empty `vector<ReadPcProp>` without throwing. (B) also works
naturally because the resolver returns an empty slice; the leaf
walker's record loop runs zero times. (C) would currently throw at
HID resolution — fine, because we've never seen it.

**Confirmation gate**: produce an empty PC via Outlook (e.g. a folder
with no custom properties beyond the spec's mandatory ones, then
delete them all) and compare. If Outlook uses (B) or (C), this entry
either confirms (A) or upgrades to a multi-pattern handler.

### M4 PC writer: subnode-NID allocation stride

**Status: pre-registered. Single-subnode synthetic test does not
exercise this; the stride is unobservable until M5+.**

When the spec table promotes a property to a subnode (`cb > 3580`
variable, or caller-forced), the writer must assign a fresh NID for
that subnode block. The spec does not prescribe the stride between
successive subnode NIDs.

**Decision for `buildPropertyContext`**: stride `+= 0x20` between
successive subnode NIDs — equivalent to `nidIndex += 1` keeping
nidType bits intact. This is the densest legal stride (smallest
gap that still preserves nidType). Caller chooses the starting
NID; the builder advances by 0x20 per promoted prop.

**Why this might be wrong:**
- Outlook may use a wider stride to leave gap-room for future
  sibling NIDs (e.g. +0x20 reserved for inline edits).
- Some implementations may assign NIDs from a global counter shared
  across all PCs in the file, in which case "stride" is determined
  by what else is being written, not by us.

**Why this is not blocking M4**: the synthetic-PC test has exactly
one subnode-promoted property (PidTagAttachDataBinary, 5500 B). The
stride is never observed. When M5 starts producing PCs with multiple
oversized properties, this entry becomes load-bearing — flag for
re-confirmation against a real Outlook PC at that point.

### M5 pre-flight finding: intermediate BBT page format (one-half-empirical confirmation)

**Status: pre-registered before M5 implementation. Confirmation gate is real-Outlook BBT extraction.**

The [MS-PST] §3.3 *Sample Intermediate BT Page* spec page text says: *"both intermediate NBT and BBT pages share this format"*. The transcribed sample (ptype=0x81, NBT) is one-half empirical confirmation — we have a positive-control byte-diff oracle for intermediate **NBT**, but not for intermediate **BBT**.

The shared-format claim is plausible (both BTPAGE / BTENTRY are common §2.2.2.7.7 / §2.2.2.7.7.2 structures), and the ptype byte is the only documented difference (0x80 BBT vs 0x81 NBT). But ptype isn't all that distinguishes them: BBT leaves use BBTENTRY (24 B) and NBT leaves use NBTENTRY (32 B) per §2.2.2.7.7.3. At the **intermediate** level both should use BTENTRY (24 B), so the format truly should be identical except for ptype.

**Decision for M5 BTPAGE writer (Phase B)**: emit the same byte layout for intermediate NBT and intermediate BBT, varying only `ptype` ∈ {0x80, 0x81}. Single writer, parameterized by ptype. The §3.3 oracle covers the NBT side; the BBT side is byte-identical-except-ptype by construction.

**Why this is not blocking M5**: the §3.5 BBT-leaf and §3.6 XBLOCK round-trips have already validated the BBT-leaf shape and the page-trailer / wSig / dwCRC machinery. M5 only adds an intermediate level on top, and the intermediate format is shared with NBT — so M5's BBT-intermediate code path is exercised by the NBT-intermediate oracle (§3.3) up to a single byte (ptype).

**Why this is registered NOW (not at contradiction time)**: the §3.5/§3.7 lesson — single-sample assumptions get registered the moment they're inferred, not when they first fail. §3.3's claim "format is shared" is precisely such an assumption.

**Confirmation gate**: extract one real Outlook-produced intermediate BBT page (ptype=0x80) and byte-diff against `buildBtIntermediatePage(..., ptype=0x80)`. If only ptype differs, this entry can be deleted. If anything else differs, log the divergence as a new M5 KNOWN_UNVERIFIED entry.

### M4 finding: HNPAGEMAP must be DWORD-aligned

**Empirical, reproducible, no spec text found that states this directly.**

The §3.8 sample's HNPAGEMAP starts at offset 0xEC (last allocation
ends at 0xEC, naturally DWORD-aligned, no pad needed). The §3.11
sample's HNPAGEMAP starts at offset 0x1BC even though the last
allocation ends at 0x1BB — there is exactly 1 byte of zero pad
between them. Both samples have `ibHnpm mod 4 == 0`.

`buildHeapOnNode(...)` therefore aligns `ibHnpm` up to the next
4-byte boundary after the final allocation. Without this rule the
§3.11 round-trip is off by one byte; with it, every byte matches.

**When a real Outlook PST is available**: extract one HN with an
allocation total that's not naturally DWORD-aligned and confirm the
1- to 3-byte pad is always zero-filled and always rounds up to the
next DWORD. If Outlook uses a different alignment (e.g. WORD = 2-byte
or QWORD = 8-byte), update `buildHeapOnNode` accordingly.

---

## M6 — Messaging Core schemas

### NameToIdMap empty-state property count (§2.4.7 + §2.7.1)

**Status: pre-registered before real-Outlook validation.**

§2.7.1 says NID_NAME_TO_ID_MAP minimum state = "Empty". §2.4.7 (Hash Table sub-page) says the hash table consists of `PidTagNameidBucketCount` (= 251) PLUS 251 hash bucket properties at PidTags 0x1000..0x10FA. For a freshly-created PST with zero named properties registered:

- **(A)** Conservative empty: emit only the 4 well-known stream/count properties (PidTagNameidBucketCount + 3 stream properties), no bucket properties at all. M6 ships this.
- **(B)** Strict empty: also emit all 251 bucket properties (each as zero-length PtypBinary at 0x1000..0x10FA). 255 properties total.
- **(C)** Spec-style empty: emit (A) AND set bucket count to 0 (no SHOULD-251 conformance, but minimal property surface).

**Decision for M6**: option (A). Rationale: §2.4.7 says the hash table "is mostly used in avoiding duplicates when attempting to add a new named property" — i.e., the buckets are only meaningful when there are named props to disambiguate. An empty map has no props to hash, so omitting buckets is semantically equivalent. The 251 bucket count is set per spec recommendation; bucket allocations grow on first named-prop insertion.

**Catches it**: Outlook open / scanpst on an M6 PST with NID 0x0061 emitted via `buildNameToIdMapPc()`. If Outlook rejects (likely error: "missing required hash bucket properties"), upgrade to option (B).

### Recipient / Attachment Template ibData layout (writer choice)

**Status: writer chose; no spec evidence either way.**

[MS-PST] Recipient Template (0x0692) and Attachment Table Template (0x0671) sub-pages list each column's PidTag + PropType but DO NOT specify ibData / iBit assignments for the row layout. The spec leaves these to the implementation — any layout that honors §2.3.4.4.1 invariants (LtpRowId at iBit=0/ibData=0, LtpRowVer at iBit=1/ibData=4) is legal.

M6 ships:
- Recipient Template: ibData={0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 49} for the 14 cols (4-byte then 1-byte regions); endBm = 52.
- Attachment Template: ibData={0, 4, 8, 12, 16, 20} for the 6 cols (all 4-byte); endBm = 25 (24 fixed + 1 CEB).

**Risk**: real Outlook may expect a specific ibData ordering, and our schema may not match. Phase E real-Outlook validation will surface any mismatch.

**Catches it**: Outlook open / scanpst on an M6 PST. If Outlook rejects with "invalid Recipient/Attachment Table layout", inspect a real-Outlook PST and align ibData/iBit values.

### Search Contents Template schema (0x0610) — best-effort guess

**Status: pre-registered; spec page absent from public [MS-PST] documentation.**

[MS-PST] §2.7.1 lists `NID_SEARCH_CONTENTS_TABLE_TEMPLATE` (0x0610) as a mandatory "Columns Only" TC, but [MS-PST] does NOT publish a dedicated sub-page for its schema (unlike the Contents Template, Hierarchy Template, Recipient Template, Attachment Template — each of which has its own §2.7.x sub-page).

M6 ships the same column schema as the Contents Template (0x060E) — search-folder rows are message-shaped, so reusing the 27-column Contents schema is a defensible default.

**Risk**: search folders may need extra columns for search criteria, search state, search-key indexing. Real-Outlook validation will reveal.

**Catches it**: Outlook open on an M6 PST. If Outlook rejects 0x0610 specifically, extract a real-Outlook search folder's TC schema and update.

### Search Folder PC schema (NID 0x2223) — best-effort guess

**Status: pre-registered; spec doesn't pin the property set.**

[MS-PST] §2.7.1 lists 0x2223 as `<Spam search Folder>` with `nidType = SEARCH_FOLDER (0x03)` and "PC / Schema Props" minimal state. The exact property schema is not pinned in the spec text reachable so far.

M6 ships the same 4-property schema as a regular folder PC (DisplayName, ContentCount, ContentUnreadCount, Subfolders). Real Outlook may include search-specific properties such as PR_SEARCH_KEY, PR_CONTAINER_FLAGS, search-criteria props per [MS-OXOSRCH].

**Catches it**: Outlook open / scanpst on M6 PST. Extract a real-Outlook PST's NID 0x2223 (or any search-folder PC) and align the schema.

### Bare-node payload (NIDs 0x01E1, 0x0201) — 4 zero bytes

**Status: pre-registered; spec says only "Empty".**

[MS-PST] §2.7.1 marks `NID_SEARCH_MANAGEMENT_QUEUE` (0x01E1) and `NID_SEARCH_ACTIVITY_LIST` (0x0201) as `Object = "node"` (lowercase, not PC or TC) with `Minimal state = "Empty"`. The spec doesn't pin a payload size or content for these bare-node entries.

M6 emits a 4-byte zero payload (smallest legal data block payload, DWORD-aligned). The block gets its own BBT entry and an NBTENTRY pointing at it with `bidSub = 0`.

**Catches it**: Outlook open / scanpst on M6 PST. If Outlook expects a different payload (e.g. structured 8-byte queue header), extract a real-Outlook PST's 0x01E1 / 0x0201 blocks and update.

### PidTagContainerClass type discrepancy: §2.7 vs §3.12

**Status: pre-registered. Two different spec pages disagree on the propType.**

[MS-PST] §2.7.x "Hierarchy Table Template" (NID 0x060D) lists property `0x3613` as **PtypBinary** (`PidTagContainerClass`, no _W suffix). [MS-PST] §3.12 sample dump shows the SAME column in a per-folder Hierarchy TC as **PtypString** (`0x3613001F`, friendly name `PidTagContainerClass_W`).

**Decision for M6**: emit `PtypString` (0x001F) for both template and per-folder Hierarchy TCs (matches §3.12's actual sample bytes). Rationale: §3.12 is byte-pinned evidence from a real Outlook PST; §2.7's template description may be stale or normative-loose.

**Risk**: real Outlook may reject the template if it strictly expects `0x3613` to be PtypBinary. Verify at M6 real-Outlook gate.

**Catches it**: Outlook open / scanpst on an M6 PST. If `[m6][hierarchy_tc_3_12]` byte-equality against a real-Outlook hierarchy TC fails specifically at the TCOLDESC for 0x3613, swap propType to PtypBinary in the template and keep PtypString in per-folder TCs (one writer with a knob).

## M7 — Mail content (Graph Message → IPM.Note)

The 10 candidates pre-registered in M7 pre-flight (MILESTONES.md "M7
KNOWN_UNVERIFIED candidates"). Status post-Phase-E:

### M7-1 — HTML body codepage

**Status: pre-registered; awaiting real-Outlook gate (gate item 10).**

[SPEC §3.13] HTML dump (1638 bytes) starts with raw ASCII `<html xmlns:v=...>` — appears UTF-8, not UTF-16-LE. PidTagInternetCodepage = 20127 (US-ASCII) on that sample.

M7 ships: `buildMailPc` emits raw UTF-8 bytes for `PidTagBodyHtml` (0x10130102, PtypBinary), and emits `PidTagBody_W` (0x1000001F, PtypString UTF-16-LE) as a fallback derived from `bodyPreview`.

**Catches it**: Outlook open of `m7_full_pst.pst` produced by `[m7][phase_e]` tests. If body renders as garbage, switch HTML encoding (UTF-16-LE bytes) and possibly emit `PidTagInternetCodepage` (0x3FDE0003) explicitly.

### M7-2 — conversationId vs conversationIndex

**Status: pre-registered; awaiting real-Outlook conversation-grouping verification.**

Graph emits both `conversationId` (string) and `conversationIndex` (binary). [SPEC §3.13] sample confirms `PidTagConversationIndex` is 22 structured bytes (5-byte header + 16-byte FILETIME + GUID prefix).

M7 ships: emits `PidTagConversationIndex` (0x00710102, PtypBinary) from Graph's `conversationIndex` raw bytes. `PidTagConversationId` (0x30130102) NOT emitted — Graph's `conversationId` is a string-form opaque identifier.

**Catches it**: Outlook conversation grouping on the M7 PST. If grouping breaks, derive `PidTagConversationId` from `PidTagConversationIndex` per [MS-OXCMSG] or emit Graph's `conversationId` directly as bytes.

### M7-3 — OneOff EntryID byte format

**Status: pre-registered; awaiting real-Outlook gate.**

[MS-OXCDATA] §2.2.5.1 OneOff EntryID layout. M7 implementation:

```
bytes  0.. 3   rgbFlags    = 0x00000000
bytes  4..19   ProviderUID = 81 2B 1F A4  BE A3 10 19  9D 6E 00 DD  01 0F 54 02
bytes 20..21   Version     = 0x0000
bytes 22..23   Flags       = 0x9001
                              bit 0 (0x0001) MAPI_ONE_OFF_NO_RICH_INFO
                              bit 12 (0x1000) reserved (set per backup.pst pattern)
                              bit 15 (0x8000) MAPI_ONE_OFF_UNICODE
bytes 24..    DisplayName  (UTF-16-LE + null terminator)
bytes ...     AddressType  = "SMTP" (UTF-16-LE + null)
bytes ...     EmailAddress (UTF-16-LE + null)
```

**Catches it**: Outlook open. If sender / recipient EntryIDs are rejected, compare against a backup.pst extraction structurally (don't print recipient names — only structural bytes).

### M7-4 — itemAttachment encoding

**Status: pre-registered; awaiting real-Outlook gate; partial implementation.**

[MS-OXCMSG] §2.2.2.9 says itemAttachment is an "Embedded Message Object". M7 ships: `buildAttachmentPc` for `AttachmentKind::Item` recursively calls `buildMailPc` on the embedded message and stores the resulting HN bytes as `PidTagAttachDataBinary` (PtypBinary, tag 0x37010102), with `PidTagAttachMethod` = 5 (afEmbeddedMessage).

**Limitation**: nested subnodes (large body / inner attachments of the embedded message) are dropped — only the embedded HN body crosses into the parent. Multi-level nested attachments are M10 hardening.

**Catches it**: open an M7 PST with an item attachment in Outlook. Click on the embedded message — if it opens with the right subject/body, encoding is accepted. Inner attachments missing = expected M10 work.

### M7-5 — itemAttachment max nesting depth

**Status: tolerated.**

No explicit cap enforced. Recursion depth is bounded by the Graph parse tree's depth. Combined with M7-4's "drop nested subnodes" simplification, deeply nested cases degrade gracefully.

### M7-6 — PidTagMessageFlags bit composition

**Status: VERIFIED via decode round-trip.**

`computeMessageFlags(GraphMessage)` sets:
- `mfRead`        (0x0001) when `isRead = true`
- `mfUnsent`      (0x0008) when `isDraft = true`
- `mfHasAttach`   (0x0010) when attachments present

Verified by `[mail_pc_round_trip]` test: input `isRead=true + hasAttachments=true` produces inline value `0x00000011`.

`mfUnmodified` (0x0002), `mfFromMe` (0x0020), `mfFAI` (0x0040), other bits: not set. M10 hardening can refine.

### M7-7 — PidTagSearchKey derivation

**Status: tolerated.**

`graph::deriveSearchKey(smtpAddress)` produces 16 bytes: `"SMTP:" + UPPER(address)` truncated/zero-padded to 16. Deterministic; case-insensitive. Test `[m7][graph_convert] deriveSearchKey deterministic` confirms.

[MS-OXOMSG] doesn't pin the exact derivation rule beyond the "SMTP:<addr>" convention; this implementation is one defensible choice.

### M7-8 — RFC 2822 internet header round-trip

**Status: VERIFIED via decode round-trip.**

`serializeInternetHeaders` produces "Name: Value\r\n" pairs concatenated. `[mail_headers]` test confirms exact output for a 2-header sample.

`buildMailPc` populates `PidTagTransportMessageHeaders` (0x007D001F, PtypString) with the UTF-16-LE encoding of this serialized text. Outlook's MIME re-roundtrip is a manual verification at gate 10.

### M7-9 — Inbox NID assignment strategy

**Status: tolerated.**

`writeM7Pst` allocates folder NIDs dynamically via `M5Allocator::allocate(NormalFolder)`. There's no hardcoded NID for "Inbox" — Graph's `parentFolderId = "Inbox"` doesn't drive anything spec-shaped at the byte level.

`PidTagIpmInboxEntryId` is NOT yet emitted at the message store PC level. If Outlook resolves "Inbox" via that property, it'll fall back to walking IPM Subtree's hierarchy. M10 hardening can add it.

### M7-10 — Folder ContainerClass casing

**Status: tolerated.**

`M7Folder::containerClass` defaults to "IPF.Note" (mixed case). `buildMailFolderPc` test confirms emission via PidTag 0x3613001F (PtypString). Outlook tolerance to casing is gate 10.

## M8 — Contacts (Graph Contact → IPM.Contact)

### M8-1 — Contact email storage uses PidTag instead of PidLid named property

**Status: tolerated; Outlook contact-UI verification pending.**

[MS-OXOCNTC] §2.2.1.1 says contact email addresses use named properties: `PidLidEmail1Address`, `PidLidEmail1AddressType`, `PidLidEmail1DisplayName`, with PSETID_Address GUID and dispid 0x8083/0x8084/0x8080 respectively. Named properties require `Name-to-ID Map` (NID 0x0061) population beyond M6's empty-state shape.

M8 ships: emits `PidTagEmailAddress_W` (0x3003001F) — the recipient-form tag — populated with `emailAddresses[0].address`, plus `PidTagAddressType_W` (0x3002001F) = "SMTP".

**Risk**: Outlook's contact UI reads the named-property variant exclusively. The PidTag variant likely shows up in `readPropertyContext` round-trips (verified in `[contact_pc_round_trip]` tests) but may not surface in Outlook's contact card.

**Catches it**: open `m8_contacts.pst` in Outlook; verify the contact's email field is populated. If empty: M10 hardening adds Name-to-ID Map machinery + named-property emission.

### M8-2 — Contact photo attachment

**Status: deferred to M10.**

Graph's contact resource doesn't carry inline photo bytes; the photo is fetched separately via `/contacts/{id}/photo/$value`. M8 doesn't fetch photos.

[MS-OXOCNTC] §2.2.1.5 says photo is `PidLidContactPhoto` (named property) with `PidTagAttachmentContactPhoto` (0x7FFF000B) marker on the attachment row. M7's `buildAttachmentPc` is already generic enough to handle this — M10 hardening wires it.

**Catches it**: not applicable — Outlook simply shows the contact without a photo.

### M8-3 — PidLidFileAs (named property)

**Status: deferred.**

Outlook's "File As" field for contacts is `PidLidFileAs` (PSETID_Common, dispid 0x8005). When absent, Outlook computes a default from `PidTagDisplayName_W` ("Last, First" or "First Last" per user setting).

M8 emits `PidTagDisplayName` and `PidTagSubject` to the same display name. Outlook should populate "File As" from these.

**Catches it**: open M8 PST in Outlook; check if contact's "File As" matches displayName. If not, named-property machinery required (same as M8-1).

### M8-4 — Multi-email contacts

**Status: deferred.**

M8 emits only the first `emailAddresses[0]`. Real contacts can have 2-3 emails (work / personal / other). Outlook expects `PidLidEmail1Address`, `PidLidEmail2Address`, `PidLidEmail3Address`.

**Catches it**: open M8 PST in Outlook; check that secondary emails are missing. M10 hardening (named props) addresses.

## M9 — Calendar (Graph Event → IPM.Appointment)

### M9-1 — Appointment named properties (PidLidAppointmentStartWhole etc.) deferred

**Status: deferred to M10.**

[MS-OXOCAL] §2.2.1 specifies that the canonical storage for appointment properties uses named properties under PSETID_Appointment. The most important ones:

| Named property | GUID set | LID | PropType | Purpose |
|---|---|---|---|---|
| PidLidAppointmentStartWhole | PSETID_Appointment | 0x820D | SystemTime | Authoritative event start |
| PidLidAppointmentEndWhole | PSETID_Appointment | 0x820E | SystemTime | Authoritative event end |
| PidLidAppointmentDuration | PSETID_Appointment | 0x8213 | Int32 | Duration in minutes |
| PidLidAppointmentSubType | PSETID_Appointment | 0x8215 | Boolean | true = all-day event |
| PidLidLocation | PSETID_Appointment | 0x8208 | String | Location string |
| PidLidIsRecurring | PSETID_Appointment | 0x8223 | Boolean | recurring-master flag |
| PidLidAppointmentRecur | PSETID_Appointment | 0x8216 | Binary | RecurrencePattern bytes |
| PidLidGlobalObjectId | PSETID_Meeting | 0x0003 | Binary | Cross-system event ID |

M9 ships: `PidTagStartDate` (0x00600040) and `PidTagEndDate` (0x00610040) — top-level PidTags that [MS-OXPROPS] documents as mirrors of the named-prop variants. Other named props are NOT emitted.

**Risk**: Outlook's calendar view may not surface events without the named-prop variants. If not surfaced, events still exist as PCs in the PST (verifiable via `readPropertyContext`) but won't render in Calendar UI.

**Catches it**: open `m9_calendar.pst` in Outlook; navigate to Calendar view. If events absent, M10 hardening adds Name-to-ID Map machinery + named-property emission.

### M9-2 — Time-zone handling

**Status: tolerated.**

Graph's `dateTimeTimeZone` carries `dateTime` (no offset suffix) + `timeZone` (Windows zone name like "Pacific Standard Time" or "UTC"). M9's `toIso(DateTimeTimeZone)` treats non-UTC zones as UTC, appending 'Z'.

For UTC events the math is correct. For PST/EST/etc. events, the FILETIME offset will be wrong by the zone's offset (3-12 hours).

**Catches it**: open M9 PST in Outlook; verify event times display correctly for non-UTC events. M10 hardening: integrate Windows tz database for proper offset application.

### M9-3 — Recurring-event expansion

**Status: deferred to M10.**

Graph's event resource carries a `recurrence` complex type with `pattern` + `range`. Per [MS-OXOCAL] §2.2.1.44, recurrence is stored in `PidLidAppointmentRecur` (named, PtypBinary) as a structured `RecurrencePattern` byte sequence.

M9 doesn't emit recurrence. `EventType::SeriesMaster` events are written as a single PC with the master's start/end; occurrences are not expanded.

**Catches it**: open M9 PST in Outlook with a recurring event; only the master event will appear (or none if Outlook depends on PidLidIsRecurring). M10 hardening implements `RecurrencePattern` encoding + occurrence expansion.

### M9-4 — Attendees → recipient TC mapping

**Status: deferred to M10.**

Graph event attendees parse correctly into `GraphEvent::attendees`. Per [MS-OXOCAL] §2.2.4, meeting attendees are stored in a recipient TC (subnode of the message PC, NID `NID_RECIPIENT_TABLE = 0x692`) — same shape as M7's mail recipient TC.

M9 doesn't emit a recipient TC for events. Reuse of M7's `buildRecipientTc` is straightforward; M10 wires it (just convert `Attendee` to `Recipient` with the kind-mapping: required→To, optional→Cc, resource→Bcc).

**Catches it**: M9 PST shows events but no attendees in Outlook. M10 hardening adds recipient TC subnode per event.

### M9-5 — Online meeting URL / provider (Teams, Zoom)

**Status: deferred to M10.**

Graph's `onlineMeeting.joinUrl` + `onlineMeetingProvider` parse correctly. Outlook stores these via `PidLidConferencingCheckInUrl` + `PidLidOnlineMeetingType` (named). M9 parses but doesn't emit.

**Catches it**: M9 PST events lack the "Join Teams meeting" button. M10 hardening adds named-prop emission.

## How to use this file

When validation against real Outlook becomes possible:

1. Open the produced `.pst` in Outlook.
2. If it opens cleanly: walk this list, mark items as "verified by passing
   Outlook gate" — no per-item byte comparison needed unless something
   downstream goes wrong.
3. If Outlook rejects: byte-diff the produced file against an
   Outlook-produced empty PST. Every difference should map to a row
   above; if it doesn't, we have a bug we never logged.

Don't add things here that the spec is unambiguous about. This file is
specifically for **interpretation gaps** where we picked one of multiple
plausible readings.

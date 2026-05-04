# KNOWN_UNVERIFIED.md

Log of guesses we made during pstwriter development that **real Outlook
would resolve**. When `tests/golden/empty.pst` (or any equivalent
ground-truth file) is finally produced by Outlook, this is the diff list
to check first.

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

### Spec-sample dwCRC anomalies (§3.5 BBT leaf and §3.7 SLBLOCK)

**Status: blocked on a real Outlook-produced PST. NOT blocked on us.**

The [MS-PST] published spec samples at:
- §3.5 *Sample Leaf BBT Page* — stored dwCRC `0xA1F6A02F`
- §3.7 *Sample SLBLOCK* — stored dwCRC `0xD9D45E50`

both fail to match a re-computation of crc32 over their own pre-trailer
bytes under our PST CRC-32 implementation. That same implementation
**reproduces the §3.2 sample header's dwCRCPartial=0x379AA90E and
dwCRCFull=0x1FD283D6 exactly**, AND **reproduces the §3.6 XBLOCK's
stored dwCRC=0x3FEECD51 byte-for-byte** — so our CRC code is correct
on the spec's own evidence.

Reasons we believe these two samples are hand-illustrated, not
Outlook-extracted:

- §3.5 also has BBTENTRY records with impossible IBs (e.g. `ib=0x20B`
  is not a multiple of 64, but blocks must be 64-byte-aligned per
  §2.2.2.8).
- The §3.6 sample sits in the *same spec section* as §3.5/§3.7 and
  has correct CRC. If a generic CRC bug affected the spec, all three
  would diverge.
- §3.5 / §3.7 are smaller, simpler illustrations — exactly the kind
  the doc author would hand-edit; §3.6 is mechanical (53 sequential
  BIDs) and easy to leave alone.

**Why this is not blocking pstwriter**: our writer reproduces every
spec-extractable byte of these structures (body + cb + wSig) and emits
self-consistent dwCRC values via the same CRC code that has already
been validated end-to-end by §3.2 and §3.6. Outlook will compute its
own CRC at read-time over the bytes we wrote, so the only failure
mode would be a generic CRC bug — which §3.2 + §3.6 already rule out.

**Action when an Outlook PST is available**: extract one real BBT page
and one real SLBLOCK, byte-diff them against `buildBbtLeaf(...)` /
`buildSlBlock(...)` output, and confirm stored dwCRCs match
`crc32(...)`. Once that confirmation lands, this entry can be deleted.

Tests that lock the §3.x oracles in:
- `[golden_spec_header]` — §3.2, full byte-for-byte (PASSING)
- `[golden_spec_nbt_leaf]` — §3.4, parse + CRC + wSig (PASSING)
- `[golden_spec_bbt_leaf]` — §3.5, body + wSig + self-consistent CRC (PASSING; see anomaly above)
- `[golden_spec_data_tree]` — §3.6, full byte-for-byte XBLOCK round-trip (PASSING)
- `[golden_spec_slblock]` — §3.7, body + wSig + self-consistent CRC (PASSING; see anomaly above)

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

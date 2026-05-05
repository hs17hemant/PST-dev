# pstwriter — Claude navigation guide

Standalone C++17 writer that converts Microsoft Graph JSON exports to Outlook-compatible Unicode PST files. Generates PST 2.0 (`wVer=23`) per the [MS-PST] specification.

**For full context start with `README.md`.** This file is the working map for Claude sessions — pointers to where things live, conventions you'll keep re-discovering, and the oracle/evidence hierarchy that drives most design decisions.

---

## Codebase tree (milestone-layered)

The architecture mirrors [MS-PST]'s section structure. Each layer has its own write/read pair and a byte-diff oracle against published spec samples. Milestone numbers are how the project (and `MILESTONES.md`) refers to layers.

| Layer | Purpose | Key files |
|---|---|---|
| **M2 — header / skeleton** | 5-page empty PST | [src/writer.cpp](src/writer.cpp), [src/ndb.cpp](src/ndb.cpp) |
| **M3 — block layer** | Data / X / XX / SL / SI blocks; CRC; encryption (Permute) | [src/block.cpp](src/block.cpp), [src/crc.cpp](src/crc.cpp), [src/encoding.cpp](src/encoding.cpp) |
| **M4 — LTP layer** | Heap-on-Node, BTH, PropertyContext (PC), TableContext (TC) | [src/ltp.cpp](src/ltp.cpp), [include/pstwriter/ltp.hpp](include/pstwriter/ltp.hpp) |
| **M5 — NBT/BBT navigation** | NID assignment, intermediate paging, `writeM5Pst` | [src/m5_allocator.cpp](src/m5_allocator.cpp), [src/nbt.cpp](src/nbt.cpp), [src/page.cpp](src/page.cpp) |
| **M6 — messaging core** | 27 §2.7.1 mandatory nodes, folder schemas, sibling-table builders | [src/messaging.cpp](src/messaging.cpp), [src/pst_baseline.cpp](src/pst_baseline.cpp) |
| **M7 — mail (`IPM.Note`)** | `writeM7Pst`, message PC, recipient TC, attachment TC | [src/mail.cpp](src/mail.cpp), [src/graph_message.cpp](src/graph_message.cpp), [src/graph_convert.cpp](src/graph_convert.cpp) |
| **M8 — contacts (`IPM.Contact`)** | `writeM8Pst` | [src/contact.cpp](src/contact.cpp), [src/graph_contact.cpp](src/graph_contact.cpp) |
| **M9 — calendar (`IPM.Appointment`)** | `writeM9Pst` | [src/event.cpp](src/event.cpp), [src/graph_event.cpp](src/graph_event.cpp) |
| **M10 — production hardening** | In progress; Contents-TC row emission, recurring events, named props | scattered — search for `M10` in source comments |
| **M11 — Aspose-vs-Outlook triage** | Bug fixes from external diffs (see `KNOWN_UNVERIFIED.md` M11-A..D) | same files, scattered |

Public headers live in [include/pstwriter/](include/pstwriter/); implementations in [src/](src/). Tools in [tools/](tools/). Tests in [tests/](tests/) (Catch2 v3, ~231 cases).

## Tools (stable entry points)

- [tools/pst_info.cpp](tools/pst_info.cpp) — structural validator. Walks HEADER → AMap → BBT → NBT → every block; prints "ALL CHECKS PASSED" or specific failures. Use this after every meaningful PST-writing change.
- [tools/pst_convert.cpp](tools/pst_convert.cpp) — CLI: `pst_convert {mail|contacts|calendar} input.json out.pst`.
- [tools/golden_gen/](tools/golden_gen/) — generators for `tests/golden/spec_sample_*.bin` byte-diff oracles.

## Build & test

```powershell
cmake --build "d:/Work Qfion/PST Dev/build/gcc"
"d:/Work Qfion/PST Dev/build/gcc/bin/pstwriter_tests.exe"   # full suite
"d:/Work Qfion/PST Dev/build/gcc/bin/pst_info.exe" <file.pst>
```

Build dir is **`build/gcc`** (MinGW GCC, `-Werror`). MSVC build is deferred to M10. Catch2 v3 is fetched via `FetchContent` at configure time.

The `[m6_gate]` test in [tests/test_messaging.cpp](tests/test_messaging.cpp) is the strictest end-to-end check; if you change anything in the §2.7.1 mandatory-nodes path, this is the test that pins it.

---

## Spec & decision sources (read these before designing anything)

| File | What's in it | When to read |
|---|---|---|
| [MILESTONES.md](MILESTONES.md) | Per-milestone design decisions, spec quotations, [VERIFIED]/[DESIGN]/[ENFORCED] markers | Start here for *any* design question — search `Decision N` and the section headers |
| [KNOWN_UNVERIFIED.md](KNOWN_UNVERIFIED.md) | Pre-registered guesses with confirmation gates; M11-A..D triage entries; oracle-confusion history | Before changing any byte-level behaviour or calling something "wrong" |
| [Claude/SPEC_GROUND_TRUTH.md](Claude/SPEC_GROUND_TRUTH.md) | The §3.x byte-pinned spec samples we reproduce | When asked "what does the spec say about X" |
| [Claude/SPEC_VERIFIED.md](Claude/SPEC_VERIFIED.md) | Promoted-to-locked rules; reader/writer invariants | Before relaxing a structural check |
| [Claude/HANDOFF.md](Claude/HANDOFF.md) | Cross-milestone handoff notes | When picking up unfamiliar work |
| [README.md](README.md) | User-facing docs; architecture diagram | First read for context |

`MILESTONES.md` is the *primary* source of truth for design intent. It is large (~2300 lines) — search by milestone (`## M7`) and decision number (`### Decision 3`) rather than reading top-to-bottom.

## Oracle hierarchy

When the spec, real Outlook, and Aspose disagree, the priority is:

1. **Real-Outlook PST byte-dump** (e.g. `backup.pst`) — strongest oracle.
2. **Aspose-generated PST** — strong oracle (`backup.pst` round-trips through Aspose match real Outlook).
3. **[MS-PST] §3.x sample bytes** — published spec dumps. Authoritative for what they show, but they're single samples and may be unrepresentative (see KNOWN_UNVERIFIED M11-D for the §3.12 / Aspose / hand-Outlook reconciliation).
4. **[MS-PST] prose** — interpretive; "0 for top-level nodes" etc. doesn't override sample evidence.

**Lesson from this codebase's history (M3 / M11 cycles):** never act on a single oracle. If a symptom suggests one oracle is wrong, get a second oracle (different PST, different tool, different code path) before flipping behaviour. Pre-register the guess in `KNOWN_UNVERIFIED.md` so you can revert cleanly if the second oracle disagrees.

---

## Conventions Claude sessions repeatedly re-discover

- **Stable buffers behind raw pointers.** TC/PC builders take `const uint8_t*` for varlen cells. Caller must keep the underlying `vector<uint8_t>` alive until the builder returns. Pattern: `vector<X> bufs; bufs.reserve(N);` — `reserve` is load-bearing because `push_back` reallocates and invalidates pointers.
- **`scheduleNode(nid, parent, body)`** in [src/mail.cpp](src/mail.cpp), [src/contact.cpp](src/contact.cpp), [src/event.cpp](src/event.cpp) is the funnel for every NBTENTRY. The `parent` argument becomes `NBTENTRY.nidParent`.
- **`buildPstBaselineEntries`** in [src/pst_baseline.cpp](src/pst_baseline.cpp) emits 24 of the 27 §2.7.1 mandatory nodes; the 3 it excludes (`0x802D` / `0x802E` / `0x802F`) are added by the M7/M8/M9 callers because those tables carry per-folder hierarchy rows.
- **CEB encoding** for TC rows: high-bit-first byte numbering. `iBit` 0 of byte 0 is bit 7 (`0x80`). Set with `byte |= (1 << (7 - (iBit % 8)))`.
- **NID allocator starts at `0x400`** (`m5_allocator.cpp:33`, `kUserAllocStart`) for every type except `Internal` (= 1) and `HID` (= 0). This is M11-A's fix — Outlook rejects user-allocated NIDs in the §2.4.3 reserved range.
- **Block dwCRC scope is `cb` bytes only**, not `cb + alignment-padding`. M11 cycle resolved a bug where the CRC was over `trailerOff` (= `totalSize - 16`); see KNOWN_UNVERIFIED.md "RESOLVED 2026-05-04".
- **Folder sibling-table NBTENTRYs (`nidType` ∈ {0x0D, 0x0E, 0x0F}) carry `nidParent = 0`.** This is the §3.12 reading and is confirmed by Aspose. Don't flip without dual-oracle evidence — see KNOWN_UNVERIFIED.md M11-D for the cycle that produced and reverted three passes of bad code.

## M10 deferred items (currently open)

These are pre-registered as M10 work in source comments + `MILESTONES.md` / `KNOWN_UNVERIFIED.md`:

- Multi-level nested itemAttachment subnodes (M7-4 limitation).
- Recurring-event expansion (`PidLidAppointmentRecur`) — M9-3.
- Time-zone offset application — M9-2.
- Contact named properties (`PidLidEmail*Address`, `PidLidFileAs`, photo) — M8-1..4.
- Online-meeting URL named props — M9-5.
- MSVC `/W4 /WX` build fix-ups.
- ~~Contents-TC row population for user folders~~ — **done**, see `buildFolderContentsTc(rows, count)` overload in [src/messaging.cpp](src/messaging.cpp) and the per-folder loop in [src/mail.cpp](src/mail.cpp).

---

## Working with this codebase efficiently

- **Before editing PST-writing code**, run the test suite once and capture the assertion count. After your change, the same count should pass — a regression silently drops assertions when a `SECTION` skips.
- **`pst_info <file.pst>` is your structural sanity check.** "ALL CHECKS PASSED" is necessary but not sufficient — it verifies internal consistency, not Outlook compatibility.
- **Don't trust a "the symptom went away" change.** The CRC scope bug, the §3.12 nidParent confusion, and the user-NID-range bug all looked fixed at one oracle and broken at another. Always test against ≥2 oracles for byte-level changes.
- **Comments referencing `KNOWN_UNVERIFIED.md M11-X`** are load-bearing — they document why the current value was chosen and what would invalidate it. Leave them alone unless you also update the entry.

## Note on graphify

A previous session asked for a graphify-built knowledge graph. The CLI's PyPI package name (`graphifyy`, double-y) doesn't match the import name (`graphify`, single-y), and the sandbox blocks the install as a typosquat risk. If you want a graph anyway, ask the user to install `graphifyy` themselves first — then the `/graphify .` skill pipeline runs cleanly. Otherwise, this file *is* the navigation tree.

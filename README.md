# pstwriter

**Standalone C++17 library + CLI that converts Microsoft Graph JSON exports to Outlook-compatible Unicode PST files.**

Built as an Aspose.Email replacement for organizational mail/contact/calendar archiving pipelines. Generates PST 2.0 (Unicode, `wVer=23`) per the [MS-PST] specification.

| | |
|---|---|
| **Status** | M1–M9 complete; M10 (production hardening) in progress |
| **Language** | C++17, no external dependencies (Catch2 v3 in tests only) |
| **Toolchains** | MinGW GCC (primary, `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`); MSVC `/W4 /WX` deferred to M10 |
| **Tests** | 231 cases / 5603 assertions / 0 failures (2 SKIPPED — manual Outlook gates) |
| **Spec** | [MS-PST] v11.2 + [MS-OXCMSG] / [MS-OXOMSG] / [MS-OXCMAIL] / [MS-OXOCNTC] / [MS-OXOCAL] / [MS-OXPROPS] / [MS-OXCDATA] |

---

## What it does

Given Microsoft Graph JSON for a user's mailbox, produces a `.pst` file that opens in Microsoft Outlook with:

- **Mail messages** (`IPM.Note`) — plain-text + HTML bodies, multi-recipient (To/Cc/Bcc), file + embedded-message attachments, RFC 2822 internet headers, organizer/sender metadata
- **Contacts** (`IPM.Contact`) — name parts, company/department, phones, business + home addresses, birthday
- **Calendar events** (`IPM.Appointment`) — single-instance appointments with subject/body/start/end/organizer

The library is **build-from-scratch only** — every PST is produced fresh from input JSON; the writer never edits existing PSTs in place.

---

## Quickstart

### Build

```bash
git clone https://github.com/<your-org>/PST-dev.git
cd PST-dev
cmake -B build -G Ninja
cmake --build build
```

Requires CMake ≥ 3.20 and a C++17 compiler. Catch2 v3.7.1 is fetched at configure time via `FetchContent`.

### Run tests

```bash
ctest --test-dir build --output-on-failure
# or:
./build/bin/pstwriter_tests
```

### Inspect a PST

```bash
./build/bin/pst_info path/to/file.pst
# Walks HEADER + AMap + BBT + NBT + every block; reports
# "ALL CHECKS PASSED" or specific structural failures.
```

### Convert Graph JSON to PST

```bash
# Single message
./build/bin/pst_convert mail message.json out.pst

# Bulk: a list response from Graph (/me/messages?$top=50)
./build/bin/pst_convert mail messages_list.json mail.pst

# Contacts
./build/bin/pst_convert contacts contacts_list.json contacts.pst

# Calendar
./build/bin/pst_convert calendar events_list.json calendar.pst
```

---

## Architecture

The codebase is layered to mirror the [MS-PST] spec's section structure. Each layer has its own write/read pair and its own byte-diff oracle against published spec samples.

```
┌────────────────────────────────────────────────────────────────────┐
│  Application layer:  pst_convert CLI                               │
├────────────────────────────────────────────────────────────────────┤
│  M7-M9 builders:  Graph JSON -> PST                                │
│  graph_message.hpp  graph_contact.hpp  graph_event.hpp             │
│  mail.hpp           contact.hpp        event.hpp                   │
│  graph_convert.hpp  (utf8/utf16le, ISO-FILETIME, base64, EntryID)  │
├────────────────────────────────────────────────────────────────────┤
│  M6 messaging core:  27 §2.7.1 mandatory nodes                     │
│  messaging.hpp  (MessageStore PC, FolderPc, HierarchyTc, ...)      │
├────────────────────────────────────────────────────────────────────┤
│  M5 NBT navigation:  NID assignment + intermediate-page paging     │
│  m5_allocator.hpp   nbt.hpp   writer.hpp::writeM5Pst(...)          │
├────────────────────────────────────────────────────────────────────┤
│  M4 LTP layer:  Heap-on-Node, BTH, PropertyContext, TableContext   │
│  ltp.hpp  (buildHeapOnNode, buildPropertyContext,                  │
│            buildTableContext, readPropertyContext)                 │
├────────────────────────────────────────────────────────────────────┤
│  M3 block layer:  data block, X/XX/SL/SI blocks                    │
│  block.hpp  (buildDataBlock, buildXBlock, buildSlBlock, ...)       │
├────────────────────────────────────────────────────────────────────┤
│  M2 page+header layer:  HEADER, AMap, NBT/BBT pages                │
│  page.hpp   ndb.hpp                                                │
├────────────────────────────────────────────────────────────────────┤
│  M1 primitives:  CRC-32, encoding, NID/BID/Bref/PropTag types      │
│  crc.hpp   encoding.hpp   types.hpp                                │
└────────────────────────────────────────────────────────────────────┘
```

### Reader contract — HID-agnostic

The `readPropertyContext` reader is **HID-order-agnostic** by spec contract. Real Outlook PSTs edit in-place over time, leaving HID slots in arbitrary layouts; M4's reader walks `HNHDR → HNPAGEMAP → BTHHEADER → leaf-records` and resolves each record's HNID directly without any positional assumptions. Same applies to `M5Allocator` — readers walking the NBT must binary-search-descend, never reconstruct allocator output.

### Writer contract — deterministic + build-from-scratch

Same logical input → byte-identical output. HID slots `0x20 / 0x40 / 0x60+` assigned in PidTag-ascending order. No in-place edits.

---

## Milestones

The project was built incrementally with milestone-based commits. Each milestone has an exit gate that requires an oracle outside the codebase confirming the bytes are right (spec sample byte-diff, real-Outlook open, or both).

| M | Scope | Gate | Status |
|---|---|---|---|
| **M1** | Core primitives — `types`, `crc`, `encoding` | crc32("123456789",9)==0x2DFD2D88; mpbb permutation bijections; cyclic encoding self-inverts | ✅ green |
| **M2** | 5-page empty PST skeleton — HEADER + AMap + NBT/BBT leaves | [MS-PST] §3.2 sample header reproduced byte-for-byte; `pst_info` ALL CHECKS PASSED | ✅ green |
| **M3** | Block writer — data block, XBLOCK/XXBLOCK, SLBLOCK/SIBLOCK | §3.5 BBT-leaf, §3.6 XBLOCK, §3.7 SLBLOCK byte-for-byte oracles | ✅ green |
| **M4** | LTP layer — Heap-on-Node, BTH, PropertyContext, TableContext | §3.8 HN, §3.9 BTH, §3.11 TC byte-for-byte; synthetic-PC composition oracle | ✅ green |
| **M5** | NBT navigation — intermediate-page paging, NID registration | §3.3 BT-intermediate; reader NID-order-agnostic; `m5_full_pst.pst` no orphan blocks | ✅ green |
| **M6** | Messaging core — 27 §2.7.1 mandatory nodes | All 27 mandatory nodes; `m6_full_pst.pst` ALL CHECKS PASSED; CRC-scope retrospective fixed (commit `5c4a5c6`) | ✅ green |
| **M7** | Mail content — Graph Message → IPM.Note PSTs | Graph JSON parser, mail PC, recipient + attachment TCs, HTML bodies, file + item attachments, internet headers, folder hierarchy | ✅ green (Outlook open pending) |
| **M8** | Contacts — Graph Contact → IPM.Contact PSTs | Contact PC with ~30 PidTags; "IPF.Contact" folder | ✅ green (Outlook open pending) |
| **M9** | Calendar — Graph Event → IPM.Appointment PSTs | Event PC with subject/body/times/organizer; "IPF.Appointment" folder | ✅ green (Outlook open pending) |
| **M10** | Production hardening + release | MSVC `/W4 /WX` clean; named-property infrastructure; multi-block HN; fuzz testing; refactor 27-node baseline | 🚧 in progress |

Detailed per-phase exit-gate evidence is in [MILESTONES.md](MILESTONES.md).

---

## Project structure

```
PST-dev/
├── include/pstwriter/         # Public C++ API headers
│   ├── types.hpp              # Nid, Bid, Ib, Bref, PropTag, FileTime
│   ├── crc.hpp                # crc32 (MS-PST polynomial)
│   ├── encoding.hpp           # NDB_CRYPT_PERMUTE / NDB_CRYPT_CYCLIC
│   ├── ndb.hpp                # HEADER, AMap, ROOT
│   ├── page.hpp               # 512-byte page primitives
│   ├── block.hpp              # data/X/XX/SL/SI block builders
│   ├── m5_allocator.hpp       # NID assignment service
│   ├── nbt.hpp                # NBT/BBT leaf + intermediate
│   ├── ltp.hpp                # HN, BTH, PC, TC builders + readPropertyContext
│   ├── messaging.hpp          # 27 §2.7.1 mandatory-node builders
│   ├── writer.hpp             # writeEmptyPst, writeM5Pst, ...
│   ├── graph_convert.hpp      # utf8<->utf16le, ISO-FILETIME, base64, OneOff EntryID
│   ├── graph_message.hpp      # GraphMessage struct + JSON parser (M7)
│   ├── mail.hpp               # buildMailPc, buildRecipientTc, writeM7Pst
│   ├── graph_contact.hpp      # GraphContact struct + JSON parser (M8)
│   ├── contact.hpp            # buildContactPc, writeM8Pst
│   ├── graph_event.hpp        # GraphEvent struct + JSON parser (M9)
│   └── event.hpp              # buildEventPc, writeM9Pst
│
├── src/                       # Library implementations
│   ├── crc.cpp encoding.cpp ndb.cpp page.cpp block.cpp ...
│   ├── ltp.cpp messaging.cpp
│   ├── graph_convert.cpp
│   ├── graph_message.cpp mail.cpp
│   ├── graph_contact.cpp contact.cpp
│   ├── graph_event.cpp event.cpp
│   └── internal_json.hpp      # shared JSON parser (M7-M9 use)
│
├── tools/
│   ├── pst_info.cpp           # PST validator (HEADER/AMap/BBT/NBT walk, CRC checks)
│   └── pst_convert.cpp        # CLI: Graph JSON → PST
│
├── tests/                     # Catch2 v3 unit + integration tests
│   ├── test_crc.cpp test_encoding.cpp test_types.cpp
│   ├── test_ndb.cpp test_block.cpp test_ltp.cpp
│   ├── test_m5_*.cpp test_messaging.cpp
│   ├── test_m7_*.cpp          # Mail (graph_convert, graph_message, mail, end_to_end)
│   ├── test_m8_contact.cpp    # Contacts
│   ├── test_m9_event.cpp      # Calendar
│   └── golden/                # [MS-PST] §3.2/§3.3/§3.5-§3.11 reference samples
│
├── MILESTONES.md              # Per-milestone exit gates + closures (2000+ lines)
├── KNOWN_UNVERIFIED.md        # Pre-registered hypotheses awaiting Outlook gates
├── CMakeLists.txt
└── README.md                  # this file
```

---

## API at a glance

### Library

```cpp
#include <pstwriter/mail.hpp>
#include <pstwriter/graph_message.hpp>

// Parse Graph JSON
auto messages = pstwriter::graph::parseGraphMessageList(jsonText);

// Build per-message PCs (in-memory, for inspection)
pstwriter::MailPcBuildContext ctx;
ctx.providerUid  = {/* 16 bytes */};
ctx.subnodeStart = pstwriter::Nid{0x10001};
auto pc = pstwriter::buildMailPc(messages[0], ctx);
// pc.hnBytes is the HN body; pc.subnodes lists promoted-to-subnode props

// Or write a full PST end-to-end
pstwriter::M7Folder inbox;
inbox.displayName = "Inbox";
inbox.parentNid   = pstwriter::Nid{0x8022};   // IPM Subtree
for (const auto& m : messages) inbox.messages.push_back(&m);

pstwriter::M7PstConfig cfg;
cfg.path           = "out.pst";
cfg.providerUid    = {/* 16 bytes */};
cfg.pstDisplayName = "My Inbox";
cfg.folders        = { inbox };

auto result = pstwriter::writeM7Pst(cfg);
if (!result.ok) std::cerr << result.message << "\n";
```

### CLI

```bash
# pst_convert <kind> <input.json> <output.pst>
#   kind ∈ {mail, contacts, calendar}

pst_convert mail   inbox_dump.json   archive.pst
pst_convert contacts contacts.json   contacts.pst
pst_convert calendar events.json     calendar.pst
```

Input JSON accepts either a single object, a bare array `[ {...}, ... ]`, or a Graph list response `{"value":[...]}`.

### Validator

```bash
pst_info path/to/file.pst
# Output:
#   HEADER: dwMagic, wVer, root brefs verified
#   AMap @ 0x400: ptype 0x84, page CRC matches
#   BBT walk: N entries / N blocks
#   NBT walk: N entries / N nodes
#   LTP HN @ block bid=...
#     PC properties: <list>
#     TC cCols=...
#   ALL CHECKS PASSED
```

---

## Validation discipline

This project enforces a strict *"oracle outside the codebase"* discipline — every milestone's exit gate requires confirmation that the bytes match either:

1. **Published [MS-PST] spec samples** (§3.2, §3.3, §3.5-§3.13) — byte-for-byte byte-diff oracles in `tests/golden/`
2. **Real-Outlook PSTs** — opens cleanly, round-trips through readers
3. **Self-consistency tests** explicitly noted as such (the CRC-scope retrospective at M6 closure documents why self-consistency alone is *not* a useful safety net)

The `KNOWN_UNVERIFIED.md` file pre-registers hypotheses *before* validation, so when an oracle confirms or contradicts, the file's status updates to Verified / Tolerated / Resolved.

The CRC-scope retrospective (commit `5c4a5c6`) is the canonical example of why this discipline matters: a CRC-bytes-scope bug present from M3 through M6 was invisible to internal tests because writer + reader were equally broken. It surfaced only when a real-Outlook PST (`backup.pst`) was used as oracle.

---

## Known limitations

These are tracked in [KNOWN_UNVERIFIED.md](KNOWN_UNVERIFIED.md) for the M10 hardening pass:

- **Named-property storage**: M8 contact emails and M9 appointment start/end currently use top-level PidTag mirrors instead of canonical `PidLid*` named properties. Outlook reads top-level mirrors for *some* fields, not all. (M8-1, M9-1)
- **Time-zone handling**: M9 events with non-UTC timezones treat as UTC; offset application requires Windows tz database (M9-2)
- **Recurring events**: M9 only writes the master record; recurrence expansion deferred (M9-3)
- **Meeting attendees**: parsed but not emitted as recipient TC (M9-4)
- **Multi-block HN**: messages with bodies >8176 bytes flagged as edge case
- **MSVC build**: deferred 6 times to M10; primary toolchain is MinGW GCC

The `m7_full_pst.pst` / `m8_contacts.pst` / `m9_calendar.pst` outputs all pass `pst_info ALL CHECKS PASSED` for structural integrity. Real-Outlook open is the manual gate that surfaces named-property gaps.

---

## Spec references

All implementation choices cite the spec section they implement. The primary references:

| Spec | Purpose |
|---|---|
| [[MS-PST]](https://learn.microsoft.com/en-us/openspecs/office_file_formats/ms-pst/) v11.2 | PST 2.0 file format (HEADER, blocks, BBT/NBT, LTP) |
| [[MS-OXCMSG]](https://learn.microsoft.com/en-us/openspecs/exchange_server_protocols/ms-oxcmsg/) | Message and Attachment Object Protocol |
| [[MS-OXOMSG]](https://learn.microsoft.com/en-us/openspecs/exchange_server_protocols/ms-oxomsg/) | Email Object Protocol (To/Cc/Bcc, headers, message flags) |
| [[MS-OXCMAIL]](https://learn.microsoft.com/en-us/openspecs/exchange_server_protocols/ms-oxcmail/) | RFC 2822 → Email Object conversion |
| [[MS-OXOCNTC]](https://learn.microsoft.com/en-us/openspecs/exchange_server_protocols/ms-oxocntc/) | Contact Object Protocol |
| [[MS-OXOCAL]](https://learn.microsoft.com/en-us/openspecs/exchange_server_protocols/ms-oxocal/) | Calendar Object Protocol |
| [[MS-OXPROPS]](https://learn.microsoft.com/en-us/openspecs/exchange_server_protocols/ms-oxprops/) | Master property list (PidTag IDs, PropTypes, canonical names) |
| [[MS-OXCDATA]](https://learn.microsoft.com/en-us/openspecs/exchange_server_protocols/ms-oxcdata/) | EntryID encoding (Store, OneOff, Exchange-DN) |
| [Microsoft Graph v1.0](https://learn.microsoft.com/en-us/graph/api/resources/message) | message / contact / event resources |

---

## Contributing / development

The repo is structured for incremental milestone-based development:

1. Each milestone has a pre-flight design section in `MILESTONES.md`
2. SKIPPED test placeholders are added at pre-flight (one per exit-gate item)
3. Implementation phases (A, B, C, ...) commit incrementally
4. Closure section appended on milestone completion with test counts + KNOWN_UNVERIFIED resolution status

When adding a new feature, follow the established pattern:
- Add SKIPPED placeholder tests at design time
- Pre-register KNOWN_UNVERIFIED hypotheses *before* writing code
- Write byte-diff tests against either spec samples or real-Outlook extractions
- Run `pst_info` on every produced PST as a smoke check
- Avoid self-consistency-only tests (CRC-scope retrospective lesson)

---

## License

(Add your chosen license here — MIT / Apache-2.0 / proprietary.)

---

## Acknowledgments

- Microsoft Open Specifications team for the [MS-PST] / [MS-OX*] documentation that made this implementation possible
- The `libpff` and `pypff` projects (Joachim Metz) for cross-validation of generated output during development
- Catch2 (Martin Hořeňovský et al.) for the test harness

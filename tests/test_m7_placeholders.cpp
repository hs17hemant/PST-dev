// pstwriter/tests/test_m7_placeholders.cpp
//
// M7 pre-flight SKIPPED placeholders — one per gate item from
// MILESTONES.md "M7 exit gate". These tests fail-skip until M7
// implementation lands; following the pattern used in M3/M4 pre-flights.
//
// When M7 implementation arrives:
//   * remove the SKIP() line in each test
//   * uncomment the assertions or replace with real round-trip checks
//   * each gate item's eventual test file may be split out into its own
//     test_m7_*.cpp file; this placeholder file can be removed entirely
//     once all 12 are implemented and absorbed.

#include <catch2/catch_test_macros.hpp>

// Gate item 1 — Phase A
TEST_CASE("M7 placeholder: Graph Message JSON parser handles all 32+ fields",
          "[m7][gate][graph_message_parser][SKIPPED]")
{
    SKIP("M7 Phase A pending — parseGraphMessage(json) not yet implemented. "
         "Gate item 1 from MILESTONES.md M7 exit gate.");
}

// Gate item 2 — Phase B
TEST_CASE("M7 placeholder: plain-text mail PC round-trip via readPropertyContext",
          "[m7][gate][mail_pc_round_trip][SKIPPED]")
{
    SKIP("M7 Phase B pending — buildMailPc(GraphMessage) not yet implemented. "
         "Gate item 2 from MILESTONES.md M7 exit gate.");
}

// Gate item 3 — Phase C
TEST_CASE("M7 placeholder: HTML mail message emits PidTagBodyHtml",
          "[m7][gate][mail_html][SKIPPED]")
{
    SKIP("M7 Phase C pending — HTML body handling per Decision 2 not yet "
         "implemented. Gate item 3 from MILESTONES.md M7 exit gate. "
         "Resolves KNOWN_UNVERIFIED M7-1 (HTML body codepage).");
}

// Gate item 4 — Phase C
TEST_CASE("M7 placeholder: file attachment round-trip with attachment table",
          "[m7][gate][mail_file_attachment][SKIPPED]")
{
    SKIP("M7 Phase C pending — buildAttachmentRow / buildAttachmentTc / "
         "buildAttachmentPc / buildFileAttachmentDataBlock not yet "
         "implemented. Gate item 4 from MILESTONES.md M7 exit gate.");
}

// Gate item 5 — Phase C
TEST_CASE("M7 placeholder: item attachment (embedded message) round-trip",
          "[m7][gate][mail_item_attachment][SKIPPED]")
{
    SKIP("M7 Phase C pending — itemAttachment encoding not yet implemented. "
         "Gate item 5 from MILESTONES.md M7 exit gate. "
         "Resolves KNOWN_UNVERIFIED M7-4 (itemAttachment encoding) and "
         "M7-5 (itemAttachment max nesting depth).");
}

// Gate item 6 — Phase D
TEST_CASE("M7 placeholder: multi-recipient TC populated for To/Cc/Bcc",
          "[m7][gate][mail_recipients][SKIPPED]")
{
    SKIP("M7 Phase D pending — recipient TC with multiple rows not yet "
         "implemented. Gate item 6 from MILESTONES.md M7 exit gate. "
         "Resolves KNOWN_UNVERIFIED M7-3 (OneOff EntryID byte format).");
}

// Gate item 7 — Phase D
TEST_CASE("M7 placeholder: internet headers round-trip via PidTagTransportMessageHeaders",
          "[m7][gate][mail_headers][SKIPPED]")
{
    SKIP("M7 Phase D pending — RFC 2822 internet header serialization not "
         "yet implemented. Gate item 7 from MILESTONES.md M7 exit gate. "
         "Resolves KNOWN_UNVERIFIED M7-8 (RFC 2822 round-trip).");
}

// Gate item 8 — Phase D
TEST_CASE("M7 placeholder: folder hierarchy with at least 2 user folders under IPM Subtree",
          "[m7][gate][mail_folder_tree][SKIPPED]")
{
    SKIP("M7 Phase D pending — dynamic folder-tree construction beyond M6 "
         "skeleton not yet implemented. Gate item 8 from MILESTONES.md M7 "
         "exit gate. Resolves KNOWN_UNVERIFIED M7-9 (Inbox NID assignment) "
         "and M7-10 (folder ContainerClass casing).");
}

// Gate item 9 — Phase E
TEST_CASE("M7 placeholder: pst_info reports zero orphan blocks on m7_full_pst.pst",
          "[m7][gate][end_to_end][m7_pst_info][SKIPPED]")
{
    SKIP("M7 Phase E pending — writeM7Pst(...) end-to-end not yet "
         "implemented. Gate item 9 from MILESTONES.md M7 exit gate. "
         "Equivalent of M5/M6's pst_info ALL CHECKS PASSED gate.");
}

// Gate item 10 — Phase E
TEST_CASE("M7 placeholder: m7_full_pst.pst opens cleanly in classic Outlook",
          "[m7][gate][end_to_end][m7_outlook][SKIPPED]")
{
    SKIP("M7 Phase E pending — real-Outlook validation gate. "
         "Gate item 10 from MILESTONES.md M7 exit gate. "
         "Substitutes for M6 Phase E since per-milestone Outlook validation "
         "is now the standing safety net (per pre-M7 MSVC deferral "
         "compensating commitments). May be deferred at M7 closure if no "
         "Outlook environment is available, but tracked.");
}

// Gate item 11 — Phase E
TEST_CASE("M7 placeholder: M6 reader walks M7 PST without breaking (additive contract)",
          "[m7][gate][end_to_end][m7_backwards_compat][SKIPPED]")
{
    SKIP("M7 Phase E pending — backwards-compatibility check not yet runnable. "
         "Gate item 11 from MILESTONES.md M7 exit gate. "
         "Existing M6 NBT reader / readPropertyContext / pst_info tests must "
         "all pass when fed an M7-produced PST as input.");
}

// Gate item 12 — Phase E
TEST_CASE("M7 placeholder: backup.pst-style structural validation on m7_full_pst.pst",
          "[m7][gate][end_to_end][m7_structural_probe][SKIPPED]")
{
    SKIP("M7 Phase E pending — backup.pst-style structural probe (block "
         "CRCs match cb-only scope per [SPEC sec 2.2.2.8.1], page CRCs "
         "match crc32(page, 496), all NBT entries reachable, HN bodies "
         "valid) not yet runnable on m7_full_pst.pst. Gate item 12 from "
         "MILESTONES.md M7 exit gate.");
}

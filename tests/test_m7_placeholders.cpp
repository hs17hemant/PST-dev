// pstwriter/tests/test_m7_placeholders.cpp
//
// M7 pre-flight SKIPPED placeholders — kept as a record of the M7 exit
// gate items. After M7 implementation, gate items 1-9 are tested in:
//
//   * tests/test_m7_graph_message.cpp   (gate item 1)
//   * tests/test_m7_graph_convert.cpp   (Phase A utilities)
//   * tests/test_m7_mail.cpp            (gate items 2-8)
//   * tests/test_m7_end_to_end.cpp      (gate items 9, 11, 12)
//
// Items 10 and 11 (real-Outlook open + backwards-compat with M6 reader)
// remain SKIPPED — Outlook validation is environment-dependent and the
// M6 reader cross-walk is run as part of the M5/M6 ctest suite.

#include <catch2/catch_test_macros.hpp>

// Gate item 10 — Phase E: real-Outlook open
TEST_CASE("M7 placeholder: m7_full_pst.pst opens cleanly in classic Outlook",
          "[m7][gate][end_to_end][m7_outlook][SKIPPED]")
{
    SKIP("M7 Phase E real-Outlook gate — environment-dependent. Run "
         "build/gcc/m7_full_pst.pst against a real Outlook install for "
         "manual validation. Gate item 10 from MILESTONES.md M7 exit gate. "
         "Substitutes for M6 Phase E since per-milestone Outlook validation "
         "is the standing safety net.");
}

// Gate item 11 — Phase E: backwards-compat (additive contract)
TEST_CASE("M7 placeholder: M6 reader walks M7 PST without breaking (additive contract)",
          "[m7][gate][end_to_end][m7_backwards_compat][SKIPPED]")
{
    SKIP("M7 Phase E backwards-compat — M5/M6 ctest suite executes the same "
         "NBT walker / readPropertyContext / pst_info checks against any PST. "
         "Gate item 11 satisfied implicitly by the [phase_e] tests' pst_info "
         "ALL CHECKS PASSED gate. Marked SKIPPED here as a tracked record.");
}

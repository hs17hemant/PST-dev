// pstwriter/tools/pst_info_run.hpp
//
// Exposes the diagnostic pipeline body of pst_info as a callable
// function so tests (and other tools) can invoke it without round-
// tripping through cmd.exe / system() shell-quoting quirks.
//
// Linkage: the test target compiles tools/pst_info.cpp with
// PSTWRITER_PST_INFO_NO_MAIN defined; the binary target compiles
// it without that flag (so main() is included).

#pragma once

#include <string>

// Returns 0 on success ("ALL CHECKS PASSED"), 1 on any check failure,
// or for usage errors / unreadable file.
int runPstInfo(const std::string& path);

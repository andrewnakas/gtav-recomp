// gtav - one-file bug reports (bug_report.cpp).
#pragma once

#include <filesystem>
#include <string>

namespace gtav::bug_report {

// Writes <logs>/gtav-bug-report-<date>-<time>.zip; returns its path ("" on failure).
std::string Create(const std::string& reason);

// If the previous run crashed (crash.txt in the working directory): keep it in the logs as
// last-crash.txt and write a report. Returns the report's path or "".
std::string CreateIfLastRunCrashed();

// Opens the file manager at the report.
void ShowInFolder(const std::string& path);

std::filesystem::path LogDir();

}  // namespace gtav::bug_report

#pragma once

// DateTime.hpp — Locale-formatted timestamp for the Edit > Time/Date (F5) command.

#include <string>

namespace notepadxp::util {

/// @brief Build the current local timestamp in classic Notepad order: time,
///        a single space, then the short date (e.g. "10:42 PM 5/27/2026").
/// @param withLeadingTrailingCrlf When true, wrap the timestamp in CRLFs; used
///        for the ".LOG" auto-stamp so the stamp lands on its own line at EOF.
/// @return The formatted string using the user's locale (short date, no seconds).
[[nodiscard]] std::wstring FormatTimestamp(bool withLeadingTrailingCrlf);

} // namespace notepadxp::util

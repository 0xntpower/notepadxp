#pragma once

// CommandLine.hpp — Parse NotepadXP's command line into a small result struct.

#include <optional>
#include <string>

namespace notepadxp::util {

/// @brief Result of parsing the process command line.
///
/// Classic Notepad accepts a leading "/A" (force ANSI) or "/W" (force
/// Unicode) switch followed by an optional file name. Encoding is reported as
/// raw flags here so this leaf utility stays independent of the file layer,
/// which maps them onto its TextEncoding type.
struct ParsedCommandLine {
    std::optional<std::wstring> filePath;  // File to open, if any.
    bool forceAnsi = false;                // "/A" present.
    bool forceUnicode = false;             // "/W" present.
};

/// @brief Parse the given Win32 command line (typically GetCommandLineW()).
/// @note Install-time relics ("/P", "/PT", "/.SETUP") are intentionally not
///       handled yet; they are ignored.
[[nodiscard]] ParsedCommandLine ParseCommandLine(const wchar_t* commandLine);

} // namespace notepadxp::util

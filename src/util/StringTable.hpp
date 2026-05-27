#pragma once

// StringTable.hpp — Access to the executable's string-table resources, plus the
// classic Notepad "%%" merge-token substitution used to build message text.

#include <string>
#include <string_view>

#include "util/WinLean.hpp"

namespace notepadxp::util {

/// @brief Load a string-table entry (IDS_*) from the running module.
/// @param id Resource string identifier (see res/Resource.h).
/// @return The string, or an empty string if the id is not present.
[[nodiscard]] std::wstring LoadStr(UINT id);

/// @brief Replace the first "%%" merge token in @p templateText with @p insertion.
///
/// Implements the classic Notepad "%%" merge behavior: message strings such as
/// IDS_SCBC contain a "%%" placeholder where a file name is spliced in. If no
/// token is present the template is returned unchanged.
[[nodiscard]] std::wstring MergeString(std::wstring_view templateText,
                                       std::wstring_view insertion);

/// @brief Convenience: load IDS string @p id and merge @p insertion into it.
[[nodiscard]] std::wstring LoadAndMerge(UINT id, std::wstring_view insertion);

/// @brief Show a message box owned by @p parent. Thin MessageBoxW wrapper.
int AlertBox(HWND parent, std::wstring_view caption, std::wstring_view text, UINT type);

} // namespace notepadxp::util

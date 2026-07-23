#pragma once

// PathName.hpp — Path-string helpers shared across the app.

#include <string>

namespace notepadxp::util {

/// @brief Final path component (after the last separator), e.g. for title bars
///        and message text. A path with no separator is returned unchanged.
[[nodiscard]] inline std::wstring PathLeaf(const std::wstring& path) {
    const size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? path : path.substr(separator + 1);
}

} // namespace notepadxp::util

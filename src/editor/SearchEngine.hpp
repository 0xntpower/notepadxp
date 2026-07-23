#pragma once

// SearchEngine.hpp — Pure text search for Find/Replace. Searches do not wrap
// around the buffer, matching classic Notepad.

#include <cstddef>
#include <string>
#include <string_view>

namespace notepadxp::editor {

/// @brief Compare @p key against @p text at [pos, pos+key.size()), honoring
///        case sensitivity (case-insensitive uses the user locale).
[[nodiscard]] bool MatchAt(const std::wstring& text, size_t pos, std::wstring_view key,
                           bool matchCase);

/// @brief First match at or after @p from; npos if none. No wrap-around.
[[nodiscard]] size_t FindForward(const std::wstring& text, std::wstring_view key, size_t from,
                                 bool matchCase);

/// @brief Last match ending at or before @p before; npos if none. No wrap-around.
[[nodiscard]] size_t FindBackward(const std::wstring& text, std::wstring_view key, size_t before,
                                  bool matchCase);

/// @brief Outcome of ReplaceAllInText: the rewritten text and the match count.
struct ReplaceAllResult {
    std::wstring text;
    int count = 0;
};

/// @brief Replace every non-overlapping match of @p key, left to right.
///        Replaced text is never re-matched, even when @p replacement
///        contains @p key.
[[nodiscard]] ReplaceAllResult ReplaceAllInText(const std::wstring& text, std::wstring_view key,
                                                std::wstring_view replacement, bool matchCase);

} // namespace notepadxp::editor

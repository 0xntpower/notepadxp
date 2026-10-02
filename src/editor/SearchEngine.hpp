#pragma once

// SearchEngine.hpp — Pure text search for Find/Replace. Searches do not wrap
// around the buffer, matching classic Notepad.

#include <cstddef>
#include <string>
#include <string_view>

namespace notepadxp::editor {

/// @brief A found match. Case-insensitive matching is linguistic (user locale),
///        so a match can differ in length from the key (e.g. "é" written as
///        "e" + combining accent).
struct Match {
    size_t pos = std::wstring_view::npos;  // npos == no match.
    size_t length = 0;
};

/// @brief Whether @p candidate (typically the selection) equals @p key, under
///        the same case rule the searches use.
[[nodiscard]] bool IsMatch(std::wstring_view candidate, std::wstring_view key, bool matchCase);

/// @brief First match at or after @p from. No wrap-around.
[[nodiscard]] Match FindForward(std::wstring_view text, std::wstring_view key, size_t from,
                                bool matchCase);

/// @brief Last match ending at or before @p before. No wrap-around.
[[nodiscard]] Match FindBackward(std::wstring_view text, std::wstring_view key, size_t before,
                                 bool matchCase);

/// @brief Outcome of ReplaceAllInText: the rewritten text and the match count.
struct ReplaceAllResult {
    std::wstring text;
    int count = 0;
};

/// @brief Replace every non-overlapping match of @p key, left to right.
///        Replaced text is never re-matched, even when @p replacement
///        contains @p key.
[[nodiscard]] ReplaceAllResult ReplaceAllInText(std::wstring_view text, std::wstring_view key,
                                                std::wstring_view replacement, bool matchCase);

} // namespace notepadxp::editor

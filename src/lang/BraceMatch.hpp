#pragma once

// BraceMatch.hpp — Pure matching-bracket search for Edit > Go to Matching Brace.

#include <cstddef>
#include <optional>
#include <string_view>

#include "lang/Language.hpp"

namespace notepadxp::lang {

/// @brief Position of the bracket matching the one at @p caretPos (or just
///        before it). Pairs () [] {}. A single forward lexical scan tracks
///        string literals (honoring the language's escape char) and line/block
///        comments so brackets inside them never participate.
/// @return The partner's index, or nullopt when there is no bracket at the
///         caret or the bracket is unbalanced.
[[nodiscard]] std::optional<size_t> FindMatchingBrace(std::wstring_view text, size_t caretPos,
                                                      const LanguageTraits& traits);

} // namespace notepadxp::lang

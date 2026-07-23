#pragma once

// IndentEngine.hpp — Pure computation of the auto-indent inserted after Enter.

#include <string>
#include <string_view>

#include "lang/Language.hpp"

namespace notepadxp::lang {

/// @brief The indentation for a new line opened at the caret: the current
///        line's leading whitespace, deepened by one @p style unit when the
///        right-trimmed text before the caret ends with one of the language's
///        indent triggers ('{' for brace languages, ':' for Python).
/// @param lineUpToCaret The current line's text from its start to the caret.
[[nodiscard]] std::wstring ComputeEnterIndent(std::wstring_view lineUpToCaret,
                                              const LanguageTraits& traits, IndentStyle style);

} // namespace notepadxp::lang

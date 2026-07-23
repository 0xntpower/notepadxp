#pragma once

// CommentToggle.hpp — Pure line-comment toggling for Edit > Toggle Comment.

#include <string>
#include <string_view>

#include "lang/Language.hpp"

namespace notepadxp::lang {

/// @brief Toggle line comments over @p block (whole lines, breaks included).
///        When every non-blank line already starts (after its indent) with the
///        language's marker — either accepted form, case-insensitive for
///        word-like markers such as REM — one marker (plus one following
///        space) is stripped per line; otherwise `lineComment + " "` is
///        inserted at the block's common minimum indent on every non-blank
///        line. Blank lines are never touched. Languages without a line
///        comment return the block unchanged.
[[nodiscard]] std::wstring ToggleLineComments(std::wstring_view block,
                                              const LanguageTraits& traits);

} // namespace notepadxp::lang

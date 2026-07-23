#pragma once

// JsonFormat.hpp — Streaming JSON pretty-print / minify for the Format menu.
//
// Single pass, O(input) time, O(nesting depth) extra space. String and number
// lexemes are copied verbatim — no parse/re-serialize round trip, so huge
// integers, exotic exponents, and \u escapes survive byte-identically.

#include <cstddef>
#include <string>
#include <string_view>

#include "lang/Language.hpp"

namespace notepadxp::lang {

/// @brief Outcome of a JSON rewrite.
struct JsonResult {
    bool ok = false;
    std::wstring text;        // The rewritten document; valid only when ok.
    size_t errorOffset = 0;   // Offset into the input; valid only when !ok.
    int errorLine = 1;        // 1-based, derived from errorOffset.
    int errorCol = 1;
};

/// @brief Reformat @p input with one element per line, @p style as the indent
///        unit, ": " after keys, and CRLF line ends.
[[nodiscard]] JsonResult PrettyPrintJson(std::wstring_view input, IndentStyle style);

/// @brief Strip all inter-token whitespace.
[[nodiscard]] JsonResult MinifyJson(std::wstring_view input);

} // namespace notepadxp::lang

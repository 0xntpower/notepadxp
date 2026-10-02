#pragma once

// LineEndings.hpp — Line-ending detection and normalization.
//
// The Win32 multiline Edit control only breaks lines on CRLF, so a file saved
// with bare LF (Unix) or bare CR (classic Mac) would otherwise render as one
// unbroken line. Text is normalized to CRLF on load for display and converted
// back to the file's original convention on save.

#include <string>
#include <string_view>

namespace notepadxp::file {

/// @brief The newline convention a file uses.
enum class LineEnding {
    Crlf,  // "\r\n" (Windows); the default for new/empty documents.
    Lf,    // "\n"   (Unix).
    Cr,    // "\r"   (classic Mac).
};

/// @brief The dominant newline style in @p text (most frequent; ties and the
///        no-newline case resolve to CRLF).
[[nodiscard]] LineEnding DetectLineEnding(std::wstring_view text);

/// @brief Rewrite every CRLF / lone LF / lone CR in @p text as CRLF, for the
///        Edit control. Text that is already all-CRLF is returned as is.
[[nodiscard]] std::wstring NormalizeToCrlf(std::wstring text);

/// @brief Rewrite the CRLF newlines in @p text (the control's form) as
///        @p target, in place, for writing back to disk.
[[nodiscard]] std::wstring ConvertFromCrlf(std::wstring text, LineEnding target);

} // namespace notepadxp::file

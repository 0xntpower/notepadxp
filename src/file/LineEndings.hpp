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

/// @brief Streaming form of DetectLineEnding + NormalizeToCrlf for loads:
///        feed the decoded (pre-normalization) text in arbitrary chunks; the
///        CRLF-normalized text is appended to @p out while line endings are
///        being counted. Embedded NUL code units are replaced with spaces,
///        matching DecodeText. Feed/Finish on the concatenation of all chunks
///        yields exactly NormalizeToCrlf(text), and Detected() yields
///        DetectLineEnding(text).
class CrlfStream {
public:
    explicit CrlfStream(std::wstring& out) : out_(&out) {}

    void Feed(std::wstring_view chunk);

    /// @brief Flush a chunk-final '\r' that never met its '\n'.
    void Finish();

    [[nodiscard]] LineEnding Detected() const;

private:
    std::wstring* out_;
    size_t crlf_ = 0;
    size_t lf_ = 0;
    size_t cr_ = 0;
    bool pendingCr_ = false;  // Saw '\r' whose successor decides crlf vs cr.
};

/// @brief Streaming form of ConvertFromCrlf for saves: feed control-form
///        (CRLF) text in arbitrary chunks; the text rewritten in @p target is
///        appended to @p out. Finish resolves a chunk-final '\r'.
class RestoreStream {
public:
    explicit RestoreStream(LineEnding target, std::wstring& out)
        : target_(target), out_(&out) {}

    void Feed(std::wstring_view chunk);
    void Finish();

private:
    LineEnding target_;
    std::wstring* out_;
    bool pendingCr_ = false;  // A '\r' at the last chunk boundary.
};

} // namespace notepadxp::file

#pragma once

// Encoding.hpp — Text encoding type plus detection, decoding and encoding.
//
// Implements the classic Notepad text-encoding behavior: BOM sniffing, the
// UTF-8 detection heuristic, BOM emission, and NUL->space substitution on load.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace notepadxp::file {

/// @brief Text encodings Notepad can read and write. The order matches the
///        Open/Save encoding combo.
enum class TextEncoding {
    Ansi = 0,     // Active code page (CP_ACP).
    Utf16Le = 1,  // UTF-16 little endian ("Unicode").
    Utf16Be = 2,  // UTF-16 big endian ("Unicode big endian").
    Utf8 = 3,     // UTF-8.
};

/// @brief Heuristic test for a valid UTF-8 byte stream.
/// @return true only if @p data is well-formed multi-byte UTF-8. Pure 7-bit
///         ASCII returns false (so the ANSI code-page path is used and the file
///         is not silently given a UTF-8 BOM on save), matching classic Notepad.
[[nodiscard]] bool IsTextUtf8(const std::byte* data, size_t size);

/// @brief Detect the encoding of a raw byte buffer.
/// @param data First bytes of the file (classic Notepad samples up to ~1 KiB).
/// @param size Number of bytes available at @p data.
/// @return BOM-based result if a BOM is present, otherwise UTF-16 (via
///         IsTextUnicode) / UTF-8 (heuristic) / ANSI in that order.
[[nodiscard]] TextEncoding DetectEncoding(const std::byte* data, size_t size);

/// @brief Decode raw file bytes of a known @p encoding into wide text.
///        A leading BOM for that encoding is skipped; embedded NUL code units
///        are replaced with spaces (the Edit control would otherwise truncate).
[[nodiscard]] std::wstring DecodeText(const std::byte* data, size_t size, TextEncoding encoding);

/// @brief Encode wide text for writing, prepending the BOM for @p encoding
///        (UTF-16 LE/BE and UTF-8 carry a BOM; ANSI does not).
/// @param lossy Set to true when an ANSI conversion could not represent some
///        character (the caller then warns, per IDS_ERRUNICODE). Always false
///        for the Unicode encodings.
[[nodiscard]] std::vector<std::byte> EncodeText(std::wstring_view text, TextEncoding encoding,
                                                bool& lossy);

} // namespace notepadxp::file

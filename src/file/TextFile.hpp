#pragma once

// TextFile.hpp — Headless text-file loading and saving: open/map/detect/decode
// on the way in, byte writing on the way out. No UI — callers turn the outcome
// enums into prompts.

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "file/Encoding.hpp"
#include "file/LineEndings.hpp"

namespace notepadxp::file {

/// @brief Why a load produced no text.
enum class LoadStatus {
    Ok,
    NotFound,     // File or path does not exist (the create-new prompt case).
    TooLarge,     // At/above the 1 GiB classic Notepad limit.
    AccessError,  // Exists but could not be opened.
    Cancelled,    // The progress hook aborted the load.
};

/// @brief Outcome of LoadTextFile: the decoded, CRLF-normalized text plus the
///        encoding and newline convention the file should be saved back with.
struct TextFileLoadResult {
    LoadStatus status = LoadStatus::AccessError;
    std::wstring text;
    TextEncoding encoding = TextEncoding::Ansi;  // Meaningful only when Ok.
    LineEnding lineEnding = LineEnding::Crlf;    // Meaningful only when Ok.
};

/// @brief Optional hooks for LoadTextFile: progress reporting and cancellation
///        for large files (the async load worker and the progress dialog).
struct LoadHooks {
    /// Called after each decoded chunk with bytes consumed so far and the
    /// total file size. Return false to cancel (status becomes Cancelled).
    std::function<bool(size_t bytesDone, size_t bytesTotal)> progress;
    size_t chunkBytes = 4 << 20;  // Bytes decoded between progress calls.
};

/// @brief Load, decode and CRLF-normalize @p path in one streaming pass
///        (equivalent to DecodeText + DetectLineEnding + NormalizeToCrlf over
///        the file). @p forced pins the encoding; otherwise it is detected
///        over the whole buffer (authoritative, unlike SniffEncoding).
[[nodiscard]] TextFileLoadResult LoadTextFile(const std::wstring& path,
                                              std::optional<TextEncoding> forced,
                                              const LoadHooks& hooks = {});

/// @brief Why a save failed.
enum class SaveStatus { Ok, CreateError, WriteError };

/// @brief Write @p bytes to @p path, replacing any existing file.
[[nodiscard]] SaveStatus WriteAllBytes(const std::wstring& path,
                                       const std::vector<std::byte>& bytes);

/// @brief Why a streamed save stopped before touching the target.
enum class StreamSaveStatus {
    Ok,
    CreateError,  // The temporary file could not be created.
    WriteError,   // A write failed.
    Aborted,      // confirmLossy declined a lossy save; target untouched.
};

/// @brief Streaming save for large documents: converts the control-form CRLF
///        text to @p target and writes @p encoding bytes in chunks, so no
///        full converted/encoded copy is ever held. The document is written to
///        a sibling temporary file and moved over @p path only on success —
///        a failed or declined save never truncates the target.
/// @param confirmLossy Optional gate, invoked at most once when some character
///        cannot be represented in @p encoding; returning false aborts before
///        @p path is touched.
[[nodiscard]] StreamSaveStatus WriteTextStreamed(const std::wstring& path,
                                                 std::wstring_view text, LineEnding target,
                                                 TextEncoding encoding, bool& lossy,
                                                 const std::function<bool()>& confirmLossy = {});

/// @brief Preview heuristic for the Open dialog: detect from a bounded prefix
/// of the file. Never authoritative — the load path detects over the
/// whole buffer. @return nullopt when the file cannot be read.
[[nodiscard]] std::optional<TextEncoding> SniffEncoding(const std::wstring& path);

/// @brief Read and decode @p path from byte offset @p fromOffset to the end
/// (the follow-tail append). @return nullopt when the file cannot be
/// read, nothing lies past the offset, or a UTF-16 tail is not aligned
/// to whole code units — callers then fall back to a full reload.
[[nodiscard]] std::optional<std::wstring> ReadTailText(const std::wstring& path,
                                                       unsigned long long fromOffset,
                                                       TextEncoding encoding);

} // namespace notepadxp::file

#pragma once

// TextFile.hpp — Headless text-file loading and saving: open/map/detect/decode
// on the way in, byte writing on the way out. No UI — callers turn the outcome
// enums into prompts.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "file/Encoding.hpp"

namespace notepadxp::file {

/// @brief Why a load produced no text.
enum class LoadStatus {
    Ok,
    NotFound,     // File or path does not exist (the create-new prompt case).
    TooLarge,     // At/above the 1 GiB classic Notepad limit.
    AccessError,  // Exists but could not be opened.
};

/// @brief Outcome of LoadTextFile: the decoded text plus the encoding the file
///        should be saved back with.
struct TextFileLoadResult {
    LoadStatus status = LoadStatus::AccessError;
    std::wstring text;
    TextEncoding encoding = TextEncoding::Ansi;  // Meaningful only when Ok.
};

/// @brief Load and decode @p path. @p forced pins the encoding; otherwise it is
///        detected over the whole buffer (authoritative, unlike SniffEncoding).
[[nodiscard]] TextFileLoadResult LoadTextFile(const std::wstring& path,
                                              std::optional<TextEncoding> forced);

/// @brief Why a save failed.
enum class SaveStatus { Ok, CreateError, WriteError };

/// @brief Write @p bytes to @p path, replacing any existing file.
[[nodiscard]] SaveStatus WriteAllBytes(const std::wstring& path,
                                       const std::vector<std::byte>& bytes);

/// @brief Preview heuristic for the Open dialog: detect from a bounded prefix
///        of the file. Never authoritative — the load path detects over the
///        whole buffer. @return nullopt when the file cannot be read.
[[nodiscard]] std::optional<TextEncoding> SniffEncoding(const std::wstring& path);

} // namespace notepadxp::file

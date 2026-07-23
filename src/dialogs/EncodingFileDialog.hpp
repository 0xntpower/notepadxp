#pragma once

// EncodingFileDialog.hpp — Open/Save common dialogs with an encoding combo.

#include <optional>
#include <string>

#include "file/Encoding.hpp"
#include "util/WinLean.hpp"

namespace notepadxp::dialogs {

/// @brief Open-dialog outcome. @c encoding is set only when the user explicitly
///        picked one in the combo; nullopt means "auto-detect at load". The
///        combo previews the sniffed encoding, but the preview is a bounded
///        heuristic and is never forced onto the load.
struct OpenDialogResult {
    std::wstring path;
    std::optional<file::TextEncoding> encoding;
};

/// @brief Save-dialog outcome: the chosen path and the encoding to write with.
struct SaveDialogResult {
    std::wstring path;
    file::TextEncoding encoding = file::TextEncoding::Ansi;
};

/// @brief The classic Open/Save dialogs augmented (via OFN_ENABLETEMPLATE) with
///        the "Encoding:" combo. On Open, selecting a file pre-selects its
///        sniffed encoding; the user may override before confirming.
class EncodingFileDialog final {
public:
    /// @brief Show the Open dialog. @return the outcome, or nullopt if cancelled.
    [[nodiscard]] static std::optional<OpenDialogResult> ShowOpen(HWND parent);

    /// @brief Show the Save As dialog, pre-filled with @p defaultPath and
    ///        @p currentEncoding. @return the outcome, or nullopt if cancelled.
    [[nodiscard]] static std::optional<SaveDialogResult> ShowSave(
        HWND parent, const std::wstring& defaultPath, file::TextEncoding currentEncoding);
};

} // namespace notepadxp::dialogs

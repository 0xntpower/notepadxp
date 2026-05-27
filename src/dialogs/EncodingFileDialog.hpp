#pragma once

// EncodingFileDialog.hpp — Open/Save common dialogs with an encoding combo.

#include <optional>
#include <string>

#include "WtlIncludes.hpp"
#include "file/Encoding.hpp"

namespace notepadxp::dialogs {

/// @brief A chosen file path together with the encoding selected in the combo.
struct FileDialogResult {
    std::wstring path;
    file::TextEncoding encoding = file::TextEncoding::Ansi;
};

/// @brief The classic Open/Save dialogs augmented (via OFN_ENABLETEMPLATE) with
///        the "Encoding:" combo. On Open, selecting a file auto-detects and
///        pre-selects its encoding; the user may override before confirming.
class EncodingFileDialog final {
public:
    /// @brief Show the Open dialog. @return the path + encoding, or nullopt if
    ///        cancelled.
    [[nodiscard]] static std::optional<FileDialogResult> ShowOpen(HWND parent);

    /// @brief Show the Save As dialog, pre-filled with @p defaultPath and
    ///        @p currentEncoding. @return the path + encoding, or nullopt.
    [[nodiscard]] static std::optional<FileDialogResult> ShowSave(
        HWND parent, const std::wstring& defaultPath, file::TextEncoding currentEncoding);
};

} // namespace notepadxp::dialogs

#pragma once

// GoToDialog.hpp — Modal "Go To Line" dialog (Edit > Go To, Ctrl+G).

#include <optional>

#include "WtlIncludes.hpp"

namespace notepadxp::dialogs {

/// @brief Modal line-number prompt backed by IDD_GOTODIALOG.
class GoToDialog final {
public:
    /// @brief Show the dialog, pre-filled with @p currentLine.
    /// @return The chosen 1-based line, or nullopt if the user cancelled.
    [[nodiscard]] static std::optional<int> Show(HWND parent, int currentLine);
};

} // namespace notepadxp::dialogs

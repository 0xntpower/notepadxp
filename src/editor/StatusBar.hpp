#pragma once

// StatusBar.hpp — The optional bottom status bar showing the caret Ln/Col.

#include "WtlIncludes.hpp"

namespace notepadxp::editor {

/// @brief Wraps the status bar control. It is split into two parts; the caret
///        line/column is shown in the right part, matching classic Notepad.
/// @threadsafety Not thread-safe; UI thread only.
class StatusBar final {
public:
    StatusBar() = default;

    StatusBar(const StatusBar&) = delete;
    StatusBar& operator=(const StatusBar&) = delete;

    /// @brief Create the status bar as a child of @p parent.
    /// @param visible Whether it starts shown.
    [[nodiscard]] bool Create(HWND parent, bool visible);

    /// @brief Re-fit the bar to the parent width and recompute its two parts.
    ///        Call from the parent's WM_SIZE handler.
    void Layout();

    void Show(bool visible);

    [[nodiscard]] bool IsVisible() const noexcept {
        return visible_;
    }

    /// @brief Height in pixels, used by the parent to reserve space for the bar.
    [[nodiscard]] int Height();

    /// @brief Set the caret position display (1-based line and column).
    void SetLineCol(int line, int col);

private:
    CStatusBarCtrl status_;
    bool visible_ = false;
};

} // namespace notepadxp::editor

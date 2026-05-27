#pragma once

// FindReplaceController.hpp — Modeless Find / Replace and the search engine.

#include "WtlIncludes.hpp"

#include <commdlg.h>  // FINDREPLACEW / FindTextW / ReplaceTextW.

#include "editor/EditView.hpp"

namespace notepadxp::dialogs {

/// @brief Drives the modeless Find and Replace common dialogs and performs the
///        searches against the edit view.
/// @threadsafety Not thread-safe; UI thread only.
///
/// The dialogs are modeless: they communicate via the registered FINDMSGSTRING
/// window message, which the frame routes here. Searches do not wrap around the
/// buffer, matching classic Notepad.
class FindReplaceController final {
public:
    explicit FindReplaceController(editor::EditView& editView);

    FindReplaceController(const FindReplaceController&) = delete;
    FindReplaceController& operator=(const FindReplaceController&) = delete;

    void ShowFind(HWND parent);
    void ShowReplace(HWND parent);
    void FindNext(HWND parent);  ///< F3: repeat the last search, or open Find.

    /// @brief The registered FINDMSGSTRING value the frame should route to us.
    [[nodiscard]] UINT RegisteredMessage() const noexcept {
        return findMessage_;
    }

    /// @brief Handle a routed FINDMSGSTRING message (FR_FINDNEXT / FR_REPLACE /
    ///        FR_REPLACEALL / FR_DIALOGTERM).
    void HandleFindMessage(WPARAM wParam, LPARAM lParam);

    /// @brief HWND of the active modeless dialog (for IsDialogMessage filtering),
    ///        or nullptr when none is open.
    [[nodiscard]] HWND ActiveDialog() const noexcept {
        return dialog_;
    }

private:
    static constexpr int kSearchMax = 128;  // Capacity of the find/replace text buffers.

    void Search();
    void ReplaceMatchThenFind();
    void ReplaceAll();
    void ReportNotFound();

    editor::EditView& editView_;
    HWND dialog_ = nullptr;   // Active modeless Find/Replace dialog.
    HWND owner_ = nullptr;    // Main window; owns message boxes when no dialog.
    UINT findMessage_ = 0;    // Registered FINDMSGSTRING.

    FINDREPLACEW request_{};
    wchar_t findBuffer_[kSearchMax] = {};
    wchar_t replaceBuffer_[kSearchMax] = {};

    bool matchCase_ = false;
    bool searchDown_ = true;
};

} // namespace notepadxp::dialogs

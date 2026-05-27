#pragma once

// EditKeyHandler.hpp — Subclasses the multiline Edit control to add the
// word-delete shortcuts the plain Win32 control lacks:
//   * Ctrl+Backspace — delete the word (and leading whitespace) to the left
//   * Ctrl+Delete    — delete the word (and trailing whitespace) to the right
// The bare control inserts a 0x7F "box" for Ctrl+Backspace and ignores
// Ctrl+Delete, so both are handled here and the stray 0x7F WM_CHAR is swallowed.

#include "WtlIncludes.hpp"

namespace notepadxp::editor {

/// @brief Attaches via SubclassWindow() to an existing Edit control; all other
///        messages fall through to the control's original window procedure.
/// @threadsafety UI-thread only.
class EditKeyHandler final : public CWindowImpl<EditKeyHandler> {
public:
    // Required by CWindowImpl; unused because we only SubclassWindow(), never Create().
    DECLARE_WND_CLASS(nullptr)

    BEGIN_MSG_MAP(EditKeyHandler)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_CHAR, OnChar)
    END_MSG_MAP()

private:
    LRESULT OnKeyDown(UINT message, WPARAM wParam, LPARAM lParam, BOOL& handled);
    LRESULT OnChar(UINT message, WPARAM wParam, LPARAM lParam, BOOL& handled);

    void DeleteWordLeft();
    void DeleteWordRight();
};

} // namespace notepadxp::editor

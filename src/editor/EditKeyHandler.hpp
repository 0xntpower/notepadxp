#pragma once

// EditKeyHandler.hpp — Subclasses the multiline Edit control to add the
// word-delete shortcuts the plain Win32 control lacks:
//   * Ctrl+Backspace — delete the word (and leading whitespace) to the left
//   * Ctrl+Delete    — delete the word (and trailing whitespace) to the right
// The bare control inserts a 0x7F "box" for Ctrl+Backspace and ignores
// Ctrl+Delete, so both are handled here and the stray 0x7F WM_CHAR is swallowed.
//
// The subclass is also the seam through which the rest of the editor observes
// the control:
//   * caret movement — every message that can move the caret is let through
//     first and then reported via the caret-notify callback;
//   * Ctrl+wheel — reported as zoom steps and swallowed;
//   * mutations — before any message that changes text, a PendingChange
//     prediction (what will be removed/inserted) is reported so the document
//     shadow can derive deltas without re-reading the buffer.

#include <functional>

#include "WtlIncludes.hpp"
#include "editor/DocumentShadow.hpp"  // PendingChange.

namespace notepadxp::editor {

/// @brief Attaches via SubclassWindow() to an existing Edit control; all other
///        messages fall through to the control's original window procedure.
/// @threadsafety UI-thread only.
class EditKeyHandler final : public CWindowImpl<EditKeyHandler> {
public:
    // Required by CWindowImpl; unused because we only SubclassWindow(), never Create().
    DECLARE_WND_CLASS(nullptr)

    /// @brief Register the listener fired after any message that can move the
    ///        caret. Survives re-subclassing (the word-wrap recreate).
    void SetCaretNotify(std::function<void()> notify) {
        caretNotify_ = std::move(notify);
    }

    /// @brief Register the listener for Ctrl+wheel zoom. @p steps is signed
    ///        (+1 per notch up); the wheel message is swallowed when Ctrl is
    ///        down so the view does not also scroll.
    void SetWheelZoomNotify(std::function<void(int steps)> notify) {
        wheelZoomNotify_ = std::move(notify);
    }

    /// @brief Register the listener fired just before a mutating message is
    ///        forwarded to the control.
    void SetPreChangeNotify(std::function<void(PendingChange)> notify) {
        preChangeNotify_ = std::move(notify);
    }

    /// @brief Register the provider consulted on Enter (Shift up): a returned
    ///        string ("\r\n" + indent) replaces the keystroke as one edit;
    ///        nullopt keeps the classic Enter.
    void SetEnterIndentProvider(std::function<std::optional<std::wstring>()> provider) {
        enterIndentProvider_ = std::move(provider);
    }

    BEGIN_MSG_MAP(EditKeyHandler)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_CHAR, OnChar)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnCaretMessage)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnCaretMessage)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
        MESSAGE_HANDLER(EM_SETSEL, OnCaretMessage)
        MESSAGE_HANDLER(EM_REPLACESEL, OnReplaceSel)
        MESSAGE_HANDLER(WM_PASTE, OnMutatingMessage)
        MESSAGE_HANDLER(WM_CUT, OnMutatingMessage)
        MESSAGE_HANDLER(WM_CLEAR, OnMutatingMessage)
        MESSAGE_HANDLER(WM_UNDO, OnMutatingMessage)
        MESSAGE_HANDLER(EM_UNDO, OnMutatingMessage)
    END_MSG_MAP()

private:
    LRESULT OnKeyDown(UINT message, WPARAM wParam, LPARAM lParam, BOOL& handled);
    LRESULT OnChar(UINT message, WPARAM wParam, LPARAM lParam, BOOL& handled);
    LRESULT OnCaretMessage(UINT message, WPARAM wParam, LPARAM lParam, BOOL& handled);
    LRESULT OnMouseMove(UINT message, WPARAM wParam, LPARAM lParam, BOOL& handled);
    LRESULT OnMouseWheel(UINT message, WPARAM wParam, LPARAM lParam, BOOL& handled);
    LRESULT OnReplaceSel(UINT message, WPARAM wParam, LPARAM lParam, BOOL& handled);
    LRESULT OnMutatingMessage(UINT message, WPARAM wParam, LPARAM lParam, BOOL& handled);

    void DeleteWordLeft();
    void DeleteWordRight();
    void NotifyCaret();
    void NotifyPreChange(PendingChange::Kind kind, std::wstring inserted = {});

    std::function<void()> caretNotify_;
    std::function<void(int)> wheelZoomNotify_;
    std::function<void(PendingChange)> preChangeNotify_;
    std::function<std::optional<std::wstring>()> enterIndentProvider_;
    int wheelRemainder_ = 0;  // Accumulates sub-notch deltas from fine-scroll wheels.
    bool inWordDelete_ = false;  // Synthetic VK_DELETEs must not re-enter word-delete.
};

} // namespace notepadxp::editor

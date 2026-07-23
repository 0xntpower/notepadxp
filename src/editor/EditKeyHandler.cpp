#include "editor/EditKeyHandler.hpp"

#include <cwctype>
#include <string>

namespace notepadxp::editor {

namespace {

bool IsCtrlDown() {
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

bool IsAltDown() {
    return (GetKeyState(VK_MENU) & 0x8000) != 0;
}

bool IsSpace(wchar_t ch) {
    return std::iswspace(static_cast<wint_t>(ch)) != 0;
}

// The full control text; selection indices from EM_GETSEL are offsets into this
// buffer (the trailing CR/LF of each line are counted, matching GetWindowText).
std::wstring ControlText(CEdit& edit) {
    const int length = edit.GetWindowTextLength();
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    if (length > 0) {
        edit.GetWindowText(text.data(), length + 1);
    }
    text.resize(static_cast<size_t>(length));
    return text;
}

} // namespace

LRESULT EditKeyHandler::OnKeyDown(UINT /*message*/, WPARAM wParam, LPARAM lParam, BOOL& handled) {
    if (IsCtrlDown() && !IsAltDown()) {  // exclude AltGr (Ctrl+Alt)
        if (wParam == VK_BACK) {
            DeleteWordLeft();
            handled = TRUE;
            return 0;
        }
        if (wParam == VK_DELETE) {
            DeleteWordRight();
            handled = TRUE;
            return 0;
        }
    }
    if (wParam == VK_DELETE) {
        // Plain (or Shift+) Delete mutates on the keydown itself; with a
        // selection either form removes exactly the selection.
        NotifyPreChange(PendingChange::Kind::DeleteOne);
    }
    // Everything else (arrows, Home/End, PgUp/PgDn, ...) may move the caret:
    // let the control process it first, then report.
    return OnCaretMessage(WM_KEYDOWN, wParam, lParam, handled);
}

LRESULT EditKeyHandler::OnCaretMessage(UINT message, WPARAM wParam, LPARAM lParam,
                                       BOOL& /*handled*/) {
    // DefWindowProc on a subclassed window calls the control's original window
    // procedure; notify only after the control has updated the caret.
    const LRESULT result = DefWindowProc(message, wParam, lParam);
    NotifyCaret();
    return result;
}

LRESULT EditKeyHandler::OnMouseMove(UINT message, WPARAM wParam, LPARAM lParam, BOOL& handled) {
    if (GetCapture() != m_hWnd) {  // The caret only moves while drag-selecting.
        handled = FALSE;
        return 0;
    }
    return OnCaretMessage(message, wParam, lParam, handled);
}

LRESULT EditKeyHandler::OnMouseWheel(UINT /*message*/, WPARAM wParam, LPARAM /*lParam*/,
                                     BOOL& handled) {
    if ((GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) == 0 || !wheelZoomNotify_) {
        handled = FALSE;  // Plain wheel: let the control scroll.
        return 0;
    }
    wheelRemainder_ += GET_WHEEL_DELTA_WPARAM(wParam);
    const int steps = wheelRemainder_ / WHEEL_DELTA;
    if (steps != 0) {
        wheelRemainder_ -= steps * WHEEL_DELTA;
        wheelZoomNotify_(steps);
    }
    return 0;  // Swallowed: Ctrl+wheel must not also scroll.
}

void EditKeyHandler::NotifyCaret() {
    if (caretNotify_) {
        caretNotify_();
    }
}

LRESULT EditKeyHandler::OnChar(UINT /*message*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& handled) {
    // Ctrl+Backspace also posts WM_CHAR 0x7F (DEL); swallow it so the control
    // does not insert the box glyph (the word is removed in OnKeyDown).
    if (wParam == 0x7F) {
        handled = TRUE;
        return 0;
    }
    // Predict the mutation for the document shadow before the control acts.
    const auto ch = static_cast<wchar_t>(wParam);
    if (ch == L'\b') {
        NotifyPreChange(PendingChange::Kind::BackspaceOne);
    } else if (ch == L'\r' || ch == L'\n') {
        // Smart indent: replace the keystroke with newline + computed indent
        // as one edit (one undo unit). Shift+Enter keeps the classic Enter.
        if (enterIndentProvider_ && (GetKeyState(VK_SHIFT) & 0x8000) == 0) {
            if (const auto replacement = enterIndentProvider_()) {
                CEdit edit(m_hWnd);
                edit.ReplaceSel(replacement->c_str(), TRUE);  // Re-enters via our hook.
                handled = TRUE;
                return 0;
            }
        }
        NotifyPreChange(PendingChange::Kind::ReplaceSelection, L"\r\n");
    } else if (ch == L'\t' || ch >= 0x20) {
        NotifyPreChange(PendingChange::Kind::ReplaceSelection, std::wstring(1, ch));
    }
    handled = FALSE;  // Let the Edit control process the character.
    return 0;
}

LRESULT EditKeyHandler::OnReplaceSel(UINT message, WPARAM wParam, LPARAM lParam, BOOL& handled) {
    const auto* text = reinterpret_cast<const wchar_t*>(lParam);
    NotifyPreChange(PendingChange::Kind::ReplaceSelection,
                    text != nullptr ? std::wstring(text) : std::wstring());
    return OnCaretMessage(message, wParam, lParam, handled);
}

LRESULT EditKeyHandler::OnMutatingMessage(UINT message, WPARAM wParam, LPARAM lParam,
                                          BOOL& handled) {
    switch (message) {
        case WM_PASTE: {
            // The control inserts the clipboard's CF_UNICODETEXT verbatim (and,
            // like us, stops at the first NUL); read it as the prediction.
            std::wstring clip;
            bool known = false;
            if (OpenClipboard() != FALSE) {
                if (const HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
                    if (const auto* chars = static_cast<const wchar_t*>(GlobalLock(data))) {
                        clip = chars;
                        GlobalUnlock(data);
                        known = true;
                    }
                }
                CloseClipboard();
            }
            if (known) {
                NotifyPreChange(PendingChange::Kind::ReplaceSelection, std::move(clip));
            } else {
                NotifyPreChange(PendingChange::Kind::Unknown);
            }
            break;
        }
        case WM_CUT:
        case WM_CLEAR:
            NotifyPreChange(PendingChange::Kind::ReplaceSelection, std::wstring());
            break;
        default:  // WM_UNDO / EM_UNDO: the restored content is unpredictable.
            NotifyPreChange(PendingChange::Kind::Unknown);
            break;
    }
    return OnCaretMessage(message, wParam, lParam, handled);
}

void EditKeyHandler::NotifyPreChange(PendingChange::Kind kind, std::wstring inserted) {
    if (!preChangeNotify_) {
        return;
    }
    PendingChange pending;
    pending.kind = kind;
    pending.inserted = std::move(inserted);
    CEdit edit(m_hWnd);
    edit.GetSel(pending.selStart, pending.selEnd);
    preChangeNotify_(std::move(pending));
}

void EditKeyHandler::DeleteWordLeft() {
    CEdit edit(m_hWnd);
    int start = 0;
    int end = 0;
    edit.GetSel(start, end);
    if (start != end) {  // A selection deletes like a plain Backspace.
        edit.ReplaceSel(L"", TRUE);
        return;
    }
    if (start <= 0) {
        return;
    }

    const std::wstring text = ControlText(edit);
    int boundary = start;
    while (boundary > 0 && IsSpace(text[static_cast<size_t>(boundary) - 1])) {
        --boundary;  // skip the whitespace run (including any line break)
    }
    while (boundary > 0 && !IsSpace(text[static_cast<size_t>(boundary) - 1])) {
        --boundary;  // skip the word
    }
    edit.SetSel(boundary, start);
    edit.ReplaceSel(L"", TRUE);
    edit.SendMessageW(EM_SCROLLCARET);
}

void EditKeyHandler::DeleteWordRight() {
    CEdit edit(m_hWnd);
    int start = 0;
    int end = 0;
    edit.GetSel(start, end);
    if (start != end) {
        edit.ReplaceSel(L"", TRUE);
        return;
    }

    const std::wstring text = ControlText(edit);
    const int length = static_cast<int>(text.size());
    if (start >= length) {
        return;
    }
    int boundary = start;
    while (boundary < length && !IsSpace(text[static_cast<size_t>(boundary)])) {
        ++boundary;  // skip the word
    }
    while (boundary < length && IsSpace(text[static_cast<size_t>(boundary)])) {
        ++boundary;  // skip the trailing whitespace run
    }
    edit.SetSel(start, boundary);
    edit.ReplaceSel(L"", TRUE);
    edit.SendMessageW(EM_SCROLLCARET);
}

} // namespace notepadxp::editor

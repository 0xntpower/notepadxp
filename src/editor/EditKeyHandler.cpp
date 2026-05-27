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

LRESULT EditKeyHandler::OnKeyDown(UINT /*message*/, WPARAM wParam, LPARAM /*lParam*/,
                                  BOOL& handled) {
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
    handled = FALSE;  // Everything else: let the Edit control handle it.
    return 0;
}

LRESULT EditKeyHandler::OnChar(UINT /*message*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& handled) {
    // Ctrl+Backspace also posts WM_CHAR 0x7F (DEL); swallow it so the control
    // does not insert the box glyph (the word is removed in OnKeyDown).
    if (wParam == 0x7F) {
        handled = TRUE;
        return 0;
    }
    handled = FALSE;
    return 0;
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

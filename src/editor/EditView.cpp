#include "editor/EditView.hpp"

#include <string>
#include <string_view>

#include "Resource.h"

namespace notepadxp::editor {

DWORD EditView::StyleFor(bool wordWrap) noexcept {
    // ES_NOHIDESEL keeps the selection visible while the modeless Find dialog
    // holds focus. A multiline Edit wraps when WS_HSCROLL is absent and scrolls
    // horizontally when it is present — that single bit is the word-wrap toggle.
    constexpr DWORD kEsStd =
        WS_CHILD | WS_VSCROLL | WS_VISIBLE | ES_MULTILINE | ES_NOHIDESEL;
    return wordWrap ? kEsStd : (kEsStd | WS_HSCROLL);
}

bool EditView::Create(HWND parent, bool wordWrap) {
    parent_ = parent;
    wordWrap_ = wordWrap;

    RECT empty = {0, 0, 0, 0};
    const HWND created = edit_.Create(parent, &empty, nullptr, StyleFor(wordWrap),
                                      WS_EX_CLIENTEDGE, static_cast<UINT>(ID_EDIT));
    if (created == nullptr) {
        return false;
    }
    edit_.LimitText(0);  // 0 == remove the default text-length cap.
    keyHandler_.SubclassWindow(edit_.m_hWnd);  // Add the word-delete shortcuts.
    keyHandler_.SetCaretNotify([this] { NotifyCaretMaybeMoved(); });
    keyHandler_.SetPreChangeNotify([this](PendingChange pending) {
        OnPreChange(std::move(pending));
    });
    undo_.Reset();
    shadow_.Reset();
    return true;
}

void EditView::OnPreChange(PendingChange pending) {
    if (suppressRecording_) {
        return;  // Our own programmatic splices sync the shadow directly.
    }
    if (!shadow_.IsMaterialized()) {
        // First modification attempt: the one-time full read that makes
        // read-only viewing free.
        shadow_.Materialize(GetText());
    }
    shadow_.SetPending(std::move(pending));
}

void EditView::NotifyCaretMaybeMoved() {
    if (!caretMoved_ || edit_.m_hWnd == nullptr) {
        return;
    }
    int line = 1;
    int col = 1;
    GetCaretLineCol(line, col);
    if (line == lastCaretLine_ && col == lastCaretCol_) {
        return;
    }
    lastCaretLine_ = line;
    lastCaretCol_ = col;
    caretMoved_();
}

void EditView::Layout(const RECT& rect) {
    if (edit_.m_hWnd != nullptr) {
        edit_.MoveWindow(&rect, TRUE);
    }
}

void EditView::SetFocusToEdit() {
    if (edit_.m_hWnd != nullptr) {
        edit_.SetFocus();
    }
}

bool EditView::SetWordWrap(bool wordWrap) {
    if (edit_.m_hWnd == nullptr) {
        return false;
    }
    if (wordWrap == wordWrap_) {
        return true;
    }

    // Preserve the document, modify flag and caret across the recreate.
    const int length = edit_.GetWindowTextLength();
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    if (length > 0) {
        edit_.GetWindowText(text.data(), length + 1);
    }
    text.resize(static_cast<size_t>(length));
    const BOOL modified = edit_.GetModify();
    int selStart = 0;
    int selEnd = 0;
    edit_.GetSel(selStart, selEnd);

    CEdit replacement;
    RECT empty = {0, 0, 0, 0};
    const HWND created = replacement.Create(parent_, &empty, nullptr, StyleFor(wordWrap),
                                            WS_EX_CLIENTEDGE, static_cast<UINT>(ID_EDIT));
    if (created == nullptr) {
        return false;  // Leave the existing control untouched.
    }
    replacement.LimitText(0);

    keyHandler_.UnsubclassWindow();  // Detach before the old control is destroyed.
    edit_.DestroyWindow();
    edit_ = created;
    keyHandler_.SubclassWindow(edit_.m_hWnd);  // Re-attach to the recreated control.
    wordWrap_ = wordWrap;

    ReapplyFont();
    suppressRecording_ = true;  // Re-set text on the new control is not a user edit.
    edit_.SetWindowText(text.c_str());
    suppressRecording_ = false;
    edit_.SetModify(modified);
    edit_.SetSel(selStart, selEnd);
    edit_.SendMessage(EM_SCROLLCARET);
    return true;
}

void EditView::SetFont(const LOGFONTW& logFont) {
    const HFONT font = CreateFontIndirectW(&logFont);
    if (font == nullptr) {
        return;
    }
    fontGuard_.Reset(font);
    ReapplyFont();
}

void EditView::ReapplyFont() {
    if (edit_.m_hWnd != nullptr && fontGuard_.IsValid()) {
        edit_.SetFont(fontGuard_.AsFont(), TRUE);
    }
}

void EditView::Undo() {
    if (const auto unit = undo_.Undo()) {
        // Invert: remove what the unit inserted, restore what it removed.
        ApplyDelta(unit->pos, unit->inserted.size(), unit->removed, unit->selStartBefore,
                   unit->selEndBefore);
    }
}

void EditView::Redo() {
    if (const auto unit = undo_.Redo()) {
        ApplyDelta(unit->pos, unit->removed.size(), unit->inserted, unit->selStartAfter,
                   unit->selEndAfter);
    }
}

void EditView::OnEditChanged() {
    if (suppressRecording_) {
        return;
    }
    int selStart = 0;
    int selEnd = 0;
    edit_.GetSel(selStart, selEnd);
    const auto delta = shadow_.CaptureChange(static_cast<size_t>(TextLength()), selStart, selEnd,
                                             [this] { return GetText(); });
    if (delta.has_value()) {
        undo_.RecordChange(*delta);
    }
    NotifyCaretMaybeMoved();  // Edits move the caret without an EM_SETSEL.
}

void EditView::ApplyDelta(size_t pos, size_t removeLen, const std::wstring& insertText,
                          int selStart, int selEnd) {
    suppressRecording_ = true;
    edit_.SetSel(static_cast<int>(pos), static_cast<int>(pos + removeLen));
    edit_.ReplaceSel(insertText.c_str(), FALSE);
    edit_.SetSel(selStart, selEnd);
    edit_.SendMessage(EM_SCROLLCARET);
    edit_.SetModify(TRUE);  // The document differs from its on-disk form again.
    suppressRecording_ = false;
    shadow_.ApplyExternal(pos, removeLen, insertText);
}

void EditView::Cut() {
    edit_.Cut();
}

void EditView::Copy() {
    edit_.Copy();
}

void EditView::Paste() {
    edit_.Paste();
}

void EditView::DeleteSelection() {
    edit_.Clear();  // WM_CLEAR removes the current selection.
}

void EditView::SelectAll() {
    edit_.SetSelAll();
}

void EditView::InsertText(std::wstring_view text) {
    const std::wstring buffer(text);
    edit_.ReplaceSel(buffer.c_str(), TRUE);  // TRUE: a single undo unit.
}

void EditView::GoToLine(int lineNumber) {
    if (lineNumber < 1) {
        lineNumber = 1;
    }
    int charIndex = edit_.LineIndex(lineNumber - 1);
    if (charIndex < 0) {
        // Past the last line: drop to the start of the final line.
        charIndex = edit_.LineIndex(edit_.GetLineCount() - 1);
        if (charIndex < 0) {
            charIndex = 0;
        }
    }
    edit_.SetSel(charIndex, charIndex);
    edit_.SendMessage(EM_SCROLLCARET);
}

void EditView::Reset() {
    suppressRecording_ = true;
    edit_.SetWindowText(L"");
    edit_.SetModify(FALSE);
    edit_.SetSel(0, 0);
    suppressRecording_ = false;
    undo_.Reset();
    shadow_.Reset();
}

void EditView::SetText(std::wstring_view text) {
    const std::wstring buffer(text);
    suppressRecording_ = true;
    edit_.SetWindowText(buffer.c_str());
    edit_.SetModify(FALSE);
    edit_.SetSel(0, 0);
    edit_.SendMessage(EM_SCROLLCARET);
    suppressRecording_ = false;
    undo_.Reset();
    shadow_.Reset();  // Re-materializes lazily on the first edit.
}

std::wstring EditView::GetText() {
    const int length = edit_.GetWindowTextLength();
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    if (length > 0) {
        edit_.GetWindowText(text.data(), length + 1);
    }
    text.resize(static_cast<size_t>(length));
    return text;
}

void EditView::MoveCaretToEnd() {
    const int length = edit_.GetWindowTextLength();
    edit_.SetSel(length, length);
    edit_.SendMessage(EM_SCROLLCARET);
}

void EditView::GetSelection(int& startOut, int& endOut) {
    int start = 0;
    int end = 0;
    edit_.GetSel(start, end);
    startOut = start;
    endOut = end;
}

void EditView::SelectRange(int start, int end) {
    edit_.SetSel(start, end);
    edit_.SendMessage(EM_SCROLLCARET);
}

bool EditView::CanUndo() {
    return undo_.CanUndo();
}

bool EditView::CanRedo() {
    return undo_.CanRedo();
}

bool EditView::HasSelection() {
    int selStart = 0;
    int selEnd = 0;
    edit_.GetSel(selStart, selEnd);
    return selStart != selEnd;
}

int EditView::TextLength() {
    return edit_.GetWindowTextLength();
}

bool EditView::IsModified() {
    return edit_.GetModify() != FALSE;
}

void EditView::SetModified(bool modified) {
    edit_.SetModify(modified ? TRUE : FALSE);
}

void EditView::GetCaretLineCol(int& lineOut, int& colOut) {
    int selStart = 0;
    int selEnd = 0;
    edit_.GetSel(selStart, selEnd);
    const int line = edit_.LineFromChar(selStart);
    const int lineStart = edit_.LineIndex(line);
    lineOut = line + 1;
    colOut = selStart - lineStart + 1;
}

} // namespace notepadxp::editor

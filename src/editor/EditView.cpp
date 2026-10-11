#include "editor/EditView.hpp"

#include <algorithm>
#include <string>
#include <string_view>

#include "Resource.h"
#include "lang/IndentEngine.hpp"

namespace notepadxp::editor {

DWORD EditView::StyleFor(bool wordWrap, bool heavy) noexcept {
    // ES_NOHIDESEL keeps the selection visible while the modeless Find dialog
    // holds focus. A multiline Edit wraps when WS_HSCROLL is absent and scrolls
    // horizontally when it is present — that single bit is the word-wrap toggle.
    constexpr DWORD kEsStd =
        WS_CHILD | WS_VSCROLL | WS_VISIBLE | ES_MULTILINE | ES_NOHIDESEL;
    if (heavy) {
        return kEsStd | WS_HSCROLL;  // FastEdit: no wrap, horizontal scrolling.
    }
    return wordWrap ? kEsStd : (kEsStd | WS_HSCROLL);
}

const wchar_t* EditView::ClassNameFor(bool heavy) noexcept {
    return heavy ? FastEditClassName() : L"EDIT";
}

bool EditView::Create(HWND parent, bool wordWrap) {
    parent_ = parent;
    wordWrap_ = wordWrap;
    RegisterFastEdit(GetModuleHandleW(nullptr));  // Idempotent; used in heavy mode.

    RECT empty = {0, 0, 0, 0};
    const HWND created = edit_.Create(parent, &empty, nullptr, StyleFor(wordWrap, false),
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
    keyHandler_.SetEnterIndentProvider([this] { return ComputeEnterReplacement(); });
    undo_.Reset();
    shadow_.Reset();
    return true;
}

bool EditView::RecreateControl(bool heavy, bool wordWrap, bool preserveText) {
    if (edit_.m_hWnd == nullptr) {
        return false;
    }

    std::wstring text;
    BOOL modified = FALSE;
    int selStart = 0;
    int selEnd = 0;
    if (preserveText) {
        text = GetText();
        modified = edit_.GetModify();
        edit_.GetSel(selStart, selEnd);
    }

    // Create at the live size: a zero-width wrapping control would first wrap
    // the whole document one character per line.
    RECT bounds = {0, 0, 0, 0};
    edit_.GetWindowRect(&bounds);
    ::MapWindowPoints(nullptr, parent_, reinterpret_cast<POINT*>(&bounds), 2);
    const HWND created = ::CreateWindowExW(
        WS_EX_CLIENTEDGE, ClassNameFor(heavy), L"", StyleFor(wordWrap, heavy),
        bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
        parent_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_EDIT)),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent_, GWLP_HINSTANCE)), nullptr);
    if (created == nullptr) {
        return false;  // Leave the existing control untouched.
    }

    keyHandler_.UnsubclassWindow();  // Detach before the old control is destroyed.
    edit_.DestroyWindow();
    edit_ = created;
    keyHandler_.SubclassWindow(edit_.m_hWnd);  // Re-attach to the new control.
    heavy_ = heavy;
    wordWrap_ = heavy ? false : wordWrap;
    edit_.LimitText(0);
    ReapplyFont();
    ApplyTabStops();  // The language's tab stops must survive the recreate.

    if (preserveText) {
        suppressRecording_ = true;  // Re-set text on the new control is not a user edit.
        edit_.SetWindowText(text.c_str());
        edit_.SetModify(modified);
        edit_.SetSel(selStart, selEnd);
        suppressRecording_ = false;
    }
    return true;
}

std::optional<std::wstring> EditView::ComputeEnterReplacement() {
    if (language_ == lang::Language::PlainText || language_ == lang::Language::Log) {
        return std::nullopt;  // Classic Enter — logs and plain text never surprise.
    }
    int selStart = 0;
    int selEnd = 0;
    edit_.GetSel(selStart, selEnd);
    const int line = edit_.LineFromChar(selStart);
    const int lineStart = edit_.LineIndex(line);
    const int upToCaret = selStart - lineStart;
    if (upToCaret <= 0) {
        return std::wstring(L"\r\n");
    }
    std::wstring lineText(static_cast<size_t>(upToCaret), L'\0');
    const int copied = edit_.GetLine(line, lineText.data(), upToCaret);
    lineText.resize(static_cast<size_t>(std::clamp(copied, 0, upToCaret)));
    return L"\r\n" + lang::ComputeEnterIndent(lineText, lang::TraitsFor(language_), indentStyle_);
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
    if (edit_.m_hWnd == nullptr || wordWrap == wordWrap_) {
        return edit_.m_hWnd != nullptr;
    }
    if (heavy_) {
        return false;  // FastEdit has no wrap; the frame disables the toggle.
    }
    return RecreateControl(false, wordWrap, true);
}

void EditView::SetLanguageContext(lang::Language language, lang::IndentStyle indentStyle) {
    language_ = language;
    indentStyle_ = indentStyle;
    ApplyTabStops();
}

void EditView::ApplyTabStops() {
    if (edit_.m_hWnd == nullptr) {
        return;
    }
    // EM_SETTABSTOPS takes dialog-template units; 4 units ~= one average char
    // (the control default of 32 == 8 characters).
    int stops = lang::TraitsFor(language_).tabStopChars * 4;
    edit_.SetTabStops(stops);
    edit_.Invalidate();
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
    // The lock is O(1) and released at the end of this statement, before any
    // further message reaches the control.
    const auto delta = shadow_.CaptureChange(selStart, selEnd, LockText().View());
    if (delta.has_value()) {
        undo_.RecordChange(*delta);
    }
    NotifyCaretMaybeMoved();  // Edits move the caret without an EM_SETSEL.
}

void EditView::ApplyDelta(size_t pos, size_t removeLen, const std::wstring& insertText,
                          int selStart, int selEnd) {
    suppressRecording_ = true;
    if (CanFastSplice(pos, removeLen, insertText)) {
        // The classic Edit control rebuilds its whole line table for any
        // selection-replacing edit (EM_REPLACESEL, backspace-on-selection) —
        // O(document), ~half a second on a 10 MB file — while its single-char
        // no-line-break path is O(edit). Replay small units through the fast
        // path: delete one char at a time, then re-type the insertion.
        edit_.SetSel(static_cast<int>(pos + removeLen), static_cast<int>(pos + removeLen));
        for (size_t i = 0; i < removeLen; ++i) {
            edit_.SendMessage(WM_CHAR, L'\b', 0);
        }
        for (const wchar_t ch : insertText) {
            edit_.SendMessage(WM_CHAR, static_cast<WPARAM>(ch), 0);
        }
    } else {
        edit_.SetSel(static_cast<int>(pos), static_cast<int>(pos + removeLen));
        edit_.ReplaceSel(insertText.c_str(), FALSE);
    }
    edit_.SetSel(selStart, selEnd);
    edit_.SendMessage(EM_SCROLLCARET);
    edit_.SetModify(TRUE);  // The document differs from its on-disk form again.
    suppressRecording_ = false;
    shadow_.ApplyExternal(pos, removeLen, insertText);
}

bool EditView::CanFastSplice(size_t pos, size_t removeLen, const std::wstring& insertText) const {
    constexpr size_t kMaxFastEdit = 64;
    if (removeLen > kMaxFastEdit || insertText.size() > kMaxFastEdit) {
        return false;
    }
    for (const wchar_t ch : insertText) {
        if (ch != L'\t' && ch < 0x20) {
            return false;  // Line breaks / control chars need the generic path.
        }
    }
    if (!shadow_.IsMaterialized()) {
        return false;
    }
    const std::wstring& text = shadow_.Text();
    if (pos + removeLen > text.size()) {
        return false;
    }
    for (size_t i = pos; i < pos + removeLen; ++i) {
        if (text[i] == L'\r' || text[i] == L'\n') {
            return false;  // Removing a line break forces the rebuild anyway.
        }
    }
    return true;
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

void EditView::InsertText(const std::wstring& text) {
    edit_.ReplaceSel(text.c_str(), TRUE);  // TRUE: a single undo unit.
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
    if (heavy_) {
        suppressRecording_ = true;
        (void)RecreateControl(false, wordWrap_, false);  // Failure keeps FastEdit; harmless.
        suppressRecording_ = false;
    }
    suppressRecording_ = true;
    edit_.SetWindowText(L"");
    edit_.SetModify(FALSE);
    edit_.SetSel(0, 0);
    suppressRecording_ = false;
    undo_.Reset();
    shadow_.Reset();
}

void EditView::SetText(const std::wstring& text) {
    const bool wantHeavy = text.size() >= kHeavyTextChars;
    if (wantHeavy != heavy_) {
        suppressRecording_ = true;
        (void)RecreateControl(wantHeavy, wordWrap_, false);  // Failure: stock control still works.
        suppressRecording_ = false;
    }
    suppressRecording_ = true;
    edit_.SetWindowText(text.c_str());
    edit_.SetModify(FALSE);
    edit_.SetSel(0, 0);
    edit_.SendMessage(EM_SCROLLCARET);
    suppressRecording_ = false;
    undo_.Reset();
    shadow_.Reset();  // Re-materializes lazily on the first edit.
}
std::wstring EditView::GetText() {
    return std::wstring(LockText().View());
}

void EditView::ReadText(const std::function<void(std::wstring_view)>& read) {
    read(LockText().View());  // The lock is released at the end of the statement.
}

std::wstring EditView::GetTextPrefix(size_t maxChars) {
    const LockedText locked(edit_.m_hWnd);
    const std::wstring_view view = locked.View();
    return std::wstring(view.substr(0, std::min(view.size(), maxChars)));
}

void EditView::MoveCaretToEnd() {
    const int length = edit_.GetWindowTextLength();
    edit_.SetSel(length, length);
    edit_.SendMessage(EM_SCROLLCARET);
}

void EditView::AppendExternal(std::wstring_view text) {
    const BOOL wasModified = edit_.GetModify();  // Disk content: not a user edit.
    suppressRecording_ = true;
    const int end = edit_.GetWindowTextLength();
    edit_.SetSel(end, end);
    const std::wstring buffer(text);
    edit_.ReplaceSel(buffer.c_str(), FALSE);
    edit_.SetModify(wasModified);
    edit_.SendMessage(EM_SCROLLCARET);
    suppressRecording_ = false;
    shadow_.Append(text);
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

void EditView::ExpandSelectionToLines(int& startCharOut, int& endCharOut) {
    int selStart = 0;
    int selEnd = 0;
    edit_.GetSel(selStart, selEnd);
    const int firstLine = edit_.LineFromChar(selStart);
    startCharOut = edit_.LineIndex(firstLine);
    int lastLine = edit_.LineFromChar(selEnd);
    if (selEnd > selStart && edit_.LineIndex(lastLine) == selEnd) {
        --lastLine;  // Selection ends at a line start: that line is not included.
    }
    const int lastLineStart = edit_.LineIndex(lastLine);
    endCharOut = lastLineStart + edit_.LineLength(lastLineStart);
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

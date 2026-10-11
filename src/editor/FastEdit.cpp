#include "editor/FastEdit.hpp"

#include <algorithm>
#include <cwchar>
#include <cwctype>

namespace notepadxp::editor {

namespace {

// A break character (either half of CRLF counts).
bool IsBreakChar(wchar_t c) noexcept {
    return c == L'\r' || c == L'\n';
}

// Break length in an arbitrary buffer (the doc text or an insertion string).
size_t BreakLengthIn(std::wstring_view text, size_t pos) noexcept {
    if (pos >= text.size()) {
        return 0;
    }
    if (text[pos] == L'\r') {
        return pos + 1 < text.size() && text[pos + 1] == L'\n' ? 2 : 1;
    }
    if (text[pos] == L'\n') {
        return 1;
    }
    return 0;
}



int XParam(LPARAM lParam) noexcept {
    return static_cast<int>(static_cast<short>(LOWORD(lParam)));
}

int YParam(LPARAM lParam) noexcept {
    return static_cast<int>(static_cast<short>(HIWORD(lParam)));
}

} // namespace

void FastEditDoc::SetText(std::wstring_view text) {
    text_.assign(text);
    Rebuild();
}

void FastEditDoc::Rebuild() {
    starts_.clear();
    starts_.push_back(0);
    const size_t size = text_.size();
    for (size_t i = 0; i < size;) {
        if (text_[i] == L'\r') {
            const size_t next = i + 1 < size && text_[i + 1] == L'\n' ? i + 2 : i + 1;
            starts_.push_back(next);
            i = next;
        } else if (text_[i] == L'\n') {
            starts_.push_back(i + 1);
            ++i;
        } else {
            ++i;
        }
    }
}

int FastEditDoc::LineStart(int line) const noexcept {
    if (line < 0 || static_cast<size_t>(line) >= starts_.size()) {
        return -1;
    }
    return static_cast<int>(starts_[static_cast<size_t>(line)]);
}

int FastEditDoc::LineFromChar(int charIndex) const noexcept {
    if (starts_.empty()) {
        return 0;
    }
    if (charIndex < 0) {
        return static_cast<int>(starts_.size() - 1);
    }
    size_t idx = static_cast<size_t>(charIndex);
    if (idx > text_.size()) {
        idx = text_.size();
    }
    // First start greater than idx; the answer is the line before it.
    const auto it = std::upper_bound(starts_.begin(), starts_.end(), idx);
    return static_cast<int>(static_cast<size_t>(it - starts_.begin()) - 1);
}

int FastEditDoc::LineLength(int charIndex) const noexcept {
    const int line = LineFromChar(charIndex);
    const int start = LineStart(line);
    if (start < 0) {
        return 0;
    }
    size_t end = static_cast<size_t>(start);
    while (end < text_.size() && !IsBreakChar(text_[end])) {
        ++end;
    }
    return static_cast<int>(end - static_cast<size_t>(start));
}

std::wstring_view FastEditDoc::LineText(int line) const noexcept {
    const int start = LineStart(line);
    if (start < 0) {
        return {};
    }
    return std::wstring_view(text_).substr(static_cast<size_t>(start),
                                           static_cast<size_t>(LineLength(start)));
}

size_t FastEditDoc::BreakLength(size_t pos) const noexcept {
    return BreakLengthIn(text_, pos);
}

void FastEditDoc::Splice(size_t pos, size_t removeLen, std::wstring_view insert) {
    if (pos > text_.size()) {
        pos = text_.size();
    }
    if (removeLen > text_.size() - pos) {
        removeLen = text_.size() - pos;
    }
    const long long delta = static_cast<long long>(insert.size()) -
                            static_cast<long long>(removeLen);
    const size_t removeEnd = pos + removeLen;

    // The line containing (or starting at) pos; its start survives any edit.
    const size_t firstLine = static_cast<size_t>(LineFromChar(static_cast<int>(pos)));
    const size_t regionStart = starts_[firstLine];

    // Drop every line start inside the removed range — a start at pos survives
    // (pos stays a boundary); a start at removeEnd is decided below, since the
    // removal may cut a CRLF in half.
    auto it = starts_.erase(std::upper_bound(starts_.begin(), starts_.end(), pos),
                            std::upper_bound(starts_.begin(), starts_.end(), removeEnd));

    // The first surviving start past the edit bounds the rescan region — but a
    // start is only real if the break before it survived the removal. When the
    // removal swallowed (part of) that break, the line merged away and its
    // start entry must go too.
    while (it != starts_.end() && *it > 0) {
        const size_t s = *it;
        size_t breakStart = s - 1;
        if (text_[s - 1] == L'\n' && s >= 2 && text_[s - 2] == L'\r') {
            breakStart = s - 2;
        }
        if (text_[s - 1] != L'\r' && text_[s - 1] != L'\n') {
            break;  // Not a break-terminated line (defensive; cannot happen).
        }
        if (breakStart < pos || breakStart >= removeEnd) {
            break;  // The break before this line was not disturbed.
        }
        it = starts_.erase(it);
    }

    const size_t oldSize = text_.size();
    const size_t anchor = it != starts_.end() ? *it : oldSize;
    const size_t insertIndex = static_cast<size_t>(it - starts_.begin());  // `it` reaches end() below.

    // Shift everything past the edit by delta.
    for (; it != starts_.end(); ++it) {
        *it = static_cast<size_t>(static_cast<long long>(*it) + delta);
    }

    text_.replace(pos, removeLen, insert);

    // Rebuild the line starts of the whole touched region: the removal may cut
    // a CRLF in half (a lone survivor is still a break) and the insertion may
    // add breaks of its own. A break ending exactly at the region end is the
    // anchor's own break — the shifted anchor entry already covers it.
    const size_t regionEnd = anchor < oldSize
                                 ? static_cast<size_t>(static_cast<long long>(anchor) +
                                                       delta)
                                 : text_.size();
    std::vector<size_t> rebuilt;
    for (size_t i = regionStart; i < regionEnd;) {
        const size_t bl = BreakLengthIn(text_, i);
        if (bl != 0 && i + bl < regionEnd) {
            rebuilt.push_back(i + bl);
            i += bl;
        } else {
            ++i;
        }
    }
    starts_.insert(starts_.begin() + static_cast<std::ptrdiff_t>(insertIndex),
                   rebuilt.begin(), rebuilt.end());
}
// --- The control -----------------------------------------------------------

namespace {

constexpr wchar_t kClassName[] = L"NxpFastEdit";
UINT g_viewMessage = 0;

struct Metrics {
    int lineHeight = 16;
    int charW = 8;      // Average char width (exact column width when mono).
    bool mono = true;   // TMPF_FIXED_PITCH == 0.
};

struct Impl {
    FastEditDoc doc;
    size_t anchor = 0;  // Selection anchor (char index).
    size_t caret = 0;   // Caret / active selection end (char index).
    size_t goalCol = 0; // Display column preserved across Up/Down.
    bool modified = false;

    HFONT font = nullptr;  // Not owned (the caller owns it, like WM_SETFONT).
    Metrics metrics;
    bool metricsValid = false;

    int tabCols = 32;  // Tab width in display columns (periodic, like EDIT).

    int firstLine = 0;
    int xOffset = 0;  // Pixels.
    int pageLines = 1;
    int clientW = 0;
    int clientH = 0;
    long long maxCols = 0;  // Widest display line (for the H scrollbar).

    bool hasCaret = false;
    bool selecting = false;

    // Double/triple click state.
    ULONGLONG lastClick = 0;
    POINT lastClickPos{};
    int clickCount = 0;

    void EnsureMetrics(HWND hwnd) {
        if (metricsValid) {
            return;
        }
        const HDC dc = GetDC(hwnd);
        TEXTMETRICW tm{};
        HFONT old = nullptr;
        if (font != nullptr) {
            old = static_cast<HFONT>(SelectObject(dc, font));
        }
        GetTextMetricsW(dc, &tm);
        if (old != nullptr) {
            SelectObject(dc, old);
        }
        ReleaseDC(hwnd, dc);
        metrics.lineHeight = tm.tmHeight + tm.tmExternalLeading;
        metrics.charW = tm.tmAveCharWidth > 0 ? tm.tmAveCharWidth : 8;
        metrics.mono = (tm.tmPitchAndFamily & TMPF_FIXED_PITCH) == 0;
        metricsValid = true;
    }

    [[nodiscard]] int SelStart() const noexcept {
        return static_cast<int>(std::min(anchor, caret));
    }
    [[nodiscard]] int SelEnd() const noexcept {
        return static_cast<int>(std::max(anchor, caret));
    }
};

Impl* ImplOf(HWND hwnd) noexcept {
    return reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

// Display columns of a line prefix (tabs expand to the next tab stop). After
// tab expansion one character occupies exactly one column.
int DisplayCols(std::wstring_view line, size_t chars, int tabCols) noexcept {
    int cols = 0;
    int nextStop = tabCols;
    const size_t limit = std::min(chars, line.size());
    for (size_t i = 0; i < limit; ++i) {
        if (line[i] == L'\t') {
            cols = nextStop;
            nextStop += tabCols;
        } else {
            ++cols;
        }
    }
    return cols;
}

// Inverse: the char index within @p line at display column @p col.
size_t CharAtDisplayCol(std::wstring_view line, int col, int tabCols) noexcept {
    int cols = 0;
    int nextStop = tabCols;
    for (size_t i = 0; i < line.size(); ++i) {
        const int next = line[i] == L'\t' ? nextStop : cols + 1;
        if (next > col) {
            return i;
        }
        if (line[i] == L'\t') {
            cols = nextStop;
            nextStop += tabCols;
        } else {
            cols = next;
        }
    }
    return line.size();
}

// Expand tabs to spaces for drawing (exact for monospace fonts).
void ExpandTabs(std::wstring_view line, int tabCols, std::wstring& out) {
    out.clear();
    int cols = 0;
    int nextStop = tabCols;
    for (const wchar_t c : line) {
        if (c == L'\t') {
            out.append(static_cast<size_t>(nextStop - cols), L' ');
            cols = nextStop;
            nextStop += tabCols;
        } else {
            out.push_back(c);
            ++cols;
        }
    }
}

void NotifyChange(HWND hwnd) {
    const HWND parent = GetParent(hwnd);
    if (parent != nullptr) {
        SendMessageW(parent, WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(hwnd), EN_CHANGE), reinterpret_cast<LPARAM>(hwnd));
    }
}

int ClampLine(const Impl& impl, int line) noexcept {
    const int count = impl.doc.LineCount();
    return std::clamp(line, 0, std::max(count - 1, 0));
}

int MaxFirstLine(const Impl& impl) noexcept {
    return std::max(impl.doc.LineCount() - impl.pageLines, 0);
}

int HExtent(const Impl& impl) noexcept {
    return static_cast<int>(std::min<long long>(impl.maxCols * impl.metrics.charW, 1 << 30));
}

void UpdateScrollbars(HWND hwnd, Impl& impl) {
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = std::max(impl.doc.LineCount() - 1, 0);
    si.nPage = static_cast<UINT>(std::max(impl.pageLines, 1));
    si.nPos = std::clamp(impl.firstLine, 0, MaxFirstLine(impl));
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);

    const int extent = std::max(HExtent(impl), 1);
    si.nMax = extent;
    si.nPage = static_cast<UINT>(std::max(impl.clientW, 1));
    si.nPos = std::clamp(impl.xOffset, 0, std::max(extent - impl.clientW, 0));
    SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);
}

void PlaceCaret(HWND /*hwnd*/, Impl& impl) {
    if (!impl.hasCaret) {
        return;
    }
    const int line = impl.doc.LineFromChar(static_cast<int>(impl.caret));
    const int start = std::max(impl.doc.LineStart(line), 0);
    const std::wstring_view lineText = impl.doc.LineText(line);
    const int cols =
        DisplayCols(lineText, impl.caret - static_cast<size_t>(start), impl.tabCols);
    SetCaretPos(cols * impl.metrics.charW - impl.xOffset,
                (line - impl.firstLine) * impl.metrics.lineHeight);
}

void ScrollCaretIntoView(HWND hwnd, Impl& impl) {
    const int line = impl.doc.LineFromChar(static_cast<int>(impl.caret));
    if (line < impl.firstLine) {
        impl.firstLine = line;
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (line >= impl.firstLine + impl.pageLines) {
        impl.firstLine = line - impl.pageLines + 1;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    const int start = std::max(impl.doc.LineStart(line), 0);
    const std::wstring_view lineText = impl.doc.LineText(line);
    const int x =
        DisplayCols(lineText, impl.caret - static_cast<size_t>(start), impl.tabCols) *
        impl.metrics.charW;
    const int lastVisible = impl.clientW - impl.metrics.charW;
    if (x < impl.xOffset) {
        impl.xOffset = std::max(x - impl.metrics.charW, 0);
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (lastVisible > 0 && x > lastVisible) {
        impl.xOffset = x - lastVisible;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    UpdateScrollbars(hwnd, impl);
    PlaceCaret(hwnd, impl);
}

void RefreshAfterEdit(HWND hwnd, Impl& impl, size_t newCaret, bool scrollToCaret) {
    const size_t len = impl.doc.Length();
    impl.caret = std::min(newCaret, len);
    impl.anchor = impl.caret;
    // Keep the running max width honest for the lines we touched.
    const int line = impl.doc.LineFromChar(static_cast<int>(impl.caret));
    for (int i = std::max(line - 1, 0); i <= std::min(line + 1, impl.doc.LineCount() - 1); ++i) {
        const std::wstring_view t = impl.doc.LineText(i);
        impl.maxCols =
            std::max(impl.maxCols, static_cast<long long>(DisplayCols(t, t.size(), impl.tabCols)));
    }
    if (scrollToCaret) {
        ScrollCaretIntoView(hwnd, impl);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
    UpdateScrollbars(hwnd, impl);
    PlaceCaret(hwnd, impl);
}

// Replace the current selection with @p text (EM_REPLACESEL / typing).
void ReplaceSelection(HWND hwnd, Impl& impl, std::wstring_view text, bool scrollToCaret) {
    const size_t start = std::min(impl.anchor, impl.caret);
    const size_t end = std::max(impl.anchor, impl.caret);
    impl.doc.Splice(start, end - start, text);
    impl.modified = true;
    RefreshAfterEdit(hwnd, impl, start + text.size(), scrollToCaret);
    NotifyChange(hwnd);
}

void MoveCaret(HWND hwnd, Impl& impl, size_t pos, bool extend, bool scroll) {
    const size_t len = impl.doc.Length();
    impl.caret = std::min(pos, len);
    if (!extend) {
        impl.anchor = impl.caret;
    }
    if (scroll) {
        ScrollCaretIntoView(hwnd, impl);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
    PlaceCaret(hwnd, impl);
}

// Word boundaries for double-click (alphanumeric runs, EDIT-like).
size_t WordStart(std::wstring_view text, size_t pos) noexcept {
    if (pos >= text.size()) {
        return text.size();
    }
    const bool alnum = std::iswalnum(static_cast<wint_t>(text[pos])) != 0;
    size_t i = pos;
    while (i > 0 && (std::iswalnum(static_cast<wint_t>(text[i - 1])) != 0) == alnum) {
        --i;
    }
    return i;
}

size_t WordEnd(std::wstring_view text, size_t pos) noexcept {
    const size_t size = text.size();
    if (pos >= size) {
        return size;
    }
    const bool alnum = std::iswalnum(static_cast<wint_t>(text[pos])) != 0;
    size_t i = pos;
    while (i < size && (std::iswalnum(static_cast<wint_t>(text[i])) != 0) == alnum) {
        ++i;
    }
    return i;
}

size_t CharFromPoint(HWND /*hwnd*/, Impl& impl, int x, int y) noexcept {
    const int line = ClampLine(impl, impl.firstLine + y / std::max(impl.metrics.lineHeight, 1));
    const int start = std::max(impl.doc.LineStart(line), 0);
    const std::wstring_view text = impl.doc.LineText(line);
    const int col = (x + impl.xOffset) / std::max(impl.metrics.charW, 1);
    const size_t inLine = CharAtDisplayCol(text, col, impl.tabCols);
    return static_cast<size_t>(start) + inLine;
}

void Paint(HWND hwnd, Impl& impl) {
    PAINTSTRUCT ps;
    const HDC dc = BeginPaint(hwnd, &ps);

    HFONT oldFont = nullptr;
    if (impl.font != nullptr) {
        oldFont = static_cast<HFONT>(SelectObject(dc, impl.font));
    }
    SetBkMode(dc, OPAQUE);
    const COLORREF textCol = GetSysColor(COLOR_WINDOWTEXT);
    const COLORREF backCol = GetSysColor(COLOR_WINDOW);
    const COLORREF selTextCol = GetSysColor(COLOR_HIGHLIGHTTEXT);
    const COLORREF selBackCol = GetSysColor(COLOR_HIGHLIGHT);

    // Background for everything (selection runs repaint their cells opaque).
    const HBRUSH backBrush = CreateSolidBrush(backCol);
    RECT client{0, 0, impl.clientW, impl.clientH};
    FillRect(dc, &client, backBrush);

    const int selStart = impl.SelStart();
    const int selEnd = impl.SelEnd();
    const bool hasSel = selEnd > selStart;
    const int lineCount = impl.doc.LineCount();
    std::wstring drawn;  // Scratch for tab expansion (1 char == 1 column).

    const int first = std::max(impl.firstLine, 0);
    const int last = std::min(first + impl.pageLines, lineCount);
    for (int line = first; line < last; ++line) {
        const int y = (line - impl.firstLine) * impl.metrics.lineHeight;
        if (y > ps.rcPaint.bottom) {
            break;
        }
        const int start = impl.doc.LineStart(line);
        const std::wstring_view raw = impl.doc.LineText(line);
        ExpandTabs(raw, impl.tabCols, drawn);

        // Column boundaries of the selection inside this line.
        int a = static_cast<int>(drawn.size());
        int b = a;
        if (hasSel) {
            const int clampedStart = std::clamp(selStart - start, 0, static_cast<int>(raw.size()));
            const int clampedEnd = std::clamp(selEnd - start, 0, static_cast<int>(raw.size()));
            a = DisplayCols(raw, static_cast<size_t>(clampedStart), impl.tabCols);
            b = DisplayCols(raw, static_cast<size_t>(clampedEnd), impl.tabCols);
        }

        const int total = static_cast<int>(drawn.size());
        const wchar_t* data = drawn.data();
        const int xBase = -impl.xOffset;
        struct Run {
            int from, to;
            bool selected;
        };
        const Run runs[] = {{0, a, false}, {a, b, true}, {b, total, false}};
        for (const Run& run : runs) {
            if (run.to <= run.from) {
                continue;
            }
            SetTextColor(dc, run.selected ? selTextCol : textCol);
            SetBkColor(dc, run.selected ? selBackCol : backCol);
            ExtTextOutW(dc, xBase + run.from * impl.metrics.charW, y, ETO_CLIPPED, nullptr,
                        data + run.from, run.to - run.from, nullptr);
        }
    }
    DeleteObject(backBrush);

    if (oldFont != nullptr) {
        SelectObject(dc, oldFont);
    }
    EndPaint(hwnd, &ps);
}

void CopySelection(HWND hwnd, Impl& impl, bool cut) {
    const size_t start = std::min(impl.anchor, impl.caret);
    const size_t end = std::max(impl.anchor, impl.caret);
    if (start == end) {
        return;
    }
    const std::wstring selected = std::wstring(impl.doc.Text().substr(start, end - start));
    if (OpenClipboard(hwnd) == FALSE) {
        return;
    }
    EmptyClipboard();
    const HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, (selected.size() + 1) * sizeof(wchar_t));
    if (handle != nullptr) {
        auto* dst = static_cast<wchar_t*>(GlobalLock(handle));
        if (dst != nullptr) {
            std::memcpy(dst, selected.c_str(), (selected.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(handle);
            SetClipboardData(CF_UNICODETEXT, handle);
        } else {
            GlobalFree(handle);
        }
    }
    CloseClipboard();
    if (cut) {
        ReplaceSelection(hwnd, impl, {}, false);
    }
}

void PasteClipboard(HWND hwnd, Impl& impl) {
    if (OpenClipboard(hwnd) == FALSE) {
        return;
    }
    std::wstring text;
    if (const HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
        const auto* src = static_cast<const wchar_t*>(GlobalLock(handle));
        if (src != nullptr) {
            const size_t maxChars = GlobalSize(handle) / sizeof(wchar_t);
            size_t len = 0;
            while (len < maxChars && src[len] != L'\0') {
                ++len;  // Like the stock control, stop at the first NUL.
            }
            text.assign(src, len);
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    if (!text.empty()) {
        ReplaceSelection(hwnd, impl, text, true);
    }
}

void HandleChar(HWND hwnd, Impl& impl, wchar_t ch) {
    if (ch == L'\b') {
        const size_t start = std::min(impl.anchor, impl.caret);
        const size_t end = std::max(impl.anchor, impl.caret);
        if (start != end) {
            ReplaceSelection(hwnd, impl, {}, true);  // Backspace deletes a selection.
        } else if (start > 0) {
            // CRLF leaves as one unit (EDIT parity): a backspace at a line
            // start removes the whole break.
            const bool crlfBefore = start >= 2 &&
                                    impl.doc.Text()[start - 2] == L'\r' &&
                                    impl.doc.Text()[start - 1] == L'\n';
            const size_t pos = crlfBefore ? start - 2 : start - 1;
            const size_t len = crlfBefore ? 2 : 1;
            impl.doc.Splice(pos, len, {});
            impl.modified = true;
            RefreshAfterEdit(hwnd, impl, pos, true);
            NotifyChange(hwnd);
        }
        return;
    }
    if (ch == L'\r' || ch == L'\n') {
        ReplaceSelection(hwnd, impl, L"\r\n", true);
        return;
    }
    if (ch >= 0x20 || ch == L'\t') {
        ReplaceSelection(hwnd, impl, std::wstring_view(&ch, 1), true);
    }
    // Other control characters are ignored, as in the stock control.
}

void HandleKeyDown(HWND hwnd, Impl& impl, WPARAM vk) {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const int caretLine = impl.doc.LineFromChar(static_cast<int>(impl.caret));
    const int lineStart = std::max(impl.doc.LineStart(caretLine), 0);
    const int lineLen = impl.doc.LineLength(lineStart);
    const size_t col = impl.caret - static_cast<size_t>(lineStart);
    const std::wstring_view text = impl.doc.Text();
    const size_t length = impl.doc.Length();

    switch (vk) {
        case VK_LEFT: {
            if (impl.caret > 0) {
                size_t target = impl.caret - 1;
                if (ctrl) {
                    while (target > 0 && std::iswspace(text[target - 1]) != 0 &&
                           !IsBreakChar(text[target - 1])) {
                        --target;
                    }
                    while (target > 0 && std::iswalnum(text[target - 1]) != 0) {
                        --target;
                    }
                }
                if (target > 0 && text[target - 1] == L'\r' && text[target] == L'\n') {
                    --target;  // Step over CRLF as one unit.
                }
                impl.goalCol = 0;
                MoveCaret(hwnd, impl, target, shift, true);
            }
            break;
        }
        case VK_RIGHT: {
            if (impl.caret < length) {
                size_t target = impl.caret + impl.doc.BreakLength(impl.caret);
                if (target == impl.caret) {
                    ++target;
                }
                if (ctrl) {
                    while (target < length && std::iswalnum(text[target]) != 0) {
                        ++target;
                    }
                    while (target < length && std::iswspace(text[target]) != 0 &&
                           !IsBreakChar(text[target])) {
                        ++target;
                    }
                }
                impl.goalCol = 0;
                MoveCaret(hwnd, impl, target, shift, true);
            }
            break;
        }
        case VK_UP:
        case VK_DOWN: {
            const int step = vk == VK_UP ? -1 : 1;
            const int line = ClampLine(impl, caretLine + step);
            if (line != caretLine) {
                if (vk == VK_UP) {
                    impl.goalCol = static_cast<size_t>(DisplayCols(
                        impl.doc.LineText(caretLine), col, impl.tabCols));
                }
                const int targetStart = std::max(impl.doc.LineStart(line), 0);
                const size_t inLine =
                    CharAtDisplayCol(impl.doc.LineText(line), static_cast<int>(impl.goalCol),
                                     impl.tabCols);
                MoveCaret(hwnd, impl, static_cast<size_t>(targetStart) + inLine, shift, true);
            }
            break;
        }
        case VK_PRIOR:
        case VK_NEXT: {
            const int step = vk == VK_PRIOR ? -impl.pageLines : impl.pageLines;
            const int line = ClampLine(impl, caretLine + step);
            const int targetStart = std::max(impl.doc.LineStart(line), 0);
            const size_t inLine =
                CharAtDisplayCol(impl.doc.LineText(line), static_cast<int>(impl.goalCol),
                                 impl.tabCols);
            MoveCaret(hwnd, impl, static_cast<size_t>(targetStart) + inLine, shift, true);
            break;
        }
        case VK_HOME: {
            impl.goalCol = 0;
            MoveCaret(hwnd, impl, ctrl ? 0 : static_cast<size_t>(lineStart), shift, true);
            break;
        }
        case VK_END: {
            impl.goalCol = 0;
            MoveCaret(hwnd, impl,
                      ctrl ? length
                           : static_cast<size_t>(lineStart) + static_cast<size_t>(lineLen),
                      shift, true);
            break;
        }
        case VK_DELETE: {
            const size_t start = std::min(impl.anchor, impl.caret);
            const size_t end = std::max(impl.anchor, impl.caret);
            if (start != end) {
                ReplaceSelection(hwnd, impl, {}, false);
            } else if (start < length) {
                const size_t bl = impl.doc.BreakLength(start);
                impl.doc.Splice(start, bl != 0 ? bl : 1, {});
                impl.modified = true;
                RefreshAfterEdit(hwnd, impl, start, false);
                NotifyChange(hwnd);
            }
            break;
        }
        default:
            break;  // WM_CHAR carries the text; everything else is default.
    }
}

void OnVerticalScroll(HWND hwnd, Impl& impl, int code, int pos) {
    const int old = impl.firstLine;
    switch (code) {
        case SB_LINEUP: impl.firstLine -= 1; break;
        case SB_LINEDOWN: impl.firstLine += 1; break;
        case SB_PAGEUP: impl.firstLine -= impl.pageLines; break;
        case SB_PAGEDOWN: impl.firstLine += impl.pageLines; break;
        case SB_TOP: impl.firstLine = 0; break;
        case SB_BOTTOM: impl.firstLine = MaxFirstLine(impl); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;
            GetScrollInfo(hwnd, SB_VERT, &si);
            impl.firstLine = code == SB_THUMBTRACK ? si.nTrackPos : pos;
            break;
        }
        default: break;
    }
    impl.firstLine = std::clamp(impl.firstLine, 0, MaxFirstLine(impl));
    if (impl.firstLine != old) {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    UpdateScrollbars(hwnd, impl);
    PlaceCaret(hwnd, impl);
}

void OnHorizontalScroll(HWND hwnd, Impl& impl, int code, int pos) {
    const int step = impl.metrics.charW;
    switch (code) {
        case SB_LINELEFT: impl.xOffset -= step; break;
        case SB_LINERIGHT: impl.xOffset += step; break;
        case SB_PAGELEFT: impl.xOffset -= impl.clientW; break;
        case SB_PAGERIGHT: impl.xOffset += impl.clientW; break;
        case SB_LEFT: impl.xOffset = 0; break;
        case SB_RIGHT: impl.xOffset = 1 << 30; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;
            GetScrollInfo(hwnd, SB_HORZ, &si);
            impl.xOffset = code == SB_THUMBTRACK ? si.nTrackPos : pos;
            break;
        }
        default: break;
    }
    impl.xOffset = std::clamp(impl.xOffset, 0, std::max(HExtent(impl) - impl.clientW, 0));
    InvalidateRect(hwnd, nullptr, FALSE);
    UpdateScrollbars(hwnd, impl);
    PlaceCaret(hwnd, impl);
}

void RecomputeMaxCols(Impl& impl) {
    impl.maxCols = 0;
    const int lines = impl.doc.LineCount();
    for (int i = 0; i < lines; ++i) {
        const std::wstring_view t = impl.doc.LineText(i);
        impl.maxCols =
            std::max(impl.maxCols, static_cast<long long>(DisplayCols(t, t.size(), impl.tabCols)));
    }
}

void OnLeftDown(HWND hwnd, Impl& impl, WPARAM wParam, LPARAM lParam) {
    SetFocus(hwnd);
    SetCapture(hwnd);
    impl.selecting = true;
    const POINT pt{XParam(lParam), YParam(lParam)};
    const ULONGLONG now = GetTickCount64();
    impl.clickCount =
        (now - impl.lastClick < GetDoubleClickTime() &&
         std::abs(pt.x - impl.lastClickPos.x) < GetSystemMetrics(SM_CXDOUBLECLK) &&
         std::abs(pt.y - impl.lastClickPos.y) < GetSystemMetrics(SM_CYDOUBLECLK))
            ? impl.clickCount % 3 + 1
            : 1;
    impl.lastClick = now;
    impl.lastClickPos = pt;
    const size_t pos = CharFromPoint(hwnd, impl, pt.x, pt.y);
    const bool shift = (wParam & MK_SHIFT) != 0;

    if (impl.clickCount == 3) {
        // Triple click: the whole line including its break.
        const int line = impl.doc.LineFromChar(static_cast<int>(pos));
        const size_t start = static_cast<size_t>(std::max(impl.doc.LineStart(line), 0));
        const size_t end =
            start + static_cast<size_t>(impl.doc.LineLength(line)) + impl.doc.BreakLength(
                                                                    start + impl.doc.LineLength(line));
        impl.anchor = start;
        impl.caret = end;
    } else if (impl.clickCount == 2) {
        const std::wstring_view text = impl.doc.Text();
        impl.anchor = WordStart(text, pos);
        impl.caret = WordEnd(text, pos);
    } else {
        MoveCaret(hwnd, impl, pos, shift, true);
        return;  // Plain (or shift) click: drag-select takes over from here.
    }
    InvalidateRect(hwnd, nullptr, FALSE);
    ScrollCaretIntoView(hwnd, impl);
    PlaceCaret(hwnd, impl);
}

LRESULT CALLBACK FastEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_NCCREATE: {
            if (ImplOf(hwnd) == nullptr) {
                SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                  reinterpret_cast<LONG_PTR>(new Impl()));
            }
            break;
        }
        case WM_NCDESTROY: {
            delete ImplOf(hwnd);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
        }
        default:
            break;
    }

    Impl* const impl = ImplOf(hwnd);
    if (impl == nullptr) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTCHARS;

        case WM_SETFONT: {
            impl->font = reinterpret_cast<HFONT>(wParam);
            impl->metricsValid = false;
            impl->EnsureMetrics(hwnd);
            if (impl->hasCaret) {  // Caret tracks the font height.
                DestroyCaret();
                CreateCaret(hwnd, nullptr, 2, impl->metrics.lineHeight);
                ShowCaret(hwnd);
                PlaceCaret(hwnd, *impl);
            }
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }

        case WM_SIZE: {
            impl->clientW = LOWORD(lParam);
            impl->clientH = HIWORD(lParam);
            impl->EnsureMetrics(hwnd);
            impl->pageLines = std::max(impl->clientH / std::max(impl->metrics.lineHeight, 1), 1);
            impl->firstLine = std::clamp(impl->firstLine, 0, MaxFirstLine(*impl));
            UpdateScrollbars(hwnd, *impl);
            PlaceCaret(hwnd, *impl);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;  // WM_PAINT fills everything.

        case WM_PAINT: {
            impl->EnsureMetrics(hwnd);
            Paint(hwnd, *impl);
            return 0;
        }

        case WM_SETFOCUS: {
            impl->EnsureMetrics(hwnd);
            CreateCaret(hwnd, nullptr, 2, impl->metrics.lineHeight);
            impl->hasCaret = true;
            ShowCaret(hwnd);
            PlaceCaret(hwnd, *impl);
            return 0;
        }
        case WM_KILLFOCUS: {
            if (impl->hasCaret) {
                DestroyCaret();
                impl->hasCaret = false;
            }
            return 0;
        }

        case WM_SETTEXT: {
            const auto* text = reinterpret_cast<const wchar_t*>(lParam);
            impl->doc.SetText(text != nullptr ? std::wstring_view(text) : std::wstring_view());
            impl->anchor = impl->caret = 0;
            impl->goalCol = 0;
            impl->firstLine = 0;
            impl->xOffset = 0;
            impl->modified = false;
            RecomputeMaxCols(*impl);
            impl->firstLine = std::clamp(impl->firstLine, 0, MaxFirstLine(*impl));
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateScrollbars(hwnd, *impl);
            PlaceCaret(hwnd, *impl);
            return TRUE;
        }
        case WM_GETTEXT: {
            const int cap = static_cast<int>(wParam);
            auto* out = reinterpret_cast<wchar_t*>(lParam);
            if (out == nullptr || cap <= 0) {
                return 0;
            }
            const size_t n = std::min(impl->doc.Length(), static_cast<size_t>(cap - 1));
            std::memcpy(out, impl->doc.Text().data(), n * sizeof(wchar_t));
            out[n] = L'\0';
            return static_cast<LRESULT>(n);
        }
        case WM_GETTEXTLENGTH:
            return static_cast<LRESULT>(impl->doc.Length());

        case EM_GETSEL: {
            if (wParam != 0) {
                *reinterpret_cast<DWORD*>(wParam) = static_cast<DWORD>(impl->SelStart());
            }
            if (lParam != 0) {
                *reinterpret_cast<DWORD*>(lParam) = static_cast<DWORD>(impl->SelEnd());
            }
            return MAKELRESULT(impl->SelStart(), impl->SelEnd());
        }
        case EM_SETSEL: {
            const int len = static_cast<int>(impl->doc.Length());
            if (static_cast<int>(wParam) == -1) {  // Deselect; keep the caret.
                impl->anchor = impl->caret;
                InvalidateRect(hwnd, nullptr, FALSE);
                PlaceCaret(hwnd, *impl);
                return 0;
            }
            int end = static_cast<int>(lParam);
            if (end == -1) {
                end = len;
            }
            const int start = std::clamp(static_cast<int>(wParam), 0, len);
            end = std::clamp(end, 0, len);
            impl->anchor = static_cast<size_t>(start);
            impl->caret = static_cast<size_t>(end);
            impl->goalCol = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
            PlaceCaret(hwnd, *impl);
            return 0;
        }
        case EM_REPLACESEL: {
            const auto* text = reinterpret_cast<const wchar_t*>(lParam);
            ReplaceSelection(hwnd, *impl,
                             text != nullptr ? std::wstring_view(text) : std::wstring_view(),
                             false);
            return 1;
        }

        case EM_GETLINECOUNT:
            return impl->doc.LineCount();
        case EM_LINEINDEX: {
            int line = static_cast<int>(wParam);
            if (line == -1) {
                line = impl->doc.LineFromChar(static_cast<int>(impl->caret));
            }
            return impl->doc.LineStart(line);
        }
        case EM_LINEFROMCHAR:
            return impl->doc.LineFromChar(static_cast<int>(wParam));
        case EM_LINELENGTH:
            return impl->doc.LineLength(static_cast<int>(wParam));
        case EM_GETLINE: {
            const int line = static_cast<int>(wParam);
            auto* out = reinterpret_cast<wchar_t*>(lParam);
            if (out == nullptr || line < 0) {
                return 0;
            }
            const int cap = *reinterpret_cast<WORD*>(out);
            const int start = impl->doc.LineStart(line);
            if (start < 0) {
                return 0;
            }
            const std::wstring_view text = impl->doc.LineText(line);
            const int n = std::min(static_cast<int>(cap), static_cast<int>(text.size()));
            std::memcpy(out, text.data(), static_cast<size_t>(n) * sizeof(wchar_t));
            return n;
        }

        case EM_SCROLLCARET:
            ScrollCaretIntoView(hwnd, *impl);
            return 1;

        case EM_GETMODIFY:
            return impl->modified ? 1 : 0;
        case EM_SETMODIFY:
            impl->modified = wParam != 0;
            return 0;

        case EM_SETTABSTOPS: {
            impl->EnsureMetrics(hwnd);
            const DWORD base = GetDialogBaseUnits();
            const int baseX = static_cast<int>(LOWORD(base));
            int cols = 32;  // The stock default.
            if (wParam == 1 && lParam != 0) {
                const int du = *reinterpret_cast<const int*>(lParam);
                cols = du * baseX / 4 / std::max(impl->metrics.charW, 1);
            } else if (wParam > 0 && lParam != 0) {
                const auto* stops = reinterpret_cast<const int*>(lParam);
                cols = stops[0] * baseX / 4 / std::max(impl->metrics.charW, 1);
            }
            impl->tabCols = std::max(cols, 1);
            RecomputeMaxCols(*impl);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 1;
        }

        case EM_SETLIMITTEXT:
        case EM_EMPTYUNDOBUFFER:
        case EM_SETHANDLE:
        case EM_UNDO:
        case WM_UNDO:
            return 0;
        case EM_CANUNDO:
            return 0;
        case EM_GETHANDLE:
            return 0;  // LockedText falls back to WM_GETTEXT / the view message.

        case WM_CHAR:
            HandleChar(hwnd, *impl, static_cast<wchar_t>(wParam));
            return 0;

        case WM_KEYDOWN:
            HandleKeyDown(hwnd, *impl, wParam);
            return 0;

        case WM_CUT:
            CopySelection(hwnd, *impl, true);
            return 1;
        case WM_COPY:
            CopySelection(hwnd, *impl, false);
            return 1;
        case WM_PASTE:
            PasteClipboard(hwnd, *impl);
            return 0;
        case WM_CLEAR:
            if (impl->anchor != impl->caret) {
                ReplaceSelection(hwnd, *impl, {}, false);
            }
            return 0;

        case WM_VSCROLL:
            OnVerticalScroll(hwnd, *impl, static_cast<int>(LOWORD(wParam)),
                             static_cast<int>(HIWORD(lParam)));
            return 0;
        case WM_HSCROLL:
            OnHorizontalScroll(hwnd, *impl, static_cast<int>(LOWORD(wParam)),
                               static_cast<short>(HIWORD(lParam)));
            return 0;

        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if ((wParam & MK_CONTROL) != 0) {
                break;  // The key-handler subclass owns Ctrl+wheel zoom.
            }
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            if (lines == WHEEL_PAGESCROLL) {
                lines = static_cast<UINT>(std::max(impl->pageLines, 1));
            }
            if ((wParam & MK_SHIFT) != 0) {  // Shift+wheel scrolls horizontally.
                const int dx = static_cast<int>(lines) * (delta / WHEEL_DELTA) *
                               impl->metrics.charW;
                impl->xOffset = std::clamp(impl->xOffset - dx, 0,
                                           std::max(HExtent(*impl) - impl->clientW, 0));
            } else {
                const int step = static_cast<int>(lines) * (delta / WHEEL_DELTA);
                impl->firstLine = std::clamp(impl->firstLine - step, 0, MaxFirstLine(*impl));
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateScrollbars(hwnd, *impl);
            PlaceCaret(hwnd, *impl);
            return 0;
        }

        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            OnLeftDown(hwnd, *impl, wParam, lParam);
            return 0;

        case WM_MOUSEMOVE: {
            if (!impl->selecting || GetCapture() != hwnd) {
                return 0;
            }
            const POINT pt{XParam(lParam), YParam(lParam)};
            if (impl->clickCount == 2) {
                const std::wstring_view text = impl->doc.Text();
                const size_t pos = CharFromPoint(hwnd, *impl, pt.x, pt.y);
                impl->caret = pos >= impl->anchor ? WordEnd(text, pos) : WordStart(text, pos);
            } else if (impl->clickCount == 3) {
                const size_t pos = CharFromPoint(hwnd, *impl, pt.x, pt.y);
                const int line = impl->doc.LineFromChar(static_cast<int>(pos));
                const size_t lineStart =
                    static_cast<size_t>(std::max(impl->doc.LineStart(line), 0));
                const size_t lineEnd =
                    lineStart + static_cast<size_t>(impl->doc.LineLength(line)) +
                    impl->doc.BreakLength(lineStart + impl->doc.LineLength(line));
                impl->caret = pos >= impl->anchor ? lineEnd : lineStart;
            } else {
                MoveCaret(hwnd, *impl, CharFromPoint(hwnd, *impl, pt.x, pt.y), true, true);
                return 0;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            ScrollCaretIntoView(hwnd, *impl);
            PlaceCaret(hwnd, *impl);
            return 0;
        }
        case WM_LBUTTONUP: {
            if (impl->selecting) {
                impl->selecting = false;
                ReleaseCapture();
            }
            return 0;
        }

        default:
            break;
    }

    if (msg == g_viewMessage) {
        auto* view = reinterpret_cast<FastEditView*>(lParam);
        if (view != nullptr) {
            view->data = impl->doc.Text().data();
            view->length = impl->doc.Length();
            return TRUE;
        }
        return FALSE;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

UINT FastEditViewMessage() noexcept {
    return g_viewMessage;
}

const wchar_t* FastEditClassName() noexcept {
    return kClassName;
}

void RegisterFastEdit(HINSTANCE instance) noexcept {
    if (g_viewMessage == 0) {
        g_viewMessage = RegisterWindowMessageW(L"NxpFastEdit_View");
    }
    WNDCLASSW wc{};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &FastEditProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    RegisterClassW(&wc);  // Fails harmlessly when already registered.
}

} // namespace notepadxp::editor

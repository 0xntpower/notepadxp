#pragma once

// FastEdit.hpp — A virtualizing multiline editor control for heavy documents.
//
// The stock Win32 EDIT control rebuilds its internal line table for every
// structural change, which is O(document): ~10 s per WM_SETTEXT and ~5 s per
// selection-replacing edit at 100 MB, growing quadratically from there. FastEdit
// implements the exact message subset NotepadXP drives the stock control with
// (WM_SETTEXT / EM_SETSEL / EM_REPLACESEL / EM_GETLINE* / WM_CHAR / clipboard
// / scroll / caret notifications), backed by a plain text buffer plus a
// line-start index, painting only the visible lines. EditView swaps it in for
// documents at/above EditView::kHeavyTextChars; everything above EditView
// (document shadow, delta undo, search, key-handler subclass, status bar)
// keeps working unchanged through the message contract.
//
// Deliberate v1 limits (documented differences from the stock control):
//   * no word wrap (EditView disables the toggle in heavy mode),
//   * no native context menu,
//   * no IME composition feedback (committed input still lands),
//   * no accessibility bridge beyond the caret,
//   * tab stops render as space-expanded columns (exact for monospace).

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "util/WinLean.hpp"

namespace notepadxp::editor {

/// @brief Text plus a line-start index; the FastEdit document model. Breaks
///        are "\r\n", "\r" or "\n" ("\r\n" counts as one). All queries are the
///        EM_* semantics the app relies on. Pure and headless-testable.
/// @threadsafety Not thread-safe; owned by the FastEdit control (UI thread).
class FastEditDoc final {
public:
    /// @brief Replace the whole text and rebuild the line index.
    void SetText(std::wstring_view text);

    [[nodiscard]] const std::wstring& Text() const noexcept {
        return text_;
    }

    [[nodiscard]] size_t Length() const noexcept {
        return text_.size();
    }

    /// @brief EM_GETLINECOUNT.
    [[nodiscard]] int LineCount() const noexcept {
        return static_cast<int>(starts_.size());
    }

    /// @brief EM_LINEINDEX: start of 0-based @p line (-1 when past the last).
    [[nodiscard]] int LineStart(int line) const noexcept;

    /// @brief EM_LINEFROMCHAR: 0-based line of @p charIndex (clamped to the
    ///        document; -1 selects the last line).
    [[nodiscard]] int LineFromChar(int charIndex) const noexcept;

    /// @brief EM_LINELENGTH: length of the line holding @p charIndex,
    ///        excluding its line break.
    [[nodiscard]] int LineLength(int charIndex) const noexcept;

    /// @brief EM_GETLINE source: [start, start + LineLength) of @p line.
    [[nodiscard]] std::wstring_view LineText(int line) const noexcept;

    /// @brief Replace [pos, pos + removeLen) with @p insert, maintaining the
    ///        line index. Char-index queries elsewhere stay valid afterwards.
    void Splice(size_t pos, size_t removeLen, std::wstring_view insert);

    /// @brief Length of the line break at @p pos (2 for CRLF, 1 for a lone
    ///        CR/LF, 0 otherwise).
    [[nodiscard]] size_t BreakLength(size_t pos) const noexcept;

private:
    void Rebuild();

    std::wstring text_;
    std::vector<size_t> starts_;  // starts_[i] = first char of line i.
};

/// @brief Filled by FastEdit in response to FastEditViewMessage(): a view of
///        the live text. Valid until the next text-changing message.
struct FastEditView {
    const wchar_t* data = nullptr;
    size_t length = 0;
};

/// @brief The registered private message for zero-copy text access.
///        lParam = FastEditView*; returns TRUE on success.
[[nodiscard]] UINT FastEditViewMessage() noexcept;

/// @brief Window class name; create with WS_CHILD | WS_VSCROLL | WS_HSCROLL.
[[nodiscard]] const wchar_t* FastEditClassName() noexcept;

/// @brief Register the window class (idempotent). Call once at startup.
void RegisterFastEdit(HINSTANCE instance) noexcept;

} // namespace notepadxp::editor

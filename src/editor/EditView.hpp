#pragma once

// EditView.hpp — Wraps the multiline edit control that is Notepad's text area.

#include <functional>
#include <string>
#include <string_view>

#include "WtlIncludes.hpp"
#include "editor/DocumentShadow.hpp"
#include "editor/EditKeyHandler.hpp"
#include "editor/LockedText.hpp"
#include "editor/UndoManager.hpp"
#include "file/TextBuffer.hpp"
#include "lang/Language.hpp"
#include "util/GdiGuard.hpp"

namespace notepadxp::editor {

/// @brief Owns and drives the child Edit control: creation, font, word-wrap
///        recreation, the clipboard/undo commands, and caret position queries.
///        Adapts file::TextBuffer, so the file lifecycle can run against it.
/// @threadsafety Not thread-safe; lives on and is used from the UI thread only.
///
/// Word wrap cannot be toggled on a live Edit control, so SetWordWrap()
/// destroys and recreates the control (preserving text, font, modify flag and
/// caret) exactly as classic Notepad does.
class EditView final : public file::TextBuffer {
public:
    EditView() = default;

    EditView(const EditView&) = delete;
    EditView& operator=(const EditView&) = delete;

    /// @brief Create the Edit control as a child of @p parent.
    /// @param wordWrap Initial word-wrap state (omits WS_HSCROLL when true).
    /// @return true on success.
    [[nodiscard]] bool Create(HWND parent, bool wordWrap);

    /// @brief Position the control to fill @p rect (client coordinates).
    void Layout(const RECT& rect);

    /// @brief Give keyboard focus to the edit control.
    void SetFocusToEdit();

    [[nodiscard]] HWND Handle() const noexcept {
        return edit_.m_hWnd;
    }

    /// @brief Toggle word wrap, recreating the control. @return true on success;
    ///        on failure the previous control and state are left intact.
    [[nodiscard]] bool SetWordWrap(bool wordWrap);

    /// @brief Apply a fully-resolved font (lfHeight already set for the display).
    void SetFont(const LOGFONTW& logFont);

    // Edit commands (mapped from the Edit menu / accelerators).
    void Undo();
    void Redo();
    void Cut();
    void Copy();
    void Paste();
    void DeleteSelection();
    void SelectAll();

    /// @brief Replace the current selection with @p text as a single undo unit.
    void InsertText(const std::wstring& text) override;

    /// @brief Record the latest edit into the undo history (call on EN_CHANGE).
    void OnEditChanged();

    /// @brief Move the caret to the start of 1-based @p lineNumber and scroll it
    ///        into view. Out-of-range values are clamped by the control.
    void GoToLine(int lineNumber);

    /// @brief Clear all text and the modify flag (the File > New action).
    void Reset() override;

    /// @brief Replace all text with @p text, clear the modify flag, home the caret.
    void SetText(const std::wstring& text) override;

    /// @brief Return a copy of the full document text.
    [[nodiscard]] std::wstring GetText() override;

    /// @brief Zero-copy view of the document. Release it before editing.
    [[nodiscard]] LockedText LockText() const {
        return LockedText(edit_.m_hWnd);
    }

    /// @brief Move the caret to end of buffer and scroll into view (.LOG stamp).
    void MoveCaretToEnd() override;

    /// @brief Append on-disk content (follow tail): no undo record, modified
    ///        flag preserved, caret pinned to the end.
    void AppendExternal(std::wstring_view text) override;

    /// @brief Read the current selection as character indices [start, end).
    void GetSelection(int& startOut, int& endOut);

    /// @brief Select [start, end) and scroll the caret into view.
    void SelectRange(int start, int end);

    /// @brief Whole-line bounds of the current selection: start of its first
    ///        line to end of its last line (excluding the trailing break). A
    ///        selection ending exactly at a line start does not pull that line in.
    void ExpandSelectionToLines(int& startCharOut, int& endCharOut);

    // State queries used to drive menu enable/check state and the status bar.
    // These are not const because the underlying WTL/CEdit accessors send window
    // messages and are not const-qualified.
    [[nodiscard]] bool CanUndo();
    [[nodiscard]] bool CanRedo();
    [[nodiscard]] bool HasSelection();
    [[nodiscard]] int TextLength() override;
    [[nodiscard]] bool IsModified() override;
    void SetModified(bool modified) override;

    /// @brief Compute the 1-based caret line and column from the selection start.
    void GetCaretLineCol(int& lineOut, int& colOut);

    /// @brief Register the owner's listener for caret line/column changes.
    ///        Fired only when the (line, col) actually changed.
    void SetCaretMovedCallback(std::function<void()> callback) {
        caretMoved_ = std::move(callback);
    }

    /// @brief Register the owner's listener for Ctrl+wheel zoom steps.
    void SetWheelZoomCallback(std::function<void(int steps)> callback) {
        keyHandler_.SetWheelZoomNotify(std::move(callback));
    }

    /// @brief Adopt the detected language and indent style: applies the
    ///        language's tab stops and drives the smart-edit behavior.
    void SetLanguageContext(lang::Language language, lang::IndentStyle indentStyle);

private:
    void NotifyCaretMaybeMoved();
    void OnPreChange(PendingChange pending);
    [[nodiscard]] std::optional<std::wstring> ComputeEnterReplacement();

    [[nodiscard]] static DWORD StyleFor(bool wordWrap) noexcept;
    void ReapplyFont();
    void ApplyTabStops();
    void ApplyDelta(size_t pos, size_t removeLen, const std::wstring& insertText, int selStart,
                    int selEnd);
    [[nodiscard]] bool CanFastSplice(size_t pos, size_t removeLen,
                                     const std::wstring& insertText) const;

    CEdit edit_;
    EditKeyHandler keyHandler_;  // Subclasses edit_: word-delete + caret notify.
    UndoManager undo_;
    util::GdiGuard fontGuard_;  // Owns the current HFONT.
    std::function<void()> caretMoved_;
    int lastCaretLine_ = 0;  // Dedupe cache for caret notifications (0 == none yet).
    int lastCaretCol_ = 0;
    DocumentShadow shadow_;  // Mirrors the control text; derives undo deltas.
    lang::Language language_ = lang::Language::PlainText;
    lang::IndentStyle indentStyle_{};
    bool wordWrap_ = false;
    bool suppressRecording_ = false;  // True while we change text programmatically.
    HWND parent_ = nullptr;
};

} // namespace notepadxp::editor

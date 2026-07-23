#pragma once

// FileService.hpp — File menu operations: New, Open, Save, Save As.

#include <functional>
#include <optional>
#include <string>

#include "file/DocumentState.hpp"
#include "file/Encoding.hpp"
#include "file/FilePrompts.hpp"
#include "file/FileWatcher.hpp"
#include "file/TextBuffer.hpp"
#include "util/WinLean.hpp"

namespace notepadxp::file {

/// @brief Orchestrates the document lifecycle against the text buffer and the
///        shared DocumentState (owned by the frame, held by reference). All
///        user interaction goes through the FilePrompts seam; the frame
///        observes identity/content changes via the document-changed callback.
/// @threadsafety Not thread-safe; UI thread only.
class FileService final {
public:
    FileService(TextBuffer& buffer, DocumentState& document, FilePrompts& prompts) noexcept
        : buffer_(buffer), document_(document), prompts_(prompts) {}

    FileService(const FileService&) = delete;
    FileService& operator=(const FileService&) = delete;

    /// @brief Set the window that owns the Open/Save dialogs. Call once after
    ///        the frame window exists.
    void AttachOwner(HWND owner) noexcept {
        owner_ = owner;
    }

    /// @brief Register the frame's listener for "a different document (or a new
    ///        identity) is now loaded" — fired after New, Open and Save As.
    void SetDocumentChangedCallback(std::function<void()> callback) {
        documentChanged_ = std::move(callback);
    }

    /// @brief Start a new, empty, untitled document (prompts to save first if the
    ///        current one is modified). @return false if the user cancelled.
    bool New();

    /// @brief Prompt for a file (with encoding combo) and load it.
    /// @return true on a successful load.
    bool Open();

    /// @brief Save to the current path, or fall through to Save As if untitled.
    bool Save();

    /// @brief Prompt for a path/encoding and save. @return true on success.
    bool SaveAs();

    /// @brief Load a specific path (command line / drag-drop). @p forced pins the
    ///        encoding (e.g. /A, /W); otherwise it is auto-detected.
    bool OpenPath(const std::wstring& path,
                  std::optional<TextEncoding> forced = std::nullopt);

    /// @brief Whether the window may close now: prompts to save a modified
    ///        document and returns false only if the user chose Cancel.
    [[nodiscard]] bool CanClose();

    /// @brief If the open file changed on disk since it was loaded/saved,
    ///        offer to reload it (call on app activation). Declining re-arms
    ///        the stamp so the same change is not asked about again.
    void PromptReloadIfChanged();

    /// @brief Reload the current file from disk (auto-detecting the encoding),
    ///        discarding the buffer. No save gate — callers decide first.
    bool Reload();

    /// @brief One follow-tail poll: pure growth appends just the tail (undo
    ///        untouched); anything else reloads silently with the caret at
    ///        the end. No-op when nothing is armed or nothing changed.
    void FollowTailTick();

    [[nodiscard]] const DocumentState& Document() const noexcept {
        return document_;
    }

private:
    [[nodiscard]] bool CheckSave();  // Prompt-to-save gate; false == user cancelled.
    [[nodiscard]] bool LoadFromPath(const std::wstring& path, std::optional<TextEncoding> forced);
    [[nodiscard]] bool SaveToPath(const std::wstring& path, TextEncoding encoding);
    void DetectDocumentLanguage(const std::wstring& text);
    void NotifyDocumentChanged();

    TextBuffer& buffer_;
    DocumentState& document_;
    FilePrompts& prompts_;
    FileWatcher watcher_;
    std::function<void()> documentChanged_;
    HWND owner_ = nullptr;
    bool checkingDiskChange_ = false;  // Reentrancy guard for the reload prompt.
};

} // namespace notepadxp::file

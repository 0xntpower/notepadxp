#pragma once

// FileService.hpp — File menu operations: New, Open, Save, Save As.

#include <optional>
#include <string>

#include "editor/EditView.hpp"
#include "file/DocumentState.hpp"
#include "file/Encoding.hpp"

namespace notepadxp::file {

/// @brief Orchestrates document lifecycle operations against the edit view and
///        the shared DocumentState (both owned by the frame, held by reference).
/// @threadsafety Not thread-safe; UI thread only.
class FileService final {
public:
    FileService(editor::EditView& editView, DocumentState& document) noexcept
        : editView_(editView), document_(document) {}

    FileService(const FileService&) = delete;
    FileService& operator=(const FileService&) = delete;

    /// @brief Set the window that owns dialogs/prompts. Call once after the
    ///        frame window exists.
    void AttachOwner(HWND owner) noexcept {
        owner_ = owner;
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

    [[nodiscard]] const DocumentState& Document() const noexcept {
        return document_;
    }

private:
    [[nodiscard]] bool CheckSave();  // Prompt-to-save gate; false == user cancelled.
    [[nodiscard]] bool LoadFromPath(const std::wstring& path, std::optional<TextEncoding> forced);
    [[nodiscard]] bool SaveToPath(const std::wstring& path, TextEncoding encoding);

    editor::EditView& editView_;
    DocumentState& document_;
    HWND owner_ = nullptr;
};

} // namespace notepadxp::file

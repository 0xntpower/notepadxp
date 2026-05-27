#pragma once

// UndoManager.hpp — Multi-level undo/redo for the plain Edit control, which
// natively offers only a single undo step. State is captured as full-text
// snapshots; consecutive typing (within a word) and consecutive single-char
// deletions coalesce into one undo unit, mirroring a typical editor.

#include <optional>
#include <string>
#include <vector>

namespace notepadxp::editor {

/// @brief A captured editor state: full text plus the selection at that moment.
struct EditSnapshot {
    std::wstring text;
    int selStart = 0;
    int selEnd = 0;
};

/// @brief Snapshot-based undo/redo history.
/// @threadsafety UI-thread only.
class UndoManager final {
public:
    /// @brief Record the control's post-change state (call on each EN_CHANGE).
    ///        Diffs against the previous state to decide whether to open a new
    ///        undo unit or coalesce into the current one.
    void RecordChange(std::wstring text, int selStart, int selEnd);

    /// @brief Drop all history and adopt @p text as the new baseline (New/Open).
    void Reset(std::wstring text, int selStart, int selEnd);

    [[nodiscard]] bool CanUndo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool CanRedo() const noexcept { return !redo_.empty(); }

    /// @brief Step back one unit. @p current* is the live state (pushed to redo).
    /// @return The state to restore, or nullopt when there is nothing to undo.
    std::optional<EditSnapshot> Undo(std::wstring currentText, int selStart, int selEnd);

    /// @brief Step forward one unit. @p current* is the live state (pushed to undo).
    std::optional<EditSnapshot> Redo(std::wstring currentText, int selStart, int selEnd);

private:
    enum class UnitKind { None, Insert, Delete };

    // Cap history depth so a long editing session cannot grow memory without
    // bound (each unit holds a full-text snapshot).
    static constexpr std::size_t kMaxDepth = 100;

    EditSnapshot last_;             // Baseline: control state as of the last record.
    std::vector<EditSnapshot> undo_;
    std::vector<EditSnapshot> redo_;
    UnitKind unitKind_ = UnitKind::None;
    int unitCaret_ = 0;             // Expected contiguous caret position for coalescing.
};

} // namespace notepadxp::editor

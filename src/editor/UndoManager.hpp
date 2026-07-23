#pragma once

// UndoManager.hpp — Multi-level undo/redo for the plain Edit control, which
// natively offers only a single undo step. History is stored as per-edit
// deltas (memory proportional to edits, not to document size); consecutive
// typing (within a word) and consecutive single-char deletions coalesce into
// one undo unit, mirroring a typical editor.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace notepadxp::editor {

/// @brief One textual change: at @c pos, @c removed was replaced by @c inserted.
struct EditDelta {
    size_t pos = 0;
    std::wstring removed;
    std::wstring inserted;
    wchar_t charBeforePos = L'\0';  // Document char at pos-1 (L'\0' at start);
                                    // drives the word-break coalescing rule.
    int selStartBefore = 0;
    int selEndBefore = 0;
    int selStartAfter = 0;
    int selEndAfter = 0;
};

/// @brief Delta-based undo/redo history.
/// @threadsafety UI-thread only.
class UndoManager final {
public:
    /// @brief Record one observed change (call per EN_CHANGE). Coalesces into
    ///        the open unit or starts a new one; clears the redo history.
    void RecordChange(const EditDelta& delta);

    /// @brief Drop all history (New/Open).
    void Reset();

    [[nodiscard]] bool CanUndo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool CanRedo() const noexcept { return !redo_.empty(); }

    /// @brief Step back one unit. The caller inverts it: replace
    ///        [pos, pos + inserted.size()) with `removed`, then select
    ///        {selStartBefore, selEndBefore}.
    std::optional<EditDelta> Undo();

    /// @brief Step forward one unit. The caller re-applies it: replace
    ///        [pos, pos + removed.size()) with `inserted`, then select
    ///        {selStartAfter, selEndAfter}.
    std::optional<EditDelta> Redo();

private:
    enum class UnitKind { None, Insert, Delete };

    // Cap history depth so a long editing session cannot grow without bound.
    static constexpr std::size_t kMaxDepth = 100;

    std::vector<EditDelta> undo_;
    std::vector<EditDelta> redo_;
    UnitKind unitKind_ = UnitKind::None;  // Kind of the open (coalescable) top unit.
};

} // namespace notepadxp::editor

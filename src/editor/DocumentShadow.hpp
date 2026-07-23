#pragma once

// DocumentShadow.hpp — A mirror of the Edit control's text that turns
// EN_CHANGE notifications into EditDeltas without reading the full buffer on
// every keystroke. The subclass predicts each mutation (typed char, clipboard
// text, EM_REPLACESEL string, single-char deletes) before the control applies
// it; the shadow validates the prediction with length/caret arithmetic and
// splices itself. Anything unpredicted (IME composition, the control's native
// context-menu undo) falls back to one full-text diff and resync.
//
// Lazily materialized: viewing a file costs no extra memory; the mirror is
// allocated on the first modification attempt.

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "editor/UndoManager.hpp"  // EditDelta.

namespace notepadxp::editor {

/// @brief What the message subclass knew just before the control mutated.
struct PendingChange {
    enum class Kind {
        ReplaceSelection,  // Selection replaced by `inserted` (typing, paste, EM_REPLACESEL).
        BackspaceOne,      // Empty selection, one char removed to the left.
        DeleteOne,         // Empty selection, one char removed to the right.
        Unknown,           // Mutation of unpredictable content (forces fallback).
    };
    Kind kind = Kind::Unknown;
    int selStart = 0;  // Selection at the moment the message arrived.
    int selEnd = 0;
    std::wstring inserted;  // Meaningful for ReplaceSelection only.
};

/// @brief Shadow text + delta derivation. Pure (no HWND); the owner feeds it
///        control state and a full-text reader for the rare fallback.
/// @threadsafety UI-thread only.
class DocumentShadow final {
public:
    [[nodiscard]] bool IsMaterialized() const noexcept {
        return materialized_;
    }

    [[nodiscard]] size_t Length() const noexcept {
        return text_.size();
    }

    /// @brief Full shadow text (test inspection).
    [[nodiscard]] const std::wstring& Text() const noexcept {
        return text_;
    }

    /// @brief Number of times CaptureChange had to resync via the full read.
    [[nodiscard]] int FallbackCount() const noexcept {
        return fallbackCount_;
    }

    /// @brief Drop the mirror (New/Open/SetText); next edit re-materializes.
    void Reset();

    /// @brief Adopt @p fullText as the mirror (the one-time first-edit read).
    void Materialize(std::wstring fullText);

    /// @brief Record the subclass's prediction for the imminent mutation.
    void SetPending(PendingChange pending);

    /// @brief Sync a programmatic (undo/redo) splice that EN_CHANGE never saw.
    void ApplyExternal(size_t pos, size_t removedLen, std::wstring_view inserted);

    /// @brief Sync a follow-tail append.
    void Append(std::wstring_view text);

    /// @brief Turn the post-change control state into a delta. Fast path uses
    ///        the pending prediction (validated by arithmetic); mismatch or an
    ///        Unknown/absent prediction falls back to @p readFullText + diff.
    ///        Unmaterialized: materializes via @p readFullText, returns nullopt
    ///        (that first observed change is not undoable — it cannot be
    ///        reconstructed without a pre-state).
    std::optional<EditDelta> CaptureChange(size_t newLength, int selStart, int selEnd,
                                           const std::function<std::wstring()>& readFullText);

private:
    std::optional<EditDelta> Fallback(int selStart, int selEnd,
                                      const std::function<std::wstring()>& readFullText);

    std::wstring text_;
    bool materialized_ = false;
    std::optional<PendingChange> pending_;
    int fallbackCount_ = 0;
};

} // namespace notepadxp::editor

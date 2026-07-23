#include "editor/UndoManager.hpp"

#include <cwctype>

namespace notepadxp::editor {

namespace {

bool IsSpace(wchar_t ch) {
    return std::iswspace(static_cast<wint_t>(ch)) != 0;
}

} // namespace

void UndoManager::RecordChange(const EditDelta& delta) {
    redo_.clear();

    const bool singleInsert = delta.removed.empty() && delta.inserted.size() == 1;
    const bool singleDelete = delta.inserted.empty() && delta.removed.size() == 1;

    if (singleInsert && unitKind_ == UnitKind::Insert && !undo_.empty()) {
        EditDelta& top = undo_.back();
        const bool contiguous = delta.pos == top.pos + top.inserted.size();
        // Break before the first letter of a new word so undo removes a word
        // at a time.
        const bool wordStart = !IsSpace(delta.inserted[0]) && delta.charBeforePos != L'\0' &&
                               IsSpace(delta.charBeforePos);
        if (contiguous && !wordStart) {
            top.inserted += delta.inserted;
            top.selStartAfter = delta.selStartAfter;
            top.selEndAfter = delta.selEndAfter;
            return;
        }
    } else if (singleDelete && unitKind_ == UnitKind::Delete && !undo_.empty()) {
        EditDelta& top = undo_.back();
        if (delta.pos == top.pos) {  // Forward Delete: caret stays put.
            top.removed += delta.removed;
            top.selStartAfter = delta.selStartAfter;
            top.selEndAfter = delta.selEndAfter;
            return;
        }
        if (delta.pos + 1 == top.pos) {  // Backspace: walks left.
            top.pos = delta.pos;
            top.removed = delta.removed + top.removed;
            top.charBeforePos = delta.charBeforePos;
            top.selStartAfter = delta.selStartAfter;
            top.selEndAfter = delta.selEndAfter;
            return;
        }
    }

    unitKind_ = singleInsert ? UnitKind::Insert
                : singleDelete ? UnitKind::Delete
                               : UnitKind::None;
    undo_.push_back(delta);
    if (undo_.size() > kMaxDepth) {
        undo_.erase(undo_.begin());
    }
}

void UndoManager::Reset() {
    undo_.clear();
    redo_.clear();
    unitKind_ = UnitKind::None;
}

std::optional<EditDelta> UndoManager::Undo() {
    if (undo_.empty()) {
        return std::nullopt;
    }
    EditDelta unit = std::move(undo_.back());
    undo_.pop_back();
    redo_.push_back(unit);
    unitKind_ = UnitKind::None;  // The next edit opens a fresh unit.
    return unit;
}

std::optional<EditDelta> UndoManager::Redo() {
    if (redo_.empty()) {
        return std::nullopt;
    }
    EditDelta unit = std::move(redo_.back());
    redo_.pop_back();
    undo_.push_back(unit);
    unitKind_ = UnitKind::None;
    return unit;
}

} // namespace notepadxp::editor

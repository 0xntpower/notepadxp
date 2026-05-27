#include "editor/UndoManager.hpp"

#include <algorithm>
#include <cwctype>
#include <utility>

namespace notepadxp::editor {

namespace {

bool IsSpace(wchar_t ch) {
    return std::iswspace(static_cast<wint_t>(ch)) != 0;
}

} // namespace

void UndoManager::RecordChange(std::wstring text, int selStart, int selEnd) {
    const std::wstring& oldText = last_.text;

    // Locate the single changed region as [prefix, ... ): common prefix + suffix.
    std::size_t prefix = 0;
    const std::size_t shared = std::min(oldText.size(), text.size());
    while (prefix < shared && oldText[prefix] == text[prefix]) {
        ++prefix;
    }
    std::size_t oldEnd = oldText.size();
    std::size_t newEnd = text.size();
    while (oldEnd > prefix && newEnd > prefix && oldText[oldEnd - 1] == text[newEnd - 1]) {
        --oldEnd;
        --newEnd;
    }
    const std::size_t deleted = oldEnd - prefix;
    const std::size_t inserted = newEnd - prefix;

    if (deleted == 0 && inserted == 0) {
        // No textual change (e.g., the word-wrap control recreate); keep history.
        last_.selStart = selStart;
        last_.selEnd = selEnd;
        return;
    }

    bool startNewUnit = true;
    if (deleted == 0 && inserted == 1) {  // single-character insertion (typing)
        const wchar_t ch = text[prefix];
        const bool contiguous =
            unitKind_ == UnitKind::Insert && static_cast<std::size_t>(unitCaret_) == prefix;
        // Break before the first letter of a new word so undo removes a word at a time.
        const bool wordStart = !IsSpace(ch) && prefix > 0 && IsSpace(text[prefix - 1]);
        startNewUnit = !contiguous || wordStart;
        unitKind_ = UnitKind::Insert;
        unitCaret_ = static_cast<int>(prefix + 1);
    } else if (inserted == 0 && deleted == 1) {  // single-character deletion
        // Backspace walks the caret left (prefix == unitCaret_ - 1); forward
        // Delete keeps it put (prefix == unitCaret_). Either continues the run.
        const bool contiguous =
            unitKind_ == UnitKind::Delete && (static_cast<std::size_t>(unitCaret_) == prefix ||
                                              static_cast<std::size_t>(unitCaret_) == prefix + 1);
        startNewUnit = !contiguous;
        unitKind_ = UnitKind::Delete;
        unitCaret_ = static_cast<int>(prefix);
    } else {  // bulk change: paste, cut, replace, word-delete, drag-drop, ...
        unitKind_ = UnitKind::None;
        unitCaret_ = static_cast<int>(prefix + inserted);
    }

    if (startNewUnit) {
        undo_.push_back(last_);
        if (undo_.size() > kMaxDepth) {
            undo_.erase(undo_.begin());
        }
        redo_.clear();
    }
    last_.text = std::move(text);
    last_.selStart = selStart;
    last_.selEnd = selEnd;
}

void UndoManager::Reset(std::wstring text, int selStart, int selEnd) {
    undo_.clear();
    redo_.clear();
    unitKind_ = UnitKind::None;
    unitCaret_ = 0;
    last_ = EditSnapshot{std::move(text), selStart, selEnd};
}

std::optional<EditSnapshot> UndoManager::Undo(std::wstring currentText, int selStart, int selEnd) {
    if (undo_.empty()) {
        return std::nullopt;
    }
    redo_.push_back(EditSnapshot{std::move(currentText), selStart, selEnd});
    EditSnapshot restored = std::move(undo_.back());
    undo_.pop_back();
    last_ = restored;
    unitKind_ = UnitKind::None;  // The next edit opens a fresh unit.
    return restored;
}

std::optional<EditSnapshot> UndoManager::Redo(std::wstring currentText, int selStart, int selEnd) {
    if (redo_.empty()) {
        return std::nullopt;
    }
    undo_.push_back(EditSnapshot{std::move(currentText), selStart, selEnd});
    EditSnapshot restored = std::move(redo_.back());
    redo_.pop_back();
    last_ = restored;
    unitKind_ = UnitKind::None;
    return restored;
}

} // namespace notepadxp::editor

#include "editor/DocumentShadow.hpp"

#include <algorithm>
#include <utility>

namespace notepadxp::editor {

void DocumentShadow::Reset() {
    text_.clear();
    text_.shrink_to_fit();
    materialized_ = false;
    pending_.reset();
}

void DocumentShadow::Materialize(std::wstring fullText) {
    text_ = std::move(fullText);
    materialized_ = true;
}

void DocumentShadow::SetPending(PendingChange pending) {
    pending_ = std::move(pending);
}

void DocumentShadow::ApplyExternal(size_t pos, size_t removedLen, std::wstring_view inserted) {
    if (materialized_ && pos <= text_.size() && pos + removedLen <= text_.size()) {
        text_.replace(pos, removedLen, inserted);
    }
}

void DocumentShadow::Append(std::wstring_view text) {
    if (materialized_) {
        text_.append(text);
    }
}

std::optional<EditDelta> DocumentShadow::CaptureChange(
    size_t newLength, int selStart, int selEnd,
    const std::function<std::wstring()>& readFullText) {
    if (!materialized_) {
        Materialize(readFullText());
        pending_.reset();
        return std::nullopt;
    }

    const std::optional<PendingChange> pending = std::move(pending_);
    pending_.reset();
    if (!pending.has_value() || pending->kind == PendingChange::Kind::Unknown) {
        return Fallback(selStart, selEnd, readFullText);
    }

    // Normalize the three predicted kinds into one removed-range + insert.
    size_t pos = 0;
    size_t removedLen = 0;
    std::wstring_view inserted;
    const auto s0 = static_cast<size_t>(std::max(pending->selStart, 0));
    const auto e0 = static_cast<size_t>(std::max(pending->selEnd, 0));
    switch (pending->kind) {
        case PendingChange::Kind::ReplaceSelection:
            pos = s0;
            removedLen = e0 - s0;
            inserted = pending->inserted;
            break;
        case PendingChange::Kind::BackspaceOne:
            if (s0 != e0) {  // With a selection, Backspace deletes the selection.
                pos = s0;
                removedLen = e0 - s0;
            } else if (s0 > 0) {
                pos = s0 - 1;
                removedLen = 1;
            }
            break;
        case PendingChange::Kind::DeleteOne:
            pos = s0;
            removedLen = s0 != e0 ? e0 - s0 : 1;
            break;
        case PendingChange::Kind::Unknown:
            break;
    }

    // Validate the prediction against the observed post-change state; any
    // mismatch (stale pending, control-side truncation, unpredicted path)
    // means the prediction is not trusted.
    const size_t expectedCaret = pos + inserted.size();
    const bool valid = removedLen + inserted.size() > 0 &&
                       pos + removedLen <= text_.size() &&
                       newLength == text_.size() - removedLen + inserted.size() &&
                       selStart == selEnd &&
                       static_cast<size_t>(selStart) == expectedCaret;
    if (!valid) {
        return Fallback(selStart, selEnd, readFullText);
    }

    EditDelta delta;
    delta.pos = pos;
    delta.removed = text_.substr(pos, removedLen);
    delta.inserted = inserted;
    if (delta.removed == delta.inserted) {
        return std::nullopt;  // Textual no-op (e.g. pasting over an identical selection).
    }
    delta.charBeforePos = pos > 0 ? text_[pos - 1] : L'\0';
    delta.selStartBefore = pending->selStart;
    delta.selEndBefore = pending->selEnd;
    delta.selStartAfter = selStart;
    delta.selEndAfter = selEnd;
    text_.replace(pos, removedLen, inserted);
    return delta;
}

std::optional<EditDelta> DocumentShadow::Fallback(
    int selStart, int selEnd, const std::function<std::wstring()>& readFullText) {
    ++fallbackCount_;
    std::wstring fullText = readFullText();

    size_t prefix = 0;
    const size_t shared = std::min(text_.size(), fullText.size());
    while (prefix < shared && text_[prefix] == fullText[prefix]) {
        ++prefix;
    }
    size_t oldEnd = text_.size();
    size_t newEnd = fullText.size();
    while (oldEnd > prefix && newEnd > prefix && text_[oldEnd - 1] == fullText[newEnd - 1]) {
        --oldEnd;
        --newEnd;
    }

    std::optional<EditDelta> delta;
    if (oldEnd != prefix || newEnd != prefix) {
        EditDelta d;
        d.pos = prefix;
        d.removed = text_.substr(prefix, oldEnd - prefix);
        d.inserted = fullText.substr(prefix, newEnd - prefix);
        d.charBeforePos = prefix > 0 ? fullText[prefix - 1] : L'\0';
        // The pre-change selection is unknown on this path; anchor undo's
        // restore point at the change site instead.
        d.selStartBefore = static_cast<int>(prefix);
        d.selEndBefore = static_cast<int>(prefix + (oldEnd - prefix));
        d.selStartAfter = selStart;
        d.selEndAfter = selEnd;
        delta = std::move(d);
    }
    text_ = std::move(fullText);
    return delta;
}

} // namespace notepadxp::editor

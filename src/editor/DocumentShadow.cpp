#include "editor/DocumentShadow.hpp"

#include <algorithm>
#include <cwchar>
#include <utility>

namespace notepadxp::editor {

namespace {

// Length of the common suffix of @p a and @p b, at most @p limit. Compared in
// wmemcmp blocks: reverse iterators would defeat the STL's vectorized compare.
size_t CommonSuffix(std::wstring_view a, std::wstring_view b, size_t limit) {
    constexpr size_t kBlock = 4096;
    size_t n = 0;
    while (n + kBlock <= limit && wmemcmp(a.data() + a.size() - n - kBlock,
                                          b.data() + b.size() - n - kBlock, kBlock) == 0) {
        n += kBlock;
    }
    while (n < limit && a[a.size() - n - 1] == b[b.size() - n - 1]) {
        ++n;
    }
    return n;
}

} // namespace

void DocumentShadow::Reset() {
    text_.clear();
    text_.shrink_to_fit();
    materialized_ = false;
    pending_.reset();
    staged_.reset();
}

void DocumentShadow::Materialize(std::wstring fullText) {
    text_ = std::move(fullText);
    materialized_ = true;
}

void DocumentShadow::SetPending(PendingChange pending) {
    pending_ = std::move(pending);
    // A new mutation means a staged delete's insert half never arrived. The
    // mirror already holds the delete, which then simply has no undo unit.
    staged_.reset();
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

std::optional<EditDelta> DocumentShadow::CaptureChange(int selStart, int selEnd,
                                                       std::wstring_view liveText) {
    if (!materialized_) {
        Materialize(std::wstring(liveText));
        pending_.reset();
        return std::nullopt;
    }

    std::optional<PendingChange> pending = std::move(pending_);
    pending_.reset();
    const std::optional<StagedDelete> staged = std::move(staged_);
    staged_.reset();
    if (!pending.has_value() || pending->kind == PendingChange::Kind::Unknown) {
        return Fallback(selStart, selEnd, liveText);
    }
    const size_t newLength = liveText.size();

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

    // Replacing a non-empty selection with text arrives as two EN_CHANGEs:
    // the delete, then the insert. On the delete, mirror it and keep the
    // prediction for the insert, which then completes a single delta.
    if (pending->kind == PendingChange::Kind::ReplaceSelection && removedLen > 0 &&
        !inserted.empty() && pos + removedLen <= text_.size() &&
        newLength == text_.size() - removedLen && selStart == selEnd &&
        static_cast<size_t>(selStart) == pos) {
        staged_ = StagedDelete{pos, text_.substr(pos, removedLen), pending->selStart,
                               pending->selEnd};
        text_.erase(pos, removedLen);
        pending->selEnd = pending->selStart;
        pending_ = std::move(pending);
        return std::nullopt;
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
        return Fallback(selStart, selEnd, liveText);
    }

    EditDelta delta;
    delta.pos = pos;
    delta.removed = text_.substr(pos, removedLen);
    delta.inserted = inserted;
    delta.charBeforePos = pos > 0 ? text_[pos - 1] : L'\0';
    delta.selStartBefore = pending->selStart;
    delta.selEndBefore = pending->selEnd;
    delta.selStartAfter = selStart;
    delta.selEndAfter = selEnd;
    if (staged.has_value() && staged->pos == pos && removedLen == 0) {
        // The insert half of a staged selection replace: one delta for both.
        delta.removed = staged->removed;
        delta.selStartBefore = staged->selStart;
        delta.selEndBefore = staged->selEnd;
    }
    text_.replace(pos, removedLen, inserted);
    if (delta.removed == delta.inserted) {
        return std::nullopt;  // Textual no-op (e.g. pasting over an identical selection).
    }
    return delta;
}

std::optional<EditDelta> DocumentShadow::Fallback(int selStart, int selEnd,
                                                  std::wstring_view liveText) {
    ++fallbackCount_;

    const size_t shared = std::min(text_.size(), liveText.size());
    const size_t prefix = static_cast<size_t>(
        std::mismatch(text_.data(), text_.data() + shared, liveText.data()).first - text_.data());
    const size_t suffix = CommonSuffix(text_, liveText, shared - prefix);
    const size_t oldEnd = text_.size() - suffix;
    const size_t newEnd = liveText.size() - suffix;
    if (oldEnd == prefix && newEnd == prefix) {
        return std::nullopt;
    }

    EditDelta d;
    d.pos = prefix;
    d.removed = text_.substr(prefix, oldEnd - prefix);
    d.inserted = liveText.substr(prefix, newEnd - prefix);
    d.charBeforePos = prefix > 0 ? liveText[prefix - 1] : L'\0';
    // The pre-change selection is unknown on this path: anchor undo's restore
    // point at the change site instead.
    d.selStartBefore = static_cast<int>(prefix);
    d.selEndBefore = static_cast<int>(oldEnd);
    d.selStartAfter = selStart;
    d.selEndAfter = selEnd;
    text_.replace(prefix, oldEnd - prefix, d.inserted);
    return d;
}

} // namespace notepadxp::editor

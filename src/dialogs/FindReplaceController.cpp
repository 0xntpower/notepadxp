#include "dialogs/FindReplaceController.hpp"

#include <string>
#include <string_view>

#include "Resource.h"
#include "util/StringTable.hpp"

namespace notepadxp::dialogs {

namespace {

// Compare key against text at [pos, pos+key.size()), honoring case sensitivity.
bool MatchAt(const std::wstring& text, size_t pos, std::wstring_view key, bool matchCase) {
    if (pos + key.size() > text.size()) {
        return false;
    }
    if (matchCase) {
        return wcsncmp(text.c_str() + pos, key.data(), key.size()) == 0;
    }
    // Locale-aware, case-insensitive comparison.
    return CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, text.c_str() + pos,
                          static_cast<int>(key.size()), key.data(),
                          static_cast<int>(key.size())) == CSTR_EQUAL;
}

// First match at or after `from`; npos if none. No wrap-around.
size_t FindForward(const std::wstring& text, std::wstring_view key, size_t from, bool matchCase) {
    if (key.empty() || key.size() > text.size()) {
        return std::wstring::npos;
    }
    for (size_t i = from; i + key.size() <= text.size(); ++i) {
        if (MatchAt(text, i, key, matchCase)) {
            return i;
        }
    }
    return std::wstring::npos;
}

// Last match ending at or before `before`; npos if none. No wrap-around.
size_t FindBackward(const std::wstring& text, std::wstring_view key, size_t before, bool matchCase) {
    if (key.empty() || key.size() > text.size() || before < key.size()) {
        return std::wstring::npos;
    }
    for (size_t i = before - key.size() + 1; i-- > 0;) {
        if (MatchAt(text, i, key, matchCase)) {
            return i;
        }
    }
    return std::wstring::npos;
}

} // namespace

FindReplaceController::FindReplaceController(editor::EditView& editView) : editView_(editView) {
    findMessage_ = RegisterWindowMessageW(FINDMSGSTRINGW);
}

void FindReplaceController::ShowFind(HWND parent) {
    owner_ = parent;
    if (dialog_ != nullptr) {
        SetFocus(dialog_);
        return;
    }
    request_ = FINDREPLACEW{};
    request_.lStructSize = sizeof(request_);
    request_.hwndOwner = parent;
    request_.lpstrFindWhat = findBuffer_;
    request_.wFindWhatLen = kSearchMax;
    request_.Flags = FR_DOWN | FR_HIDEWHOLEWORD;
    dialog_ = FindTextW(&request_);
}

void FindReplaceController::ShowReplace(HWND parent) {
    owner_ = parent;
    if (dialog_ != nullptr) {
        SetFocus(dialog_);
        return;
    }
    request_ = FINDREPLACEW{};
    request_.lStructSize = sizeof(request_);
    request_.hwndOwner = parent;
    request_.lpstrFindWhat = findBuffer_;
    request_.wFindWhatLen = kSearchMax;
    request_.lpstrReplaceWith = replaceBuffer_;
    request_.wReplaceWithLen = kSearchMax;
    request_.Flags = FR_DOWN | FR_HIDEWHOLEWORD;
    dialog_ = ReplaceTextW(&request_);
}

void FindReplaceController::FindNext(HWND parent) {
    owner_ = parent;
    if (findBuffer_[0] == L'\0') {
        ShowFind(parent);
        return;
    }
    Search();
}

void FindReplaceController::HandleFindMessage(WPARAM /*wParam*/, LPARAM lParam) {
    const auto* request = reinterpret_cast<const FINDREPLACEW*>(lParam);
    if ((request->Flags & FR_DIALOGTERM) != 0) {
        dialog_ = nullptr;
        return;
    }
    matchCase_ = (request->Flags & FR_MATCHCASE) != 0;
    searchDown_ = (request->Flags & FR_DOWN) != 0;

    if ((request->Flags & FR_REPLACEALL) != 0) {
        ReplaceAll();
    } else if ((request->Flags & FR_REPLACE) != 0) {
        ReplaceMatchThenFind();
    } else if ((request->Flags & FR_FINDNEXT) != 0) {
        Search();
    }
}

void FindReplaceController::Search() {
    const std::wstring key = findBuffer_;
    if (key.empty()) {
        return;
    }
    const std::wstring text = editView_.GetText();
    int selStart = 0;
    int selEnd = 0;
    editView_.GetSelection(selStart, selEnd);

    const size_t found = searchDown_
                             ? FindForward(text, key, static_cast<size_t>(selEnd), matchCase_)
                             : FindBackward(text, key, static_cast<size_t>(selStart), matchCase_);
    if (found == std::wstring::npos) {
        ReportNotFound();
        return;
    }
    editView_.SelectRange(static_cast<int>(found), static_cast<int>(found + key.size()));
}

void FindReplaceController::ReplaceMatchThenFind() {
    const std::wstring key = findBuffer_;
    if (key.empty()) {
        return;
    }
    const std::wstring text = editView_.GetText();
    int selStart = 0;
    int selEnd = 0;
    editView_.GetSelection(selStart, selEnd);

    // Replace only if the current selection is exactly the search text.
    if (static_cast<size_t>(selEnd - selStart) == key.size() &&
        MatchAt(text, static_cast<size_t>(selStart), key, matchCase_)) {
        editView_.InsertText(replaceBuffer_);
    }
    Search();
}

void FindReplaceController::ReplaceAll() {
    const std::wstring key = findBuffer_;
    if (key.empty()) {
        return;
    }
    const std::wstring replacement = replaceBuffer_;

    editView_.SelectRange(0, 0);
    // Re-read the text each pass (it changes as we replace); search from the caret,
    // which sits just past the inserted replacement, so we never re-match it.
    while (true) {
        const std::wstring text = editView_.GetText();
        int selStart = 0;
        int selEnd = 0;
        editView_.GetSelection(selStart, selEnd);

        const size_t found = FindForward(text, key, static_cast<size_t>(selEnd), matchCase_);
        if (found == std::wstring::npos) {
            break;
        }
        editView_.SelectRange(static_cast<int>(found), static_cast<int>(found + key.size()));
        editView_.InsertText(replacement);
    }
    editView_.SelectRange(0, 0);
}

void FindReplaceController::ReportNotFound() {
    const HWND owner = dialog_ != nullptr ? dialog_ : owner_;
    const std::wstring message = util::LoadAndMerge(IDS_CFS, findBuffer_);
    util::AlertBox(owner, util::LoadStr(IDS_NN), message, MB_OK | MB_ICONINFORMATION);
}

} // namespace notepadxp::dialogs

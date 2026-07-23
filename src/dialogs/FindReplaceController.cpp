#include "dialogs/FindReplaceController.hpp"

#include <string>

#include "Resource.h"
#include "editor/SearchEngine.hpp"
#include "util/StringTable.hpp"

namespace notepadxp::dialogs {

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

    const size_t found =
        searchDown_ ? editor::FindForward(text, key, static_cast<size_t>(selEnd), matchCase_)
                    : editor::FindBackward(text, key, static_cast<size_t>(selStart), matchCase_);
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
        editor::MatchAt(text, static_cast<size_t>(selStart), key, matchCase_)) {
        editView_.InsertText(replaceBuffer_);
    }
    Search();
}

void FindReplaceController::ReplaceAll() {
    const std::wstring key = findBuffer_;
    if (key.empty()) {
        return;
    }
    const editor::ReplaceAllResult result =
        editor::ReplaceAllInText(editView_.GetText(), key, replaceBuffer_, matchCase_);
    if (result.count > 0) {
        // Select-all + insert applies the rewrite as a single undo unit.
        editView_.SelectAll();
        editView_.InsertText(result.text);
    }
    editView_.SelectRange(0, 0);
}

void FindReplaceController::ReportNotFound() {
    const HWND owner = dialog_ != nullptr ? dialog_ : owner_;
    const std::wstring message = util::LoadAndMerge(IDS_CFS, findBuffer_);
    util::AlertBox(owner, util::LoadStr(IDS_NN), message, MB_OK | MB_ICONINFORMATION);
}

} // namespace notepadxp::dialogs

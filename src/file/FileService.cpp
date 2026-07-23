#include "file/FileService.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Resource.h"
#include "dialogs/EncodingFileDialog.hpp"
#include "file/TextFile.hpp"
#include "util/DateTime.hpp"
#include "util/PathName.hpp"
#include "util/StringTable.hpp"

namespace notepadxp::file {

namespace {

// True when the document begins with the classic ".LOG" auto-timestamp marker.
bool StartsWithLogTag(const std::wstring& text) {
    return text.compare(0, 4, L".LOG") == 0;
}

} // namespace

bool FileService::New() {
    if (!CheckSave()) {
        return false;
    }
    editView_.Reset();
    document_.filePath.clear();
    document_.untitled = true;
    document_.encoding = TextEncoding::Ansi;
    return true;
}

bool FileService::Open() {
    if (!CheckSave()) {
        return false;
    }
    const std::optional<dialogs::OpenDialogResult> result =
        dialogs::EncodingFileDialog::ShowOpen(owner_);
    if (!result.has_value()) {
        return false;
    }
    return LoadFromPath(result->path, result->encoding);
}

bool FileService::Save() {
    if (document_.untitled) {
        return SaveAs();
    }
    return SaveToPath(document_.filePath, document_.encoding);
}

bool FileService::SaveAs() {
    const std::wstring defaultPath = document_.untitled ? std::wstring{} : document_.filePath;
    const std::optional<dialogs::SaveDialogResult> result =
        dialogs::EncodingFileDialog::ShowSave(owner_, defaultPath, document_.encoding);
    if (!result.has_value()) {
        return false;
    }
    if (!SaveToPath(result->path, result->encoding)) {
        return false;
    }
    document_.filePath = result->path;
    document_.untitled = false;
    document_.encoding = result->encoding;
    return true;
}

bool FileService::OpenPath(const std::wstring& path, std::optional<TextEncoding> forced) {
    if (!CheckSave()) {
        return false;
    }
    return LoadFromPath(path, forced);
}

bool FileService::CanClose() {
    return CheckSave();
}

bool FileService::CheckSave() {
    // No prompt for an untitled, empty buffer, or an unmodified document.
    if (document_.untitled && editView_.TextLength() == 0) {
        return true;
    }
    if (!editView_.IsModified()) {
        return true;
    }

    const std::wstring name =
        document_.untitled ? util::LoadStr(IDS_UNTITLED) : util::PathLeaf(document_.filePath);
    const std::wstring text = util::LoadAndMerge(IDS_SCBC, name);
    const int answer =
        util::AlertBox(owner_, util::LoadStr(IDS_NN), text, MB_YESNOCANCEL | MB_ICONEXCLAMATION);
    if (answer == IDYES) {
        return Save();  // false if the user then cancels Save As.
    }
    if (answer == IDCANCEL) {
        return false;
    }
    return true;  // IDNO: discard changes.
}

bool FileService::LoadFromPath(const std::wstring& path, std::optional<TextEncoding> forced) {
    const TextFileLoadResult loaded = LoadTextFile(path, forced);
    switch (loaded.status) {
        case LoadStatus::NotFound: {
            // Offer to create a new file with this name (the file-not-found prompt).
            const std::wstring message = util::LoadAndMerge(IDS_FNF, util::PathLeaf(path));
            if (util::AlertBox(owner_, util::LoadStr(IDS_NN), message,
                               MB_YESNO | MB_ICONEXCLAMATION) == IDYES) {
                editView_.Reset();
                document_.filePath = path;
                document_.untitled = false;
                document_.encoding = TextEncoding::Ansi;
                return true;
            }
            return false;
        }
        case LoadStatus::TooLarge:
            util::AlertBox(owner_, util::LoadStr(IDS_NN),
                           util::LoadAndMerge(IDS_FTL, util::PathLeaf(path)),
                           MB_OK | MB_ICONEXCLAMATION);
            return false;
        case LoadStatus::AccessError:
            util::AlertBox(owner_, util::LoadStr(IDS_NN),
                           util::LoadAndMerge(IDS_DISKERROR, util::PathLeaf(path)),
                           MB_OK | MB_ICONEXCLAMATION);
            return false;
        case LoadStatus::Ok:
            break;
    }

    editView_.SetText(loaded.text);
    document_.filePath = path;
    document_.untitled = false;
    document_.encoding = loaded.encoding;

    // ".LOG" files get a timestamp appended at end-of-file on open.
    if (StartsWithLogTag(loaded.text)) {
        editView_.MoveCaretToEnd();
        editView_.InsertText(util::FormatTimestamp(true));
    }
    return true;
}

bool FileService::SaveToPath(const std::wstring& path, TextEncoding encoding) {
    const std::wstring text = editView_.GetText();
    bool lossy = false;
    const std::vector<std::byte> bytes = EncodeText(text, encoding, lossy);
    if (lossy) {
        const std::wstring message = util::LoadAndMerge(IDS_ERRUNICODE, util::PathLeaf(path));
        if (util::AlertBox(owner_, util::LoadStr(IDS_NN), message,
                           MB_OKCANCEL | MB_ICONEXCLAMATION) == IDCANCEL) {
            return false;
        }
    }

    switch (WriteAllBytes(path, bytes)) {
        case SaveStatus::CreateError:
            util::AlertBox(owner_, util::LoadStr(IDS_NN),
                           util::LoadAndMerge(IDS_CREATEERR, util::PathLeaf(path)),
                           MB_OK | MB_ICONEXCLAMATION);
            return false;
        case SaveStatus::WriteError:
            util::AlertBox(owner_, util::LoadStr(IDS_NN),
                           util::LoadAndMerge(IDS_DISKERROR, util::PathLeaf(path)),
                           MB_OK | MB_ICONEXCLAMATION);
            return false;
        case SaveStatus::Ok:
            break;
    }

    editView_.SetModified(false);
    return true;
}

} // namespace notepadxp::file

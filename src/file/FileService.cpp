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
    buffer_.Reset();
    document_.filePath.clear();
    document_.untitled = true;
    document_.encoding = TextEncoding::Ansi;
    NotifyDocumentChanged();
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
    NotifyDocumentChanged();
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
    if (document_.untitled && buffer_.TextLength() == 0) {
        return true;
    }
    if (!buffer_.IsModified()) {
        return true;
    }

    const std::wstring name =
        document_.untitled ? util::LoadStr(IDS_UNTITLED) : util::PathLeaf(document_.filePath);
    switch (prompts_.AskSaveChanges(name)) {
        case FilePrompts::SaveChoice::Save:
            return Save();  // false if the user then cancels Save As.
        case FilePrompts::SaveChoice::Cancel:
            return false;
        case FilePrompts::SaveChoice::Discard:
            break;
    }
    return true;
}

bool FileService::LoadFromPath(const std::wstring& path, std::optional<TextEncoding> forced) {
    const TextFileLoadResult loaded = LoadTextFile(path, forced);
    switch (loaded.status) {
        case LoadStatus::NotFound:
            // Offer to create a new file with this name (the file-not-found prompt).
            if (prompts_.AskCreateNewFile(util::PathLeaf(path))) {
                buffer_.Reset();
                document_.filePath = path;
                document_.untitled = false;
                document_.encoding = TextEncoding::Ansi;
                NotifyDocumentChanged();
                return true;
            }
            return false;
        case LoadStatus::TooLarge:
            prompts_.ReportError(FilePrompts::Error::TooLarge, util::PathLeaf(path));
            return false;
        case LoadStatus::AccessError:
            prompts_.ReportError(FilePrompts::Error::OpenFailed, util::PathLeaf(path));
            return false;
        case LoadStatus::Ok:
            break;
    }

    buffer_.SetText(loaded.text);
    document_.filePath = path;
    document_.untitled = false;
    document_.encoding = loaded.encoding;

    // ".LOG" files get a timestamp appended at end-of-file on open.
    if (StartsWithLogTag(loaded.text)) {
        buffer_.MoveCaretToEnd();
        buffer_.InsertText(util::FormatTimestamp(true));
    }
    NotifyDocumentChanged();
    return true;
}

bool FileService::SaveToPath(const std::wstring& path, TextEncoding encoding) {
    const std::wstring text = buffer_.GetText();
    bool lossy = false;
    const std::vector<std::byte> bytes = EncodeText(text, encoding, lossy);
    if (lossy && !prompts_.AskContinueLossySave(util::PathLeaf(path))) {
        return false;
    }

    switch (WriteAllBytes(path, bytes)) {
        case SaveStatus::CreateError:
            prompts_.ReportError(FilePrompts::Error::CreateFailed, util::PathLeaf(path));
            return false;
        case SaveStatus::WriteError:
            prompts_.ReportError(FilePrompts::Error::WriteFailed, util::PathLeaf(path));
            return false;
        case SaveStatus::Ok:
            break;
    }

    buffer_.SetModified(false);
    return true;
}

void FileService::NotifyDocumentChanged() {
    if (documentChanged_) {
        documentChanged_();
    }
}

} // namespace notepadxp::file

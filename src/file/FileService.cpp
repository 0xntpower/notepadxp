#include "file/FileService.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Resource.h"
#include "dialogs/EncodingFileDialog.hpp"
#include "file/LineEndings.hpp"
#include "file/TextFile.hpp"
#include "util/DateTime.hpp"
#include "util/PathName.hpp"
#include "util/StringTable.hpp"

namespace notepadxp::file {

namespace {

// Text prefix handed to language detection / indent sniffing.
constexpr size_t kDetectPrefixChars = 64 * 1024;

// True when the document begins with the classic ".LOG" auto-timestamp marker.
bool StartsWithLogTag(const std::wstring& text) {
    return text.compare(0, 4, L".LOG") == 0;
}

} // namespace

void FileService::DetectDocumentLanguage(const std::wstring& text) {
    const std::wstring_view prefix = std::wstring_view(text).substr(
        0, std::min(text.size(), kDetectPrefixChars));
    document_.language = lang::DetectLanguage(document_.filePath, prefix);
    document_.indentStyle = lang::SniffIndentStyle(prefix, document_.language);
}

bool FileService::New() {
    if (!CheckSave()) {
        return false;
    }
    buffer_.Reset();
    document_.filePath.clear();
    document_.untitled = true;
    document_.encoding = TextEncoding::Ansi;
    document_.language = lang::Language::PlainText;
    document_.indentStyle = {};
    document_.lineEnding = LineEnding::Crlf;  // Windows convention for new files.
    watcher_.Disarm();
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
    DetectDocumentLanguage(buffer_.GetText());  // The new name may change the language.
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

void FileService::PromptReloadIfChanged() {
    if (checkingDiskChange_ || document_.untitled || !watcher_.IsArmed()) {
        return;
    }
    if (watcher_.Check() == FileWatcher::Verdict::Unchanged) {
        return;
    }
    checkingDiskChange_ = true;
    if (prompts_.AskReloadChanged(util::PathLeaf(document_.filePath), buffer_.IsModified())) {
        Reload();
    } else {
        watcher_.Rearm();  // Same change: do not ask again.
    }
    checkingDiskChange_ = false;
}

bool FileService::Reload() {
    return LoadFromPath(document_.filePath, std::nullopt);
}

void FileService::FollowTailTick() {
    if (document_.untitled || !watcher_.IsArmed()) {
        return;
    }
    const FileWatcher::Verdict verdict = watcher_.Check();
    if (verdict == FileWatcher::Verdict::Unchanged) {
        return;
    }
    if (verdict == FileWatcher::Verdict::Grown) {
        if (const auto tail =
                ReadTailText(document_.filePath, watcher_.ArmedSize(), document_.encoding)) {
            buffer_.AppendExternal(NormalizeToCrlf(*tail));  // Match the control's CRLF.
            watcher_.Rearm();
            return;
        }
    }
    // Replaced (or an unreadable tail): full silent reload, pinned to the end.
    if (Reload()) {
        buffer_.MoveCaretToEnd();
    }
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
                document_.lineEnding = LineEnding::Crlf;
                DetectDocumentLanguage(std::wstring());  // Extension decides.
                watcher_.Disarm();  // Nothing on disk yet; the first save arms.
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

    // Detect the file's newline style, then normalize to CRLF so the Edit
    // control renders LF-only (Unix) and CR-only files with real line breaks;
    // the original style is restored on save.
    document_.lineEnding = DetectLineEnding(loaded.text);
    buffer_.SetText(NormalizeToCrlf(loaded.text));
    document_.filePath = path;
    document_.untitled = false;
    document_.encoding = loaded.encoding;
    DetectDocumentLanguage(loaded.text);
    watcher_.Arm(path);

    // ".LOG" files get a timestamp appended at end-of-file on open.
    if (StartsWithLogTag(loaded.text)) {
        buffer_.MoveCaretToEnd();
        buffer_.InsertText(util::FormatTimestamp(true));
    }
    NotifyDocumentChanged();
    return true;
}

bool FileService::SaveToPath(const std::wstring& path, TextEncoding encoding) {
    // Restore the file's original newline convention (the control holds CRLF).
    const std::wstring text = ConvertFromCrlf(buffer_.GetText(), document_.lineEnding);
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
    watcher_.Arm(path);
    return true;
}

void FileService::NotifyDocumentChanged() {
    if (documentChanged_) {
        documentChanged_();
    }
}

} // namespace notepadxp::file

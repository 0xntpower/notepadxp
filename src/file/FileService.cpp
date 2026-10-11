#include "file/FileService.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <thread>
#include <utility>

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

// Files at/above this size load on a worker thread behind the progress
// dialog; smaller ones load inline (a dialog would just flash).
constexpr uint64_t kAsyncLoadBytes = 16ull << 20;

uint64_t FileSizeOnDisk(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) == FALSE) {
        return 0;
    }
    return (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
}

// True when the document begins with the classic ".LOG" auto-timestamp marker.
bool StartsWithLogTag(const std::wstring& text) {
    return text.compare(0, 4, L".LOG") == 0;
}

} // namespace

void FileService::DetectDocumentLanguage(std::wstring_view text) {
    const std::wstring_view prefix =
        text.substr(0, std::min(text.size(), kDetectPrefixChars));
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
    // The new name may change the language; sniff the cheap prefix, not a
    // full copy of a large document.
    DetectDocumentLanguage(buffer_.GetTextPrefix(kDetectPrefixChars));
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
    if (FileSizeOnDisk(path) >= kAsyncLoadBytes) {
        return LoadFromPathAsync(path, forced);
    }
    return ApplyLoadResult(LoadTextFile(path, forced), path);
}

bool FileService::LoadFromPathAsync(const std::wstring& path,
                                    std::optional<TextEncoding> forced) {
    // The decode/normalize pass runs on a worker; the UI thread pumps the
    // progress dialog through the prompts seam. join() synchronizes the
    // hand-off, so `result` needs no further guarding.
    LoadProgressShared shared;
    TextFileLoadResult result;
    std::thread worker([&path, forced, &shared, &result]() {
        LoadHooks hooks;
        hooks.progress = [&shared](size_t bytesDone, size_t bytesTotal) {
            shared.bytesDone.store(static_cast<uint64_t>(bytesDone),
                                   std::memory_order_relaxed);
            shared.bytesTotal.store(static_cast<uint64_t>(bytesTotal),
                                    std::memory_order_relaxed);
            return !shared.cancelRequested.load(std::memory_order_relaxed);
        };
        result = LoadTextFile(path, forced, hooks);
        shared.finished.store(true, std::memory_order_release);
    });
    prompts_.PumpLoadProgress(util::PathLeaf(path), shared);
    worker.join();
    return ApplyLoadResult(std::move(result), path);
}

bool FileService::ApplyLoadResult(TextFileLoadResult result, const std::wstring& path) {
    switch (result.status) {
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
        case LoadStatus::Cancelled:
            return false;  // The user aborted from the progress dialog; keep the document.
        case LoadStatus::Ok:
            break;
    }

    // The text arrives already CRLF-normalized (the Edit control only breaks
    // lines on CRLF); the detected original convention is restored on save.
    document_.lineEnding = result.lineEnding;
    document_.filePath = path;
    document_.untitled = false;
    document_.encoding = result.encoding;
    DetectDocumentLanguage(result.text);
    const bool logTag = StartsWithLogTag(result.text);
    buffer_.SetText(std::move(result.text));
    watcher_.Arm(path);

    // ".LOG" files get a timestamp appended at end-of-file on open.
    if (logTag) {
        buffer_.MoveCaretToEnd();
        buffer_.InsertText(util::FormatTimestamp(true));
    }
    NotifyDocumentChanged();
    return true;
}

bool FileService::SaveToPath(const std::wstring& path, TextEncoding encoding) {
    // Streamed in chunks straight off the control buffer: no full converted
    // or encoded copy of a large document is ever held, and the lossy prompt
    // gates the final move so a declined save leaves the target untouched.
    bool lossy = false;
    StreamSaveStatus saved = StreamSaveStatus::CreateError;
    buffer_.ReadText([&](std::wstring_view text) {
        saved = WriteTextStreamed(path, text, document_.lineEnding, encoding, lossy,
                                  [this, &path] {
                                      return prompts_.AskContinueLossySave(
                                          util::PathLeaf(path));
                                  });
    });

    switch (saved) {
        case StreamSaveStatus::CreateError:
            prompts_.ReportError(FilePrompts::Error::CreateFailed, util::PathLeaf(path));
            return false;
        case StreamSaveStatus::WriteError:
            prompts_.ReportError(FilePrompts::Error::WriteFailed, util::PathLeaf(path));
            return false;
        case StreamSaveStatus::Aborted:
            return false;  // The user declined the lossy save; no error box.
        case StreamSaveStatus::Ok:
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

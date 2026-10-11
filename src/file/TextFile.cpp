#include "file/TextFile.hpp"

#include <array>

#include "util/FileMapping.hpp"
#include "util/HandleGuard.hpp"
#include "util/WinLean.hpp"

namespace notepadxp::file {

namespace {

// Notepad refuses files at/above 1 GiB, matching classic Notepad.
constexpr long long kMaxFileSize = 0x40000000LL;

// Bytes sampled from the file head by SniffEncoding (the Open-dialog preview).
constexpr size_t kSniffBytes = 1024;

util::HandleGuard OpenForRead(const std::wstring& path) {
    return util::HandleGuard(CreateFileW(path.c_str(), GENERIC_READ,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
}

} // namespace

TextFileLoadResult LoadTextFile(const std::wstring& path, std::optional<TextEncoding> forced,
                                const LoadHooks& hooks) {
    TextFileLoadResult result;
    const util::HandleGuard file = OpenForRead(path);
    if (!file.IsValid()) {
        const DWORD error = GetLastError();
        result.status = (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
                            ? LoadStatus::NotFound
                            : LoadStatus::AccessError;
        return result;
    }

    LARGE_INTEGER size{};
    if (GetFileSizeEx(file.Get(), &size) &&
        (size.QuadPart >= kMaxFileSize || size.HighPart != 0)) {
        result.status = LoadStatus::TooLarge;
        return result;
    }

    result.encoding = forced.value_or(TextEncoding::Ansi);
    const util::ReadOnlyFileMapping mapping(file.Get());  // Invalid for an empty file.
    if (mapping.IsValid()) {
        const std::byte* data = mapping.Data();
        const size_t bytes = mapping.Size();
        if (hooks.progress && !hooks.progress(0, bytes)) {
            result.status = LoadStatus::Cancelled;
            return result;
        }
        result.encoding = forced.value_or(DetectEncoding(data, bytes));

        // One streaming pass: decode chunk by chunk (never splitting a
        // multi-byte sequence), folding NULs, counting line endings and
        // emitting CRLF-normalized text — the exact composition of DecodeText
        // + DetectLineEnding + NormalizeToCrlf, but with progress and cancel.
        result.text.reserve(bytes + bytes / 16 + 16);
        CrlfStream normalizer(result.text);
        std::wstring raw;  // Reused per-chunk decode buffer.
        raw.reserve(hooks.chunkBytes + 4);
        const size_t bom = BomLength(data, bytes, result.encoding);
        size_t pos = bom;
        bool cancelled = false;
        while (pos < bytes) {
            raw.clear();
            pos += DecodeChunk(data + pos, bytes - pos, hooks.chunkBytes, result.encoding, raw);
            normalizer.Feed(raw);
            if (hooks.progress && !hooks.progress(pos, bytes)) {
                cancelled = true;
                break;
            }
        }
        if (cancelled) {
            result.status = LoadStatus::Cancelled;
            return result;
        }
        normalizer.Finish();
        result.lineEnding = normalizer.Detected();
    }
    result.status = LoadStatus::Ok;
    return result;
}

SaveStatus WriteAllBytes(const std::wstring& path, const std::vector<std::byte>& bytes) {
    const util::HandleGuard file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                             FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.IsValid()) {
        return SaveStatus::CreateError;
    }
    if (!bytes.empty()) {
        DWORD written = 0;
        const BOOL ok = WriteFile(file.Get(), bytes.data(), static_cast<DWORD>(bytes.size()),
                                  &written, nullptr);
        if (ok == FALSE || written != bytes.size()) {
            return SaveStatus::WriteError;
        }
    }
    return SaveStatus::Ok;
}

StreamSaveStatus WriteTextStreamed(const std::wstring& path, std::wstring_view text,
                                   LineEnding target, TextEncoding encoding, bool& lossy,
                                   const std::function<bool()>& confirmLossy) {
    lossy = false;

    // Write to a sibling temp file, then move over the target: a failed or
    // declined save never truncates the document.
    const std::wstring temp =
        path + L".nxpsave-" + std::to_wstring(GetCurrentProcessId());
    util::HandleGuard file(CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.IsValid()) {
        return StreamSaveStatus::CreateError;
    }

    auto writeAll = [&file](const void* data, size_t bytes) {
        const auto* p = static_cast<const char*>(data);
        while (bytes > 0) {
            DWORD written = 0;
            if (WriteFile(file.Get(), p, static_cast<DWORD>(bytes), &written, nullptr) ==
                    FALSE ||
                written == 0) {
                return false;
            }
            p += written;
            bytes -= written;
        }
        return true;
    };

    // The BOM, once, at the head of the stream.
    switch (encoding) {
        case TextEncoding::Utf16Le:
            if (!writeAll("\xFF\xFE", 2)) {
                DeleteFileW(temp.c_str());
                return StreamSaveStatus::WriteError;
            }
            break;
        case TextEncoding::Utf16Be:
            if (!writeAll("\xFE\xFF", 2)) {
                DeleteFileW(temp.c_str());
                return StreamSaveStatus::WriteError;
            }
            break;
        case TextEncoding::Utf8:
            if (!writeAll("\xEF\xBB\xBF", 3)) {
                DeleteFileW(temp.c_str());
                return StreamSaveStatus::WriteError;
            }
            break;
        case TextEncoding::Ansi:
        default:
            break;
    }

    constexpr size_t kChunkChars = 1 << 20;  // 2 MB of wchar per chunk.
    std::wstring converted;                  // Reused per-chunk restore buffer.
    for (size_t pos = 0; pos < text.size();) {
        size_t take = text.size() - pos < kChunkChars ? text.size() - pos : kChunkChars;
        if (pos + take < text.size()) {
            // Never split a surrogate pair across chunks (its UTF-8/ANSI
            // encoding would differ from the one-shot conversion). A CRLF
            // split needs no backing off: RestoreStream carries the '\r'.
            const wchar_t last = text[pos + take - 1];
            if (last >= 0xD800 && last <= 0xDBFF) {
                --take;
            }
        }

        // Crlf is the control's own form: an identity pass (ConvertFromCrlf
        // parity). Only Lf/Cr rewrite, streaming with the '\r' carry.
        bool chunkLossy = false;
        std::vector<std::byte> bytes;
        if (target == LineEnding::Crlf) {
            bytes = EncodeChunk(text.substr(pos, take), encoding, chunkLossy);
        } else {
            converted.clear();
            RestoreStream restorer(target, converted);
            restorer.Feed(text.substr(pos, take));
            restorer.Finish();
            bytes = EncodeChunk(converted, encoding, chunkLossy);
        }
        lossy = lossy || chunkLossy;
        if (!bytes.empty() && !writeAll(bytes.data(), bytes.size())) {
            DeleteFileW(temp.c_str());
            return StreamSaveStatus::WriteError;
        }
        pos += take;
    }

    if (lossy && confirmLossy && !confirmLossy()) {
        DeleteFileW(temp.c_str());
        return StreamSaveStatus::Aborted;
    }

    file.Reset();  // Close before the move (the target must not be in use).
    if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) == FALSE) {
        DeleteFileW(temp.c_str());
        return StreamSaveStatus::WriteError;
    }
    return StreamSaveStatus::Ok;
}

std::optional<std::wstring> ReadTailText(const std::wstring& path,
                                         unsigned long long fromOffset, TextEncoding encoding) {
    const util::HandleGuard file = OpenForRead(path);
    if (!file.IsValid()) {
        return std::nullopt;
    }
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file.Get(), &size) == FALSE ||
        static_cast<unsigned long long>(size.QuadPart) <= fromOffset) {
        return std::nullopt;
    }
    const unsigned long long tailBytes =
        static_cast<unsigned long long>(size.QuadPart) - fromOffset;
    if (tailBytes >= static_cast<unsigned long long>(kMaxFileSize)) {
        return std::nullopt;
    }
    if ((encoding == TextEncoding::Utf16Le || encoding == TextEncoding::Utf16Be) &&
        tailBytes % 2 != 0) {
        return std::nullopt;  // Misaligned UTF-16 tail: let the caller reload.
    }
    LARGE_INTEGER pos{};
    pos.QuadPart = static_cast<LONGLONG>(fromOffset);
    if (SetFilePointerEx(file.Get(), pos, nullptr, FILE_BEGIN) == FALSE) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<size_t>(tailBytes));
    DWORD read = 0;
    if (ReadFile(file.Get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) ==
            FALSE ||
        read != bytes.size()) {
        return std::nullopt;
    }
    // DecodeText would strip a leading BOM; a growing log's tail never starts
    // with one, and a rewritten file reports Replaced (full reload) instead.
    return DecodeText(bytes.data(), bytes.size(), encoding);
}

std::optional<TextEncoding> SniffEncoding(const std::wstring& path) {
    const util::HandleGuard file = OpenForRead(path);
    if (!file.IsValid()) {
        return std::nullopt;
    }
    std::array<std::byte, kSniffBytes> buffer{};
    DWORD read = 0;
    if (ReadFile(file.Get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) ==
        FALSE) {
        return std::nullopt;
    }
    return DetectEncoding(buffer.data(), read);
}

} // namespace notepadxp::file

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

TextFileLoadResult LoadTextFile(const std::wstring& path, std::optional<TextEncoding> forced) {
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
        result.encoding = forced.value_or(DetectEncoding(data, bytes));
        result.text = DecodeText(data, bytes, result.encoding);
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

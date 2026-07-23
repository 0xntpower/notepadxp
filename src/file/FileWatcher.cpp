#include "file/FileWatcher.hpp"

namespace notepadxp::file {

bool FileWatcher::Stat(const std::wstring& path, FILETIME& mtimeOut,
                       unsigned long long& sizeOut) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) == FALSE) {
        return false;
    }
    mtimeOut = data.ftLastWriteTime;
    sizeOut = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return true;
}

void FileWatcher::Arm(const std::wstring& path) {
    path_ = path;
    armed_ = Stat(path_, mtime_, size_);
}

void FileWatcher::Disarm() noexcept {
    armed_ = false;
    path_.clear();
    size_ = 0;
    mtime_ = FILETIME{};
}

FileWatcher::Verdict FileWatcher::Check() const {
    if (!armed_) {
        return Verdict::Unchanged;
    }
    FILETIME mtime{};
    unsigned long long size = 0;
    if (!Stat(path_, mtime, size)) {
        return Verdict::Unchanged;  // Missing/locked: handled by the next open.
    }
    const int timeOrder = CompareFileTime(&mtime, &mtime_);
    if (timeOrder == 0 && size == size_) {
        return Verdict::Unchanged;
    }
    if (size > size_ && timeOrder >= 0) {
        return Verdict::Grown;
    }
    return Verdict::Replaced;
}

void FileWatcher::Rearm() {
    if (armed_) {
        armed_ = Stat(path_, mtime_, size_);
    }
}

} // namespace notepadxp::file

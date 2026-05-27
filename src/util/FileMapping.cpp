#include "util/FileMapping.hpp"

#include <cstddef>
#include <utility>

namespace notepadxp::util {

ReadOnlyFileMapping::ReadOnlyFileMapping(HANDLE file) noexcept {
    LARGE_INTEGER fileSize{};
    if (file == nullptr || file == INVALID_HANDLE_VALUE || GetFileSizeEx(file, &fileSize) == 0) {
        return;
    }
    if (fileSize.QuadPart <= 0) {
        return;  // Empty files cannot be mapped; caller treats as empty content.
    }

    mapping_ = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping_ == nullptr) {
        return;
    }

    view_ = MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0);
    if (view_ == nullptr) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
        return;
    }
    size_ = static_cast<size_t>(fileSize.QuadPart);
}

ReadOnlyFileMapping::~ReadOnlyFileMapping() {
    Reset();
}

ReadOnlyFileMapping::ReadOnlyFileMapping(ReadOnlyFileMapping&& other) noexcept
    : mapping_(other.mapping_), view_(other.view_), size_(other.size_) {
    other.mapping_ = nullptr;
    other.view_ = nullptr;
    other.size_ = 0;
}

ReadOnlyFileMapping& ReadOnlyFileMapping::operator=(ReadOnlyFileMapping&& other) noexcept {
    if (this != &other) {
        Reset();
        mapping_ = other.mapping_;
        view_ = other.view_;
        size_ = other.size_;
        other.mapping_ = nullptr;
        other.view_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

void ReadOnlyFileMapping::Reset() noexcept {
    if (view_ != nullptr) {
        UnmapViewOfFile(view_);
        view_ = nullptr;
    }
    if (mapping_ != nullptr) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }
    size_ = 0;
}

} // namespace notepadxp::util

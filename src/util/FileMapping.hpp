#pragma once

// FileMapping.hpp — RAII read-only memory mapping of an open file.

#include <cstddef>

#include "util/WinLean.hpp"

namespace notepadxp::util {

/// @brief Maps an open file read-only into memory and tears the mapping down on
///        destruction. Loading via a mapping avoids pagefile pressure for large
///        files, matching classic Notepad's load path.
/// @threadsafety Not thread-safe. Move-only.
///
/// Mapping a zero-length file is not possible; for such files IsValid() returns
/// false and the caller should treat the content as empty.
class ReadOnlyFileMapping final {
public:
    ReadOnlyFileMapping() noexcept = default;

    /// @brief Create a read-only mapping over @p file. The file handle must
    ///        outlive this object; this class does not take ownership of it.
    explicit ReadOnlyFileMapping(HANDLE file) noexcept;

    ~ReadOnlyFileMapping();

    ReadOnlyFileMapping(const ReadOnlyFileMapping&) = delete;
    ReadOnlyFileMapping& operator=(const ReadOnlyFileMapping&) = delete;
    ReadOnlyFileMapping(ReadOnlyFileMapping&& other) noexcept;
    ReadOnlyFileMapping& operator=(ReadOnlyFileMapping&& other) noexcept;

    [[nodiscard]] bool IsValid() const noexcept {
        return view_ != nullptr;
    }

    /// @brief Base address of the mapped, read-only bytes (null if invalid).
    [[nodiscard]] const std::byte* Data() const noexcept {
        return static_cast<const std::byte*>(view_);
    }

    /// @brief Size of the mapped region in bytes.
    [[nodiscard]] size_t Size() const noexcept {
        return size_;
    }

private:
    void Reset() noexcept;

    HANDLE mapping_ = nullptr;
    const void* view_ = nullptr;
    size_t size_ = 0;
};

} // namespace notepadxp::util

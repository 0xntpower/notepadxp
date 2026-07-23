#pragma once

// FileWatcher.hpp — Cheap change detection for the open file: one stat
// (GetFileAttributesExW) per check, no handles held, no timers of its own.

#include <string>

#include "util/WinLean.hpp"

namespace notepadxp::file {

/// @brief Remembers a file's {last-write time, size} and classifies later
///        states against that stamp.
/// @threadsafety UI-thread only.
class FileWatcher final {
public:
    enum class Verdict {
        Unchanged,  // Same stamp (or nothing armed / stat failed).
        Grown,      // Strictly larger, not older: an append (tailable).
        Replaced,   // Any other difference: rewritten, truncated, swapped.
    };

    /// @brief Stamp @p path as the baseline.
    void Arm(const std::wstring& path);

    void Disarm() noexcept;

    [[nodiscard]] bool IsArmed() const noexcept {
        return armed_;
    }

    /// @brief Compare the file's current state against the stamp.
    [[nodiscard]] Verdict Check() const;

    /// @brief Adopt the current on-disk state as the new baseline (after a
    ///        declined reload, so the same change is not re-asked).
    void Rearm();

    /// @brief Size at the last (re)arm — the tail-read offset in bytes.
    [[nodiscard]] unsigned long long ArmedSize() const noexcept {
        return size_;
    }

private:
    static bool Stat(const std::wstring& path, FILETIME& mtimeOut,
                     unsigned long long& sizeOut);

    std::wstring path_;
    FILETIME mtime_{};
    unsigned long long size_ = 0;
    bool armed_ = false;
};

} // namespace notepadxp::file

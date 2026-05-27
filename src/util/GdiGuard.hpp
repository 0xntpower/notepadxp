#pragma once

// GdiGuard.hpp — RAII owner for a GDI object (HFONT, HBRUSH, HPEN, ...).

#include "util/WinLean.hpp"

namespace notepadxp::util {

/// @brief Owns a GDI object and frees it via DeleteObject on destruction.
/// @threadsafety Not thread-safe. One owner per object; transfer with move.
///
/// HFONT, HBRUSH, HPEN and HBITMAP are all HGDIOBJ subtypes that DeleteObject
/// releases, so this single guard serves them all.
class GdiGuard final {
public:
    GdiGuard() noexcept = default;
    explicit GdiGuard(HGDIOBJ object) noexcept : object_(object) {}

    ~GdiGuard() {
        Reset();
    }

    GdiGuard(const GdiGuard&) = delete;
    GdiGuard& operator=(const GdiGuard&) = delete;

    GdiGuard(GdiGuard&& other) noexcept : object_(other.object_) {
        other.object_ = nullptr;
    }

    GdiGuard& operator=(GdiGuard&& other) noexcept {
        if (this != &other) {
            Reset(other.object_);
            other.object_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] bool IsValid() const noexcept {
        return object_ != nullptr;
    }

    [[nodiscard]] HGDIOBJ Get() const noexcept {
        return object_;
    }

    /// @brief Typed accessor; the caller asserts the stored object is an HFONT.
    [[nodiscard]] HFONT AsFont() const noexcept {
        return static_cast<HFONT>(object_);
    }

    /// @brief Delete the current object and adopt a new one (default: none).
    void Reset(HGDIOBJ object = nullptr) noexcept {
        if (object_ != nullptr) {
            DeleteObject(object_);
        }
        object_ = object;
    }

    [[nodiscard]] HGDIOBJ Release() noexcept {
        HGDIOBJ released = object_;
        object_ = nullptr;
        return released;
    }

private:
    HGDIOBJ object_ = nullptr;
};

} // namespace notepadxp::util

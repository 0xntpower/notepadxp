#pragma once

// HandleGuard.hpp — RAII owner for a Win32 kernel HANDLE.

#include "util/WinLean.hpp"

namespace notepadxp::util {

/// @brief Owns a Win32 HANDLE and closes it via CloseHandle on destruction.
/// @threadsafety Not thread-safe. One owner per handle; transfer with move.
class HandleGuard final {
public:
    HandleGuard() noexcept = default;
    explicit HandleGuard(HANDLE handle) noexcept : handle_(handle) {}

    ~HandleGuard() {
        Reset();
    }

    HandleGuard(const HandleGuard&) = delete;
    HandleGuard& operator=(const HandleGuard&) = delete;

    HandleGuard(HandleGuard&& other) noexcept : handle_(other.handle_) {
        other.handle_ = INVALID_HANDLE_VALUE;
    }

    HandleGuard& operator=(HandleGuard&& other) noexcept {
        if (this != &other) {
            Reset(other.handle_);
            other.handle_ = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    /// @brief True when the owned handle is neither null nor INVALID_HANDLE_VALUE.
    [[nodiscard]] bool IsValid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    [[nodiscard]] HANDLE Get() const noexcept {
        return handle_;
    }

    /// @brief Close the current handle and adopt a new one (default: empty).
    void Reset(HANDLE handle = INVALID_HANDLE_VALUE) noexcept {
        if (IsValid()) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

    /// @brief Relinquish ownership without closing; caller becomes responsible.
    [[nodiscard]] HANDLE Release() noexcept {
        HANDLE released = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return released;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

} // namespace notepadxp::util

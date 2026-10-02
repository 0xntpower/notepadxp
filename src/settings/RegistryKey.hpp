#pragma once

// RegistryKey.hpp — RAII wrapper over an HKEY with typed get/set helpers.

#include <string>

#include "util/WinLean.hpp"

namespace notepadxp::settings {

/// @brief Owns an open registry key (closed via RegCloseKey on destruction) and
///        provides typed accessors that fall back to a default on any failure.
/// @threadsafety Not thread-safe. Move-only.
class RegistryKey final {
public:
    RegistryKey() noexcept = default;
    ~RegistryKey();

    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;
    RegistryKey(RegistryKey&& other) noexcept;
    RegistryKey& operator=(RegistryKey&& other) noexcept;

    /// @brief Open @p subKey under @p root, creating it if absent.
    /// @return A valid key on success, an empty (invalid) key on failure.
    [[nodiscard]] static RegistryKey CreateOrOpen(HKEY root, const wchar_t* subKey);

    /// @brief Open an existing @p subKey under @p root for reading only.
    /// @return A valid key on success, an empty (invalid) key if absent.
    [[nodiscard]] static RegistryKey OpenForRead(HKEY root, const wchar_t* subKey);

    [[nodiscard]] bool IsValid() const noexcept {
        return key_ != nullptr;
    }

    /// @brief Read a REG_DWORD; returns @p defaultValue if missing or wrong type.
    [[nodiscard]] DWORD GetDword(const wchar_t* name, DWORD defaultValue) const;

    /// @brief Read a REG_SZ. Returns @p defaultValue if missing or wrong type.
    [[nodiscard]] std::wstring GetString(const wchar_t* name,
                                         const std::wstring& defaultValue) const;

    bool SetDword(const wchar_t* name, DWORD value) const;
    bool SetString(const wchar_t* name, const std::wstring& value) const;

private:
    explicit RegistryKey(HKEY key) noexcept : key_(key) {}

    HKEY key_ = nullptr;
};

} // namespace notepadxp::settings

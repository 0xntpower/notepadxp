#pragma once

// RegistryKey.hpp — RAII wrapper over an HKEY with typed get/set helpers.

#include <string>
#include <string_view>

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
    [[nodiscard]] static RegistryKey CreateOrOpen(HKEY root, std::wstring_view subKey);

    [[nodiscard]] bool IsValid() const noexcept {
        return key_ != nullptr;
    }

    /// @brief Read a REG_DWORD; returns @p defaultValue if missing or wrong type.
    [[nodiscard]] DWORD GetDword(std::wstring_view name, DWORD defaultValue) const;

    /// @brief Read a REG_SZ; returns @p defaultValue if missing or wrong type.
    [[nodiscard]] std::wstring GetString(std::wstring_view name,
                                         std::wstring_view defaultValue) const;

    bool SetDword(std::wstring_view name, DWORD value) const;
    bool SetString(std::wstring_view name, std::wstring_view value) const;

private:
    explicit RegistryKey(HKEY key) noexcept : key_(key) {}

    HKEY key_ = nullptr;
};

} // namespace notepadxp::settings

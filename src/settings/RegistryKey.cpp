#include "settings/RegistryKey.hpp"

#include <array>
#include <string>
#include <string_view>

namespace notepadxp::settings {

RegistryKey::~RegistryKey() {
    if (key_ != nullptr) {
        RegCloseKey(key_);
    }
}

RegistryKey::RegistryKey(RegistryKey&& other) noexcept : key_(other.key_) {
    other.key_ = nullptr;
}

RegistryKey& RegistryKey::operator=(RegistryKey&& other) noexcept {
    if (this != &other) {
        if (key_ != nullptr) {
            RegCloseKey(key_);
        }
        key_ = other.key_;
        other.key_ = nullptr;
    }
    return *this;
}

RegistryKey RegistryKey::CreateOrOpen(HKEY root, std::wstring_view subKey) {
    const std::wstring path(subKey);
    HKEY opened = nullptr;
    const LSTATUS status = RegCreateKeyExW(root, path.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                           KEY_READ | KEY_WRITE, nullptr, &opened, nullptr);
    if (status != ERROR_SUCCESS) {
        return RegistryKey{};
    }
    return RegistryKey{opened};
}

DWORD RegistryKey::GetDword(std::wstring_view name, DWORD defaultValue) const {
    if (key_ == nullptr) {
        return defaultValue;
    }
    DWORD value = 0;
    DWORD size = sizeof(value);
    const std::wstring valueName(name);
    const LSTATUS status = RegGetValueW(key_, nullptr, valueName.c_str(), RRF_RT_REG_DWORD, nullptr,
                                        &value, &size);
    return status == ERROR_SUCCESS ? value : defaultValue;
}

std::wstring RegistryKey::GetString(std::wstring_view name, std::wstring_view defaultValue) const {
    if (key_ == nullptr) {
        return std::wstring(defaultValue);
    }
    const std::wstring valueName(name);

    // First query the required size, then read into a right-sized buffer.
    DWORD bytes = 0;
    LSTATUS status =
        RegGetValueW(key_, nullptr, valueName.c_str(), RRF_RT_REG_SZ, nullptr, nullptr, &bytes);
    if (status != ERROR_SUCCESS || bytes < sizeof(wchar_t)) {
        return std::wstring(defaultValue);
    }

    std::wstring buffer(bytes / sizeof(wchar_t), L'\0');
    status = RegGetValueW(key_, nullptr, valueName.c_str(), RRF_RT_REG_SZ, nullptr, buffer.data(),
                          &bytes);
    if (status != ERROR_SUCCESS) {
        return std::wstring(defaultValue);
    }
    // RegGetValueW guarantees null termination; trim the embedded trailing nulls.
    buffer.resize(wcslen(buffer.c_str()));
    return buffer;
}

bool RegistryKey::SetDword(std::wstring_view name, DWORD value) const {
    if (key_ == nullptr) {
        return false;
    }
    const std::wstring valueName(name);
    return RegSetValueExW(key_, valueName.c_str(), 0, REG_DWORD,
                          reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
    // reinterpret_cast: REG_DWORD payload is the raw little-endian bytes of `value`.
}

bool RegistryKey::SetString(std::wstring_view name, std::wstring_view value) const {
    if (key_ == nullptr) {
        return false;
    }
    const std::wstring valueName(name);
    const std::wstring data(value);
    // Size must include the terminating null.
    const DWORD bytes = static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t));
    return RegSetValueExW(key_, valueName.c_str(), 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(data.c_str()), bytes) == ERROR_SUCCESS;
    // reinterpret_cast: REG_SZ payload is the raw UTF-16 code units of `data`.
}

} // namespace notepadxp::settings

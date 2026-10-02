#include "settings/RegistryKey.hpp"

#include <string>

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

RegistryKey RegistryKey::CreateOrOpen(HKEY root, const wchar_t* subKey) {
    HKEY opened = nullptr;
    const LSTATUS status = RegCreateKeyExW(root, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                           KEY_READ | KEY_WRITE, nullptr, &opened, nullptr);
    if (status != ERROR_SUCCESS) {
        return RegistryKey{};
    }
    return RegistryKey{opened};
}

RegistryKey RegistryKey::OpenForRead(HKEY root, const wchar_t* subKey) {
    HKEY opened = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &opened) != ERROR_SUCCESS) {
        return RegistryKey{};
    }
    return RegistryKey{opened};
}

DWORD RegistryKey::GetDword(const wchar_t* name, DWORD defaultValue) const {
    if (key_ == nullptr) {
        return defaultValue;
    }
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LSTATUS status =
        RegGetValueW(key_, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size);
    return status == ERROR_SUCCESS ? value : defaultValue;
}

std::wstring RegistryKey::GetString(const wchar_t* name, const std::wstring& defaultValue) const {
    if (key_ == nullptr) {
        return defaultValue;
    }

    // First query the required size, then read into a right-sized buffer.
    DWORD bytes = 0;
    LSTATUS status = RegGetValueW(key_, nullptr, name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes);
    if (status != ERROR_SUCCESS || bytes < sizeof(wchar_t)) {
        return defaultValue;
    }

    std::wstring buffer(bytes / sizeof(wchar_t), L'\0');
    status = RegGetValueW(key_, nullptr, name, RRF_RT_REG_SZ, nullptr, buffer.data(), &bytes);
    if (status != ERROR_SUCCESS) {
        return defaultValue;
    }
    // RegGetValueW guarantees null termination; trim the embedded trailing nulls.
    buffer.resize(wcslen(buffer.c_str()));
    return buffer;
}

bool RegistryKey::SetDword(const wchar_t* name, DWORD value) const {
    if (key_ == nullptr) {
        return false;
    }
    return RegSetValueExW(key_, name, 0, REG_DWORD,
                          reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
    // reinterpret_cast: REG_DWORD payload is the raw little-endian bytes of `value`.
}

bool RegistryKey::SetString(const wchar_t* name, const std::wstring& value) const {
    if (key_ == nullptr) {
        return false;
    }
    // Size must include the terminating null.
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    return RegSetValueExW(key_, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                          bytes) == ERROR_SUCCESS;
    // reinterpret_cast: REG_SZ payload is the raw UTF-16 code units of `value`.
}

} // namespace notepadxp::settings

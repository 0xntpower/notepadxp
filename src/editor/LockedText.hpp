#pragma once

// LockedText.hpp — Zero-copy read access to a multiline Edit control's text.

#include <string>
#include <string_view>

#include "util/WinLean.hpp"

namespace notepadxp::editor {

/// @brief Read-only view of a multiline Edit control's live buffer, taken via
///        EM_GETHANDLE + LocalLock: O(1), no copy. Falls back to a WM_GETTEXT
///        copy if the control does not hand out its buffer.
/// @threadsafety UI-thread only. The view lives exactly as long as this object,
///        and nothing may edit the control meanwhile (a locked buffer cannot be
///        reallocated): let it go before sending any text-changing message.
class LockedText final {
public:
    explicit LockedText(HWND edit) {
        const auto length = static_cast<size_t>(GetWindowTextLengthW(edit));
        const auto handle = reinterpret_cast<HLOCAL>(SendMessageW(edit, EM_GETHANDLE, 0, 0));
        if (handle != nullptr && LocalSize(handle) >= (length + 1) * sizeof(wchar_t)) {
            if (const auto* chars = static_cast<const wchar_t*>(LocalLock(handle))) {
                handle_ = handle;
                view_ = std::wstring_view(chars, length);
                return;
            }
        }
        copy_.resize(length + 1);
        copy_.resize(static_cast<size_t>(
            GetWindowTextW(edit, copy_.data(), static_cast<int>(copy_.size()))));
        view_ = copy_;
    }

    ~LockedText() {
        if (handle_ != nullptr) {
            LocalUnlock(handle_);
        }
    }

    LockedText(const LockedText&) = delete;
    LockedText& operator=(const LockedText&) = delete;

    [[nodiscard]] std::wstring_view View() const noexcept {
        return view_;
    }

private:
    HLOCAL handle_ = nullptr;
    std::wstring copy_;  // Fallback storage only.
    std::wstring_view view_;
};

} // namespace notepadxp::editor

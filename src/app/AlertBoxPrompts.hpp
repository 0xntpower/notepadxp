#pragma once

// AlertBoxPrompts.hpp — Message-box adapter at the FilePrompts seam.

#include <string>

#include "file/FilePrompts.hpp"
#include "util/WinLean.hpp"

namespace notepadxp::app {

/// @brief Turns FileService's questions and reports into the classic Notepad
///        message boxes, owned by the frame window.
class AlertBoxPrompts final : public file::FilePrompts {
public:
    /// @brief Set the window that owns the message boxes. Call once after the
    ///        frame window exists.
    void SetOwner(HWND owner) noexcept {
        owner_ = owner;
    }

    [[nodiscard]] SaveChoice AskSaveChanges(const std::wstring& documentName) override;
    [[nodiscard]] bool AskCreateNewFile(const std::wstring& fileName) override;
    [[nodiscard]] bool AskContinueLossySave(const std::wstring& fileName) override;
    void ReportError(Error error, const std::wstring& fileName) override;

private:
    HWND owner_ = nullptr;
};

} // namespace notepadxp::app

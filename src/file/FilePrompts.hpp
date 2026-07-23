#pragma once

// FilePrompts.hpp — The seam through which the document lifecycle asks or
// tells the user. The app satisfies it with message boxes
// (app::AlertBoxPrompts); tests substitute fakes.

#include <string>

namespace notepadxp::file {

/// @brief User decisions and reports the document lifecycle needs. FileService
///        decides what to ask; an adapter at this seam does the asking.
class FilePrompts {
public:
    virtual ~FilePrompts() = default;

    enum class SaveChoice { Save, Discard, Cancel };

    /// @brief Failures FileService reports but does not decide on.
    enum class Error {
        OpenFailed,    // The file exists but could not be read.
        TooLarge,      // At/above the classic Notepad size limit.
        CreateFailed,  // The file could not be created for writing.
        WriteFailed,   // The write itself failed.
    };

    /// @brief "The text in <name> has changed. Save the changes?"
    [[nodiscard]] virtual SaveChoice AskSaveChanges(const std::wstring& documentName) = 0;

    /// @brief File-not-found on open: create a new file with this name?
    [[nodiscard]] virtual bool AskCreateNewFile(const std::wstring& fileName) = 0;

    /// @brief The chosen encoding cannot represent every character; save anyway?
    [[nodiscard]] virtual bool AskContinueLossySave(const std::wstring& fileName) = 0;

    /// @brief Report a failed operation on @p fileName.
    virtual void ReportError(Error error, const std::wstring& fileName) = 0;
};

} // namespace notepadxp::file

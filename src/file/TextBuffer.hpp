#pragma once

// TextBuffer.hpp — The document text as the file lifecycle needs it.

#include <string>
#include <string_view>

namespace notepadxp::file {

/// @brief Interface over the document's text and modified flag. The edit view
///        adapts this in the app; tests substitute an in-memory fake.
class TextBuffer {
public:
    virtual ~TextBuffer() = default;

    /// @brief Clear all text and the modified flag (File > New).
    virtual void Reset() = 0;

    /// @brief Replace all text with @p text and clear the modified flag.
    virtual void SetText(std::wstring_view text) = 0;

    /// @brief Return the full document text.
    [[nodiscard]] virtual std::wstring GetText() = 0;

    [[nodiscard]] virtual int TextLength() = 0;
    [[nodiscard]] virtual bool IsModified() = 0;
    virtual void SetModified(bool modified) = 0;

    /// @brief Move the caret to end of buffer (the .LOG timestamp position).
    virtual void MoveCaretToEnd() = 0;

    /// @brief Replace the current selection with @p text.
    virtual void InsertText(std::wstring_view text) = 0;
};

} // namespace notepadxp::file

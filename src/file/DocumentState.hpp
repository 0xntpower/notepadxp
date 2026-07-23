#pragma once

// DocumentState.hpp — Identity, encoding, and detected language of the
// document currently open.

#include <string>

#include "file/Encoding.hpp"
#include "lang/Language.hpp"

namespace notepadxp::file {

/// @brief What file (if any) is loaded, how it should be saved, and what
///        language the smart features should assume.
/// @threadsafety Plain data; owned and mutated on the UI thread.
struct DocumentState {
    std::wstring filePath;  // Full path; empty while untitled.
    bool untitled = true;
    TextEncoding encoding = TextEncoding::Ansi;  // Encoding to save with.
    lang::Language language = lang::Language::PlainText;
    lang::IndentStyle indentStyle{};
};

} // namespace notepadxp::file

#pragma once

// DocumentState.hpp — Identity and encoding of the document currently open.

#include <string>

#include "file/Encoding.hpp"

namespace notepadxp::file {

/// @brief What file (if any) is loaded, and how it should be saved.
/// @threadsafety Plain data; owned and mutated on the UI thread.
struct DocumentState {
    std::wstring filePath;  // Full path; empty while untitled.
    bool untitled = true;
    TextEncoding encoding = TextEncoding::Ansi;  // Encoding to save with.
};

} // namespace notepadxp::file

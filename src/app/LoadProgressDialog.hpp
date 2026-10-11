#pragma once

// LoadProgressDialog.hpp — The progress window shown while a large document
// loads on the worker thread: file name, byte progress bar, Cancel. Pumps
// messages until the load finishes; Cancel asks the worker to abort (it exits
// at the next decoded chunk). Hand-built (no dialog template) and font-driven
// like the rest of the app.

#include <string>

#include "file/FilePrompts.hpp"
#include "util/WinLean.hpp"

namespace notepadxp::app {

class LoadProgressDialog final {
public:
    /// @brief Show the window over @p owner and pump until
    ///        @p shared.finished. Blocks; the caller joins the worker after.
    static void Run(HWND owner, const std::wstring& fileName, file::LoadProgressShared& shared);
};

} // namespace notepadxp::app

#pragma once

// Application.hpp — Process bootstrap and the main message loop.

namespace notepadxp::app {

/// @brief Owns process-wide initialization (COM, common controls, the ATL/WTL
///        module), creates the main frame, and runs the message loop.
/// @threadsafety Run() must be called once, on the main UI thread.
class Application final {
public:
    /// @brief Initialize, create the window, pump messages until quit.
    /// @param commandLine Full command line (e.g. GetCommandLineW()).
    /// @param showCmd The nCmdShow passed to wWinMain.
    /// @return The process exit code (the WM_QUIT wParam).
    [[nodiscard]] int Run(const wchar_t* commandLine, int showCmd);
};

} // namespace notepadxp::app

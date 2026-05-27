#include "app/Application.hpp"

#include "WtlIncludes.hpp"

#include "Resource.h"
#include "app/MainFrame.hpp"
#include "util/CommandLine.hpp"

// The single ATL/WTL module instance (declared extern in WtlIncludes.hpp).
CAppModule _Module;

namespace notepadxp::app {

int Application::Run(const wchar_t* commandLine, int showCmd) {
    // Common dialogs and shell drag-drop expect an initialized COM apartment.
    const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    AtlInitCommonControls(ICC_WIN95_CLASSES);  // Registers the (classic) status bar class.
    _Module.Init(nullptr, GetModuleHandleW(nullptr));

    int exitCode = 0;
    {
        const util::ParsedCommandLine parsed = util::ParseCommandLine(commandLine);

        MainFrame frame;
        if (!frame.RunSetup(parsed, showCmd)) {
            _Module.Term();
            if (SUCCEEDED(comInit)) {
                CoUninitialize();
            }
            return 1;
        }

        const HACCEL accelerators =
            LoadAcceleratorsW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(ID_ACCEL));

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            // Let the modeless Find/Replace dialog (when open) handle its keys.
            const HWND modeless = frame.ActiveModelessDialog();
            if (modeless != nullptr && IsDialogMessageW(modeless, &msg)) {
                continue;
            }
            if (accelerators != nullptr &&
                TranslateAcceleratorW(frame.m_hWnd, accelerators, &msg)) {
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        exitCode = static_cast<int>(msg.wParam);
    }

    _Module.Term();
    if (SUCCEEDED(comInit)) {
        CoUninitialize();
    }
    return exitCode;
}

} // namespace notepadxp::app

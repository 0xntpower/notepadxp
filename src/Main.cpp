// Main.cpp — wWinMain entry point. Delegates to notepadxp::app::Application.
//
// NOTE: There is intentionally no /manifestdependency pragma for ComCtl32 v6.
// The classic (non-themed) look is selected via res/Notepad.manifest, which
// omits that dependency.

#include "WtlIncludes.hpp"

#include "app/Application.hpp"

int WINAPI wWinMain(HINSTANCE /*instance*/, HINSTANCE /*prevInstance*/, LPWSTR /*cmdLine*/,
                    int nCmdShow) {
    notepadxp::app::Application app;
    return app.Run(GetCommandLineW(), nCmdShow);
}

#include "dialogs/AboutBox.hpp"

#include "Resource.h"

namespace notepadxp::dialogs {

namespace {

// Static "About NotepadXP" box (IDD_ABOUT): the template carries the icon and
// the fixed text. The proc fills in the version and closes on OK/Cancel.
INT_PTR CALLBACK AboutProc(HWND dialog, UINT message, WPARAM wParam, LPARAM /*lParam*/) {
    switch (message) {
        case WM_INITDIALOG:
            SetDlgItemTextW(dialog, IDC_ABOUT_VERSION, L"Version " NOTEPADXP_VERSION_STRING);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                EndDialog(dialog, LOWORD(wParam));
                return TRUE;
            }
            return FALSE;
        default:
            return FALSE;
    }
}

} // namespace

void AboutBox::Show(HWND parent) {
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ABOUT), parent, &AboutProc, 0);
}

} // namespace notepadxp::dialogs

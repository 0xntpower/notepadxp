#include "dialogs/AboutBox.hpp"

#include "Resource.h"

namespace notepadxp::dialogs {

namespace {

// Static "About NotepadXP" box (IDD_ABOUT): the template carries all the text
// and the application icon, so the proc only needs to close on OK/Cancel.
INT_PTR CALLBACK AboutProc(HWND dialog, UINT message, WPARAM wParam, LPARAM /*lParam*/) {
    switch (message) {
        case WM_INITDIALOG:
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

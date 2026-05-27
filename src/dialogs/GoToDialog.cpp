#include "dialogs/GoToDialog.hpp"

#include <optional>

#include "Resource.h"

namespace notepadxp::dialogs {

namespace {

// The dialog carries an int* (the 1-based line, in/out) through lParam.
INT_PTR CALLBACK GoToProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dialog, GWLP_USERDATA, static_cast<LONG_PTR>(lParam));
            const int* line = reinterpret_cast<const int*>(lParam);
            SetDlgItemInt(dialog, IDC_GOTO, static_cast<UINT>(*line), FALSE);
            SendDlgItemMessageW(dialog, IDC_GOTO, EM_SETSEL, 0, -1);  // Select for overtype.
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK: {
                    BOOL valid = FALSE;
                    const UINT value = GetDlgItemInt(dialog, IDC_GOTO, &valid, FALSE);
                    if (valid && value >= 1) {
                        auto* line =
                            reinterpret_cast<int*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
                        if (line != nullptr) {
                            *line = static_cast<int>(value);
                        }
                        EndDialog(dialog, IDOK);
                    } else {
                        MessageBeep(MB_ICONEXCLAMATION);
                    }
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(dialog, IDCANCEL);
                    return TRUE;
                default:
                    return FALSE;
            }
        default:
            return FALSE;
    }
}

} // namespace

std::optional<int> GoToDialog::Show(HWND parent, int currentLine) {
    int line = currentLine;  // The dialog reads and writes this through lParam.
    const INT_PTR result =
        DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_GOTODIALOG), parent,
                        &GoToProc, reinterpret_cast<LPARAM>(&line));
    if (result == IDOK) {
        return line;
    }
    return std::nullopt;
}

} // namespace notepadxp::dialogs

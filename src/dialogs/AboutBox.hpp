#pragma once

// AboutBox.hpp — The "About Notepad" dialog.

#include "WtlIncludes.hpp"

namespace notepadxp::dialogs {

/// @brief Shows the modal "About NotepadXP" dialog (IDD_ABOUT): application
///        icon, name, version, description, and copyright.
class AboutBox final {
public:
    static void Show(HWND parent);
};

} // namespace notepadxp::dialogs

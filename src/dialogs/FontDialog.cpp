#include "dialogs/FontDialog.hpp"

#include <commdlg.h>  // ChooseFontW (excluded by WIN32_LEAN_AND_MEAN).

namespace notepadxp::dialogs {

std::optional<FontDialog::Result> FontDialog::Choose(HWND parent, const LOGFONTW& current) {
    LOGFONTW logFont = current;  // ChooseFontW reads and writes through lpLogFont.

    CHOOSEFONTW chooseFont{};
    chooseFont.lStructSize = sizeof(chooseFont);
    chooseFont.hwndOwner = parent;
    chooseFont.lpLogFont = &logFont;
    chooseFont.Flags = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS | CF_NOVERTFONTS;
    chooseFont.nFontType = SCREEN_FONTTYPE;
    if (ChooseFontW(&chooseFont) == FALSE) {
        return std::nullopt;
    }
    return Result{logFont, chooseFont.iPointSize};  // iPointSize is tenths of a point.
}

} // namespace notepadxp::dialogs

#include "printing/PageSetup.hpp"

#include <array>
#include <string>

#include <commdlg.h>

#include "Resource.h"
#include "util/Measurement.hpp"

namespace notepadxp::printing {

namespace {

constexpr int kHeaderFooterMax = 39;  // Max header/footer length, in characters.
constexpr int kBufferChars = 64;

// Header/footer text passed to/from the hook via PAGESETUPDLG::lCustData.
struct PageSetupState {
    std::wstring header;
    std::wstring footer;
};

PageSetupState* StateOf(HWND dialog) {
    return reinterpret_cast<PageSetupState*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
}

UINT_PTR CALLBACK PageSetupHookProc(HWND dialog, UINT message, WPARAM /*wParam*/, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG: {
            auto* psd = reinterpret_cast<LPPAGESETUPDLGW>(lParam);
            SetWindowLongPtrW(dialog, GWLP_USERDATA, static_cast<LONG_PTR>(psd->lCustData));
            auto* state = reinterpret_cast<PageSetupState*>(psd->lCustData);
            SetDlgItemTextW(dialog, ID_HEADER, state->header.c_str());
            SetDlgItemTextW(dialog, ID_FOOTER, state->footer.c_str());
            SendDlgItemMessageW(dialog, ID_HEADER, EM_LIMITTEXT, kHeaderFooterMax, 0);
            SendDlgItemMessageW(dialog, ID_FOOTER, EM_LIMITTEXT, kHeaderFooterMax, 0);
            return TRUE;
        }
        case WM_DESTROY: {
            // Capture the fields as the dialog tears down; the caller keeps them
            // only if PageSetupDlg ultimately returned OK.
            if (PageSetupState* state = StateOf(dialog)) {
                std::array<wchar_t, kBufferChars> buffer{};
                GetDlgItemTextW(dialog, ID_HEADER, buffer.data(), static_cast<int>(buffer.size()));
                state->header = buffer.data();
                GetDlgItemTextW(dialog, ID_FOOTER, buffer.data(), static_cast<int>(buffer.size()));
                state->footer = buffer.data();
            }
            return 0;
        }
        default:
            return 0;
    }
}

} // namespace

void PageSetup::ShowDialog(HWND parent) {
    PageSetupState state{settings_.header, settings_.footer};

    PAGESETUPDLGW psd{};
    psd.lStructSize = sizeof(psd);
    psd.hwndOwner = parent;
    psd.hInstance = GetModuleHandleW(nullptr);
    psd.Flags = PSD_ENABLEPAGESETUPTEMPLATE | PSD_ENABLEPAGESETUPHOOK | PSD_MARGINS |
                (util::UsesUsMeasurement() ? PSD_INTHOUSANDTHSOFINCHES
                                     : PSD_INHUNDREDTHSOFMILLIMETERS);
    psd.rtMargin.left = settings_.marginLeft;
    psd.rtMargin.top = settings_.marginTop;
    psd.rtMargin.right = settings_.marginRight;
    psd.rtMargin.bottom = settings_.marginBottom;
    psd.lpPageSetupTemplateName = MAKEINTRESOURCEW(IDD_PAGESETUP);
    psd.lpfnPageSetupHook = &PageSetupHookProc;
    psd.lCustData = reinterpret_cast<LPARAM>(&state);  // Carried to the hook.

    if (PageSetupDlgW(&psd)) {
        settings_.marginLeft = psd.rtMargin.left;
        settings_.marginTop = psd.rtMargin.top;
        settings_.marginRight = psd.rtMargin.right;
        settings_.marginBottom = psd.rtMargin.bottom;
        settings_.header = state.header;
        settings_.footer = state.footer;
    }
}

} // namespace notepadxp::printing

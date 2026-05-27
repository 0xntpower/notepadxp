#include "printing/PrintService.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include <commdlg.h>

#include "Resource.h"
#include "printing/HeaderFooter.hpp"
#include "util/GdiGuard.hpp"
#include "util/StringTable.hpp"

namespace notepadxp::printing {

namespace {

// Print is synchronous and single-threaded, so a single set of abort statics is
// safe. The abort dialog's Cancel button flips g_abortRequested.
bool g_abortRequested = false;
HWND g_abortDialog = nullptr;

constexpr int kHundredthsMmPerInch = 2540;  // 25.4 mm * 100.
constexpr int kThousandthsPerInch = 1000;
constexpr int kTabSizeInChars = 8;
constexpr int kPointScale = 720;  // 72 pt/inch * 10 (pointSize is in tenths).
constexpr int kFieldBufferChars = 128;

std::wstring FileNameOnly(const std::wstring& path) {
    const size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? path : path.substr(separator + 1);
}

bool UsesUsMeasurement() {
    DWORD measure = 1;
    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IMEASURE | LOCALE_RETURN_NUMBER,
                    reinterpret_cast<LPWSTR>(&measure),
                    static_cast<int>(sizeof(measure) / sizeof(wchar_t)));
    return measure != 0;
}

INT_PTR CALLBACK AbortDialogProc(HWND /*dialog*/, UINT message, WPARAM wParam, LPARAM /*lParam*/) {
    switch (message) {
        case WM_INITDIALOG:
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDCANCEL) {
                g_abortRequested = true;
                return TRUE;
            }
            return FALSE;
        default:
            return FALSE;
    }
}

// The GDI abort procedure: pump messages so the abort dialog stays responsive,
// and return FALSE once the user cancels.
BOOL CALLBACK PrintAbortProc(HDC /*dc*/, int /*error*/) {
    MSG msg;
    while (!g_abortRequested && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != FALSE) {
        if (g_abortDialog == nullptr || IsDialogMessageW(g_abortDialog, &msg) == FALSE) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return g_abortRequested ? FALSE : TRUE;
}

// Draw one header/footer band: left text left-aligned, center centered, right
// text right-aligned, all on baseline `y` within [left, right].
void DrawBand(HDC dc, const std::wstring& templateText, const HeaderFooterContext& context, int y,
              int left, int right) {
    const AlignedText bands = ExpandTemplate(templateText, context);
    SetBkMode(dc, TRANSPARENT);
    if (!bands.left.empty()) {
        TextOutW(dc, left, y, bands.left.c_str(), static_cast<int>(bands.left.size()));
    }
    if (!bands.center.empty()) {
        SIZE size{};
        GetTextExtentPoint32W(dc, bands.center.c_str(), static_cast<int>(bands.center.size()),
                              &size);
        const int x = (left + right) / 2 - size.cx / 2;
        TextOutW(dc, x, y, bands.center.c_str(), static_cast<int>(bands.center.size()));
    }
    if (!bands.right.empty()) {
        SIZE size{};
        GetTextExtentPoint32W(dc, bands.right.c_str(), static_cast<int>(bands.right.size()), &size);
        TextOutW(dc, right - size.cx, y, bands.right.c_str(), static_cast<int>(bands.right.size()));
    }
}

} // namespace

void PrintService::Print(HWND parent) {
    const std::wstring text = editView_.GetText();

    PRINTDLGW pd{};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = parent;
    pd.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
    const BOOL chosen = PrintDlgW(&pd);
    if (pd.hDevMode != nullptr) {
        GlobalFree(pd.hDevMode);
    }
    if (pd.hDevNames != nullptr) {
        GlobalFree(pd.hDevNames);
    }
    if (chosen == FALSE) {
        // A nonzero extended error means the dialog failed to initialize (vs. the
        // user simply cancelling, which is silent) — matches classic Notepad.
        if (CommDlgExtendedError() != 0) {
            util::AlertBox(parent, util::LoadStr(IDS_NN), util::LoadStr(IDS_PRINTDLGINIT),
                           MB_OK | MB_ICONEXCLAMATION);
        }
        return;
    }
    if (pd.hDC == nullptr) {
        return;
    }
    HDC dc = pd.hDC;

    const std::wstring fileName =
        document_.untitled ? util::LoadStr(IDS_UNTITLED) : FileNameOnly(document_.filePath);

    // Device metrics.
    const int dpiX = GetDeviceCaps(dc, LOGPIXELSX);
    const int dpiY = GetDeviceCaps(dc, LOGPIXELSY);
    const int printW = GetDeviceCaps(dc, HORZRES);
    const int printH = GetDeviceCaps(dc, VERTRES);
    const int physW = GetDeviceCaps(dc, PHYSICALWIDTH);
    const int physH = GetDeviceCaps(dc, PHYSICALHEIGHT);
    const int physOffX = GetDeviceCaps(dc, PHYSICALOFFSETX);
    const int physOffY = GetDeviceCaps(dc, PHYSICALOFFSETY);

    // Font scaled to the printer's DPI.
    LOGFONTW logFont = settings_.font;
    logFont.lfHeight = -MulDiv(settings_.pointSize, dpiY, kPointScale);
    logFont.lfWidth = 0;
    util::GdiGuard fontGuard(CreateFontIndirectW(&logFont));
    const HGDIOBJ oldFont = SelectObject(dc, fontGuard.AsFont());

    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    const int lineHeight = tm.tmHeight + tm.tmExternalLeading;
    const int tabPixels = tm.tmAveCharWidth * kTabSizeInChars;

    // Margins (in locale units) converted to device pixels, then to borders
    // relative to the printable area (which excludes the physical offsets).
    const int unitDiv = UsesUsMeasurement() ? kThousandthsPerInch : kHundredthsMmPerInch;
    const int leftBorder = std::max(MulDiv(settings_.marginLeft, dpiX, unitDiv) - physOffX, 0);
    const int rightBorder =
        std::max(MulDiv(settings_.marginRight, dpiX, unitDiv) - (physW - printW - physOffX), 0);
    const int topBorder = std::max(MulDiv(settings_.marginTop, dpiY, unitDiv) - physOffY, 0);
    const int bottomBorder =
        std::max(MulDiv(settings_.marginBottom, dpiY, unitDiv) - (physH - printH - physOffY), 0);

    const bool hasHeader = !settings_.header.empty();
    const bool hasFooter = !settings_.footer.empty();
    const int bodyLeft = leftBorder;
    const int bodyRight = printW - rightBorder;
    const int bodyTop = topBorder + (hasHeader ? lineHeight : 0);
    const int bodyBottom = printH - bottomBorder - (hasFooter ? lineHeight : 0);

    if (bodyBottom - bodyTop < lineHeight || bodyRight - bodyLeft <= 0) {
        SelectObject(dc, oldFont);
        DeleteDC(dc);
        util::AlertBox(parent, util::LoadStr(IDS_NN), util::LoadStr(IDS_FONTTOOBIG),
                       MB_OK | MB_ICONEXCLAMATION);
        return;
    }

    // Long-form date/time for the header/footer codes.
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const auto formatField = [](auto formatter) -> std::wstring {
        std::array<wchar_t, kFieldBufferChars> buffer{};
        const int written = formatter(buffer.data(), static_cast<int>(buffer.size()));
        return written > 0 ? std::wstring(buffer.data()) : std::wstring{};
    };
    const std::wstring date = formatField([&now](LPWSTR buffer, int cch) {
        return GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &now, nullptr, buffer, cch,
                               nullptr);
    });
    const std::wstring time = formatField([&now](LPWSTR buffer, int cch) {
        return GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &now, nullptr, buffer, cch);
    });

    // Modeless abort dialog while the job runs.
    g_abortRequested = false;
    EnableWindow(parent, FALSE);
    g_abortDialog = CreateDialogW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ABORTPRINT),
                                  parent, &AbortDialogProc);
    if (g_abortDialog != nullptr) {
        SetDlgItemTextW(g_abortDialog, ID_FILENAME, fileName.c_str());
        ShowWindow(g_abortDialog, SW_SHOW);
    }
    SetAbortProc(dc, &PrintAbortProc);

    const std::wstring docName = fileName + util::LoadStr(IDS_NOTEPAD);
    DOCINFOW docInfo{};
    docInfo.cbSize = sizeof(docInfo);
    docInfo.lpszDocName = docName.c_str();

    const std::wstring pageFormat = util::LoadStr(IDS_CURRENT_PAGE);  // "Page %d"
    bool ok = StartDocW(dc, &docInfo) > 0;
    size_t index = 0;
    int pageNumber = 1;

    while (ok && index < text.size() && !g_abortRequested) {
        if (StartPage(dc) <= 0) {
            ok = false;
            break;
        }
        if (g_abortDialog != nullptr) {
            std::array<wchar_t, 64> pageText{};
            _snwprintf_s(pageText.data(), pageText.size(), _TRUNCATE, pageFormat.c_str(),
                         pageNumber);
            SetDlgItemTextW(g_abortDialog, ID_PAGENUMBER, pageText.data());
        }

        const HeaderFooterContext context{fileName, date, time, pageNumber};
        if (hasHeader) {
            DrawBand(dc, settings_.header, context, topBorder, bodyLeft, bodyRight);
        }

        RECT body{bodyLeft, bodyTop, bodyRight, bodyBottom};
        DRAWTEXTPARAMS params{};
        params.cbSize = sizeof(params);
        params.iTabLength = tabPixels;
        const int remaining = static_cast<int>(text.size() - index);
        // const_cast: DrawTextEx does not modify the buffer without DT_MODIFYSTRING.
        DrawTextExW(dc, const_cast<LPWSTR>(text.c_str() + index), remaining, &body,
                    DT_EDITCONTROL | DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS, &params);
        const UINT drawn = params.uiLengthDrawn;

        if (hasFooter) {
            DrawBand(dc, settings_.footer, context, printH - bottomBorder - lineHeight, bodyLeft,
                     bodyRight);
        }
        EndPage(dc);

        if (drawn == 0) {
            ok = false;  // No progress (font too large for the page).
            break;
        }
        index += drawn;
        ++pageNumber;
    }

    if (ok && !g_abortRequested) {
        EndDoc(dc);
    } else {
        AbortDoc(dc);
    }

    if (g_abortDialog != nullptr) {
        DestroyWindow(g_abortDialog);
        g_abortDialog = nullptr;
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    SelectObject(dc, oldFont);
    DeleteDC(dc);
}

} // namespace notepadxp::printing

#include "settings/Settings.hpp"

#include <cwchar>
#include <string>

#include "settings/RegistryKey.hpp"

namespace notepadxp::settings {

namespace {

constexpr std::wstring_view kRegistryPath = L"Software\\NotepadXP";

// Font defaults: Lucida Console 10pt, matching classic Notepad.
constexpr wchar_t kDefaultFaceName[] = L"Lucida Console";
constexpr int kDefaultPointSize = 100;  // Tenths of a point.

// Point-size-to-pixels divisor: 72 points/inch * 10 (pointSize is in tenths).
constexpr int kPointSizeScale = 720;

constexpr std::wstring_view kDefaultHeader = L"&f";
constexpr std::wstring_view kDefaultFooter = L"Page &p";

// Default margins in locale-native units.
constexpr int kUsMarginTopBottom = 1000;   // 1.00 in (thousandths of an inch).
constexpr int kUsMarginLeftRight = 750;    // 0.75 in.
constexpr int kMetricMarginTopBottom = 2500;  // 25.0 mm (hundredths of a mm).
constexpr int kMetricMarginLeftRight = 2000;  // 20.0 mm.

LOGFONTW MakeDefaultFont() {
    LOGFONTW lf{};
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = DEFAULT_QUALITY;
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    wcsncpy_s(lf.lfFaceName, kDefaultFaceName, _TRUNCATE);
    return lf;
}

// True when the user's locale uses the US measurement system (inches).
bool UsesUsMeasurement() {
    DWORD measure = 1;  // Default to US if the query fails.
    // LOCALE_RETURN_NUMBER makes GetLocaleInfoEx write a DWORD through lpLCData;
    // the reinterpret_cast is the documented way to pass the DWORD buffer.
    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IMEASURE | LOCALE_RETURN_NUMBER,
                    reinterpret_cast<LPWSTR>(&measure),
                    static_cast<int>(sizeof(measure) / sizeof(wchar_t)));
    return measure != 0;
}

} // namespace

Settings Settings::Load() {
    Settings s;
    s.font = MakeDefaultFont();
    s.pointSize = kDefaultPointSize;
    s.header = kDefaultHeader;
    s.footer = kDefaultFooter;

    const bool usMeasurement = UsesUsMeasurement();
    s.marginTop = s.marginBottom = usMeasurement ? kUsMarginTopBottom : kMetricMarginTopBottom;
    s.marginLeft = s.marginRight = usMeasurement ? kUsMarginLeftRight : kMetricMarginLeftRight;

    const RegistryKey key = RegistryKey::CreateOrOpen(HKEY_CURRENT_USER, kRegistryPath);
    if (!key.IsValid()) {
        return s;
    }

    s.font.lfEscapement = static_cast<LONG>(key.GetDword(L"lfEscapement",
                                                         static_cast<DWORD>(s.font.lfEscapement)));
    s.font.lfOrientation = static_cast<LONG>(key.GetDword(L"lfOrientation",
                                                          static_cast<DWORD>(s.font.lfOrientation)));
    s.font.lfWeight =
        static_cast<LONG>(key.GetDword(L"lfWeight", static_cast<DWORD>(s.font.lfWeight)));
    s.font.lfItalic = static_cast<BYTE>(key.GetDword(L"lfItalic", s.font.lfItalic));
    s.font.lfUnderline = static_cast<BYTE>(key.GetDword(L"lfUnderline", s.font.lfUnderline));
    s.font.lfStrikeOut = static_cast<BYTE>(key.GetDword(L"lfStrikeOut", s.font.lfStrikeOut));
    s.font.lfCharSet = static_cast<BYTE>(key.GetDword(L"lfCharSet", s.font.lfCharSet));
    s.font.lfOutPrecision = static_cast<BYTE>(key.GetDword(L"lfOutPrecision", s.font.lfOutPrecision));
    s.font.lfClipPrecision =
        static_cast<BYTE>(key.GetDword(L"lfClipPrecision", s.font.lfClipPrecision));
    s.font.lfQuality = static_cast<BYTE>(key.GetDword(L"lfQuality", s.font.lfQuality));
    s.font.lfPitchAndFamily =
        static_cast<BYTE>(key.GetDword(L"lfPitchAndFamily", s.font.lfPitchAndFamily));

    const std::wstring face = key.GetString(L"lfFaceName", s.font.lfFaceName);
    wcsncpy_s(s.font.lfFaceName, face.c_str(), _TRUNCATE);

    s.pointSize = static_cast<int>(key.GetDword(L"iPointSize", static_cast<DWORD>(s.pointSize)));
    s.wordWrap = key.GetDword(L"fWrap", 0) != 0;
    s.statusBar = key.GetDword(L"StatusBar", 0) != 0;

    s.windowX = static_cast<int>(key.GetDword(L"iWindowPosX", static_cast<DWORD>(s.windowX)));
    s.windowY = static_cast<int>(key.GetDword(L"iWindowPosY", static_cast<DWORD>(s.windowY)));
    s.windowWidth =
        static_cast<int>(key.GetDword(L"iWindowPosDX", static_cast<DWORD>(s.windowWidth)));
    s.windowHeight =
        static_cast<int>(key.GetDword(L"iWindowPosDY", static_cast<DWORD>(s.windowHeight)));

    s.marginTop = static_cast<int>(key.GetDword(L"iMarginTop", static_cast<DWORD>(s.marginTop)));
    s.marginBottom =
        static_cast<int>(key.GetDword(L"iMarginBottom", static_cast<DWORD>(s.marginBottom)));
    s.marginLeft = static_cast<int>(key.GetDword(L"iMarginLeft", static_cast<DWORD>(s.marginLeft)));
    s.marginRight =
        static_cast<int>(key.GetDword(L"iMarginRight", static_cast<DWORD>(s.marginRight)));

    s.header = key.GetString(L"szHeader", s.header);
    s.footer = key.GetString(L"szTrailer", s.footer);
    return s;
}

void Settings::Save() const {
    const RegistryKey key = RegistryKey::CreateOrOpen(HKEY_CURRENT_USER, kRegistryPath);
    if (!key.IsValid()) {
        return;
    }

    key.SetDword(L"lfEscapement", static_cast<DWORD>(font.lfEscapement));
    key.SetDword(L"lfOrientation", static_cast<DWORD>(font.lfOrientation));
    key.SetDword(L"lfWeight", static_cast<DWORD>(font.lfWeight));
    key.SetDword(L"lfItalic", font.lfItalic);
    key.SetDword(L"lfUnderline", font.lfUnderline);
    key.SetDword(L"lfStrikeOut", font.lfStrikeOut);
    key.SetDword(L"lfCharSet", font.lfCharSet);
    key.SetDword(L"lfOutPrecision", font.lfOutPrecision);
    key.SetDword(L"lfClipPrecision", font.lfClipPrecision);
    key.SetDword(L"lfQuality", font.lfQuality);
    key.SetDword(L"lfPitchAndFamily", font.lfPitchAndFamily);
    key.SetString(L"lfFaceName", font.lfFaceName);

    key.SetDword(L"iPointSize", static_cast<DWORD>(pointSize));
    key.SetDword(L"fWrap", wordWrap ? 1 : 0);
    key.SetDword(L"StatusBar", statusBar ? 1 : 0);

    key.SetDword(L"iWindowPosX", static_cast<DWORD>(windowX));
    key.SetDword(L"iWindowPosY", static_cast<DWORD>(windowY));
    key.SetDword(L"iWindowPosDX", static_cast<DWORD>(windowWidth));
    key.SetDword(L"iWindowPosDY", static_cast<DWORD>(windowHeight));

    key.SetDword(L"iMarginTop", static_cast<DWORD>(marginTop));
    key.SetDword(L"iMarginBottom", static_cast<DWORD>(marginBottom));
    key.SetDword(L"iMarginLeft", static_cast<DWORD>(marginLeft));
    key.SetDword(L"iMarginRight", static_cast<DWORD>(marginRight));

    key.SetString(L"szHeader", header);
    key.SetString(L"szTrailer", footer);
}

LOGFONTW Settings::ResolvedFont(HDC dc) const {
    LOGFONTW lf = font;
    const int dpiY = GetDeviceCaps(dc, LOGPIXELSY);
    lf.lfHeight = -MulDiv(pointSize, dpiY, kPointSizeScale);
    lf.lfWidth = 0;  // Let the mapper choose the matching width.
    return lf;
}

} // namespace notepadxp::settings

#include "util/Measurement.hpp"

#include "util/WinLean.hpp"

namespace notepadxp::util {

bool UsesUsMeasurement() {
    DWORD measure = 1;  // Default to US if the query fails.
    // LOCALE_RETURN_NUMBER makes GetLocaleInfoEx write a DWORD through lpLCData;
    // the reinterpret_cast is the documented way to pass the DWORD buffer.
    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IMEASURE | LOCALE_RETURN_NUMBER,
                    reinterpret_cast<LPWSTR>(&measure),
                    static_cast<int>(sizeof(measure) / sizeof(wchar_t)));
    return measure != 0;
}

} // namespace notepadxp::util

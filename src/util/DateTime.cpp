#include "util/DateTime.hpp"

#include <array>
#include <string>

#include "util/WinLean.hpp"

namespace notepadxp::util {

namespace {

constexpr int kFormatBufferChars = 128;
constexpr std::wstring_view kCrlf = L"\r\n";

// Format the given local time with one of the Get{Date,Time}FormatEx APIs.
// `formatter` is invoked as int(LPWSTR buffer, int cch).
template <typename Formatter>
std::wstring FormatField(Formatter formatter) {
    std::array<wchar_t, kFormatBufferChars> buffer{};
    const int written = formatter(buffer.data(), static_cast<int>(buffer.size()));
    if (written <= 0) {
        return std::wstring{};
    }
    // `written` includes the terminating null for these APIs.
    return std::wstring(buffer.data());
}

} // namespace

std::wstring FormatTimestamp(bool withLeadingTrailingCrlf) {
    SYSTEMTIME now{};
    GetLocalTime(&now);

    const std::wstring time = FormatField([&now](LPWSTR buffer, int cch) {
        return GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &now, nullptr, buffer, cch);
    });
    const std::wstring date = FormatField([&now](LPWSTR buffer, int cch) {
        return GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &now, nullptr, buffer, cch,
                               nullptr);
    });

    std::wstring result;
    if (withLeadingTrailingCrlf) {
        result.append(kCrlf);
    }
    result.append(time);
    result.push_back(L' ');
    result.append(date);
    if (withLeadingTrailingCrlf) {
        result.append(kCrlf);
    }
    return result;
}

} // namespace notepadxp::util

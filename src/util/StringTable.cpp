#include "util/StringTable.hpp"

#include <string>
#include <string_view>

namespace notepadxp::util {

namespace {

// The two-character merge token substituted in message templates.
constexpr std::wstring_view kMergeToken = L"%%";

} // namespace

std::wstring LoadStr(UINT id) {
    // LoadStringW with cchBufferMax == 0 treats lpBuffer as a (LPWSTR*) out-param
    // and writes a pointer to the read-only resource string into it, returning the
    // length. The reinterpret_cast is the documented way to pass &resourcePtr as
    // the LPWSTR the API expects; we only read through the result.
    LPWSTR resourcePtr = nullptr;
    const int length = LoadStringW(GetModuleHandleW(nullptr), id,
                                   reinterpret_cast<LPWSTR>(&resourcePtr), 0);
    if (length <= 0 || resourcePtr == nullptr) {
        return std::wstring{};
    }
    return std::wstring(resourcePtr, static_cast<size_t>(length));
}

std::wstring MergeString(std::wstring_view templateText, std::wstring_view insertion) {
    const size_t tokenPos = templateText.find(kMergeToken);
    if (tokenPos == std::wstring_view::npos) {
        return std::wstring(templateText);
    }
    std::wstring result;
    result.reserve(templateText.size() + insertion.size());
    result.append(templateText.substr(0, tokenPos));
    result.append(insertion);
    result.append(templateText.substr(tokenPos + kMergeToken.size()));
    return result;
}

std::wstring LoadAndMerge(UINT id, std::wstring_view insertion) {
    return MergeString(LoadStr(id), insertion);
}

int AlertBox(HWND parent, std::wstring_view caption, std::wstring_view text, UINT type) {
    // MessageBoxW requires null-terminated strings; views may not be terminated.
    const std::wstring captionStr(caption);
    const std::wstring textStr(text);
    return MessageBoxW(parent, textStr.c_str(), captionStr.c_str(), type);
}

} // namespace notepadxp::util

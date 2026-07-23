#include "editor/SearchEngine.hpp"

#include <cwchar>

#include "util/WinLean.hpp"

namespace notepadxp::editor {

bool MatchAt(const std::wstring& text, size_t pos, std::wstring_view key, bool matchCase) {
    if (pos + key.size() > text.size()) {
        return false;
    }
    if (matchCase) {
        return wcsncmp(text.c_str() + pos, key.data(), key.size()) == 0;
    }
    // Locale-aware, case-insensitive comparison.
    return CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, text.c_str() + pos,
                          static_cast<int>(key.size()), key.data(),
                          static_cast<int>(key.size())) == CSTR_EQUAL;
}

size_t FindForward(const std::wstring& text, std::wstring_view key, size_t from, bool matchCase) {
    if (key.empty() || key.size() > text.size()) {
        return std::wstring::npos;
    }
    for (size_t i = from; i + key.size() <= text.size(); ++i) {
        if (MatchAt(text, i, key, matchCase)) {
            return i;
        }
    }
    return std::wstring::npos;
}

size_t FindBackward(const std::wstring& text, std::wstring_view key, size_t before,
                    bool matchCase) {
    if (key.empty() || key.size() > text.size() || before < key.size()) {
        return std::wstring::npos;
    }
    for (size_t i = before - key.size() + 1; i-- > 0;) {
        if (MatchAt(text, i, key, matchCase)) {
            return i;
        }
    }
    return std::wstring::npos;
}

ReplaceAllResult ReplaceAllInText(const std::wstring& text, std::wstring_view key,
                                  std::wstring_view replacement, bool matchCase) {
    ReplaceAllResult result;
    result.text.reserve(text.size());
    size_t pos = 0;
    while (true) {
        const size_t found = FindForward(text, key, pos, matchCase);
        if (found == std::wstring::npos) {
            break;
        }
        result.text.append(text, pos, found - pos);
        result.text.append(replacement);
        pos = found + key.size();
        ++result.count;
    }
    result.text.append(text, pos, std::wstring::npos);
    return result;
}

} // namespace notepadxp::editor

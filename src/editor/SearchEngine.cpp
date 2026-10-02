#include "editor/SearchEngine.hpp"

#include "util/WinLean.hpp"

namespace notepadxp::editor {

namespace {

// One NLS call over the whole window instead of a locale compare at every
// position. NORM_IGNORECASE matches the comparison IsMatch uses.
Match FindIgnoreCase(std::wstring_view window, std::wstring_view key, DWORD direction,
                     size_t offset) {
    if (window.empty()) {
        return {};  // FindNLSStringEx rejects a zero-length source.
    }
    int foundLength = 0;
    const int pos = FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, direction | NORM_IGNORECASE,
                                    window.data(), static_cast<int>(window.size()), key.data(),
                                    static_cast<int>(key.size()), &foundLength, nullptr, nullptr,
                                    0);
    if (pos < 0 || foundLength <= 0) {
        return {};  // A zero-length (all-ignorable) match is no match.
    }
    return {offset + static_cast<size_t>(pos), static_cast<size_t>(foundLength)};
}

} // namespace

bool IsMatch(std::wstring_view candidate, std::wstring_view key, bool matchCase) {
    if (matchCase) {
        return candidate == key;
    }
    return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE, candidate.data(),
                           static_cast<int>(candidate.size()), key.data(),
                           static_cast<int>(key.size()), nullptr, nullptr, 0) == CSTR_EQUAL;
}

Match FindForward(std::wstring_view text, std::wstring_view key, size_t from, bool matchCase) {
    if (key.empty() || from > text.size()) {
        return {};
    }
    if (!matchCase) {
        return FindIgnoreCase(text.substr(from), key, FIND_FROMSTART, from);
    }
    const size_t pos = text.find(key, from);
    return pos == std::wstring_view::npos ? Match{} : Match{pos, key.size()};
}

Match FindBackward(std::wstring_view text, std::wstring_view key, size_t before, bool matchCase) {
    if (key.empty()) {
        return {};
    }
    const std::wstring_view window = text.substr(0, before);
    if (!matchCase) {
        return FindIgnoreCase(window, key, FIND_FROMEND, 0);
    }
    const size_t pos = window.rfind(key);
    return pos == std::wstring_view::npos ? Match{} : Match{pos, key.size()};
}

ReplaceAllResult ReplaceAllInText(std::wstring_view text, std::wstring_view key,
                                  std::wstring_view replacement, bool matchCase) {
    ReplaceAllResult result;
    result.text.reserve(text.size());
    size_t pos = 0;
    for (Match m = FindForward(text, key, 0, matchCase); m.pos != std::wstring_view::npos;
         m = FindForward(text, key, pos, matchCase)) {
        result.text.append(text.substr(pos, m.pos - pos));
        result.text.append(replacement);
        pos = m.pos + m.length;
        ++result.count;
    }
    result.text.append(text.substr(pos));
    return result;
}

} // namespace notepadxp::editor

#include "file/LineEndings.hpp"

namespace notepadxp::file {

LineEnding DetectLineEnding(std::wstring_view text) {
    size_t crlf = 0;
    size_t lf = 0;
    size_t cr = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r') {
            if (i + 1 < text.size() && text[i + 1] == L'\n') {
                ++crlf;
                ++i;  // Consume the paired LF.
            } else {
                ++cr;
            }
        } else if (text[i] == L'\n') {
            ++lf;
        }
    }
    if (crlf >= lf && crlf >= cr) {
        return LineEnding::Crlf;  // Also the tie / no-newline default.
    }
    return lf >= cr ? LineEnding::Lf : LineEnding::Cr;
}

std::wstring NormalizeToCrlf(std::wstring_view text) {
    std::wstring out;
    out.reserve(text.size() + text.size() / 16);
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        if (ch == L'\r') {
            out += L"\r\n";
            if (i + 1 < text.size() && text[i + 1] == L'\n') {
                ++i;  // Collapse an existing CRLF into one.
            }
        } else if (ch == L'\n') {
            out += L"\r\n";
        } else {
            out += ch;  // A backslash-n escape inside a string is left untouched.
        }
    }
    return out;
}

std::wstring ConvertFromCrlf(std::wstring_view text, LineEnding target) {
    if (target == LineEnding::Crlf) {
        return std::wstring(text);
    }
    const wchar_t replacement = target == LineEnding::Lf ? L'\n' : L'\r';
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') {
            out += replacement;
            ++i;
        } else {
            out += text[i];
        }
    }
    return out;
}

} // namespace notepadxp::file

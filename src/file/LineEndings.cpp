#include "file/LineEndings.hpp"

namespace notepadxp::file {

namespace {

constexpr wchar_t kBreakChars[] = L"\r\n";

bool IsCrlfAt(std::wstring_view text, size_t i) {
    return text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n';
}

} // namespace

LineEnding DetectLineEnding(std::wstring_view text) {
    size_t crlf = 0;
    size_t lf = 0;
    size_t cr = 0;
    // Jump between breaks with the (vectorized) search instead of testing
    // every character.
    for (size_t i = text.find_first_of(kBreakChars); i != std::wstring_view::npos;
         i = text.find_first_of(kBreakChars, i + 1)) {
        if (text[i] == L'\n') {
            ++lf;
        } else if (IsCrlfAt(text, i)) {
            ++crlf;
            ++i;  // Consume the paired LF.
        } else {
            ++cr;
        }
    }
    if (crlf >= lf && crlf >= cr) {
        return LineEnding::Crlf;  // Also the tie / no-newline default.
    }
    return lf >= cr ? LineEnding::Lf : LineEnding::Cr;
}

std::wstring NormalizeToCrlf(std::wstring text) {
    // Find the first break that is not already CRLF. None: nothing to rewrite.
    size_t i = text.find_first_of(kBreakChars);
    while (i != std::wstring::npos && IsCrlfAt(text, i)) {
        i = text.find_first_of(kBreakChars, i + 2);
    }
    if (i == std::wstring::npos) {
        return text;
    }

    std::wstring out;
    out.reserve(text.size() + text.size() / 16);
    size_t runStart = 0;
    for (; i != std::wstring::npos; i = text.find_first_of(kBreakChars, i)) {
        out.append(text, runStart, i - runStart);
        out += L"\r\n";  // A backslash-n escape inside a string is never a break.
        i += IsCrlfAt(text, i) ? 2 : 1;
        runStart = i;
    }
    out.append(text, runStart, std::wstring::npos);
    return out;
}

std::wstring ConvertFromCrlf(std::wstring text, LineEnding target) {
    if (target == LineEnding::Crlf) {
        return text;
    }
    // Compact in place: the output is never longer than the input.
    const wchar_t replacement = target == LineEnding::Lf ? L'\n' : L'\r';
    size_t write = 0;
    for (size_t read = 0; read < text.size(); ++read) {
        if (IsCrlfAt(text, read)) {
            text[write++] = replacement;
            ++read;
        } else {
            text[write++] = text[read];
        }
    }
    text.resize(write);
    return text;
}

} // namespace notepadxp::file

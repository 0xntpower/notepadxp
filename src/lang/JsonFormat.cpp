#include "lang/JsonFormat.hpp"

#include <vector>

namespace notepadxp::lang {

namespace {

bool IsJsonWs(wchar_t ch) {
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
}

bool IsNumberChar(wchar_t ch) {
    return (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'+' || ch == L'.' || ch == L'e' ||
           ch == L'E';
}

JsonResult Fail(std::wstring_view input, size_t offset) {
    JsonResult result;
    result.ok = false;
    result.errorOffset = offset;
    result.errorLine = 1;
    size_t lineStart = 0;
    const size_t end = offset < input.size() ? offset : input.size();
    for (size_t i = 0; i < end; ++i) {
        if (input[i] == L'\n') {
            ++result.errorLine;
            lineStart = i + 1;
        }
    }
    result.errorCol = static_cast<int>(end - lineStart) + 1;
    return result;
}

JsonResult FormatJson(std::wstring_view input, bool pretty, const std::wstring& indentUnit) {
    // What the grammar allows next.
    enum class Expect { Value, ValueOrClose, KeyOrClose, Key, Colon, CommaOrClose, End };

    std::wstring out;
    out.reserve(input.size() + (pretty ? input.size() / 4 : 0));
    std::vector<wchar_t> stack;  // Open containers: L'{' or L'['.
    Expect expect = Expect::Value;
    bool lastWasOpen = false;  // Tracks "{}"/"[]" so empty containers stay inline.
    size_t i = 0;

    const auto newlineIndent = [&](size_t depth) {
        out += L"\r\n";
        for (size_t d = 0; d < depth; ++d) {
            out += indentUnit;
        }
    };

    // Copy one complete scalar or key token verbatim; false on lex error.
    const auto copyString = [&]() -> bool {
        const size_t start = i;
        ++i;  // Opening quote.
        while (i < input.size()) {
            const wchar_t ch = input[i];
            if (ch == L'\r' || ch == L'\n') {
                return false;  // Raw line break inside a string literal.
            }
            if (ch == L'\\') {
                i += 2;
                continue;
            }
            ++i;
            if (ch == L'"') {
                out.append(input.substr(start, i - start));
                return true;
            }
        }
        return false;  // Unterminated.
    };

    while (true) {
        while (i < input.size() && IsJsonWs(input[i])) {
            ++i;
        }
        if (i >= input.size()) {
            break;
        }
        const wchar_t ch = input[i];
        const size_t tokenStart = i;

        if (ch == L'{' || ch == L'[') {
            if (expect != Expect::Value && expect != Expect::ValueOrClose) {
                return Fail(input, tokenStart);
            }
            if (pretty && lastWasOpen) {
                newlineIndent(stack.size());  // Directly nested opens each get a line.
            }
            out += ch;
            stack.push_back(ch);
            expect = ch == L'{' ? Expect::KeyOrClose : Expect::ValueOrClose;
            lastWasOpen = true;
            ++i;
            continue;
        }
        if (ch == L'}' || ch == L']') {
            const wchar_t opener = ch == L'}' ? L'{' : L'[';
            const bool closable = expect == Expect::CommaOrClose ||
                                  expect == Expect::KeyOrClose ||
                                  expect == Expect::ValueOrClose;
            if (!closable || stack.empty() || stack.back() != opener) {
                return Fail(input, tokenStart);
            }
            stack.pop_back();
            if (pretty && !lastWasOpen) {
                newlineIndent(stack.size());
            }
            out += ch;
            lastWasOpen = false;
            expect = stack.empty() ? Expect::End : Expect::CommaOrClose;
            ++i;
            continue;
        }
        if (ch == L',') {
            if (expect != Expect::CommaOrClose) {
                return Fail(input, tokenStart);
            }
            out += L',';
            if (pretty) {
                newlineIndent(stack.size());
            }
            expect = stack.back() == L'{' ? Expect::Key : Expect::Value;
            ++i;
            continue;
        }
        if (ch == L':') {
            if (expect != Expect::Colon) {
                return Fail(input, tokenStart);
            }
            out += pretty ? L": " : L":";
            expect = Expect::Value;
            ++i;
            continue;
        }

        // A key or a scalar value.
        const bool keyPosition = expect == Expect::Key || expect == Expect::KeyOrClose;
        const bool valuePosition = expect == Expect::Value || expect == Expect::ValueOrClose;
        if (!keyPosition && !valuePosition) {
            return Fail(input, tokenStart);
        }
        if (pretty && lastWasOpen) {
            newlineIndent(stack.size());
        }
        lastWasOpen = false;

        if (ch == L'"') {
            if (!copyString()) {
                return Fail(input, tokenStart);
            }
        } else if (keyPosition) {
            return Fail(input, tokenStart);  // Object keys must be strings.
        } else if (IsNumberChar(ch) && ch != L'+' && ch != L'e' && ch != L'E' && ch != L'.') {
            while (i < input.size() && IsNumberChar(input[i])) {
                ++i;
            }
            out.append(input.substr(tokenStart, i - tokenStart));
        } else if (input.compare(tokenStart, 4, L"true") == 0) {
            out += L"true";
            i += 4;
        } else if (input.compare(tokenStart, 5, L"false") == 0) {
            out += L"false";
            i += 5;
        } else if (input.compare(tokenStart, 4, L"null") == 0) {
            out += L"null";
            i += 4;
        } else {
            return Fail(input, tokenStart);
        }
        expect = keyPosition ? Expect::Colon
                 : stack.empty() ? Expect::End
                                 : Expect::CommaOrClose;
    }

    if (expect != Expect::End || !stack.empty()) {
        return Fail(input, input.size());  // Truncated (or empty) document.
    }
    JsonResult result;
    result.ok = true;
    result.text = std::move(out);
    return result;
}

} // namespace

JsonResult PrettyPrintJson(std::wstring_view input, IndentStyle style) {
    return FormatJson(input, true, style.Unit());
}

JsonResult MinifyJson(std::wstring_view input) {
    return FormatJson(input, false, std::wstring());
}

} // namespace notepadxp::lang

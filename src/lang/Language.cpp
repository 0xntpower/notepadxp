#include "lang/Language.hpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <numeric>

#include "util/PathName.hpp"

namespace notepadxp::lang {

namespace {

constexpr size_t kSniffLines = 50;    // Lines examined by the log heuristic.
constexpr size_t kIndentLines = 400;  // Lines examined by the indent sniff.

const LanguageTraits kTraits[] = {
    // displayName    line     alt    blockO  blockC  strings   esc     triggers  tab
    {L"",             L"",     L"",   L"",    L"",    L"",      0,      L"",      8},  // PlainText
    {L"Log",          L"",     L"",   L"",    L"",    L"",      0,      L"",      8},  // Log
    {L"JSON",         L"",     L"",   L"",    L"",    L"\"",    L'\\',  L"{[",    4},  // Json
    {L"Assembly",     L";",    L"",   L"",    L"",    L"\"'",   0,      L"",      8},  // Asm
    {L"C",            L"//",   L"",   L"/*",  L"*/",  L"\"'",   L'\\',  L"{",     4},  // C
    {L"C++",          L"//",   L"",   L"/*",  L"*/",  L"\"'",   L'\\',  L"{",     4},  // Cpp
    {L"Java",         L"//",   L"",   L"/*",  L"*/",  L"\"'",   L'\\',  L"{",     4},  // Java
    {L"Go",           L"//",   L"",   L"/*",  L"*/",  L"\"'`",  L'\\',  L"{",     4},  // Go
    {L"Python",       L"#",    L"",   L"",    L"",    L"\"'",   L'\\',  L":",     4},  // Python
    {L"WinDbg",       L"$$",   L"",   L"",    L"",    L"\"",    0,      L"",      4},  // WinDbg
    {L"Batch",        L"REM",  L"::", L"",    L"",    L"",      0,      L"",      4},  // Batch
    {L"Bash",         L"#",    L"",   L"",    L"",    L"\"'",   L'\\',  L"{",     4},  // Bash
    {L"PowerShell",   L"#",    L"",   L"<#",  L"#>",  L"\"'",   L'`',   L"{",     4},  // PowerShell
};

std::wstring LowerAscii(std::wstring_view text) {
    std::wstring lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t ch) {
        return ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch - L'A' + L'a') : ch;
    });
    return lower;
}

// Visit up to `maxLines` lines of `text` (line content without breaks).
template <typename Visitor>
void ForEachLine(std::wstring_view text, size_t maxLines, Visitor visit) {
    size_t start = 0;
    size_t seen = 0;
    while (start <= text.size() && seen < maxLines) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring_view::npos) {
            end = text.size();
        }
        std::wstring_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') {
            line.remove_suffix(1);
        }
        visit(line);
        ++seen;
        if (end == text.size()) {
            break;
        }
        start = end + 1;
    }
}

bool StartsWith(std::wstring_view text, std::wstring_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

Language FromShebang(std::wstring_view firstLine) {
    const std::wstring lower = LowerAscii(firstLine);
    if (lower.find(L"python") != std::wstring::npos) {
        return Language::Python;
    }
    if (lower.find(L"pwsh") != std::wstring::npos ||
        lower.find(L"powershell") != std::wstring::npos) {
        return Language::PowerShell;
    }
    if (lower.find(L"sh") != std::wstring::npos) {  // sh, bash, zsh, dash, ...
        return Language::Bash;
    }
    return Language::PlainText;
}

// True when the prefix reads as (a prefix of) a JSON document. Tolerates the
// text being cut off mid-structure — only a definite violation rejects.
bool LooksLikeJson(std::wstring_view text) {
    size_t i = 0;
    const auto skipWs = [&] {
        while (i < text.size() && (text[i] == L' ' || text[i] == L'\t' || text[i] == L'\r' ||
                                   text[i] == L'\n')) {
            ++i;
        }
    };
    skipWs();
    if (i >= text.size() || (text[i] != L'{' && text[i] != L'[')) {
        return false;
    }
    // Walk tokens; punctuation and containers are structural, scalars are
    // validated loosely. Any character that cannot start a JSON token rejects.
    int depth = 0;
    while (i < text.size()) {
        skipWs();
        if (i >= text.size()) {
            break;
        }
        const wchar_t ch = text[i];
        if (ch == L'{' || ch == L'[') {
            ++depth;
            ++i;
        } else if (ch == L'}' || ch == L']') {
            if (--depth < 0) {
                return false;
            }
            ++i;
        } else if (ch == L',' || ch == L':') {
            ++i;
        } else if (ch == L'"') {
            ++i;
            while (i < text.size() && text[i] != L'"') {
                i += text[i] == L'\\' ? 2 : 1;
            }
            ++i;  // Closing quote (or end of prefix).
        } else if (ch == L'-' || (ch >= L'0' && ch <= L'9')) {
            while (i < text.size() && (std::iswalnum(static_cast<wint_t>(text[i])) != 0 ||
                                       text[i] == L'.' || text[i] == L'-' || text[i] == L'+')) {
                ++i;
            }
        } else if (StartsWith(text.substr(i), L"true") || StartsWith(text.substr(i), L"null")) {
            i += 4;
        } else if (StartsWith(text.substr(i), L"false")) {
            i += 5;
        } else {
            return false;
        }
    }
    return true;
}

bool LooksLikeWinDbg(std::wstring_view text) {
    int score = 0;
    ForEachLine(text, kSniffLines, [&score](std::wstring_view line) {
        while (!line.empty() && (line.front() == L' ' || line.front() == L'\t')) {
            line.remove_prefix(1);
        }
        if (StartsWith(line, L"$$")) {
            score += 2;
        } else if (StartsWith(line, L".echo") || StartsWith(line, L".if") ||
                   StartsWith(line, L".foreach") || StartsWith(line, L".printf") ||
                   StartsWith(line, L".sympath") || StartsWith(line, L".reload")) {
            score += 1;
        } else if (StartsWith(line, L"bp ") || StartsWith(line, L"bu ") ||
                   StartsWith(line, L"ba ")) {
            score += 1;
        }
    });
    return score >= 2;
}

// Timestamp-ish or [LEVEL]-ish opening, e.g. "2026-07-23 10:00:01", "[INFO]".
bool StartsLikeLogLine(std::wstring_view line) {
    if (!line.empty() && line.front() == L'[') {
        line.remove_prefix(1);
    }
    int digits = 0;
    int dateChars = 0;
    const size_t window = std::min<size_t>(line.size(), 10);
    for (size_t i = 0; i < window; ++i) {
        const wchar_t ch = line[i];
        if (ch >= L'0' && ch <= L'9') {
            ++digits;
            ++dateChars;
        } else if (ch == L'-' || ch == L'/' || ch == L':' || ch == L'.' || ch == L' ' ||
                   ch == L'T' || ch == L',') {
            ++dateChars;
        }
    }
    if (digits >= 4 && dateChars >= 8) {
        return true;
    }
    static constexpr std::wstring_view kLevels[] = {L"info", L"warn", L"warning", L"error",
                                                    L"debug", L"trace", L"fatal"};
    const size_t tokenEnd = line.find_first_of(L" ]:");
    const std::wstring token = LowerAscii(line.substr(0, std::min(tokenEnd, line.size())));
    return std::any_of(std::begin(kLevels), std::end(kLevels),
                       [&token](std::wstring_view level) { return token == level; });
}

bool LooksLikeLog(std::wstring_view text) {
    int total = 0;
    int hits = 0;
    ForEachLine(text, kSniffLines, [&](std::wstring_view line) {
        if (line.empty()) {
            return;
        }
        ++total;
        if (StartsLikeLogLine(line)) {
            ++hits;
        }
    });
    return total >= 3 && hits * 100 >= total * 60;
}

bool HasCppMarker(std::wstring_view text) {
    return text.find(L"class ") != std::wstring_view::npos ||
           text.find(L"template") != std::wstring_view::npos ||
           text.find(L"namespace") != std::wstring_view::npos ||
           text.find(L"::") != std::wstring_view::npos;
}

Language SniffContent(std::wstring_view text) {
    if (StartsWith(text, L"#!")) {
        const size_t lineEnd = text.find_first_of(L"\r\n");
        return FromShebang(text.substr(0, std::min(lineEnd, text.size())));
    }
    if (LooksLikeJson(text)) {
        return Language::Json;
    }
    if (LooksLikeWinDbg(text)) {
        return Language::WinDbg;
    }
    if (LooksLikeLog(text)) {
        return Language::Log;
    }
    return Language::PlainText;
}

} // namespace

const LanguageTraits& TraitsFor(Language language) {
    return kTraits[static_cast<size_t>(language)];
}

Language DetectLanguage(const std::wstring& path, std::wstring_view contentPrefix) {
    const std::wstring leaf = util::PathLeaf(path);
    const size_t dot = leaf.find_last_of(L'.');
    const std::wstring ext =
        dot == std::wstring::npos ? std::wstring() : LowerAscii(leaf.substr(dot + 1));

    if (ext == L"json") return Language::Json;
    if (ext == L"log") return Language::Log;
    if (ext == L"asm" || ext == L"s" || ext == L"inc") return Language::Asm;
    if (ext == L"c") return Language::C;
    if (ext == L"h") {
        // Headers are ambiguous; C++ markers decide, empty files default to C++.
        return contentPrefix.empty() || HasCppMarker(contentPrefix) ? Language::Cpp : Language::C;
    }
    if (ext == L"cpp" || ext == L"cc" || ext == L"cxx" || ext == L"hpp" || ext == L"hh" ||
        ext == L"hxx") {
        return Language::Cpp;
    }
    if (ext == L"java") return Language::Java;
    if (ext == L"go") return Language::Go;
    if (ext == L"py" || ext == L"pyw") return Language::Python;
    if (ext == L"bat" || ext == L"cmd") return Language::Batch;
    if (ext == L"sh") return Language::Bash;
    if (ext == L"ps1" || ext == L"psm1" || ext == L"psd1") return Language::PowerShell;
    if (ext == L"wds") return Language::WinDbg;

    return SniffContent(contentPrefix);
}

IndentStyle SniffIndentStyle(std::wstring_view contentPrefix, Language language) {
    int tabLines = 0;
    int spaceLines = 0;
    int gcd = 0;
    ForEachLine(contentPrefix, kIndentLines, [&](std::wstring_view line) {
        if (line.empty()) {
            return;
        }
        if (line.front() == L'\t') {
            ++tabLines;
            return;
        }
        size_t spaces = 0;
        while (spaces < line.size() && line[spaces] == L' ') {
            ++spaces;
        }
        if (spaces >= 2 && spaces < line.size() && spaces <= 32) {
            ++spaceLines;
            gcd = std::gcd(gcd, static_cast<int>(spaces));
        }
    });

    IndentStyle byDefault;
    switch (language) {
        case Language::Go:
            byDefault = {true, 4};
            break;
        case Language::Json:
            byDefault = {false, 2};
            break;
        default:
            byDefault = {false, 4};
            break;
    }

    constexpr int kMinEvidence = 3;
    if (tabLines >= kMinEvidence && tabLines > spaceLines) {
        return {true, byDefault.width};
    }
    if (spaceLines >= kMinEvidence && spaceLines > tabLines && gcd >= 2 && gcd <= 8) {
        return {false, gcd};
    }
    return byDefault;
}

} // namespace notepadxp::lang

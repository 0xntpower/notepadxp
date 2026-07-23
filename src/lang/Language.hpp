#pragma once

// Language.hpp — Document-language detection and per-language knowledge.
//
// One shared detector (extension first, content sniff second) feeds every
// smart-behavior feature; LanguageTraits is the single table describing how
// each language comments, quotes, and indents. Detection runs once per
// document change over a bounded prefix — never during typing.

#include <cstddef>
#include <string>
#include <string_view>

namespace notepadxp::lang {

enum class Language {
    PlainText,
    Log,
    Json,
    Asm,
    C,
    Cpp,
    Java,
    Go,
    Python,
    WinDbg,
    Batch,
    Bash,
    PowerShell,
};

/// @brief How a document indents: tabs, or spaces of a given width.
struct IndentStyle {
    bool useTabs = false;
    int width = 4;

    /// @brief One indentation unit as text.
    [[nodiscard]] std::wstring Unit() const {
        return useTabs ? std::wstring(1, L'\t') : std::wstring(static_cast<size_t>(width), L' ');
    }
};

/// @brief Static per-language knowledge consumed by the smart features.
struct LanguageTraits {
    std::wstring_view displayName;       // Status-bar text; empty for PlainText.
    std::wstring_view lineComment;       // L"//", L"#", L";", L"REM", L"$$"; empty = none.
    std::wstring_view lineCommentAlt;    // Second accepted form (batch L"::"); empty = none.
    std::wstring_view blockCommentOpen;  // L"/*" / L"<#"; empty = none.
    std::wstring_view blockCommentClose;
    std::wstring_view stringDelims;      // Chars that open/close string literals.
    wchar_t escapeChar;                  // L'\\', PowerShell L'`', 0 = no escapes.
    std::wstring_view indentTriggers;    // Line-final chars that deepen the next line.
    int tabStopChars;                    // Tab stop width for EM_SETTABSTOPS.
};

/// @brief The traits table entry for @p language.
[[nodiscard]] const LanguageTraits& TraitsFor(Language language);

/// @brief Detect the document language from @p path's extension, falling back
///        to a content sniff of @p contentPrefix (shebang, JSON structure,
///        WinDbg markers, log-line shape) for unknown/.txt/missing extensions.
[[nodiscard]] Language DetectLanguage(const std::wstring& path, std::wstring_view contentPrefix);

/// @brief Infer the file's indentation style by majority vote over the leading
///        whitespace in @p contentPrefix; language default when inconclusive
///        (Go: tabs; JSON: 2 spaces; everything else: 4 spaces).
[[nodiscard]] IndentStyle SniffIndentStyle(std::wstring_view contentPrefix, Language language);

} // namespace notepadxp::lang

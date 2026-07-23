#include "lang/BraceMatch.hpp"

#include <cwctype>
#include <vector>

namespace notepadxp::lang {

namespace {

bool IsBracket(wchar_t ch) {
    return ch == L'(' || ch == L')' || ch == L'[' || ch == L']' || ch == L'{' || ch == L'}';
}

bool IsOpener(wchar_t ch) {
    return ch == L'(' || ch == L'[' || ch == L'{';
}

wchar_t PartnerOf(wchar_t ch) {
    switch (ch) {
        case L'(': return L')';
        case L'[': return L']';
        case L'{': return L'}';
        case L')': return L'(';
        case L']': return L'[';
        default:   return L'{';
    }
}

bool MatchesAt(std::wstring_view text, size_t pos, std::wstring_view token) {
    return !token.empty() && text.compare(pos, token.size(), token) == 0;
}

// Word-like comment markers (batch REM) only count at a word boundary.
bool CommentStartsAt(std::wstring_view text, size_t pos, std::wstring_view token) {
    if (!MatchesAt(text, pos, token)) {
        return false;
    }
    if (std::iswalpha(static_cast<wint_t>(token.front())) != 0 && pos > 0) {
        const wchar_t before = text[pos - 1];
        return before == L' ' || before == L'\t' || before == L'\n';
    }
    return true;
}

} // namespace

std::optional<size_t> FindMatchingBrace(std::wstring_view text, size_t caretPos,
                                        const LanguageTraits& traits) {
    size_t target = std::wstring_view::npos;
    if (caretPos < text.size() && IsBracket(text[caretPos])) {
        target = caretPos;
    } else if (caretPos > 0 && caretPos - 1 < text.size() && IsBracket(text[caretPos - 1])) {
        target = caretPos - 1;
    }
    if (target == std::wstring_view::npos) {
        return std::nullopt;
    }

    // One forward scan with lexical state; openers stack their positions.
    enum class State { Code, String, LineComment, BlockComment };
    State state = State::Code;
    wchar_t stringDelim = 0;
    bool escaped = false;
    std::vector<size_t> stack;

    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        switch (state) {
            case State::String:
                if (escaped) {
                    escaped = false;
                } else if (traits.escapeChar != 0 && ch == traits.escapeChar) {
                    escaped = true;
                } else if (ch == stringDelim || ch == L'\n') {  // \n: unterminated literal.
                    state = State::Code;
                }
                continue;
            case State::LineComment:
                if (ch == L'\n') {
                    state = State::Code;
                }
                continue;
            case State::BlockComment:
                if (MatchesAt(text, i, traits.blockCommentClose)) {
                    state = State::Code;
                    i += traits.blockCommentClose.size() - 1;
                }
                continue;
            case State::Code:
                break;
        }

        if (CommentStartsAt(text, i, traits.lineComment) ||
            CommentStartsAt(text, i, traits.lineCommentAlt)) {
            state = State::LineComment;
            continue;
        }
        if (MatchesAt(text, i, traits.blockCommentOpen)) {
            state = State::BlockComment;
            i += traits.blockCommentOpen.size() - 1;
            continue;
        }
        if (traits.stringDelims.find(ch) != std::wstring_view::npos) {
            state = State::String;
            stringDelim = ch;
            escaped = false;
            continue;
        }
        if (!IsBracket(ch)) {
            continue;
        }

        if (IsOpener(ch)) {
            stack.push_back(i);
            continue;
        }
        // Closer: resolve against the innermost matching-kind opener.
        if (stack.empty()) {
            if (i == target) {
                return std::nullopt;  // Unbalanced closer.
            }
            continue;
        }
        const size_t opener = stack.back();
        stack.pop_back();
        if (text[opener] != PartnerOf(ch)) {
            // Mismatched kinds (broken source): give up on this pair.
            if (i == target || opener == target) {
                return std::nullopt;
            }
            continue;
        }
        if (i == target) {
            return opener;
        }
        if (opener == target) {
            return i;
        }
    }
    return std::nullopt;  // The target was inside a string/comment, or unclosed.
}

} // namespace notepadxp::lang

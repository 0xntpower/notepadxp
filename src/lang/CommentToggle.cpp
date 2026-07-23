#include "lang/CommentToggle.hpp"

#include <algorithm>
#include <cwctype>
#include <vector>

namespace notepadxp::lang {

namespace {

struct BlockLine {
    std::wstring_view text;   // Line content without the break.
    std::wstring_view brk;    // The break that followed ("\r\n", "\n", or "").
    size_t indent = 0;        // Leading whitespace chars.
    bool blank = true;
};

std::vector<BlockLine> SplitLines(std::wstring_view block) {
    std::vector<BlockLine> lines;
    size_t start = 0;
    while (start <= block.size()) {
        size_t end = block.find(L'\n', start);
        size_t brkLen = 0;
        size_t textEnd;
        if (end == std::wstring_view::npos) {
            textEnd = block.size();
        } else {
            textEnd = end;
            brkLen = 1;
            if (textEnd > start && block[textEnd - 1] == L'\r') {
                --textEnd;
                ++brkLen;
            }
        }
        BlockLine line;
        line.text = block.substr(start, textEnd - start);
        line.brk = block.substr(textEnd, brkLen);
        while (line.indent < line.text.size() &&
               (line.text[line.indent] == L' ' || line.text[line.indent] == L'\t')) {
            ++line.indent;
        }
        line.blank = line.indent == line.text.size();
        lines.push_back(line);
        if (end == std::wstring_view::npos) {
            break;
        }
        start = end + 1;
    }
    return lines;
}

// Marker match at `pos`, case-insensitive with a word boundary for word-like
// markers (batch REM / rem).
bool MarkerAt(std::wstring_view line, size_t pos, std::wstring_view marker) {
    if (marker.empty() || pos + marker.size() > line.size()) {
        return false;
    }
    for (size_t i = 0; i < marker.size(); ++i) {
        if (std::towlower(static_cast<wint_t>(line[pos + i])) !=
            std::towlower(static_cast<wint_t>(marker[i]))) {
            return false;
        }
    }
    if (std::iswalpha(static_cast<wint_t>(marker.front())) != 0) {
        const size_t after = pos + marker.size();
        if (after < line.size() && line[after] != L' ' && line[after] != L'\t') {
            return false;
        }
    }
    return true;
}

// Length of the marker present at `pos`, or 0.
size_t MarkerLengthAt(std::wstring_view line, size_t pos, const LanguageTraits& traits) {
    if (MarkerAt(line, pos, traits.lineComment)) {
        return traits.lineComment.size();
    }
    if (MarkerAt(line, pos, traits.lineCommentAlt)) {
        return traits.lineCommentAlt.size();
    }
    return 0;
}

} // namespace

std::wstring ToggleLineComments(std::wstring_view block, const LanguageTraits& traits) {
    if (traits.lineComment.empty()) {
        return std::wstring(block);
    }
    const std::vector<BlockLine> lines = SplitLines(block);

    bool anyNonBlank = false;
    bool allCommented = true;
    size_t minIndent = std::wstring_view::npos;
    for (const BlockLine& line : lines) {
        if (line.blank) {
            continue;
        }
        anyNonBlank = true;
        minIndent = std::min(minIndent, line.indent);
        if (MarkerLengthAt(line.text, line.indent, traits) == 0) {
            allCommented = false;
        }
    }
    if (!anyNonBlank) {
        return std::wstring(block);
    }

    std::wstring result;
    result.reserve(block.size() + lines.size() * (traits.lineComment.size() + 1));
    for (const BlockLine& line : lines) {
        if (line.blank) {
            result += line.text;
            result += line.brk;
            continue;
        }
        if (allCommented) {
            const size_t markerLen = MarkerLengthAt(line.text, line.indent, traits);
            size_t restStart = line.indent + markerLen;
            if (restStart < line.text.size() && line.text[restStart] == L' ') {
                ++restStart;  // Strip the single space the toggle inserts.
            }
            result += line.text.substr(0, line.indent);
            result += line.text.substr(restStart);
        } else {
            result += line.text.substr(0, minIndent);
            result += traits.lineComment;
            result += L' ';
            result += line.text.substr(minIndent);
        }
        result += line.brk;
    }
    return result;
}

} // namespace notepadxp::lang

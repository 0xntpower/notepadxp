#include "lang/IndentEngine.hpp"

namespace notepadxp::lang {

std::wstring ComputeEnterIndent(std::wstring_view lineUpToCaret, const LanguageTraits& traits,
                                IndentStyle style) {
    size_t ws = 0;
    while (ws < lineUpToCaret.size() &&
           (lineUpToCaret[ws] == L' ' || lineUpToCaret[ws] == L'\t')) {
        ++ws;
    }
    std::wstring indent(lineUpToCaret.substr(0, ws));

    std::wstring_view trimmed = lineUpToCaret;
    while (!trimmed.empty() && (trimmed.back() == L' ' || trimmed.back() == L'\t')) {
        trimmed.remove_suffix(1);
    }
    if (trimmed.size() > ws &&
        traits.indentTriggers.find(trimmed.back()) != std::wstring_view::npos) {
        indent += style.Unit();
    }
    return indent;
}

} // namespace notepadxp::lang

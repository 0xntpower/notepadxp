#pragma once

// HeaderFooter.hpp — Expansion of print header/footer template codes.

#include <string>
#include <string_view>

namespace notepadxp::printing {

/// @brief Context substituted into header/footer templates for one page.
struct HeaderFooterContext {
    std::wstring fileName;    // &f / &F
    std::wstring date;        // &d / &D (long date)
    std::wstring time;        // &t / &T (long time)
    int pageNumber = 1;       // &p / &P (optionally with a +offset)
};

/// @brief Text destined for one horizontal alignment band of a header/footer.
struct AlignedText {
    std::wstring left;
    std::wstring center;  // The default band when no alignment code is given.
    std::wstring right;
};

/// @brief Expand a header/footer template (e.g. "&f", "Page &p") into aligned
///        text using @p context. Codes follow the localizable IDS_LETTERS map:
///        f=file p=page t=time d=date c=center r=right l=left, and "&&" => '&'.
[[nodiscard]] AlignedText ExpandTemplate(std::wstring_view templateText,
                                         const HeaderFooterContext& context);

/// @brief As above, but with an explicit code map @p letters (pairs of
///        lower/upper letters, "fFpPtTdDcCrRlL"). Lets callers — and unit tests —
///        avoid the resource lookup. The default overload supplies IDS_LETTERS.
[[nodiscard]] AlignedText ExpandTemplate(std::wstring_view templateText,
                                         const HeaderFooterContext& context,
                                         std::wstring_view letters);

} // namespace notepadxp::printing

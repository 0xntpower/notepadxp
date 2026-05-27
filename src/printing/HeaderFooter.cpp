#include "printing/HeaderFooter.hpp"

#include <string>
#include <string_view>

#include "Resource.h"
#include "util/StringTable.hpp"

namespace notepadxp::printing {

namespace {

// Roles, indexed by (position in IDS_LETTERS) / 2. The letters string pairs the
// lower- and upper-case form of each code: "fFpPtTdDcCrRlL".
enum class Role { File = 0, Page = 1, Time = 2, Date = 3, Center = 4, Right = 5, Left = 6 };

bool IsDigit(wchar_t c) {
    return c >= L'0' && c <= L'9';
}

} // namespace

AlignedText ExpandTemplate(std::wstring_view templateText, const HeaderFooterContext& context) {
    return ExpandTemplate(templateText, context, util::LoadStr(IDS_LETTERS));
}

AlignedText ExpandTemplate(std::wstring_view templateText, const HeaderFooterContext& context,
                           std::wstring_view letters) {
    AlignedText result;
    std::wstring* active = &result.center;  // Default band is center.

    for (size_t i = 0; i < templateText.size(); ++i) {
        const wchar_t ch = templateText[i];
        if (ch != L'&') {
            active->push_back(ch);
            continue;
        }
        if (i + 1 >= templateText.size()) {
            active->push_back(L'&');  // Trailing '&' is literal.
            break;
        }

        const wchar_t code = templateText[++i];
        if (code == L'&') {
            active->push_back(L'&');  // "&&" -> literal '&'.
            continue;
        }

        const size_t position = letters.find(code);
        if (position == std::wstring::npos) {
            active->push_back(code);  // Unknown code: emit the character.
            continue;
        }

        switch (static_cast<Role>(position / 2)) {
            case Role::File:
                *active += context.fileName;
                break;
            case Role::Page: {
                int page = context.pageNumber;
                // Optional "+<digits>" offset, e.g. "&p+2".
                if (i + 1 < templateText.size() && templateText[i + 1] == L'+') {
                    size_t j = i + 2;
                    int offset = 0;
                    bool any = false;
                    while (j < templateText.size() && IsDigit(templateText[j])) {
                        offset = offset * 10 + (templateText[j] - L'0');
                        ++j;
                        any = true;
                    }
                    if (any) {
                        page += offset;
                        i = j - 1;
                    }
                }
                *active += std::to_wstring(page);
                break;
            }
            case Role::Time:
                *active += context.time;
                break;
            case Role::Date:
                *active += context.date;
                break;
            case Role::Center:
                active = &result.center;
                break;
            case Role::Right:
                active = &result.right;
                break;
            case Role::Left:
                active = &result.left;
                break;
        }
    }
    return result;
}

} // namespace notepadxp::printing

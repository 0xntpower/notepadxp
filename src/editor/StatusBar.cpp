#include "editor/StatusBar.hpp"

#include <array>
#include <cstdio>
#include <string>

#include "Resource.h"
#include "util/StringTable.hpp"

namespace notepadxp::editor {

namespace {

constexpr UINT kStatusBarId = 2001;
constexpr int kPartCount = 2;
constexpr int kLineColPart = 1;     // Right part holds the Ln/Col text.
constexpr int kLeftPartFraction = 3;  // Left part spans 3/4 of the width.
constexpr int kFractionDenominator = 4;
constexpr int kTextBufferChars = 128;

} // namespace

bool StatusBar::Create(HWND parent, bool visible) {
    DWORD style = WS_CHILD | WS_CLIPSIBLINGS | SBARS_SIZEGRIP;
    if (visible) {
        style |= WS_VISIBLE;
    }
    const HWND created = status_.Create(parent, nullptr, nullptr, style, 0, kStatusBarId);
    if (created == nullptr) {
        return false;
    }
    visible_ = visible;
    Layout();
    SetLineCol(1, 1);
    return true;
}

void StatusBar::Layout() {
    if (status_.m_hWnd == nullptr) {
        return;
    }
    // Let the control resize itself to the parent's width, then split it.
    status_.SendMessage(WM_SIZE, 0, 0);

    RECT rc = {0, 0, 0, 0};
    status_.GetClientRect(&rc);
    std::array<int, kPartCount> parts{};
    parts[0] = (rc.right * kLeftPartFraction) / kFractionDenominator;
    parts[1] = -1;  // The right part extends to the edge.
    status_.SetParts(kPartCount, parts.data());
}

void StatusBar::Show(bool visible) {
    if (status_.m_hWnd == nullptr) {
        return;
    }
    visible_ = visible;
    status_.ShowWindow(visible ? SW_SHOW : SW_HIDE);
}

int StatusBar::Height() {
    if (status_.m_hWnd == nullptr) {
        return 0;
    }
    RECT rc = {0, 0, 0, 0};
    status_.GetWindowRect(&rc);
    return rc.bottom - rc.top;
}

void StatusBar::SetLineCol(int line, int col) {
    if (status_.m_hWnd == nullptr) {
        return;
    }
    const std::wstring format = util::LoadStr(IDS_LINECOL);  // "   Ln %d, Col %d  "
    std::array<wchar_t, kTextBufferChars> buffer{};
    // The format string is our own resource with exactly two %d fields.
    _snwprintf_s(buffer.data(), buffer.size(), _TRUNCATE, format.c_str(), line, col);
    status_.SetText(kLineColPart, buffer.data());
}

} // namespace notepadxp::editor

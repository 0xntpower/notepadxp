#include "app/LoadProgressDialog.hpp"

#include <cwchar>

#include "Resource.h"
#include "util/StringTable.hpp"

namespace notepadxp::app {

namespace {

constexpr int kIdBar = 100;
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kTimerMs = 100;

struct DialogState {
    file::LoadProgressShared* shared = nullptr;
    HWND label = nullptr;
    HWND bar = nullptr;
    HWND cancel = nullptr;
    bool quitPosted = false;
};

LRESULT CALLBACK ProgressProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        case WM_TIMER: {
            if (wParam == kTimerId && state != nullptr) {
                const uint64_t done = state->shared->bytesDone.load(std::memory_order_relaxed);
                const uint64_t total = state->shared->bytesTotal.load(std::memory_order_relaxed);
                wchar_t line[96]{};
                if (total > 0) {
                    swprintf_s(line, L"%llu / %llu MB", done >> 20, total >> 20);
                } else {
                    swprintf_s(line, L"%llu MB", done >> 20);
                }
                SetWindowTextW(state->label, line);
                InvalidateRect(state->bar, nullptr, FALSE);
            }
            return 0;
        }
        case WM_DRAWITEM: {
            if (wParam == static_cast<WPARAM>(kIdBar) && state != nullptr) {
                const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
                const uint64_t total =
                    state->shared->bytesTotal.load(std::memory_order_relaxed);
                const uint64_t done = state->shared->bytesDone.load(std::memory_order_relaxed);
                FillRect(dis->hDC, &dis->rcItem,
                         reinterpret_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH)));
                if (total > 0) {
                    const auto span =
                        static_cast<__int64>(dis->rcItem.right - dis->rcItem.left);
                    RECT fill = dis->rcItem;
                    fill.right = dis->rcItem.left +
                                 static_cast<LONG>(span * static_cast<__int64>(done) /
                                                   static_cast<__int64>(total));
                    const HBRUSH brush = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
                    FillRect(dis->hDC, &fill, brush);
                    DeleteObject(brush);
                }
                return TRUE;
            }
            break;
        }
        case WM_COMMAND: {
            if (state != nullptr && !state->shared->cancelRequested.exchange(true)) {
                SetWindowTextW(state->cancel, util::LoadStr(IDS_CANCELLING).c_str());
                EnableWindow(state->cancel, FALSE);
            }
            return 0;
        }
        case WM_CLOSE:
            if (state != nullptr) {
                state->shared->cancelRequested.store(true);  // Stay until joined.
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void RegisterProgressClass() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &ProgressProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"NxpLoadProgress";
    RegisterClassW(&wc);  // Fails harmlessly when already registered.
}

} // namespace

void LoadProgressDialog::Run(HWND owner, const std::wstring& fileName,
                             file::LoadProgressShared& shared) {
    RegisterProgressClass();
    DialogState state;
    state.shared = &shared;

    const int width = 380;
    const int height = 150;
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

    // Title: "Opening <name> - Notepad"; the label carries the byte counter.
    const std::wstring title =
        util::LoadAndMerge(IDS_OPENING, fileName) + util::LoadStr(IDS_NOTEPAD);
    const HWND dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME, L"NxpLoadProgress", title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height, owner, nullptr,
        GetModuleHandleW(nullptr), &state);
    if (dialog == nullptr) {
        while (!shared.finished.load(std::memory_order_acquire)) {  // Headless wait.
            Sleep(10);
        }
        return;
    }

    // Lay the window out by hand: label, bar, cancel.
    const int margin = 12;
    const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    state.label = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, margin, margin,
                                  width - 2 * margin, 20, dialog, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    state.bar = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                                margin, margin + 28, width - 2 * margin - 2 * margin, 18,
                                dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdBar)),
                                GetModuleHandleW(nullptr), nullptr);
    state.cancel = CreateWindowExW(
        0, L"BUTTON", util::LoadStr(IDS_CANCEL).c_str(),
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP, width - 90 - margin,
        height - 60, 90, 26, dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(state.label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(state.cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    SetTimer(dialog, kTimerId, kTimerMs, nullptr);
    SendMessageW(dialog, WM_TIMER, kTimerId, 0);  // Paint the first frame now.

    // Pump until the worker reports finished. WM_QUIT is swallowed and
    // re-posted: the app is closing, but the worker must still be joined.
    MSG msg{};
    while (!shared.finished.load(std::memory_order_acquire)) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) == FALSE) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, kTimerMs, QS_ALLINPUT);
            continue;
        }
        if (msg.message == WM_QUIT) {
            state.quitPosted = true;
            continue;
        }
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    KillTimer(dialog, kTimerId);
    EnableWindow(owner, TRUE);
    DestroyWindow(dialog);
    if (state.quitPosted) {
        PostQuitMessage(0);
    }
}

} // namespace notepadxp::app

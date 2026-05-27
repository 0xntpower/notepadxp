#include "dialogs/EncodingFileDialog.hpp"

#include <array>
#include <cstddef>
#include <cwchar>
#include <optional>
#include <string>

#include <commdlg.h>
#include <dlgs.h>

#include "Resource.h"
#include "file/Encoding.hpp"
#include "util/HandleGuard.hpp"
#include "util/StringTable.hpp"

namespace notepadxp::dialogs {

namespace {

constexpr size_t kSniffBytes = 1024;  // Bytes sampled from the file head to sniff the encoding.
constexpr size_t kFileBufferChars = 2048;

// Shared between ShowOpen/ShowSave and the hook via OPENFILENAME::lCustData.
struct DialogState {
    file::TextEncoding encoding = file::TextEncoding::Ansi;
    bool isSave = false;
};

DialogState* StateOf(HWND dialog) {
    return reinterpret_cast<DialogState*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
}

// Build the (double-null-terminated) Open/Save filter string.
std::wstring BuildFilter() {
    std::wstring filter;
    filter += util::LoadStr(IDS_ANSITEXT);  // "Text Documents (*.txt)"
    filter.push_back(L'\0');
    filter += L"*.txt";
    filter.push_back(L'\0');
    filter += util::LoadStr(IDS_ALLFILES);  // "All Files "
    filter.push_back(L'\0');
    filter += L"*.*";
    filter.push_back(L'\0');
    filter.push_back(L'\0');  // Final terminator.
    return filter;
}

void AddEncodingName(HWND combo, UINT stringId) {
    const std::wstring name = util::LoadStr(stringId);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
}

// Read the head of a file and detect its encoding for the Open preview.
std::optional<file::TextEncoding> DetectFileEncoding(const wchar_t* path) {
    util::HandleGuard file(CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.IsValid()) {
        return std::nullopt;
    }
    std::array<std::byte, kSniffBytes> buffer{};
    DWORD read = 0;
    if (ReadFile(file.Get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) ==
        FALSE) {
        return std::nullopt;
    }
    return file::DetectEncoding(buffer.data(), read);
}

UINT_PTR CALLBACK EncodingHookProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG: {
            auto* ofn = reinterpret_cast<LPOPENFILENAMEW>(lParam);
            // lCustData carries our DialogState pointer; stash it for later messages.
            SetWindowLongPtrW(dialog, GWLP_USERDATA, static_cast<LONG_PTR>(ofn->lCustData));
            auto* state = reinterpret_cast<DialogState*>(ofn->lCustData);
            HWND combo = GetDlgItem(dialog, IDC_FILETYPE);
            AddEncodingName(combo, IDS_FT_ANSI);
            AddEncodingName(combo, IDS_FT_UNICODE);
            AddEncodingName(combo, IDS_FT_UNICODEBE);
            AddEncodingName(combo, IDS_FT_UTF8);
            SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(state->encoding), 0);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_FILETYPE && HIWORD(wParam) == CBN_SELCHANGE) {
                if (DialogState* state = StateOf(dialog)) {
                    const auto sel = static_cast<int>(
                        SendMessageW(GetDlgItem(dialog, IDC_FILETYPE), CB_GETCURSEL, 0, 0));
                    if (sel >= 0) {
                        state->encoding = static_cast<file::TextEncoding>(sel);
                    }
                }
            }
            return 0;
        case WM_NOTIFY: {
            DialogState* state = StateOf(dialog);
            if (state == nullptr) {
                return 0;
            }
            const auto* header = reinterpret_cast<const NMHDR*>(lParam);
            if (header->code == CDN_SELCHANGE && !state->isSave) {
                std::array<wchar_t, MAX_PATH> path{};
                const HWND parentDialog = GetParent(dialog);
                if (CommDlg_OpenSave_GetFilePath(parentDialog, path.data(),
                                                 static_cast<int>(path.size())) > 0) {
                    if (const std::optional<file::TextEncoding> enc =
                            DetectFileEncoding(path.data())) {
                        state->encoding = enc.value();
                        SendMessageW(GetDlgItem(dialog, IDC_FILETYPE), CB_SETCURSEL,
                                     static_cast<WPARAM>(enc.value()), 0);
                    }
                }
            } else if (header->code == CDN_FILEOK) {
                const auto sel = static_cast<int>(
                    SendMessageW(GetDlgItem(dialog, IDC_FILETYPE), CB_GETCURSEL, 0, 0));
                if (sel >= 0) {
                    state->encoding = static_cast<file::TextEncoding>(sel);
                }
            }
            return 0;
        }
        default:
            return 0;
    }
}

} // namespace

std::optional<FileDialogResult> EncodingFileDialog::ShowOpen(HWND parent) {
    DialogState state{file::TextEncoding::Ansi, false};
    std::array<wchar_t, kFileBufferChars> fileBuffer{};
    const std::wstring filter = BuildFilter();

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.hInstance = GetModuleHandleW(nullptr);
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = fileBuffer.data();
    ofn.nMaxFile = static_cast<DWORD>(fileBuffer.size());
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_EXPLORER | OFN_ENABLETEMPLATE | OFN_ENABLEHOOK | OFN_ENABLESIZING |
                OFN_HIDEREADONLY | OFN_FILEMUSTEXIST;
    ofn.lpTemplateName = L"NpEncodingDialog";
    ofn.lpfnHook = &EncodingHookProc;
    ofn.lCustData = reinterpret_cast<LPARAM>(&state);  // Passed to the hook.

    if (GetOpenFileNameW(&ofn) == FALSE) {
        return std::nullopt;
    }
    return FileDialogResult{std::wstring(fileBuffer.data()), state.encoding};
}

std::optional<FileDialogResult> EncodingFileDialog::ShowSave(HWND parent,
                                                            const std::wstring& defaultPath,
                                                            file::TextEncoding currentEncoding) {
    DialogState state{currentEncoding, true};
    std::array<wchar_t, kFileBufferChars> fileBuffer{};
    if (!defaultPath.empty()) {
        wcsncpy_s(fileBuffer.data(), fileBuffer.size(), defaultPath.c_str(), _TRUNCATE);
    }
    const std::wstring filter = BuildFilter();

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.hInstance = GetModuleHandleW(nullptr);
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = fileBuffer.data();
    ofn.nMaxFile = static_cast<DWORD>(fileBuffer.size());
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_EXPLORER | OFN_ENABLETEMPLATE | OFN_ENABLEHOOK | OFN_ENABLESIZING |
                OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_NOREADONLYRETURN | OFN_PATHMUSTEXIST;
    ofn.lpTemplateName = L"NpEncodingDialog";
    ofn.lpfnHook = &EncodingHookProc;
    ofn.lCustData = reinterpret_cast<LPARAM>(&state);

    if (GetSaveFileNameW(&ofn) == FALSE) {
        return std::nullopt;
    }
    return FileDialogResult{std::wstring(fileBuffer.data()), state.encoding};
}

} // namespace notepadxp::dialogs

#include "app/MainFrame.hpp"

#include <optional>
#include <string>

#include <shellapi.h>  // DragAcceptFiles / DragQueryFileW / DragFinish.

#include "Resource.h"
#include "dialogs/AboutBox.hpp"
#include "dialogs/FontDialog.hpp"
#include "dialogs/GoToDialog.hpp"
#include "file/Encoding.hpp"
#include "lang/BraceMatch.hpp"
#include "lang/CommentToggle.hpp"
#include "lang/JsonFormat.hpp"
#include "lang/Language.hpp"
#include "util/DateTime.hpp"
#include "util/PathName.hpp"
#include "util/StringTable.hpp"

namespace notepadxp::app {

namespace {

bool ClipboardHasText() {
    return IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE ||
           IsClipboardFormatAvailable(CF_TEXT) != FALSE;
}

} // namespace

MainFrame::MainFrame()
    : fileService_(editView_, document_, prompts_),
      findReplace_(editView_),
      printService_(editView_, settings_, document_),
      pageSetup_(settings_) {}

bool MainFrame::RunSetup(const util::ParsedCommandLine& commandLine, int showCmd) {
    settings_ = settings::Settings::Load();

    const HMENU menu = LoadMenuW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(ID_MENUBAR));
    constexpr DWORD kStyle = WS_OVERLAPPEDWINDOW;

    const bool havePlacement =
        settings_.windowX != CW_USEDEFAULT && settings_.windowY != CW_USEDEFAULT &&
        settings_.windowWidth != CW_USEDEFAULT && settings_.windowHeight != CW_USEDEFAULT;

    HWND created = nullptr;
    if (havePlacement) {
        RECT rect = {settings_.windowX, settings_.windowY,
                     settings_.windowX + settings_.windowWidth,
                     settings_.windowY + settings_.windowHeight};
        created = Create(nullptr, &rect, L"", kStyle, 0, menu);
    } else {
        created = Create(nullptr, nullptr, L"", kStyle, 0, menu);
    }
    if (created == nullptr) {
        return false;
    }

    if (commandLine.filePath.has_value()) {
        std::optional<file::TextEncoding> forced;
        if (commandLine.forceAnsi) {
            forced = file::TextEncoding::Ansi;
        } else if (commandLine.forceUnicode) {
            forced = file::TextEncoding::Utf16Le;
        }
        fileService_.OpenPath(commandLine.filePath.value(), forced);
    }
    UpdateTitle();
    ShowWindow(showCmd);
    UpdateWindow();
    return true;
}

HWND MainFrame::ActiveModelessDialog() const {
    return findReplace_.ActiveDialog();
}

int MainFrame::OnCreate(LPCREATESTRUCT /*createStruct*/) {
    const HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(ID_APPICON));
    if (icon != nullptr) {
        SetIcon(icon, TRUE);
        SetIcon(icon, FALSE);
    }

    if (!editView_.Create(m_hWnd, settings_.wordWrap)) {
        return -1;
    }
    editView_.SetCaretMovedCallback([this] { OnCaretMoved(); });
    editView_.SetWheelZoomCallback([this](int steps) { AdjustZoom(steps); });
    ApplyFontFromSettings();
    prompts_.SetOwner(m_hWnd);
    fileService_.AttachOwner(m_hWnd);
    fileService_.SetDocumentChangedCallback([this] {
        const file::DocumentState& doc = fileService_.Document();
        editView_.SetLanguageContext(doc.language, doc.indentStyle);
        statusBar_.SetLanguageName(lang::TraitsFor(doc.language).displayName);
        UpdateTitle();
        OnCaretMoved();
    });
    DragAcceptFiles(TRUE);  // CWindow member: enables WM_DROPFILES for this window.

    const bool statusVisible = settings_.statusBar && !settings_.wordWrap;
    if (!statusBar_.Create(m_hWnd, statusVisible)) {
        // Non-fatal: the editor remains fully usable without a status bar.
    }

    LayoutChildren();
    editView_.SetFocusToEdit();
    return 0;
}

void MainFrame::OnSize(UINT type, CSize /*size*/) {
    if (type == SIZE_MINIMIZED) {
        return;
    }
    LayoutChildren();
}

void MainFrame::OnActivate(UINT state, BOOL minimized, CWindow /*other*/) {
    if (state != WA_INACTIVE && minimized == FALSE) {
        fileService_.PromptReloadIfChanged();
    }
}

void MainFrame::OnSetFocus(CWindow /*oldFocus*/) {
    editView_.SetFocusToEdit();
}

void MainFrame::OnInitMenuPopup(CMenuHandle /*menu*/, UINT /*index*/, BOOL isSystemMenu) {
    if (!isSystemMenu) {
        UpdateMenuState();
    }
}

void MainFrame::OnCommand(UINT notifyCode, int id, CWindow /*control*/) {
    if (id == ID_EDIT && notifyCode == EN_CHANGE) {
        editView_.OnEditChanged();
        return;
    }
    switch (id) {
        case M_NEW:
            fileService_.New();
            break;
        case M_OPEN:
            fileService_.Open();
            break;
        case M_SAVE:
            fileService_.Save();
            break;
        case M_SAVEAS:
            fileService_.SaveAs();
            break;
        case M_PAGESETUP:
            pageSetup_.ShowDialog(m_hWnd);
            break;
        case M_PRINT:
            printService_.Print(m_hWnd);
            break;
        case M_EXIT:
            PostMessageW(WM_CLOSE);
            break;

        case M_UNDO:
            editView_.Undo();
            break;
        case M_REDO:
            editView_.Redo();
            break;
        case M_CUT:
            editView_.Cut();
            break;
        case M_COPY:
            editView_.Copy();
            break;
        case M_PASTE:
            editView_.Paste();
            break;
        case M_DELETE:
            editView_.DeleteSelection();
            break;
        case M_FIND:
            findReplace_.ShowFind(m_hWnd);
            break;
        case M_FINDNEXT:
            findReplace_.FindNext(m_hWnd);
            break;
        case M_REPLACE:
            findReplace_.ShowReplace(m_hWnd);
            break;
        case M_GOTO:
            OnGoTo();
            break;
        case M_MATCHBRACE: {
            int selStart = 0;
            int selEnd = 0;
            editView_.GetSelection(selStart, selEnd);
            const std::wstring text = editView_.GetText();
            const auto match = lang::FindMatchingBrace(
                text, static_cast<size_t>(selStart),
                lang::TraitsFor(fileService_.Document().language));
            if (match.has_value()) {
                editView_.SelectRange(static_cast<int>(*match), static_cast<int>(*match) + 1);
            } else {
                MessageBeep(MB_ICONEXCLAMATION);
            }
            break;
        }
        case M_TOGGLECOMMENT: {
            const auto& traits = lang::TraitsFor(fileService_.Document().language);
            if (traits.lineComment.empty()) {
                break;
            }
            int startChar = 0;
            int endChar = 0;
            editView_.ExpandSelectionToLines(startChar, endChar);
            const std::wstring text = editView_.GetText();
            const std::wstring block = text.substr(
                static_cast<size_t>(startChar), static_cast<size_t>(endChar - startChar));
            const std::wstring toggled = lang::ToggleLineComments(block, traits);
            if (toggled != block) {
                editView_.SelectRange(startChar, endChar);
                editView_.InsertText(toggled);
                editView_.SelectRange(startChar, startChar + static_cast<int>(toggled.size()));
            }
            break;
        }
        case M_SELECTALL:
            editView_.SelectAll();
            break;
        case M_DATETIME:
            editView_.InsertText(util::FormatTimestamp(false));
            break;

        case M_WORDWRAP:
            OnToggleWordWrap();
            break;
        case M_JSONPRETTY:
        case M_JSONMINIFY: {
            const std::wstring text = editView_.GetText();
            const lang::JsonResult result =
                id == M_JSONPRETTY
                    ? lang::PrettyPrintJson(text, fileService_.Document().indentStyle)
                    : lang::MinifyJson(text);
            if (!result.ok) {
                wchar_t message[128] = {0};
                _snwprintf_s(message, _TRUNCATE, util::LoadStr(IDS_JSONERR).c_str(),
                             result.errorLine, result.errorCol);
                util::AlertBox(m_hWnd, util::LoadStr(IDS_NN), message,
                               MB_OK | MB_ICONEXCLAMATION);
                editView_.SelectRange(static_cast<int>(result.errorOffset),
                                      static_cast<int>(result.errorOffset));
                editView_.SetFocusToEdit();
                break;
            }
            if (result.text != text) {
                editView_.SelectAll();
                editView_.InsertText(result.text);  // One undo unit.
            }
            editView_.SelectRange(0, 0);
            break;
        }
        case M_ZOOMIN:
            AdjustZoom(1);
            break;
        case M_ZOOMOUT:
            AdjustZoom(-1);
            break;
        case M_ZOOMRESET:
            AdjustZoom(-(zoomPercent_ - 100) / 10);  // Straight back to 100%.
            break;
        case M_SETFONT:
            OnChooseFont();
            break;
        case M_STATUSBAR:
            OnToggleStatusBar();
            break;

        case M_HELP:
            // TODO: "Help Topics" is deferred — Win11 has no WinHelp/.hlp support.
            break;
        case M_ABOUT:
            dialogs::AboutBox::Show(m_hWnd);
            break;
        default:
            break;
    }
}

void MainFrame::OnClose() {
    // Prompt to save a modified document; a Cancel aborts the close.
    if (!fileService_.CanClose()) {
        return;
    }
    SaveWindowPlacement();
    settings_.Save();
    DestroyWindow();
}

LRESULT MainFrame::OnFindReplaceMessage(UINT /*message*/, WPARAM wParam, LPARAM lParam,
                                        BOOL& /*handled*/) {
    findReplace_.HandleFindMessage(wParam, lParam);
    return 0;
}

LRESULT MainFrame::OnQueryEndSession(UINT /*message*/, WPARAM /*wParam*/, LPARAM /*lParam*/,
                                     BOOL& handled) {
    // Windows is logging off/shutting down: prompt to save; veto (return FALSE)
    // only if the user cancels.
    handled = TRUE;
    return fileService_.CanClose() ? TRUE : FALSE;
}

void MainFrame::OnDropFiles(HDROP dropInfo) {
    // Open only the first dropped file, matching classic Notepad.
    wchar_t path[MAX_PATH] = {0};
    if (DragQueryFileW(dropInfo, 0, path, MAX_PATH) > 0) {
        SetActiveWindow();
        fileService_.OpenPath(path);
    }
    DragFinish(dropInfo);
}

void MainFrame::OnDestroy() {
    PostQuitMessage(0);
}

void MainFrame::OnToggleWordWrap() {
    const bool newWrap = !settings_.wordWrap;
    if (!editView_.SetWordWrap(newWrap)) {
        util::AlertBox(m_hWnd, util::LoadStr(IDS_NN), util::LoadStr(IDS_NOWW),
                       MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    settings_.wordWrap = newWrap;

    // Word wrap and the (line-based) status bar are mutually exclusive: hide the
    // status bar while wrapping, and restore it (per preference) when unwrapping.
    if (newWrap) {
        if (statusBar_.IsVisible()) {
            statusBar_.Show(false);
        }
    } else if (settings_.statusBar) {
        statusBar_.Show(true);
    }
    LayoutChildren();
    editView_.SetFocusToEdit();
}

void MainFrame::OnToggleStatusBar() {
    // The menu item is grayed while word wrap is on, so this only runs when off.
    settings_.statusBar = !settings_.statusBar;
    statusBar_.Show(settings_.statusBar);
    LayoutChildren();
    if (settings_.statusBar) {
        OnCaretMoved();  // Populate Ln/Col immediately.
    }
}

void MainFrame::OnChooseFont() {
    HDC dc = ::GetDC(nullptr);
    const LOGFONTW current = settings_.ResolvedFont(dc);
    ::ReleaseDC(nullptr, dc);

    if (const auto chosen = dialogs::FontDialog::Choose(m_hWnd, current)) {
        settings_.font = chosen->font;
        settings_.pointSize = chosen->pointSize;
        ApplyFontFromSettings();
    }
}

void MainFrame::OnGoTo() {
    int line = 1;
    int col = 1;
    editView_.GetCaretLineCol(line, col);
    const std::optional<int> chosen = dialogs::GoToDialog::Show(m_hWnd, line);
    if (chosen.has_value()) {
        editView_.GoToLine(chosen.value());
    }
}

void MainFrame::LayoutChildren() {
    RECT client = {0, 0, 0, 0};
    GetClientRect(&client);

    int statusHeight = 0;
    if (statusBar_.IsVisible()) {
        statusBar_.Layout();
        statusHeight = statusBar_.Height();
    }
    const RECT editRect = {client.left, client.top, client.right, client.bottom - statusHeight};
    editView_.Layout(editRect);
}

void MainFrame::ApplyFontFromSettings() {
    HDC dc = ::GetDC(nullptr);
    const LOGFONTW logFont = settings_.ResolvedFont(dc, zoomPercent_);
    ::ReleaseDC(nullptr, dc);
    editView_.SetFont(logFont);
}

void MainFrame::AdjustZoom(int steps) {
    constexpr int kZoomStep = 10;
    constexpr int kZoomFloor = 100;  // The configured font size; never smaller.
    constexpr int kZoomCeiling = 500;
    const int target = zoomPercent_ + steps * kZoomStep;
    const int clamped = target < kZoomFloor ? kZoomFloor
                        : target > kZoomCeiling ? kZoomCeiling
                                                : target;
    if (clamped == zoomPercent_) {
        return;
    }
    zoomPercent_ = clamped;
    ApplyFontFromSettings();
}

void MainFrame::UpdateTitle() {
    const std::wstring name =
        document_.untitled ? util::LoadStr(IDS_UNTITLED) : util::PathLeaf(document_.filePath);
    const std::wstring title = name + util::LoadStr(IDS_NOTEPAD);  // " - Notepad"
    SetWindowText(title.c_str());
}

void MainFrame::UpdateMenuState() {
    CMenuHandle menu = GetMenu();
    if (menu.IsNull()) {
        return;
    }

    const auto enable = [&menu](int id, bool on) {
        menu.EnableMenuItem(static_cast<UINT>(id), MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED));
    };

    enable(M_UNDO, editView_.CanUndo());
    enable(M_REDO, editView_.CanRedo());

    const bool hasSelection = editView_.HasSelection();
    enable(M_CUT, hasSelection);
    enable(M_COPY, hasSelection);
    enable(M_DELETE, hasSelection);
    enable(M_PASTE, ClipboardHasText());

    const bool hasText = editView_.TextLength() > 0;
    enable(M_FIND, hasText);
    enable(M_FINDNEXT, hasText);
    enable(M_REPLACE, hasText);
    enable(M_MATCHBRACE, hasText);
    enable(M_TOGGLECOMMENT,
           !lang::TraitsFor(fileService_.Document().language).lineComment.empty());

    enable(M_GOTO, !settings_.wordWrap);

    const bool isJson = fileService_.Document().language == lang::Language::Json;
    enable(M_JSONPRETTY, isJson);
    enable(M_JSONMINIFY, isJson);

    menu.CheckMenuItem(M_WORDWRAP, MF_BYCOMMAND | (settings_.wordWrap ? MF_CHECKED : MF_UNCHECKED));
    menu.CheckMenuItem(M_STATUSBAR,
                       MF_BYCOMMAND | (settings_.statusBar ? MF_CHECKED : MF_UNCHECKED));
    enable(M_STATUSBAR, !settings_.wordWrap);
}

void MainFrame::SaveWindowPlacement() {
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(&placement)) {
        const RECT& normal = placement.rcNormalPosition;
        settings_.windowX = normal.left;
        settings_.windowY = normal.top;
        settings_.windowWidth = normal.right - normal.left;
        settings_.windowHeight = normal.bottom - normal.top;
    }
}

void MainFrame::OnCaretMoved() {
    if (!statusBar_.IsVisible()) {
        return;
    }
    int line = 1;
    int col = 1;
    editView_.GetCaretLineCol(line, col);
    statusBar_.SetLineCol(line, col);
}

} // namespace notepadxp::app

#pragma once

// MainFrame.hpp — The Notepad main window: menu, edit area, and status bar.

#include <string>

#include "WtlIncludes.hpp"
#include "app/AlertBoxPrompts.hpp"
#include "dialogs/FindReplaceController.hpp"
#include "editor/EditView.hpp"
#include "editor/StatusBar.hpp"
#include "file/DocumentState.hpp"
#include "file/FileService.hpp"
#include "printing/PageSetup.hpp"
#include "printing/PrintService.hpp"
#include "settings/Settings.hpp"
#include "util/CommandLine.hpp"

namespace notepadxp::app {

/// @brief The top-level frame. Owns the edit view, the status bar, the
///        settings, the document state, and the feature services, and routes
///        the menu/accelerator commands to them.
/// @threadsafety Not thread-safe; created and used on the single UI thread.
class MainFrame final : public CWindowImpl<MainFrame> {
public:
    DECLARE_WND_CLASS_EX(L"Notepad", 0, COLOR_WINDOW)

    MainFrame();

    MainFrame(const MainFrame&) = delete;
    MainFrame& operator=(const MainFrame&) = delete;

    BEGIN_MSG_MAP(MainFrame)
        MESSAGE_HANDLER(findReplace_.RegisteredMessage(), OnFindReplaceMessage)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_SIZE(OnSize)
        MSG_WM_ACTIVATE(OnActivate)
        MSG_WM_SETFOCUS(OnSetFocus)
        MSG_WM_INITMENUPOPUP(OnInitMenuPopup)
        MSG_WM_TIMER(OnTimer)
        MSG_WM_COMMAND(OnCommand)
        MSG_WM_DROPFILES(OnDropFiles)
        MESSAGE_HANDLER(WM_QUERYENDSESSION, OnQueryEndSession)
        MSG_WM_CLOSE(OnClose)
        MSG_WM_DESTROY(OnDestroy)
    END_MSG_MAP()

    /// @brief Load settings, create and show the window, and open any
    ///        command-line file. @return true on success.
    [[nodiscard]] bool RunSetup(const util::ParsedCommandLine& commandLine, int showCmd);

    /// @brief HWND of the active modeless Find/Replace dialog (for the message
    ///        loop's IsDialogMessage filtering), or nullptr when none is open.
    [[nodiscard]] HWND ActiveModelessDialog() const;

private:
    int OnCreate(LPCREATESTRUCT createStruct);
    void OnSize(UINT type, CSize size);
    void OnActivate(UINT state, BOOL minimized, CWindow other);
    void OnSetFocus(CWindow oldFocus);
    void OnInitMenuPopup(CMenuHandle menu, UINT index, BOOL isSystemMenu);
    void OnCommand(UINT notifyCode, int id, CWindow control);
    void OnDropFiles(HDROP dropInfo);
    LRESULT OnFindReplaceMessage(UINT message, WPARAM wParam, LPARAM lParam, BOOL& handled);
    LRESULT OnQueryEndSession(UINT message, WPARAM wParam, LPARAM lParam, BOOL& handled);
    void OnClose();
    void OnDestroy();

    // Command handlers with logic of their own.
    void OnToggleWordWrap();
    void OnToggleStatusBar();
    void OnToggleFollowTail();
    void StopFollowTail();
    void OnTimer(UINT_PTR id);
    void OnChooseFont();
    void OnGoTo();

    void LayoutChildren();
    void ApplyFontFromSettings();
    void UpdateTitle();
    void UpdateMenuState();
    void SaveWindowPlacement();
    void OnCaretMoved();
    void AdjustZoom(int steps);

    settings::Settings settings_;
    file::DocumentState document_;
    editor::EditView editView_;
    editor::StatusBar statusBar_;

    AlertBoxPrompts prompts_;
    file::FileService fileService_;
    dialogs::FindReplaceController findReplace_;
    printing::PrintService printService_;
    printing::PageSetup pageSetup_;

    // View-only zoom multiplier; 100 == the configured font size (the floor).
    // Session-only by design: never persisted, dies with the window.
    int zoomPercent_ = 100;

    // Follow Tail (View menu): the poll timer exists only while enabled.
    bool followTail_ = false;
    std::wstring followedPath_;
};

} // namespace notepadxp::app

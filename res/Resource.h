// Resource.h — Resource identifiers for NotepadXP.
//
// This is the single file where `#define` is used for numeric constants, because
// the Windows resource compiler (rc.exe) does not understand C++ `constexpr`.
// This is a deliberate, scoped waiver of HRC §3.1.
// Keep this header free of any other dependency so both Notepad.rc and C++ TUs
// can include it cheaply.

#ifndef NOTEPADXP_RESOURCE_H
#define NOTEPADXP_RESOURCE_H

// --- Icons -----------------------------------------------------------------
#define ID_APPICON              1     // Application icon (Explorer requires id == 1).
#define ID_ICON                 2     // Main window icon.

// --- Menu & accelerators ---------------------------------------------------
#define ID_MENUBAR              1     // Main menu bar.
#define ID_ACCEL                1     // MainAcc accelerator table.

// --- Menu command IDs ------------------------------------------------------
// File
#define M_NEW                   40001
#define M_OPEN                  40002
#define M_SAVE                  40003
#define M_SAVEAS                40004
#define M_PAGESETUP             40005
#define M_PRINT                 40006
#define M_EXIT                  40007
// Edit
#define M_REDO                  40009
#define M_UNDO                  40010
#define M_CUT                   40011
#define M_COPY                  40012
#define M_PASTE                 40013
#define M_DELETE                40014
#define M_FIND                  40015
#define M_FINDNEXT              40016
#define M_REPLACE               40017
#define M_GOTO                  40018
#define M_SELECTALL             40019
#define M_DATETIME              40020
#define M_MATCHBRACE            40021
#define M_TOGGLECOMMENT         40022
// Format
#define M_WORDWRAP              40030
#define M_SETFONT               40031
#define M_JSONPRETTY            40032
#define M_JSONMINIFY            40033
// View
#define M_STATUSBAR             40040
// Zoom (accelerator-only commands; no menu items — classic look).
#define M_ZOOMIN                40041
#define M_ZOOMOUT               40042
#define M_ZOOMRESET             40043
// Help
#define M_HELP                  40050
#define M_ABOUT                 40051

// --- Child control IDs -----------------------------------------------------
#define ID_EDIT                 15    // Main multiline edit control.

// --- Dialog IDs ------------------------------------------------------------
#define IDD_ABORTPRINT          11
#define IDD_PAGESETUP           12
#define IDD_ABOUT               13
#define IDD_GOTODIALOG          14

// --- Dialog control IDs ----------------------------------------------------
// Encoding combo (merged into the Open/Save common dialog via OFN_ENABLETEMPLATE).
#define IDC_FILETYPE            257   // Encoding combo box.
#define IDC_GOTO                258   // Go To line-number edit.
#define IDC_ENCODING            259   // "Encoding:" static label.
#define IDC_GOTO_LABEL          1200  // "Line Number:" static label.
// Abort-print dialog.
#define ID_FILENAME             20
#define ID_PAGENUMBER           21
// Page Setup header/footer (the other page-setup IDs are the standard <dlgs.h> values).
#define ID_HEADER               30
#define ID_FOOTER               31
#define ID_HEADER_LABEL         32
#define ID_FOOTER_LABEL         33

// --- String table IDs (numbering kept faithful to the original Notepad) ----
#define IDS_DISKERROR           1
#define IDS_FNF                 2
#define IDS_SCBC                3
#define IDS_UNTITLED            4
#define IDS_NOTEPAD             5
#define IDS_CFS                 6
#define IDS_ERRSPACE            7
#define IDS_FTL                 8
#define IDS_NN                  9
#define IDS_COMMDLGINIT         10
#define IDS_PRINTDLGINIT        11
#define IDS_CANTPRINT           12
#define IDS_NVF                 13
#define IDS_CREATEERR           14
#define IDS_NOWW                15
#define IDS_MERGE1              16
#define IDS_HELPFILE            17
#define IDS_HEADER              18
#define IDS_FOOTER              19
#define IDS_ANSITEXT            20
#define IDS_ALLFILES            21
#define IDS_OPENCAPTION         22
#define IDS_SAVECAPTION         23
#define IDS_CANNOTQUIT          24
#define IDS_LOADDRVFAIL         25
#define IDS_ACCESSDENY          26
#define IDS_ERRUNICODE          27
#define IDS_FONTTOOBIG          28
#define IDS_COMMDLGERR          29
#define IDS_LINEERROR           30
#define IDS_LINETOOLARGE        31
#define IDS_FT_ANSI             32
#define IDS_FT_UNICODE          33
#define IDS_FT_UNICODEBE        34
#define IDS_FT_UTF8             35
#define IDS_CURRENT_PAGE        36
#define IDS_LINECOL             37
#define IDS_COMPRESSED_FILE     38
#define IDS_ENCRYPTED_FILE      39
#define IDS_HIDDEN_FILE         40
#define IDS_OFFLINE_FILE        41
#define IDS_READONLY_FILE       42
#define IDS_SYSTEM_FILE         43
#define IDS_FILE                44
#define IDS_LETTERS             45
#define IDS_JSONERR             46
#define IDS_RELOAD              47
#define IDS_RELOADMOD           48
#define IDS_TEXT_FRIENDLY_NAME  469

// --- Context-help IDs ------------------------------------------------------
#define IDH_PAGE_FOOTER         1000
#define IDH_PAGE_HEADER         1001
#define IDH_FILETYPE            1002
#define IDH_GOTO                1003

#endif // NOTEPADXP_RESOURCE_H

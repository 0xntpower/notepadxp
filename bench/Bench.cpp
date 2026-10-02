// Bench.cpp — On-demand performance harness for NotepadXP.
//
// Creates a real (hidden) multiline Edit control wired through EditView —
// subclass, document shadow, delta undo, caret notify — and measures the
// app-side costs that matter: load, per-keystroke latency, undo, and the
// zoom font swap. Doubles as an integration test: typing and undo run
// through the genuine message path and are verified for correctness.
//
// Build (from bench/): python ../../vcbuild/vcbuild.py release
// Run: ./build/notepadxp_bench.exe [maxSizeMB]   (default 100)

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "WtlIncludes.hpp"

#include "editor/DocumentShadow.cpp"
#include "editor/EditKeyHandler.cpp"
#include "editor/EditView.cpp"
#include "editor/SearchEngine.cpp"
#include "editor/UndoManager.cpp"
#include "file/Encoding.cpp"
#include "file/LineEndings.cpp"
#include "file/TextFile.cpp"
#include "lang/IndentEngine.cpp"
#include "lang/JsonFormat.cpp"
#include "lang/Language.cpp"
#include "util/FileMapping.cpp"

CAppModule _Module;

namespace {

using notepadxp::editor::EditView;
using notepadxp::editor::FindForward;
using notepadxp::editor::ReplaceAllInText;
using notepadxp::file::ConvertFromCrlf;
using notepadxp::file::DetectLineEnding;
using notepadxp::file::EncodeText;
using notepadxp::file::NormalizeToCrlf;
using notepadxp::file::LoadStatus;
using notepadxp::file::LoadTextFile;
using notepadxp::file::SaveStatus;
using notepadxp::file::TextEncoding;
using notepadxp::file::WriteAllBytes;

int g_failures = 0;

void Verify(bool condition, const char* what) {
    if (!condition) {
        ++g_failures;
        std::printf("  VERIFY FAIL: %s\n", what);
    }
}

double g_qpcFrequency = 0.0;

LARGE_INTEGER Now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t;
}

double MsSince(LARGE_INTEGER start) {
    const LARGE_INTEGER end = Now();
    return (static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0) / g_qpcFrequency;
}

EditView* g_view = nullptr;

LRESULT CALLBACK HostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COMMAND && LOWORD(wParam) == ID_EDIT && HIWORD(wParam) == EN_CHANGE &&
        g_view != nullptr) {
        g_view->OnEditChanged();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Synthetic log text of roughly `mb` million characters (== `mb` MB on disk as UTF-8).
std::wstring GenerateLog(int mb, const wchar_t* line =
    L"2026-07-23 10:00:01.123 INFO  scheduler - task 0000 finished in 42 ms, queue depth 7\r\n") {
    const size_t target = static_cast<size_t>(mb) * 1024 * 1024;
    std::wstring text;
    text.reserve(target + wcslen(line));
    while (text.size() < target) {
        text += line;
    }
    return text;
}

std::wstring TempPath(const wchar_t* name) {
    wchar_t dir[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + name;
}

void Row(const char* scenario, const char* metric, double value, const char* unit) {
    std::printf("%-28s %-26s %10.2f %s\n", scenario, metric, value, unit);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpcFrequency = static_cast<double>(freq.QuadPart);

    int maxMb = 100;
    if (argc > 1) {
        maxMb = _wtoi(argv[1]);
    }

    _Module.Init(nullptr, GetModuleHandleW(nullptr));

    WNDCLASSW wc{};
    wc.lpfnWndProc = &HostProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"NxpBenchHost";
    RegisterClassW(&wc);
    const HWND host = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 800,
                                      600, nullptr, nullptr, wc.hInstance, nullptr);
    EditView view;
    if (host == nullptr || !view.Create(host, false)) {
        std::printf("bench: failed to create the host window / edit control\n");
        return 2;
    }
    g_view = &view;
    RECT client{};
    GetClientRect(host, &client);
    view.Layout(client);  // A real size, as the frame gives it (wrap depends on it).

    LOGFONTW font{};
    font.lfHeight = -13;
    font.lfWeight = FW_NORMAL;
    font.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    wcsncpy_s(font.lfFaceName, L"Lucida Console", _TRUNCATE);
    view.SetFont(font);

    std::printf("%-28s %-26s %10s\n", "scenario", "metric", "value");
    std::printf("---------------------------------------------------------------------\n");

    // --- Open: the FileService::LoadFromPath pipeline ------------------------
    // A BOM-less ASCII log is the common case: detection scans the whole
    // buffer (IsTextUnicode + the UTF-8 heuristic) before the ANSI decode.
    const std::wstring benchFile = TempPath(L"notepadxp_bench.txt");
    for (const int mb : {1, 10, 100}) {
        if (mb > maxMb) {
            continue;
        }
        const std::wstring text = GenerateLog(mb);
        bool lossy = false;
        Verify(WriteAllBytes(benchFile, EncodeText(text, TextEncoding::Ansi, lossy)) ==
                   SaveStatus::Ok,
               "write bench file");

        char label[32];
        sprintf_s(label, "open %d MB ansi log", mb);
        LARGE_INTEGER t = Now();
        auto loaded = LoadTextFile(benchFile, std::nullopt);
        Row(label, "detect+decode", MsSince(t), "ms");
        Verify(loaded.status == LoadStatus::Ok && loaded.text == text, "load round-trip");
        t = Now();
        (void)DetectLineEnding(loaded.text);
        const std::wstring normalized = NormalizeToCrlf(std::move(loaded.text));
        Row(label, "line endings", MsSince(t), "ms");
        t = Now();
        view.SetText(normalized);
        Row(label, "SetWindowText (control)", MsSince(t), "ms");
    }

    // --- Encodings: detect+decode, line endings, save-encode per format -------
    {
        const int encMb = maxMb < 10 ? maxMb : 10;
        const std::wstring ascii = GenerateLog(encMb);
        const std::wstring accented = GenerateLog(
            encMb, L"2026-07-23 10:00:01.123 INFO  café - tâche 0000 terminée en 42 ms, file 7\r\n");
        const std::wstring unix = GenerateLog(
            encMb, L"2026-07-23 10:00:01.123 INFO  scheduler - task 0000 finished in 42 ms\n");
        struct Case {
            const char* name;
            const std::wstring* text;
            TextEncoding encoding;
            bool stripBom;
        };
        const Case cases[] = {
            {"ansi ascii", &ascii, TextEncoding::Ansi, false},
            {"ansi LF-only", &unix, TextEncoding::Ansi, false},
            {"utf-8 BOM", &ascii, TextEncoding::Utf8, false},
            {"utf-8 no-BOM accented", &accented, TextEncoding::Utf8, true},
            {"utf-16 LE", &ascii, TextEncoding::Utf16Le, false},
            {"utf-16 BE", &ascii, TextEncoding::Utf16Be, false},
        };
        for (const Case& c : cases) {
            bool lossy = false;
            std::vector<std::byte> bytes = EncodeText(*c.text, c.encoding, lossy);
            if (c.stripBom) {
                bytes.erase(bytes.begin(), bytes.begin() + 3);
            }
            Verify(WriteAllBytes(benchFile, bytes) == SaveStatus::Ok, "write encoding file");

            char label[40];
            sprintf_s(label, "%s %d MB", c.name, encMb);
            LARGE_INTEGER t = Now();
            auto loaded = LoadTextFile(benchFile, std::nullopt);
            Row(label, "detect+decode", MsSince(t), "ms");
            Verify(loaded.status == LoadStatus::Ok && loaded.encoding == c.encoding &&
                       loaded.text == *c.text,
                   "encoding round-trip");
            t = Now();
            const auto ending = DetectLineEnding(loaded.text);
            std::wstring normalized = NormalizeToCrlf(std::move(loaded.text));
            Row(label, "line endings", MsSince(t), "ms");
            t = Now();
            const auto saved =
                EncodeText(ConvertFromCrlf(std::move(normalized), ending), c.encoding, lossy);
            Row(label, "save: restore+encode", MsSince(t), "ms");
            Verify(saved.size() == bytes.size() + (c.stripBom ? 3 : 0), "save round-trip size");
        }
    }
    DeleteFileW(benchFile.c_str());

    // --- Find / Replace on the largest loaded doc (key absent: full scan) ----
    {
        LARGE_INTEGER t = Now();
        const std::wstring text = view.GetText();
        Row("text access", "GetText full copy", MsSince(t), "ms");
        t = Now();
        Verify(view.LockText().View().size() == text.size(), "lock length");
        Row("text access", "LockText (in place)", MsSince(t), "ms");
        // As FindReplaceController::Search runs it: lock, search, release.
        t = Now();
        Verify(FindForward(view.LockText().View(), L"zebra", 0, true).pos == std::wstring::npos,
               "find miss");
        Row("find next (miss)", "match case", MsSince(t), "ms");
        t = Now();
        Verify(FindForward(view.LockText().View(), L"zebra", 0, false).pos == std::wstring::npos,
               "find miss");
        Row("find next (miss)", "ignore case", MsSince(t), "ms");
        t = Now();
        const auto replaced = ReplaceAllInText(text, L"info", L"WARN", false);
        Row("replace all (engine)", "ignore case", MsSince(t), "ms");
        Verify(replaced.count > 0 && replaced.text.size() == text.size(), "replace all");
    }

    // --- Word wrap toggle: recreate + the frame's re-layout -------------------
    {
        LARGE_INTEGER t = Now();
        Verify(view.SetWordWrap(true), "wrap on");
        view.Layout(client);
        Row("word wrap", "toggle on", MsSince(t), "ms");
        t = Now();
        Verify(view.SetWordWrap(false), "wrap off");
        view.Layout(client);
        Row("word wrap", "toggle off", MsSince(t), "ms");
    }

    // --- Typing latency on the largest loaded doc ---------------------------
    {
        const int mid = view.TextLength() / 2;
        SendMessageW(view.Handle(), EM_SETSEL, mid, mid);

        LARGE_INTEGER t = Now();
        SendMessageW(view.Handle(), WM_CHAR, L'x', 0);
        Row("typing", "first keystroke (materialize)", MsSince(t), "ms");

        constexpr int kKeystrokes = 1000;
        std::vector<double> times;
        times.reserve(kKeystrokes);
        for (int i = 0; i < kKeystrokes; ++i) {
            t = Now();
            SendMessageW(view.Handle(), WM_CHAR, L'x', 0);
            times.push_back(MsSince(t));
        }
        std::sort(times.begin(), times.end());
        double sum = 0.0;
        for (const double v : times) {
            sum += v;
        }
        Row("typing (1000 keys)", "avg app-side", sum / kKeystrokes, "ms");
        Row("typing (1000 keys)", "p99", times[static_cast<size_t>(kKeystrokes * 0.99)], "ms");
        Row("typing (1000 keys)", "max", times.back(), "ms");
    }

    // --- Undo across units ---------------------------------------------------
    {
        // "a " cycles: each 'a' after a space opens a new unit -> 150 units,
        // capped at 100 by the manager.
        for (int i = 0; i < 150; ++i) {
            SendMessageW(view.Handle(), WM_CHAR, L'a', 0);
            SendMessageW(view.Handle(), WM_CHAR, L' ', 0);
        }
        constexpr int kUndos = 100;
        LARGE_INTEGER t = Now();
        int undone = 0;
        for (int i = 0; i < kUndos && view.CanUndo(); ++i) {
            view.Undo();
            ++undone;
        }
        const double totalMs = MsSince(t);
        Verify(undone == kUndos, "undo depth available");
        Row("undo (100 units)", "avg per undo", totalMs / kUndos, "ms");
    }

    // --- Probe: which Edit-control message is slow at this size --------------
    {
        const HWND edit = view.Handle();
        const int mid = view.TextLength() / 2;
        LARGE_INTEGER t = Now();
        SendMessageW(edit, EM_SETSEL, mid, mid);
        Row("probe", "EM_SETSEL jump to mid", MsSince(t), "ms");
        t = Now();
        SendMessageW(edit, EM_SETSEL, mid, mid + 2);
        Row("probe", "EM_SETSEL extend +2", MsSince(t), "ms");
        t = Now();
        SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
        Row("probe", "EM_REPLACESEL delete 2", MsSince(t), "ms");
        t = Now();
        SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L"ab"));
        Row("probe", "EM_REPLACESEL insert 2", MsSince(t), "ms");
        t = Now();
        SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"cd"));
        Row("probe", "EM_REPLACESEL insert 2 undo=1", MsSince(t), "ms");
        t = Now();
        SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"ef"));
        Row("probe", "EM_REPLACESEL again undo=1", MsSince(t), "ms");
        t = Now();
        SendMessageW(edit, EM_SCROLLCARET, 0, 0);
        Row("probe", "EM_SCROLLCARET", MsSince(t), "ms");
        t = Now();
        SendMessageW(edit, WM_CHAR, L'x', 0);
        Row("probe", "WM_CHAR (reference)", MsSince(t), "ms");
        SendMessageW(edit, EM_SETSEL, mid, mid + 2);
        t = Now();
        SendMessageW(edit, WM_CHAR, L'\b', 0);
        Row("probe", "WM_CHAR backspace on sel", MsSince(t), "ms");
        t = Now();
        SendMessageW(edit, WM_CHAR, L'\b', 0);
        Row("probe", "WM_CHAR backspace empty", MsSince(t), "ms");
        t = Now();
        SendMessageW(edit, WM_CHAR, L'\r', 0);
        Row("probe", "WM_CHAR enter (CRLF)", MsSince(t), "ms");
    }

    // --- JSON pretty-print throughput ----------------------------------------
    {
        std::wstring json = L"{\"items\":[";
        const std::wstring element =
            L"{\"id\":123456,\"name\":\"component-alpha\",\"ok\":true,\"values\":[1,2.5e3,null]},";
        const size_t target = 10 * 1024 * 1024;
        while (json.size() < target) {
            json += element;
        }
        json.back() = L']';  // Replace the trailing comma.
        json += L'}';
        LARGE_INTEGER t = Now();
        const auto prettied = notepadxp::lang::PrettyPrintJson(json, {false, 2});
        const double ms = MsSince(t);
        Verify(prettied.ok, "pretty-print valid input");
        Row("json pretty-print", "10 MB throughput",
            (static_cast<double>(json.size()) / (1024.0 * 1024.0)) / (ms / 1000.0), "MB/s");
        t = Now();
        const auto minified = notepadxp::lang::MinifyJson(prettied.text);
        Verify(minified.ok && minified.text == json, "minify(pretty(x)) == x");
        Row("json minify", "round-trip throughput",
            (static_cast<double>(prettied.text.size()) / (1024.0 * 1024.0)) / (MsSince(t) / 1000.0),
            "MB/s");
    }

    // --- Zoom font swap ------------------------------------------------------
    {
        LOGFONTW zoomed = font;
        zoomed.lfHeight = -26;
        LARGE_INTEGER t = Now();
        view.SetFont(zoomed);
        Row("zoom", "font swap on loaded doc", MsSince(t), "ms");
        view.SetFont(font);
    }

    // --- Correctness: the real message path round-trips -----------------------
    {
        view.SetText(L"base");
        SendMessageW(view.Handle(), EM_SETSEL, 4, 4);
        for (int i = 0; i < 20; ++i) {
            SendMessageW(view.Handle(), WM_CHAR, L'q', 0);
        }
        Verify(view.TextLength() == 24, "typed chars landed");
        while (view.CanUndo()) {
            view.Undo();
        }
        Verify(view.GetText() == L"base", "undo restores the original");
        while (view.CanRedo()) {
            view.Redo();
        }
        Verify(view.TextLength() == 24, "redo replays the typing");

        // A selection replace (the control reports delete, then insert) is
        // one undo unit, for EM_REPLACESEL and for typing over a selection.
        view.SetText(L"hello world");
        view.SelectRange(0, 5);
        view.InsertText(L"HELLO");
        SendMessageW(view.Handle(), EM_SETSEL, 6, 11);
        SendMessageW(view.Handle(), WM_CHAR, L'W', 0);
        Verify(view.GetText() == L"HELLO W", "selection replaces landed");
        view.Undo();
        Verify(view.GetText() == L"HELLO world", "one undo reverts typing over a selection");
        view.Undo();
        Verify(view.GetText() == L"hello world", "one undo reverts a replace-selection");
        Verify(!view.CanUndo(), "no stray half-units left");
    }

    g_view = nullptr;
    DestroyWindow(host);
    _Module.Term();

    std::printf("---------------------------------------------------------------------\n");
    std::printf("bench: %s (%d verification failures)\n", g_failures == 0 ? "OK" : "FAILED",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}

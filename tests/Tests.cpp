// Tests.cpp — Unit tests for NotepadXP's pure logic: text-encoding detection /
// decoding / encoding and header/footer template expansion.
//
// Built as a standalone console executable via tests/vcbuild.json. It includes
// the implementation .cpp files directly (the project builds a single GUI exe,
// so there is no separate library to link). Run: builds to tests/build, exit
// code 0 means all checks passed.

#include <array>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include "editor/DocumentShadow.cpp"
#include "editor/SearchEngine.cpp"
#include "editor/UndoManager.cpp"
#include "file/Encoding.cpp"
#include "file/FileService.cpp"
#include "file/TextFile.cpp"
#include "lang/BraceMatch.cpp"
#include "lang/CommentToggle.cpp"
#include "lang/IndentEngine.cpp"
#include "lang/JsonFormat.cpp"
#include "lang/Language.cpp"
#include "printing/HeaderFooter.cpp"
#include "util/CommandLine.cpp"
#include "util/DateTime.cpp"
#include "util/FileMapping.cpp"
#include "util/PathName.hpp"
#include "util/StringTable.cpp"  // Provides util::LoadStr referenced by HeaderFooter.

// Dialog stubs: FileService references the Open/Save dialogs, but the tests
// exercise it only through the headless paths, so these are never reached.
namespace notepadxp::dialogs {

std::optional<OpenDialogResult> EncodingFileDialog::ShowOpen(HWND /*parent*/) {
    return std::nullopt;
}

std::optional<SaveDialogResult> EncodingFileDialog::ShowSave(HWND /*parent*/,
                                                             const std::wstring& /*defaultPath*/,
                                                             file::TextEncoding /*currentEncoding*/) {
    return std::nullopt;
}

} // namespace notepadxp::dialogs

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const char* expression, int line) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL (line %d): %s\n", line, expression);
    }
}

#define CHECK(expr) Check((expr), #expr, __LINE__)

std::vector<std::byte> Bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    out.reserve(values.size());
    for (const int value : values) {
        out.push_back(static_cast<std::byte>(value));
    }
    return out;
}

const std::byte* AsBytes(const char* text) {
    return reinterpret_cast<const std::byte*>(text);
}

void TestEncoding() {
    using notepadxp::file::DecodeText;
    using notepadxp::file::DetectEncoding;
    using notepadxp::file::EncodeText;
    using notepadxp::file::IsTextUtf8;
    using notepadxp::file::TextEncoding;

    // Pure ASCII is reported NOT-UTF-8 (so the code-page path is used) and ANSI.
    CHECK(!IsTextUtf8(AsBytes("hello world"), 11));
    CHECK(DetectEncoding(AsBytes("hello world"), 11) == TextEncoding::Ansi);

    // Multi-byte UTF-8 (no BOM) is recognized.
    {
        const auto b = Bytes({0x48, 0xC3, 0xA9});  // "He" + U+00E9
        CHECK(IsTextUtf8(b.data(), b.size()));
        CHECK(DetectEncoding(b.data(), b.size()) == TextEncoding::Utf8);
    }
    // Malformed UTF-8 is rejected.
    CHECK(!IsTextUtf8(Bytes({0x80, 0x41}).data(), 2));  // Lone continuation byte.
    CHECK(!IsTextUtf8(Bytes({0xC3}).data(), 1));        // Truncated lead byte.

    // BOM-based detection.
    CHECK(DetectEncoding(Bytes({0xEF, 0xBB, 0xBF, 0x41}).data(), 4) == TextEncoding::Utf8);
    CHECK(DetectEncoding(Bytes({0xFF, 0xFE, 0x41, 0x00}).data(), 4) == TextEncoding::Utf16Le);
    CHECK(DetectEncoding(Bytes({0xFE, 0xFF, 0x00, 0x41}).data(), 4) == TextEncoding::Utf16Be);

    // Decode strips the BOM.
    {
        const auto b = Bytes({0xFF, 0xFE, 0x41, 0x00, 0x42, 0x00});
        CHECK(DecodeText(b.data(), b.size(), TextEncoding::Utf16Le) == L"AB");
    }
    {
        const auto b = Bytes({0xFE, 0xFF, 0x00, 0x41, 0x00, 0x42});
        CHECK(DecodeText(b.data(), b.size(), TextEncoding::Utf16Be) == L"AB");
    }

    // Embedded NUL becomes a space on load.
    CHECK(DecodeText(Bytes({0x41, 0x00, 0x42}).data(), 3, TextEncoding::Ansi) == L"A B");

    // Round trips: encode then decode reproduces the text.
    {
        bool lossy = true;
        const std::wstring src = L"Hello, é世界";  // includes U+00E9, CJK
        const auto enc = EncodeText(src, TextEncoding::Utf8, lossy);
        CHECK(!lossy);
        CHECK(DecodeText(enc.data(), enc.size(), TextEncoding::Utf8) == src);
    }
    {
        bool lossy = true;
        const std::wstring src = L"AbCé";
        const auto enc = EncodeText(src, TextEncoding::Utf16Le, lossy);
        CHECK(enc.size() == 2 + src.size() * 2);  // BOM + 2 bytes per code unit.
        CHECK(DecodeText(enc.data(), enc.size(), TextEncoding::Utf16Le) == src);
    }
    {
        bool lossy = true;
        const std::wstring src = L"XYé";
        const auto enc = EncodeText(src, TextEncoding::Utf16Be, lossy);
        CHECK(DecodeText(enc.data(), enc.size(), TextEncoding::Utf16Be) == src);
    }
}

void TestHeaderFooter() {
    using notepadxp::printing::AlignedText;
    using notepadxp::printing::ExpandTemplate;
    using notepadxp::printing::HeaderFooterContext;

    const std::wstring letters = L"fFpPtTdDcCrRlL";
    const HeaderFooterContext ctx{L"notes.txt", L"May 27, 2026", L"10:42 PM", 3};

    {
        const AlignedText a = ExpandTemplate(L"&f", ctx, letters);
        CHECK(a.center == L"notes.txt");
        CHECK(a.left.empty() && a.right.empty());
    }
    CHECK(ExpandTemplate(L"Page &p", ctx, letters).center == L"Page 3");
    CHECK(ExpandTemplate(L"&F", ctx, letters).center == L"notes.txt");  // Upper == lower.
    CHECK(ExpandTemplate(L"A&&B", ctx, letters).center == L"A&B");      // "&&" literal.
    CHECK(ExpandTemplate(L"&p+2", ctx, letters).center == L"5");        // Page offset.
    CHECK(ExpandTemplate(L"&d &t", ctx, letters).center == L"May 27, 2026 10:42 PM");
    {
        const AlignedText a = ExpandTemplate(L"&lleft&rright", ctx, letters);
        CHECK(a.left == L"left");
        CHECK(a.right == L"right");
        CHECK(a.center.empty());
    }
}

void TestPathName() {
    using notepadxp::util::PathLeaf;

    CHECK(PathLeaf(L"C:\\dir\\notes.txt") == L"notes.txt");
    CHECK(PathLeaf(L"C:/dir/notes.txt") == L"notes.txt");
    CHECK(PathLeaf(L"notes.txt") == L"notes.txt");
    CHECK(PathLeaf(L"C:\\dir\\") == L"");
    CHECK(PathLeaf(L"") == L"");
}

std::wstring TempFilePath(const wchar_t* name) {
    std::array<wchar_t, MAX_PATH> dir{};
    GetTempPathW(static_cast<DWORD>(dir.size()), dir.data());
    return std::wstring(dir.data()) + name;
}

void TestTextFile() {
    using notepadxp::file::EncodeText;
    using notepadxp::file::LoadStatus;
    using notepadxp::file::LoadTextFile;
    using notepadxp::file::SaveStatus;
    using notepadxp::file::SniffEncoding;
    using notepadxp::file::TextEncoding;
    using notepadxp::file::WriteAllBytes;

    const std::wstring path = TempFilePath(L"notepadxp_textfile_test.txt");

    // Round trip through each Unicode encoding: write, load with auto-detect.
    for (const TextEncoding enc :
         {TextEncoding::Utf8, TextEncoding::Utf16Le, TextEncoding::Utf16Be}) {
        bool lossy = true;
        const std::wstring src = L"Héllo\r\nWörld";
        CHECK(WriteAllBytes(path, EncodeText(src, enc, lossy)) == SaveStatus::Ok);
        const auto loaded = LoadTextFile(path, std::nullopt);
        CHECK(loaded.status == LoadStatus::Ok);
        CHECK(loaded.encoding == enc);
        CHECK(loaded.text == src);
    }

    // Plain ASCII detects as ANSI, and the sniff preview agrees with the load.
    {
        bool lossy = true;
        CHECK(WriteAllBytes(path, EncodeText(L"plain ascii", TextEncoding::Ansi, lossy)) ==
              SaveStatus::Ok);
        const auto loaded = LoadTextFile(path, std::nullopt);
        CHECK(loaded.status == LoadStatus::Ok);
        CHECK(loaded.encoding == TextEncoding::Ansi);
        CHECK(loaded.text == L"plain ascii");
        CHECK(SniffEncoding(path) == std::optional(TextEncoding::Ansi));
    }

    // A forced encoding overrides detection.
    {
        const auto loaded = LoadTextFile(path, TextEncoding::Utf16Le);
        CHECK(loaded.status == LoadStatus::Ok);
        CHECK(loaded.encoding == TextEncoding::Utf16Le);
    }

    // The sniff preview reports a BOM'd file correctly.
    {
        bool lossy = true;
        CHECK(WriteAllBytes(path, EncodeText(L"bom", TextEncoding::Utf8, lossy)) == SaveStatus::Ok);
        CHECK(SniffEncoding(path) == std::optional(TextEncoding::Utf8));
    }

    // An empty file loads as empty ANSI text.
    {
        CHECK(WriteAllBytes(path, {}) == SaveStatus::Ok);
        const auto loaded = LoadTextFile(path, std::nullopt);
        CHECK(loaded.status == LoadStatus::Ok);
        CHECK(loaded.text.empty());
        CHECK(loaded.encoding == TextEncoding::Ansi);
    }

    DeleteFileW(path.c_str());

    // Missing files and paths report NotFound; the sniff reports nothing.
    CHECK(LoadTextFile(path, std::nullopt).status == LoadStatus::NotFound);
    CHECK(LoadTextFile(TempFilePath(L"notepadxp_no_such_dir\\x.txt"), std::nullopt).status ==
          LoadStatus::NotFound);
    CHECK(SniffEncoding(path) == std::nullopt);
}

// In-memory adapter at the TextBuffer seam.
class FakeTextBuffer final : public notepadxp::file::TextBuffer {
public:
    void Reset() override {
        text.clear();
        modified = false;
    }
    void SetText(std::wstring_view t) override {
        text = t;
        modified = false;
    }
    std::wstring GetText() override {
        return text;
    }
    int TextLength() override {
        return static_cast<int>(text.size());
    }
    bool IsModified() override {
        return modified;
    }
    void SetModified(bool m) override {
        modified = m;
    }
    void MoveCaretToEnd() override {}
    void InsertText(std::wstring_view t) override {
        text += t;
        modified = true;
    }

    std::wstring text;
    bool modified = false;
};

// Scripted adapter at the FilePrompts seam.
class FakePrompts final : public notepadxp::file::FilePrompts {
public:
    SaveChoice AskSaveChanges(const std::wstring& /*documentName*/) override {
        ++saveChangesAsked;
        return saveChoice;
    }
    bool AskCreateNewFile(const std::wstring& /*fileName*/) override {
        ++createAsked;
        return createAnswer;
    }
    bool AskContinueLossySave(const std::wstring& /*fileName*/) override {
        ++lossyAsked;
        return lossyAnswer;
    }
    void ReportError(Error /*error*/, const std::wstring& /*fileName*/) override {
        ++errorsReported;
    }

    SaveChoice saveChoice = SaveChoice::Discard;
    bool createAnswer = false;
    bool lossyAnswer = true;
    int saveChangesAsked = 0;
    int createAsked = 0;
    int lossyAsked = 0;
    int errorsReported = 0;
};

void TestFileService() {
    using notepadxp::file::DocumentState;
    using notepadxp::file::EncodeText;
    using notepadxp::file::FilePrompts;
    using notepadxp::file::FileService;
    using notepadxp::file::LoadStatus;
    using notepadxp::file::LoadTextFile;
    using notepadxp::file::SaveStatus;
    using notepadxp::file::TextEncoding;
    using notepadxp::file::WriteAllBytes;
    using SaveChoice = FilePrompts::SaveChoice;

    // The prompt-to-save decision table, via CanClose().
    {
        FakeTextBuffer buffer;
        FakePrompts prompts;
        DocumentState doc;
        FileService svc(buffer, doc, prompts);

        // Untitled + empty: closes without a prompt.
        CHECK(svc.CanClose());
        CHECK(prompts.saveChangesAsked == 0);

        // Untitled with text but unmodified: still no prompt.
        buffer.text = L"x";
        buffer.modified = false;
        CHECK(svc.CanClose());
        CHECK(prompts.saveChangesAsked == 0);

        // Modified + Discard: closes, asked exactly once.
        buffer.modified = true;
        prompts.saveChoice = SaveChoice::Discard;
        CHECK(svc.CanClose());
        CHECK(prompts.saveChangesAsked == 1);

        // Modified + Cancel: refuses to close.
        prompts.saveChoice = SaveChoice::Cancel;
        CHECK(!svc.CanClose());

        // Untitled + Save falls through to Save As, whose dialog stub cancels,
        // so the close is aborted.
        prompts.saveChoice = SaveChoice::Save;
        CHECK(!svc.CanClose());

        // Titled + Save: writes to disk and closes.
        const std::wstring path = TempFilePath(L"notepadxp_filesvc_close.txt");
        doc.filePath = path;
        doc.untitled = false;
        doc.encoding = TextEncoding::Ansi;
        buffer.text = L"saved on close";
        buffer.modified = true;
        CHECK(svc.CanClose());
        CHECK(!buffer.modified);
        const auto loaded = LoadTextFile(path, std::nullopt);
        CHECK(loaded.status == LoadStatus::Ok);
        CHECK(loaded.text == L"saved on close");
        DeleteFileW(path.c_str());
    }

    // OpenPath, New, and the file-not-found create flow raise the
    // document-changed signal exactly when the identity/content changes.
    {
        FakeTextBuffer buffer;
        FakePrompts prompts;
        DocumentState doc;
        FileService svc(buffer, doc, prompts);
        int changed = 0;
        svc.SetDocumentChangedCallback([&changed] { ++changed; });

        const std::wstring path = TempFilePath(L"notepadxp_filesvc_open.txt");
        bool lossy = true;
        CHECK(WriteAllBytes(path, EncodeText(L"content", TextEncoding::Utf8, lossy)) ==
              SaveStatus::Ok);
        CHECK(svc.OpenPath(path));
        CHECK(changed == 1);
        CHECK(buffer.text == L"content");
        CHECK(!doc.untitled);
        CHECK(doc.encoding == TextEncoding::Utf8);

        CHECK(svc.New());
        CHECK(changed == 2);
        CHECK(doc.untitled);
        CHECK(buffer.text.empty());

        // Missing file, user declines the create prompt: no change, no signal.
        DeleteFileW(path.c_str());
        prompts.createAnswer = false;
        CHECK(!svc.OpenPath(path));
        CHECK(prompts.createAsked == 1);
        CHECK(changed == 2);

        // Missing file, user accepts: empty document adopts that identity.
        prompts.createAnswer = true;
        CHECK(svc.OpenPath(path));
        CHECK(prompts.createAsked == 2);
        CHECK(changed == 3);
        CHECK(!doc.untitled);
        CHECK(doc.filePath == path);
    }

    // Detection rides the load path: language + indent style land on the document.
    {
        FakeTextBuffer buffer;
        FakePrompts prompts;
        DocumentState doc;
        FileService svc(buffer, doc, prompts);
        bool lossy = true;

        const std::wstring jsonPath = TempFilePath(L"notepadxp_filesvc_detect.json");
        CHECK(WriteAllBytes(jsonPath,
                            EncodeText(L"{\"a\": [1, 2]}", TextEncoding::Utf8, lossy)) ==
              SaveStatus::Ok);
        CHECK(svc.OpenPath(jsonPath));
        CHECK(doc.language == notepadxp::lang::Language::Json);
        DeleteFileW(jsonPath.c_str());

        const std::wstring pyPath = TempFilePath(L"notepadxp_filesvc_detect.py");
        CHECK(WriteAllBytes(pyPath, EncodeText(L"def f():\n  a()\n  b()\n  c()\n",
                                               TextEncoding::Utf8, lossy)) == SaveStatus::Ok);
        CHECK(svc.OpenPath(pyPath));
        CHECK(doc.language == notepadxp::lang::Language::Python);
        CHECK(!doc.indentStyle.useTabs);
        CHECK(doc.indentStyle.width == 2);
        DeleteFileW(pyPath.c_str());

        CHECK(svc.New());
        CHECK(doc.language == notepadxp::lang::Language::PlainText);
    }

    // The lossy-save gate: decline leaves the file unwritten and the buffer dirty.
    {
        FakeTextBuffer buffer;
        FakePrompts prompts;
        DocumentState doc;
        FileService svc(buffer, doc, prompts);

        const std::wstring path = TempFilePath(L"notepadxp_filesvc_lossy.txt");
        doc.filePath = path;
        doc.untitled = false;
        doc.encoding = TextEncoding::Ansi;
        buffer.text = L"\x2603";  // Snowman: not representable in any ANSI code page.
        buffer.modified = true;

        prompts.lossyAnswer = false;
        CHECK(!svc.Save());
        CHECK(prompts.lossyAsked == 1);
        CHECK(buffer.modified);
        CHECK(LoadTextFile(path, std::nullopt).status == LoadStatus::NotFound);

        prompts.lossyAnswer = true;
        CHECK(svc.Save());
        CHECK(!buffer.modified);
        DeleteFileW(path.c_str());
    }
}

void TestSearchEngine() {
    using notepadxp::editor::FindBackward;
    using notepadxp::editor::FindForward;
    using notepadxp::editor::MatchAt;
    using notepadxp::editor::ReplaceAllInText;
    constexpr size_t npos = std::wstring::npos;

    const std::wstring text = L"the cat sat on the mat";

    CHECK(FindForward(text, L"the", 0, true) == 0);
    CHECK(FindForward(text, L"the", 1, true) == 15);
    CHECK(FindForward(text, L"the", 16, true) == npos);  // No wrap-around.
    CHECK(FindForward(text, L"THE", 0, false) == 0);     // Case-insensitive.
    CHECK(FindForward(text, L"THE", 0, true) == npos);
    CHECK(FindForward(text, L"", 0, true) == npos);      // Empty key never matches.
    CHECK(FindForward(L"ab", L"abc", 0, true) == npos);  // Key longer than text.
    CHECK(FindForward(text, L"mat", 19, true) == 19);    // Match flush at the end.

    CHECK(FindBackward(text, L"the", text.size(), true) == 15);
    CHECK(FindBackward(text, L"the", 15, true) == 0);   // Ends at/before 15.
    CHECK(FindBackward(text, L"the", 2, true) == npos); // Nothing fits before 2.
    CHECK(FindBackward(text, L"mat", text.size(), true) == 19);

    CHECK(MatchAt(text, 19, L"mat", true));
    CHECK(!MatchAt(text, 20, L"mat", true));  // Would run past the end.

    {
        const auto r = ReplaceAllInText(L"aaaa", L"aa", L"b", true);
        CHECK(r.text == L"bb");  // Non-overlapping, left to right.
        CHECK(r.count == 2);
    }
    {
        const auto r = ReplaceAllInText(L"abc", L"b", L"bb", true);
        CHECK(r.text == L"abbc");  // Replacement containing the key is not re-matched.
        CHECK(r.count == 1);
    }
    {
        const auto r = ReplaceAllInText(L"a-a-a", L"-", L"", true);
        CHECK(r.text == L"aaa");
        CHECK(r.count == 2);
    }
    {
        const auto r = ReplaceAllInText(L"Cat cat", L"cat", L"dog", false);
        CHECK(r.text == L"dog dog");
        CHECK(r.count == 2);
    }
    {
        const auto r = ReplaceAllInText(L"xyz", L"q", L"r", true);
        CHECK(r.text == L"xyz");
        CHECK(r.count == 0);
    }
}

// Delta builders for the undo tests.
notepadxp::editor::EditDelta InsDelta(size_t pos, std::wstring_view inserted, wchar_t before) {
    notepadxp::editor::EditDelta d;
    d.pos = pos;
    d.inserted = inserted;
    d.charBeforePos = before;
    d.selStartBefore = d.selEndBefore = static_cast<int>(pos);
    d.selStartAfter = d.selEndAfter = static_cast<int>(pos + inserted.size());
    return d;
}

notepadxp::editor::EditDelta DelDelta(size_t pos, std::wstring_view removed, wchar_t before) {
    notepadxp::editor::EditDelta d;
    d.pos = pos;
    d.removed = removed;
    d.charBeforePos = before;
    d.selStartBefore = d.selEndBefore = static_cast<int>(pos + removed.size());
    d.selStartAfter = d.selEndAfter = static_cast<int>(pos);
    return d;
}

void TestUndoManager() {
    using notepadxp::editor::UndoManager;

    // Typing coalesces into one unit per word; undo/redo round-trips.
    {
        UndoManager u;
        u.RecordChange(InsDelta(0, L"h", L'\0'));
        u.RecordChange(InsDelta(1, L"e", L'h'));
        u.RecordChange(InsDelta(2, L"y", L'e'));
        CHECK(u.CanUndo());
        const auto unit = u.Undo();
        CHECK(unit.has_value());
        CHECK(unit->pos == 0);
        CHECK(unit->inserted == L"hey");  // One unit for the whole word.
        CHECK(unit->removed.empty());
        CHECK(!u.CanUndo());
        CHECK(u.CanRedo());
        const auto redone = u.Redo();
        CHECK(redone.has_value());
        CHECK(redone->inserted == L"hey");
        CHECK(!u.CanRedo());
        CHECK(u.CanUndo());
    }

    // A new word after a space opens a new unit at the word start.
    {
        UndoManager u;
        u.RecordChange(InsDelta(0, L"a", L'\0'));
        u.RecordChange(InsDelta(1, L"b", L'a'));
        u.RecordChange(InsDelta(2, L" ", L'b'));
        u.RecordChange(InsDelta(3, L"c", L' '));  // Word start: new unit.
        const auto unit = u.Undo();
        CHECK(unit.has_value());
        CHECK(unit->pos == 3);
        CHECK(unit->inserted == L"c");  // Only the new word is removed.
        const auto unit2 = u.Undo();
        CHECK(unit2.has_value());
        CHECK(unit2->pos == 0);
        CHECK(unit2->inserted == L"ab ");
        CHECK(!u.CanUndo());
    }

    // A backspace run coalesces into one unit ("abc" deleted right-to-left).
    {
        UndoManager u;
        u.RecordChange(DelDelta(2, L"c", L'b'));
        u.RecordChange(DelDelta(1, L"b", L'a'));
        u.RecordChange(DelDelta(0, L"a", L'\0'));
        const auto unit = u.Undo();
        CHECK(unit.has_value());
        CHECK(unit->pos == 0);
        CHECK(unit->removed == L"abc");
        CHECK(unit->inserted.empty());
        CHECK(!u.CanUndo());
    }

    // A forward-delete run (position stays put) coalesces too.
    {
        UndoManager u;
        u.RecordChange(DelDelta(0, L"a", L'\0'));
        u.RecordChange(DelDelta(0, L"b", L'\0'));
        u.RecordChange(DelDelta(0, L"c", L'\0'));
        const auto unit = u.Undo();
        CHECK(unit.has_value());
        CHECK(unit->pos == 0);
        CHECK(unit->removed == L"abc");
        CHECK(!u.CanUndo());
    }

    // An insert after a delete run opens a new unit (kind change).
    {
        UndoManager u;
        u.RecordChange(DelDelta(0, L"a", L'\0'));
        u.RecordChange(InsDelta(0, L"b", L'\0'));
        const auto unit = u.Undo();
        CHECK(unit.has_value());
        CHECK(unit->inserted == L"b");
        CHECK(u.CanUndo());
    }

    // An edit clears the redo history.
    {
        UndoManager u;
        u.RecordChange(InsDelta(0, L"a", L'\0'));
        CHECK(u.Undo().has_value());
        CHECK(u.CanRedo());
        u.RecordChange(InsDelta(0, L"b", L'\0'));
        CHECK(!u.CanRedo());
    }

    // History depth is capped: 105 bulk units keep only the last 100.
    {
        UndoManager u;
        for (int i = 0; i < 105; ++i) {
            notepadxp::editor::EditDelta d;
            d.pos = 0;
            d.removed = L"old";
            d.inserted = L"new" + std::to_wstring(i);  // Bulk: never coalesces.
            u.RecordChange(d);
        }
        int undone = 0;
        while (u.CanUndo()) {
            CHECK(u.Undo().has_value());
            ++undone;
        }
        CHECK(undone == 100);
    }
}

void TestLanguage() {
    using notepadxp::lang::DetectLanguage;
    using notepadxp::lang::IndentStyle;
    using notepadxp::lang::Language;
    using notepadxp::lang::SniffIndentStyle;
    using notepadxp::lang::TraitsFor;

    // Extension map.
    CHECK(DetectLanguage(L"C:\\x\\data.json", L"") == Language::Json);
    CHECK(DetectLanguage(L"app.log", L"") == Language::Log);
    CHECK(DetectLanguage(L"boot.asm", L"") == Language::Asm);
    CHECK(DetectLanguage(L"start.s", L"") == Language::Asm);
    CHECK(DetectLanguage(L"main.c", L"") == Language::C);
    CHECK(DetectLanguage(L"view.cpp", L"") == Language::Cpp);
    CHECK(DetectLanguage(L"view.HPP", L"") == Language::Cpp);  // Case-insensitive.
    CHECK(DetectLanguage(L"App.java", L"") == Language::Java);
    CHECK(DetectLanguage(L"main.go", L"") == Language::Go);
    CHECK(DetectLanguage(L"tool.py", L"") == Language::Python);
    CHECK(DetectLanguage(L"build.bat", L"") == Language::Batch);
    CHECK(DetectLanguage(L"deploy.cmd", L"") == Language::Batch);
    CHECK(DetectLanguage(L"run.sh", L"") == Language::Bash);
    CHECK(DetectLanguage(L"setup.ps1", L"") == Language::PowerShell);
    CHECK(DetectLanguage(L"trace.wds", L"") == Language::WinDbg);

    // Ambiguous .h: C++ markers decide; empty defaults to C++.
    CHECK(DetectLanguage(L"api.h", L"class Widget { public: void Draw(); };") == Language::Cpp);
    CHECK(DetectLanguage(L"api.h", L"typedef struct point { int x; } point;\nint add(int a);") ==
          Language::C);
    CHECK(DetectLanguage(L"api.h", L"") == Language::Cpp);

    // Content sniff for .txt / extensionless.
    CHECK(DetectLanguage(L"script.txt", L"#!/bin/bash\necho hi") == Language::Bash);
    CHECK(DetectLanguage(L"script", L"#!/usr/bin/env python3\nprint(1)") == Language::Python);
    CHECK(DetectLanguage(L"payload", L"{\"a\": [1, 2.5e3], \"b\": null, \"c\": \"x\"}") ==
          Language::Json);
    CHECK(DetectLanguage(L"notes.txt", L"The quick brown fox\njumps over\nthe lazy dog\n") ==
          Language::PlainText);
    CHECK(DetectLanguage(L"output",
                         L"2026-07-23 10:00:01 INFO started\n"
                         L"2026-07-23 10:00:02 INFO listening\n"
                         L"2026-07-23 10:00:05 ERROR timeout\n") == Language::Log);
    CHECK(DetectLanguage(L"bp.txt",
                         L"$$ set up breakpoints\nbp nt!NtOpenFile\n.echo attached\n") ==
          Language::WinDbg);
    // C-ish braces at the start must not read as JSON.
    CHECK(DetectLanguage(L"snippet", L"{ int x = f(y); }") == Language::PlainText);

    // Traits spot checks.
    CHECK(TraitsFor(Language::Batch).lineComment == L"REM");
    CHECK(TraitsFor(Language::Batch).lineCommentAlt == L"::");
    CHECK(TraitsFor(Language::Python).indentTriggers == L":");
    CHECK(TraitsFor(Language::WinDbg).lineComment == L"$$");
    CHECK(TraitsFor(Language::Asm).tabStopChars == 8);
    CHECK(TraitsFor(Language::Cpp).tabStopChars == 4);
    CHECK(TraitsFor(Language::PlainText).displayName.empty());

    // Indent sniffing.
    const IndentStyle two =
        SniffIndentStyle(L"def f():\n  a()\n  b()\n  c()\n", Language::Python);
    CHECK(!two.useTabs);
    CHECK(two.width == 2);
    const IndentStyle tabs =
        SniffIndentStyle(L"func main() {\n\tx()\n\ty()\n\tz()\n}\n", Language::Cpp);
    CHECK(tabs.useTabs);
    const IndentStyle mixed = SniffIndentStyle(
        L"a {\n    b\n        c\n    d\n        e\n    f\n}\n", Language::Cpp);
    CHECK(!mixed.useTabs);
    CHECK(mixed.width == 4);
    // No evidence: language defaults.
    CHECK(SniffIndentStyle(L"abc\ndef\n", Language::Python).width == 4);
    CHECK(SniffIndentStyle(L"abc\ndef\n", Language::Go).useTabs);
    CHECK(SniffIndentStyle(L"", Language::Json).width == 2);
    CHECK(SniffIndentStyle(L"", Language::Json).Unit() == L"  ");
    CHECK(SniffIndentStyle(L"", Language::Go).Unit() == L"\t");
}

void TestIndentEngine() {
    using notepadxp::lang::ComputeEnterIndent;
    using notepadxp::lang::IndentStyle;
    using notepadxp::lang::Language;
    using notepadxp::lang::TraitsFor;

    const auto& cpp = TraitsFor(Language::Cpp);
    const auto& python = TraitsFor(Language::Python);
    const auto& go = TraitsFor(Language::Go);
    const auto& asm_ = TraitsFor(Language::Asm);
    const IndentStyle four{false, 4};
    const IndentStyle two{false, 2};
    const IndentStyle tabs{true, 4};

    // Copy the current indent.
    CHECK(ComputeEnterIndent(L"    int x = 1;", cpp, four) == L"    ");
    CHECK(ComputeEnterIndent(L"\t\treturn x;", cpp, tabs) == L"\t\t");
    CHECK(ComputeEnterIndent(L"no indent", cpp, four) == L"");

    // Deepen after a trigger — including with trailing whitespace.
    CHECK(ComputeEnterIndent(L"int main() {", cpp, four) == L"    ");
    CHECK(ComputeEnterIndent(L"    if (a) {", cpp, four) == L"        ");
    CHECK(ComputeEnterIndent(L"int main() {   ", cpp, four) == L"    ");
    CHECK(ComputeEnterIndent(L"def f():", python, four) == L"    ");
    CHECK(ComputeEnterIndent(L"  def g():", python, two) == L"    ");
    CHECK(ComputeEnterIndent(L"func main() {", go, tabs) == L"\t");
    CHECK(ComputeEnterIndent(L"\tif x {", go, tabs) == L"\t\t");

    // No trigger for languages without one; a bare trigger line still deepens.
    CHECK(ComputeEnterIndent(L"    mov eax, 1", asm_, four) == L"    ");
    CHECK(ComputeEnterIndent(L"{", cpp, four) == L"    ");

    // Whitespace-only lines never deepen (nothing beyond the indent).
    CHECK(ComputeEnterIndent(L"    ", cpp, four) == L"    ");
}

void TestBraceMatch() {
    using notepadxp::lang::FindMatchingBrace;
    using notepadxp::lang::Language;
    using notepadxp::lang::TraitsFor;
    const auto& cpp = TraitsFor(Language::Cpp);
    const auto& python = TraitsFor(Language::Python);
    const auto& json = TraitsFor(Language::Json);

    // Nested pairs, both directions; caret on or just after a bracket.
    const std::wstring nested = L"a{b[c(d)e]f}g";
    CHECK(FindMatchingBrace(nested, 1, cpp) == std::optional<size_t>(11));   // { -> }
    CHECK(FindMatchingBrace(nested, 11, cpp) == std::optional<size_t>(1));   // } -> {
    CHECK(FindMatchingBrace(nested, 3, cpp) == std::optional<size_t>(9));    // [ -> ]
    CHECK(FindMatchingBrace(nested, 5, cpp) == std::optional<size_t>(7));    // ( -> )
    CHECK(FindMatchingBrace(nested, 6, cpp) == std::optional<size_t>(7));    // after ( -> )
    CHECK(!FindMatchingBrace(nested, 0, cpp).has_value());                   // Not a bracket.

    // Brackets inside strings and comments never participate (C++).
    const std::wstring code = L"f(\"a}b\"); // }\n{ /* } */ x }";
    //                          01 234567    ...
    CHECK(FindMatchingBrace(code, 1, cpp) == std::optional<size_t>(7));      // ( skips "a}b"
    const size_t bracePos = code.find(L'\n') + 1;
    CHECK(FindMatchingBrace(code, bracePos, cpp) == std::optional<size_t>(code.size() - 1));
    // A bracket inside a string cannot be matched from.
    CHECK(!FindMatchingBrace(code, 4, cpp).has_value());

    // Python: # comments hide brackets, strings honored.
    const std::wstring py = L"d = {'k': [1, 2]}  # }";
    CHECK(FindMatchingBrace(py, 4, python) == std::optional<size_t>(16));
    CHECK(FindMatchingBrace(py, 10, python) == std::optional<size_t>(15));

    // JSON with escapes in strings.
    const std::wstring js = L"{\"a\": \"x\\\"}y\", \"b\": [1]}";
    CHECK(FindMatchingBrace(js, 0, json) == std::optional<size_t>(js.size() - 1));

    // Unbalanced: no match.
    CHECK(!FindMatchingBrace(L"{ open", 0, cpp).has_value());
    CHECK(!FindMatchingBrace(L"close }", 6, cpp).has_value());
}

void TestCommentToggle() {
    using notepadxp::lang::Language;
    using notepadxp::lang::ToggleLineComments;
    using notepadxp::lang::TraitsFor;
    const auto& cpp = TraitsFor(Language::Cpp);
    const auto& python = TraitsFor(Language::Python);
    const auto& asm_ = TraitsFor(Language::Asm);
    const auto& windbg = TraitsFor(Language::WinDbg);
    const auto& batch = TraitsFor(Language::Batch);
    const auto& json = TraitsFor(Language::Json);

    // Round trips per language.
    CHECK(ToggleLineComments(L"int x;", cpp) == L"// int x;");
    CHECK(ToggleLineComments(L"// int x;", cpp) == L"int x;");
    CHECK(ToggleLineComments(L"a()", python) == L"# a()");
    CHECK(ToggleLineComments(L"# a()", python) == L"a()");
    CHECK(ToggleLineComments(L"mov eax, 1", asm_) == L"; mov eax, 1");
    CHECK(ToggleLineComments(L"bp nt!Foo", windbg) == L"$$ bp nt!Foo");
    CHECK(ToggleLineComments(L"$$ bp nt!Foo", windbg) == L"bp nt!Foo");
    CHECK(ToggleLineComments(L"echo hi", batch) == L"REM echo hi");
    CHECK(ToggleLineComments(L"REM echo hi", batch) == L"echo hi");
    CHECK(ToggleLineComments(L":: echo hi", batch) == L"echo hi");   // Alt form stripped.
    CHECK(ToggleLineComments(L"rem echo hi", batch) == L"echo hi");  // Case-insensitive.
    // JSON has no line comment: unchanged.
    CHECK(ToggleLineComments(L"{\"a\": 1}", json) == L"{\"a\": 1}");

    // Multi-line block at the common minimum indent; blank lines untouched.
    CHECK(ToggleLineComments(L"    a();\r\n\r\n        b();\r\n", cpp) ==
          L"    // a();\r\n\r\n    //     b();\r\n");
    CHECK(ToggleLineComments(L"    // a();\r\n\r\n    // b();\r\n", cpp) ==
          L"    a();\r\n\r\n    b();\r\n");

    // Mixed commented/uncommented lines: comment everything.
    CHECK(ToggleLineComments(L"// a\r\nb\r\n", cpp) == L"// // a\r\n// b\r\n");

    // "REM" only counts at a word boundary.
    CHECK(ToggleLineComments(L"remark this", batch) == L"REM remark this");
}

void TestJsonFormat() {
    using notepadxp::lang::IndentStyle;
    using notepadxp::lang::MinifyJson;
    using notepadxp::lang::PrettyPrintJson;
    const IndentStyle two{false, 2};

    // Exact pretty output for a nested document.
    {
        const auto r = PrettyPrintJson(L"{\"a\":[1,{\"b\":null}],\"c\":\"x\"}", two);
        CHECK(r.ok);
        CHECK(r.text ==
              L"{\r\n"
              L"  \"a\": [\r\n"
              L"    1,\r\n"
              L"    {\r\n"
              L"      \"b\": null\r\n"
              L"    }\r\n"
              L"  ],\r\n"
              L"  \"c\": \"x\"\r\n"
              L"}");
    }

    // Minify strips everything; minify(pretty(x)) == minify(x).
    {
        const std::wstring src = L"{ \"a\" : [ 1 , 2 ] , \"b\" : true }";
        const auto mini = MinifyJson(src);
        CHECK(mini.ok);
        CHECK(mini.text == L"{\"a\":[1,2],\"b\":true}");
        const auto pretty = PrettyPrintJson(src, two);
        CHECK(pretty.ok);
        const auto again = MinifyJson(pretty.text);
        CHECK(again.ok);
        CHECK(again.text == mini.text);
    }

    // Lexemes survive byte-identically: huge numbers, exponents, \u escapes.
    {
        const auto r = MinifyJson(
            L"[1e999, 0.10000000000000000001, 123456789012345678901234567890, \"\\u00e9\\n\"]");
        CHECK(r.ok);
        CHECK(r.text ==
              L"[1e999,0.10000000000000000001,123456789012345678901234567890,\"\\u00e9\\n\"]");
    }

    // Empty containers stay inline.
    {
        const auto r = PrettyPrintJson(L"{\"a\":{},\"b\":[]}", two);
        CHECK(r.ok);
        CHECK(r.text == L"{\r\n  \"a\": {},\r\n  \"b\": []\r\n}");
    }

    // LF and CRLF inputs both work.
    CHECK(PrettyPrintJson(L"{\n\"a\": 1\n}", two).ok);
    CHECK(PrettyPrintJson(L"{\r\n\"a\": 1\r\n}", two).ok);

    // Errors report offset + line/col.
    {
        const auto r = MinifyJson(L"{\"a\":}");
        CHECK(!r.ok);
        CHECK(r.errorOffset == 5);
        CHECK(r.errorLine == 1);
        CHECK(r.errorCol == 6);
    }
    {
        const auto r = MinifyJson(L"{\r\n  \"a\": 1,,\r\n}");
        CHECK(!r.ok);
        CHECK(r.errorLine == 2);
        CHECK(r.errorCol == 10);  // Second comma, 1-based within line 2.
    }
    {
        const auto r = MinifyJson(L"{\"a\": 1} trailing");
        CHECK(!r.ok);
        CHECK(r.errorOffset == 9);
    }
    CHECK(!MinifyJson(L"").ok);
    CHECK(!MinifyJson(L"{\"a\": 1").ok);       // Truncated.
    CHECK(!MinifyJson(L"{\"a\": bare}").ok);   // Bare word value.
    CHECK(!MinifyJson(L"{1: 2}").ok);          // Non-string key.
}

notepadxp::editor::PendingChange Pending(notepadxp::editor::PendingChange::Kind kind, int selStart,
                                         int selEnd, std::wstring inserted = {}) {
    notepadxp::editor::PendingChange p;
    p.kind = kind;
    p.selStart = selStart;
    p.selEnd = selEnd;
    p.inserted = std::move(inserted);
    return p;
}

void TestDocumentShadow() {
    using notepadxp::editor::DocumentShadow;
    using Kind = notepadxp::editor::PendingChange::Kind;

    // A reader that must not be consulted on the fast path: returns a poison
    // value that would corrupt the shadow if the fallback ran.
    const std::function<std::wstring()> poison = [] { return std::wstring(L"POISON"); };

    // Unmaterialized capture: materializes, yields no delta.
    {
        DocumentShadow s;
        CHECK(!s.IsMaterialized());
        CHECK(!s.CaptureChange(5, 5, 5, [] { return std::wstring(L"hello"); }).has_value());
        CHECK(s.IsMaterialized());
        CHECK(s.Text() == L"hello");
        CHECK(s.FallbackCount() == 0);
    }

    // Typed char at the caret.
    {
        DocumentShadow s;
        s.Materialize(L"hello");
        s.SetPending(Pending(Kind::ReplaceSelection, 5, 5, L"X"));
        const auto d = s.CaptureChange(6, 6, 6, poison);
        CHECK(d.has_value());
        CHECK(d->pos == 5);
        CHECK(d->inserted == L"X");
        CHECK(d->removed.empty());
        CHECK(d->charBeforePos == L'o');
        CHECK(s.Text() == L"helloX");
        CHECK(s.FallbackCount() == 0);
    }

    // Typing over a selection.
    {
        DocumentShadow s;
        s.Materialize(L"hello");
        s.SetPending(Pending(Kind::ReplaceSelection, 0, 5, L"Hi"));
        const auto d = s.CaptureChange(2, 2, 2, poison);
        CHECK(d.has_value());
        CHECK(d->removed == L"hello");
        CHECK(d->inserted == L"Hi");
        CHECK(s.Text() == L"Hi");
    }

    // Backspace, with and without a selection.
    {
        DocumentShadow s;
        s.Materialize(L"hello");
        s.SetPending(Pending(Kind::BackspaceOne, 5, 5));
        const auto d = s.CaptureChange(4, 4, 4, poison);
        CHECK(d.has_value());
        CHECK(d->pos == 4);
        CHECK(d->removed == L"o");
        s.SetPending(Pending(Kind::BackspaceOne, 1, 3));
        const auto d2 = s.CaptureChange(2, 1, 1, poison);
        CHECK(d2.has_value());
        CHECK(d2->pos == 1);
        CHECK(d2->removed == L"el");
        CHECK(s.Text() == L"hl");
    }

    // Forward delete.
    {
        DocumentShadow s;
        s.Materialize(L"hello");
        s.SetPending(Pending(Kind::DeleteOne, 0, 0));
        const auto d = s.CaptureChange(4, 0, 0, poison);
        CHECK(d.has_value());
        CHECK(d->pos == 0);
        CHECK(d->removed == L"h");
        CHECK(s.Text() == L"ello");
    }

    // Multi-line paste over a selection.
    {
        DocumentShadow s;
        s.Materialize(L"abcdef");
        s.SetPending(Pending(Kind::ReplaceSelection, 2, 4, L"XY\r\nZ"));
        const auto d = s.CaptureChange(9, 7, 7, poison);
        CHECK(d.has_value());
        CHECK(d->removed == L"cd");
        CHECK(d->inserted == L"XY\r\nZ");
        CHECK(s.Text() == L"abXY\r\nZef");
        CHECK(s.FallbackCount() == 0);
    }

    // Arithmetic mismatch (stale prediction) resyncs via the fallback diff.
    {
        DocumentShadow s;
        s.Materialize(L"abc");
        s.SetPending(Pending(Kind::ReplaceSelection, 0, 0, L"Q"));
        const auto d = s.CaptureChange(7, 7, 7, [] { return std::wstring(L"abcdefg"); });
        CHECK(d.has_value());
        CHECK(d->pos == 3);
        CHECK(d->inserted == L"defg");
        CHECK(s.FallbackCount() == 1);
        CHECK(s.Text() == L"abcdefg");
    }

    // Unknown prediction and missing prediction both fall back.
    {
        DocumentShadow s;
        s.Materialize(L"abc");
        s.SetPending(Pending(Kind::Unknown, 0, 0));
        CHECK(s.CaptureChange(4, 4, 4, [] { return std::wstring(L"abcd"); }).has_value());
        CHECK(s.FallbackCount() == 1);
        CHECK(s.CaptureChange(5, 5, 5, [] { return std::wstring(L"abcde"); }).has_value());
        CHECK(s.FallbackCount() == 2);
        CHECK(s.Text() == L"abcde");
    }

    // Pasting text identical to the selection records nothing.
    {
        DocumentShadow s;
        s.Materialize(L"abc");
        s.SetPending(Pending(Kind::ReplaceSelection, 0, 3, L"abc"));
        CHECK(!s.CaptureChange(3, 3, 3, poison).has_value());
        CHECK(s.FallbackCount() == 0);
        CHECK(s.Text() == L"abc");
    }

    // External splices (undo/redo) and follow-tail appends keep the mirror honest.
    {
        DocumentShadow s;
        s.Materialize(L"hello world");
        s.ApplyExternal(0, 5, L"goodbye");
        CHECK(s.Text() == L"goodbye world");
        s.Append(L"!!");
        CHECK(s.Text() == L"goodbye world!!");
        s.Reset();
        CHECK(!s.IsMaterialized());
        CHECK(s.Length() == 0);
    }
}

void TestCommandLine() {
    using notepadxp::util::ParseCommandLine;

    {
        const auto p = ParseCommandLine(L"notepad.exe");
        CHECK(!p.filePath.has_value());
        CHECK(!p.forceAnsi);
        CHECK(!p.forceUnicode);
    }
    {
        const auto p = ParseCommandLine(L"notepad.exe /A file.txt");
        CHECK(p.forceAnsi);
        CHECK(!p.forceUnicode);
        CHECK(p.filePath == L"file.txt");
    }
    {
        const auto p = ParseCommandLine(L"notepad.exe -w \"C:\\My Dir\\notes.txt\"");
        CHECK(p.forceUnicode);
        CHECK(p.filePath == L"C:\\My Dir\\notes.txt");
    }
    {
        // First non-switch token wins; later tokens are ignored.
        const auto p = ParseCommandLine(L"notepad.exe one.txt two.txt");
        CHECK(p.filePath == L"one.txt");
    }
}

} // namespace

int main() {
    TestEncoding();
    TestHeaderFooter();
    TestPathName();
    TestTextFile();
    TestFileService();
    TestSearchEngine();
    TestUndoManager();
    TestDocumentShadow();
    TestCommandLine();
    TestLanguage();
    TestIndentEngine();
    TestBraceMatch();
    TestCommentToggle();
    TestJsonFormat();
    std::printf("notepadxp tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

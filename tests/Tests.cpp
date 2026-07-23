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

#include "file/Encoding.cpp"
#include "file/FileService.cpp"
#include "file/TextFile.cpp"
#include "printing/HeaderFooter.cpp"
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

} // namespace

int main() {
    TestEncoding();
    TestHeaderFooter();
    TestPathName();
    TestTextFile();
    TestFileService();
    std::printf("notepadxp tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

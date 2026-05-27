// Tests.cpp — Unit tests for NotepadXP's pure logic: text-encoding detection /
// decoding / encoding and header/footer template expansion.
//
// Built as a standalone console executable via tests/vcbuild.json. It includes
// the implementation .cpp files directly (the project builds a single GUI exe,
// so there is no separate library to link). Run: builds to tests/build, exit
// code 0 means all checks passed.

#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include "file/Encoding.cpp"
#include "printing/HeaderFooter.cpp"
#include "util/StringTable.cpp"  // Provides util::LoadStr referenced by HeaderFooter.

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

} // namespace

int main() {
    TestEncoding();
    TestHeaderFooter();
    std::printf("notepadxp tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

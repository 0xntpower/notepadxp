#include "file/Encoding.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "util/WinLean.hpp"

namespace notepadxp::file {

namespace {

// Byte-order marks, as raw bytes on disk.
constexpr std::byte kUtf16LeBom0{0xFF};
constexpr std::byte kUtf16LeBom1{0xFE};
constexpr std::byte kUtf16BeBom0{0xFE};
constexpr std::byte kUtf16BeBom1{0xFF};
constexpr std::byte kUtf8Bom0{0xEF};
constexpr std::byte kUtf8Bom1{0xBB};
constexpr std::byte kUtf8Bom2{0xBF};
constexpr size_t kUtf8BomLength = 3;
constexpr size_t kUtf16BomLength = 2;

constexpr uint8_t kHighBit = 0x80;
constexpr uint8_t kContinuationMask = 0xC0;
constexpr uint8_t kContinuationTag = 0x80;

// Reinterpret the byte buffer as the char/wchar the Win32 NLS APIs expect.
// The bytes are raw file content being parsed; we only read through the result.
const char* AsChars(const std::byte* data) {
    return reinterpret_cast<const char*>(data);
}

// Replace embedded NUL code units with spaces, in place.
void NullsToSpaces(std::wstring& text) {
    for (wchar_t& ch : text) {
        if (ch == L'\0') {
            ch = L' ';
        }
    }
}

std::wstring DecodeCodePage(const std::byte* data, size_t size, UINT codePage) {
    if (size == 0) {
        return std::wstring{};
    }
    const int byteCount = static_cast<int>(size);
    const int wideCount =
        MultiByteToWideChar(codePage, 0, AsChars(data), byteCount, nullptr, 0);
    if (wideCount <= 0) {
        return std::wstring{};
    }
    std::wstring text(static_cast<size_t>(wideCount), L'\0');
    MultiByteToWideChar(codePage, 0, AsChars(data), byteCount, text.data(), wideCount);
    NullsToSpaces(text);
    return text;
}

std::wstring DecodeUtf16(const std::byte* data, size_t size, bool bigEndian) {
    const size_t count = size / sizeof(wchar_t);
    std::wstring text(count, L'\0');
    for (size_t i = 0; i < count; ++i) {
        const auto low = static_cast<uint16_t>(data[i * 2]);
        const auto high = static_cast<uint16_t>(data[i * 2 + 1]);
        const uint16_t unit = bigEndian ? static_cast<uint16_t>((low << 8) | high)
                                        : static_cast<uint16_t>((high << 8) | low);
        text[i] = static_cast<wchar_t>(unit);
    }
    NullsToSpaces(text);
    return text;
}

bool HasUtf8Bom(const std::byte* data, size_t size) {
    return size >= kUtf8BomLength && data[0] == kUtf8Bom0 && data[1] == kUtf8Bom1 &&
           data[2] == kUtf8Bom2;
}

bool HasUtf16LeBom(const std::byte* data, size_t size) {
    return size >= kUtf16BomLength && data[0] == kUtf16LeBom0 && data[1] == kUtf16LeBom1;
}

bool HasUtf16BeBom(const std::byte* data, size_t size) {
    return size >= kUtf16BomLength && data[0] == kUtf16BeBom0 && data[1] == kUtf16BeBom1;
}

} // namespace

bool IsTextUtf8(const std::byte* data, size_t size) {
    // Validate the UTF-8 lead/continuation structure; "all ASCII" is reported
    // as NOT UTF-8 so the code-page path wins.
    DWORD octetsToGo = 0;
    bool allAscii = true;

    for (size_t i = 0; i < size; ++i) {
        auto ch = static_cast<uint8_t>(data[i]);
        if ((ch & kHighBit) != 0) {
            allAscii = false;
        }

        if (octetsToGo == 0) {
            if (ch >= kHighBit) {
                // The number of leading 1-bits is the encoded length.
                do {
                    ch = static_cast<uint8_t>(ch << 1);
                    ++octetsToGo;
                } while ((ch & kHighBit) != 0);

                --octetsToGo;  // The count includes the lead byte itself.
                if (octetsToGo == 0) {
                    return false;  // Lead byte must be 11xxxxxx, not 10xxxxxx.
                }
            }
        } else {
            if ((ch & kContinuationMask) != kContinuationTag) {
                return false;  // Continuation bytes must be 10xxxxxx.
            }
            --octetsToGo;
        }
    }

    if (octetsToGo > 0) {
        return false;  // Truncated multi-byte sequence at end of buffer.
    }
    return !allAscii;
}

TextEncoding DetectEncoding(const std::byte* data, size_t size) {
    if (size <= 1) {
        return TextEncoding::Ansi;
    }
    if (HasUtf16LeBom(data, size)) {
        return TextEncoding::Utf16Le;
    }
    if (HasUtf16BeBom(data, size)) {
        return TextEncoding::Utf16Be;
    }
    if (HasUtf8Bom(data, size)) {
        return TextEncoding::Utf8;
    }
    // No BOM: trust the OS Unicode test first, then the UTF-8 heuristic.
    int unicodeTests = ~0;  // Request all IsTextUnicode tests.
    if (IsTextUnicode(data, static_cast<int>(size), &unicodeTests)) {
        return TextEncoding::Utf16Le;
    }
    if (IsTextUtf8(data, size)) {
        return TextEncoding::Utf8;
    }
    return TextEncoding::Ansi;
}

std::wstring DecodeText(const std::byte* data, size_t size, TextEncoding encoding) {
    switch (encoding) {
        case TextEncoding::Utf16Le: {
            const bool bom = HasUtf16LeBom(data, size);
            return DecodeUtf16(data + (bom ? kUtf16BomLength : 0),
                               size - (bom ? kUtf16BomLength : 0), false);
        }
        case TextEncoding::Utf16Be: {
            const bool bom = HasUtf16BeBom(data, size);
            return DecodeUtf16(data + (bom ? kUtf16BomLength : 0),
                               size - (bom ? kUtf16BomLength : 0), true);
        }
        case TextEncoding::Utf8: {
            const bool bom = HasUtf8Bom(data, size);
            return DecodeCodePage(data + (bom ? kUtf8BomLength : 0),
                                  size - (bom ? kUtf8BomLength : 0), CP_UTF8);
        }
        case TextEncoding::Ansi:
        default:
            return DecodeCodePage(data, size, CP_ACP);
    }
}

std::vector<std::byte> EncodeText(std::wstring_view text, TextEncoding encoding, bool& lossy) {
    lossy = false;
    std::vector<std::byte> out;

    switch (encoding) {
        case TextEncoding::Utf16Le: {
            out.push_back(kUtf16LeBom0);
            out.push_back(kUtf16LeBom1);
            for (const wchar_t ch : text) {
                const auto unit = static_cast<uint16_t>(ch);
                out.push_back(static_cast<std::byte>(unit & 0xFF));
                out.push_back(static_cast<std::byte>((unit >> 8) & 0xFF));
            }
            return out;
        }
        case TextEncoding::Utf16Be: {
            out.push_back(kUtf16BeBom0);
            out.push_back(kUtf16BeBom1);
            for (const wchar_t ch : text) {
                const auto unit = static_cast<uint16_t>(ch);
                out.push_back(static_cast<std::byte>((unit >> 8) & 0xFF));
                out.push_back(static_cast<std::byte>(unit & 0xFF));
            }
            return out;
        }
        case TextEncoding::Utf8: {
            out.push_back(kUtf8Bom0);
            out.push_back(kUtf8Bom1);
            out.push_back(kUtf8Bom2);
            if (!text.empty()) {
                const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                                       static_cast<int>(text.size()), nullptr, 0,
                                                       nullptr, nullptr);
                if (needed > 0) {
                    const size_t base = out.size();
                    out.resize(base + static_cast<size_t>(needed));
                    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                        reinterpret_cast<char*>(out.data() + base), needed, nullptr,
                                        nullptr);
                    // reinterpret_cast: writing the converted bytes into our buffer.
                }
            }
            return out;
        }
        case TextEncoding::Ansi:
        default: {
            if (text.empty()) {
                return out;
            }
            const int len = static_cast<int>(text.size());

            // Output bytes use best-fit mapping (dwFlags == 0), matching what the
            // classic Notepad writes once the user accepts a lossy save.
            const int needed =
                WideCharToMultiByte(CP_ACP, 0, text.data(), len, nullptr, 0, nullptr, nullptr);
            if (needed > 0) {
                out.resize(static_cast<size_t>(needed));
                WideCharToMultiByte(CP_ACP, 0, text.data(), len,
                                    reinterpret_cast<char*>(out.data()), needed, nullptr, nullptr);
                // reinterpret_cast: writing the converted bytes into our buffer.
            }

            // Detect data loss with a strict (no best-fit) pass: WC_NO_BEST_FIT_CHARS
            // makes unrepresentable characters set usedDefault.
            BOOL usedDefault = FALSE;
            const int strictNeeded = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, text.data(),
                                                         len, nullptr, 0, nullptr, nullptr);
            if (strictNeeded > 0) {
                std::vector<char> scratch(static_cast<size_t>(strictNeeded));
                WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, text.data(), len, scratch.data(),
                                    strictNeeded, nullptr, &usedDefault);
            }
            lossy = usedDefault != FALSE;
            return out;
        }
    }
}

} // namespace notepadxp::file

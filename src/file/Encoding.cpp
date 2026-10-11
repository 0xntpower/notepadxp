#include "file/Encoding.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>  // _byteswap_ushort.
#include <cstring>
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

// Replace embedded NUL code units with spaces, in place. The search is
// vectorized, so the usual no-NUL file costs one fast scan and no writes.
void NullsToSpaces(std::wstring& text) {
    for (size_t i = text.find(L'\0'); i != std::wstring::npos; i = text.find(L'\0', i + 1)) {
        text[i] = L' ';
    }
}

std::wstring DecodeCodePage(const std::byte* data, size_t size, UINT codePage) {
    // UTF-8 and every ANSI code page decode to at most one UTF-16 unit per
    // byte, so the byte count bounds the output: convert once, then trim.
    const int byteCount = static_cast<int>(size);
    std::wstring text(size, L'\0');
    const int wideCount = size == 0 ? 0
                                    : MultiByteToWideChar(codePage, 0, AsChars(data), byteCount,
                                                          text.data(), byteCount);
    text.resize(wideCount > 0 ? static_cast<size_t>(wideCount) : 0);
    NullsToSpaces(text);
    return text;
}

std::wstring DecodeUtf16(const std::byte* data, size_t size, bool bigEndian) {
    // x64 is little-endian: LE is a straight copy, BE a per-unit byte swap.
    std::wstring text(size / sizeof(wchar_t), L'\0');
    std::memcpy(text.data(), data, text.size() * sizeof(wchar_t));
    if (bigEndian) {
        for (wchar_t& unit : text) {
            unit = static_cast<wchar_t>(_byteswap_ushort(static_cast<unsigned short>(unit)));
        }
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
        if (octetsToGo == 0) {
            // Between sequences, skip 8 ASCII bytes at a time (no high bit set).
            constexpr uint64_t kHighBits = 0x8080808080808080ULL;
            while (i + sizeof(uint64_t) <= size) {
                uint64_t chunk = 0;
                std::memcpy(&chunk, data + i, sizeof(chunk));
                if ((chunk & kHighBits) != 0) {
                    break;
                }
                i += sizeof(chunk);
            }
            if (i == size) {
                break;
            }
        }
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

size_t BomLength(const std::byte* data, size_t size, TextEncoding encoding) {
    switch (encoding) {
        case TextEncoding::Utf16Le:
            return HasUtf16LeBom(data, size) ? kUtf16BomLength : 0;
        case TextEncoding::Utf16Be:
            return HasUtf16BeBom(data, size) ? kUtf16BomLength : 0;
        case TextEncoding::Utf8:
            return HasUtf8Bom(data, size) ? kUtf8BomLength : 0;
        case TextEncoding::Ansi:
        default:
            return 0;
    }
}

size_t DecodeChunk(const std::byte* data, size_t available, size_t maxBytes,
                   TextEncoding encoding, std::wstring& sink) {
    size_t bytes = maxBytes < available ? maxBytes : available;
    switch (encoding) {
        case TextEncoding::Utf16Le:
        case TextEncoding::Utf16Be: {
            bytes &= ~size_t{1};  // Never split a UTF-16 code unit.
            if (bytes == 0) {
                // A single trailing byte cannot form a code unit; consume it
                // (emitting nothing), matching DecodeUtf16's floor division.
                return available > 0 ? 1 : 0;
            }
            const size_t units = bytes / sizeof(wchar_t);
            const size_t base = sink.size();
            sink.resize(base + units);
            wchar_t* out = sink.data() + base;
            if (encoding == TextEncoding::Utf16Le) {
                std::memcpy(out, data, bytes);
            } else {
                const auto* src = reinterpret_cast<const unsigned short*>(data);
                for (size_t i = 0; i < units; ++i) {
                    out[i] = static_cast<wchar_t>(_byteswap_ushort(src[i]));
                }
            }
            return bytes;
        }
        case TextEncoding::Utf8:
        case TextEncoding::Ansi:
        default: {
            if (bytes == 0) {
                return 0;
            }
            if (bytes < available) {
                // Keep the final multi-byte character whole. Back off over
                // UTF-8 continuation bytes, then over a lead byte left at the
                // boundary (its sequence is split across chunks), or over a
                // trailing DBCS lead byte. The one-byte floor guarantees
                // progress; sequences span at most 4 bytes, so chunk sizes
                // >= 4 never hit it.
                if (encoding == TextEncoding::Utf8) {
                    while (bytes > 0 &&
                           (static_cast<uint8_t>(data[bytes - 1]) & kContinuationMask) ==
                               kContinuationTag) {
                        --bytes;
                    }
                    if (bytes > 0 && static_cast<uint8_t>(data[bytes - 1]) >= kHighBit) {
                        --bytes;  // Lead byte at the boundary: split sequence.
                    }
                    if (bytes == 0) {
                        bytes = 1;
                    }
                } else if (bytes > 1 && IsDBCSLeadByte(static_cast<BYTE>(data[bytes - 1]))) {
                    --bytes;  // DBCS lead byte at the boundary: split pair.
                }
            }
            const size_t base = sink.size();
            sink.resize(base + bytes);  // At most one wchar per input byte.
            const int wide = MultiByteToWideChar(
                encoding == TextEncoding::Utf8 ? CP_UTF8 : CP_ACP, 0, AsChars(data),
                static_cast<int>(bytes), sink.data() + base, static_cast<int>(bytes));
            sink.resize(base + (wide > 0 ? static_cast<size_t>(wide) : 0));
            return bytes;
        }
    }
}

std::vector<std::byte> EncodeChunk(std::wstring_view text, TextEncoding encoding, bool& lossy) {
    lossy = false;
    std::vector<std::byte> out;

    switch (encoding) {
        case TextEncoding::Utf16Le:
        case TextEncoding::Utf16Be: {
            // x64 is little-endian: LE is a straight copy, BE a per-unit swap.
            const bool bigEndian = encoding == TextEncoding::Utf16Be;
            out.resize(text.size() * sizeof(wchar_t));
            std::byte* units = out.data();
            if (bigEndian) {
                for (size_t i = 0; i < text.size(); ++i) {
                    const unsigned short unit =
                        _byteswap_ushort(static_cast<unsigned short>(text[i]));
                    std::memcpy(units + i * sizeof(unit), &unit, sizeof(unit));
                }
            } else {
                std::memcpy(units, text.data(), text.size() * sizeof(wchar_t));
            }
            return out;
        }
        case TextEncoding::Utf8: {
            if (!text.empty()) {
                const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                                       static_cast<int>(text.size()), nullptr, 0,
                                                       nullptr, nullptr);
                if (needed > 0) {
                    out.resize(static_cast<size_t>(needed));
                    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                        reinterpret_cast<char*>(out.data()), needed, nullptr,
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

            // Size for best-fit output (dwFlags == 0), what classic Notepad writes
            // once the user accepts a lossy save.
            const int needed =
                WideCharToMultiByte(CP_ACP, 0, text.data(), len, nullptr, 0, nullptr, nullptr);
            if (needed <= 0) {
                return out;
            }
            out.resize(static_cast<size_t>(needed));
            // reinterpret_cast: writing the converted bytes into our buffer.
            auto* bytes = reinterpret_cast<char*>(out.data());

            // Strict pass first (WC_NO_BEST_FIT_CHARS sets usedDefault on any
            // unrepresentable character). When nothing was lost its bytes are
            // exactly the best-fit bytes, so the common case is one pass.
            BOOL usedDefault = FALSE;
            int written = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, text.data(), len, bytes,
                                              needed, nullptr, &usedDefault);
            if (written <= 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                usedDefault = TRUE;  // Only default-char substitutions outgrow best-fit.
            }
            if (written <= 0 || usedDefault != FALSE) {
                // Lossy, or a code page that rejects the strict flags (UTF-8 ACP).
                written = WideCharToMultiByte(CP_ACP, 0, text.data(), len, bytes, needed,
                                              nullptr, nullptr);
            }
            out.resize(written > 0 ? static_cast<size_t>(written) : 0);
            lossy = usedDefault != FALSE;
            return out;
        }
    }
}

std::vector<std::byte> EncodeText(std::wstring_view text, TextEncoding encoding, bool& lossy) {
    bool chunkLossy = false;
    std::vector<std::byte> body = EncodeChunk(text, encoding, chunkLossy);

    std::vector<std::byte> out;
    switch (encoding) {
        case TextEncoding::Utf16Le:
        case TextEncoding::Utf16Be:
            out.reserve(kUtf16BomLength + body.size());
            out.push_back(encoding == TextEncoding::Utf16Be ? kUtf16BeBom0 : kUtf16LeBom0);
            out.push_back(encoding == TextEncoding::Utf16Be ? kUtf16BeBom1 : kUtf16LeBom1);
            break;
        case TextEncoding::Utf8:
            out.reserve(kUtf8BomLength + body.size());
            out.push_back(kUtf8Bom0);
            out.push_back(kUtf8Bom1);
            out.push_back(kUtf8Bom2);
            break;
        case TextEncoding::Ansi:
        default:
            break;
    }
    out.insert(out.end(), body.begin(), body.end());
    lossy = chunkLossy;
    return out;
}

} // namespace notepadxp::file

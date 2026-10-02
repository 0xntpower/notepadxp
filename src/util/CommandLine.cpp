#include "util/CommandLine.hpp"

#include <memory>
#include <string>
#include <string_view>

#include "util/WinLean.hpp"

#include <shellapi.h>

namespace notepadxp::util {

namespace {

// `letter` is given in lowercase. Matches either case (e.g. "/a" or "/A").
bool IsSwitch(std::wstring_view arg, wchar_t letter) {
    constexpr wchar_t kCaseBit = L'a' - L'A';
    return arg.size() == 2 && (arg[0] == L'/' || arg[0] == L'-') &&
           (arg[1] == letter || arg[1] == static_cast<wchar_t>(letter - kCaseBit));
}

// Whether anything follows the program name. The name is either quoted (no
// escapes inside, as for argv[0]) or ends at the first blank.
bool HasArguments(std::wstring_view commandLine) {
    const size_t nameEnd = commandLine.starts_with(L'"') ? commandLine.find(L'"', 1)
                                                         : commandLine.find_first_of(L" \t");
    return nameEnd != std::wstring_view::npos &&
           commandLine.find_first_not_of(L" \t", nameEnd + 1) != std::wstring_view::npos;
}

} // namespace

ParsedCommandLine ParseCommandLine(const wchar_t* commandLine) {
    ParsedCommandLine parsed;
    if (commandLine == nullptr || !HasArguments(commandLine)) {
        return parsed;  // A bare launch never loads shell32 (delay-loaded).
    }

    int argc = 0;
    // CommandLineToArgvW allocates with LocalAlloc; free with LocalFree.
    const std::unique_ptr<LPWSTR, decltype(&LocalFree)> argv(
        CommandLineToArgvW(commandLine, &argc), &LocalFree);
    if (!argv) {
        return parsed;
    }

    // argv[0] is the program name; scan the rest.
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg = argv.get()[i];
        if (IsSwitch(arg, L'a')) {
            parsed.forceAnsi = true;
        } else if (IsSwitch(arg, L'w')) {
            parsed.forceUnicode = true;
        } else if (!parsed.filePath.has_value()) {
            // First non-switch token is the file to open.
            parsed.filePath = std::wstring(arg);
        }
    }
    return parsed;
}

} // namespace notepadxp::util

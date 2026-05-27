#pragma once

// Settings.hpp — Persisted application preferences (font, word wrap, window
// placement, print margins, header/footer), stored under HKCU\Software\NotepadXP.

#include <string>

#include "util/WinLean.hpp"

namespace notepadxp::settings {

/// @brief In-memory snapshot of all persisted preferences.
///
/// This is a plain data record: load it once at startup with Load(), apply it
/// to the UI, mutate fields as the user changes preferences, and persist with
/// Save() on shutdown. Value names and defaults mirror classic Notepad,
/// but under our own registry key.
/// @threadsafety Not thread-safe; owned by the UI thread.
struct Settings {
    // Font. lfHeight is intentionally NOT persisted; it is recomputed from
    // pointSize for the target device via ResolvedFont().
    LOGFONTW font{};
    int pointSize = 0;  // Tenths of a point (100 == 10pt). Set by Load().

    bool wordWrap = false;
    bool statusBar = false;

    // Window placement. CW_USEDEFAULT sentinels mean "let Windows choose".
    int windowX = CW_USEDEFAULT;
    int windowY = CW_USEDEFAULT;
    int windowWidth = CW_USEDEFAULT;
    int windowHeight = CW_USEDEFAULT;

    // Print margins, in the user locale's native unit (thousandths of an inch
    // for US measurement, hundredths of a millimetre for metric).
    int marginLeft = 0;
    int marginTop = 0;
    int marginRight = 0;
    int marginBottom = 0;

    // Print header/footer templates (see printing/HeaderFooter for the codes).
    std::wstring header;
    std::wstring footer;

    /// @brief Load preferences from the registry, applying defaults for any that
    ///        are absent. Always returns a fully-populated, usable instance.
    [[nodiscard]] static Settings Load();

    /// @brief Persist the current values to the registry. Best-effort; failures
    ///        (e.g. no write access) are silently ignored.
    void Save() const;

    /// @brief Produce a LOGFONTW for @p dc with lfHeight derived from pointSize,
    ///        suitable for CreateFontIndirect. @p dc supplies the device DPI.
    [[nodiscard]] LOGFONTW ResolvedFont(HDC dc) const;
};

} // namespace notepadxp::settings

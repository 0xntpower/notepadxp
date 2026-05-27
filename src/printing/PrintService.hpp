#pragma once

// PrintService.hpp — Printing the document (File > Print).

#include "WtlIncludes.hpp"
#include "editor/EditView.hpp"
#include "file/DocumentState.hpp"
#include "settings/Settings.hpp"

namespace notepadxp::printing {

/// @brief Prints the edit view's text, paginating with DrawTextEx and drawing
///        the header/footer from the persisted templates and margins.
/// @threadsafety Not thread-safe; UI thread only.
class PrintService final {
public:
    PrintService(editor::EditView& editView, settings::Settings& settings,
                 file::DocumentState& document) noexcept
        : editView_(editView), settings_(settings), document_(document) {}

    PrintService(const PrintService&) = delete;
    PrintService& operator=(const PrintService&) = delete;

    /// @brief Show the Print dialog and print the document. Shows a modeless
    ///        "Now Printing" abort dialog while the job runs.
    void Print(HWND parent);

private:
    editor::EditView& editView_;
    settings::Settings& settings_;
    file::DocumentState& document_;
};

} // namespace notepadxp::printing

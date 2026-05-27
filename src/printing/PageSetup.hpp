#pragma once

// PageSetup.hpp — The Page Setup dialog and persistence of its values.

#include "WtlIncludes.hpp"
#include "settings/Settings.hpp"

namespace notepadxp::printing {

/// @brief Shows the common Page Setup dialog (with the header/footer fields)
///        and writes the chosen margins and header/footer back into Settings.
/// @threadsafety Not thread-safe; UI thread only.
class PageSetup final {
public:
    explicit PageSetup(settings::Settings& settings) noexcept : settings_(settings) {}

    PageSetup(const PageSetup&) = delete;
    PageSetup& operator=(const PageSetup&) = delete;

    /// @brief Show the dialog; on OK, update the settings.
    void ShowDialog(HWND parent);

private:
    settings::Settings& settings_;
};

} // namespace notepadxp::printing

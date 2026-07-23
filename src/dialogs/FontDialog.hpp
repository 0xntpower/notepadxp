#pragma once

// FontDialog.hpp — Modal font chooser (Format > Font).

#include <optional>

#include "util/WinLean.hpp"

namespace notepadxp::dialogs {

/// @brief The common font-chooser dialog behind a one-method interface,
///        mirroring GoToDialog.
class FontDialog final {
public:
    /// @brief A chosen font plus its size in tenths of a point (100 == 10pt).
    struct Result {
        LOGFONTW font{};
        int pointSize = 0;
    };

    /// @brief Show the chooser seeded with @p current (lfHeight already resolved
    ///        for the display). @return the choice, or nullopt if cancelled.
    [[nodiscard]] static std::optional<Result> Choose(HWND parent, const LOGFONTW& current);
};

} // namespace notepadxp::dialogs

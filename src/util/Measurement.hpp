#pragma once

// Measurement.hpp — The user locale's measurement system. One module owns this
// convention; margin defaults (Settings), Page Setup and printing all consume it.

namespace notepadxp::util {

/// @brief True when the user's locale uses the US measurement system (inches);
///        false for metric. Margins are stored in the locale-native unit:
///        thousandths of an inch (US) or hundredths of a millimetre (metric).
[[nodiscard]] bool UsesUsMeasurement();

} // namespace notepadxp::util

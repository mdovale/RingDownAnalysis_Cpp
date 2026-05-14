#pragma once

#include <string_view>

namespace ringdown {

/**
 * @brief Returns the library version string.
 *
 * @return Version identifier for this C++ port.
 */
[[nodiscard]] std::string_view version() noexcept;

} // namespace ringdown

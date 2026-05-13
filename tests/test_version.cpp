#include "test_support.hpp"

#include <ringdown/version.hpp>

RINGDOWN_TEST(version_is_available) {
  ringdown::test::require(!ringdown::version().empty(), "version string must not be empty");
}

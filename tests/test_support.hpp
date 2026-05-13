#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace ringdown::test {

using TestFn = void (*)();

bool register_test(std::string_view name, TestFn run);

class AssertionFailure : public std::runtime_error {
public:
  explicit AssertionFailure(const std::string& message) : std::runtime_error(message) {}
};

inline void require(bool condition, std::string_view message) {
  if (!condition) {
    throw AssertionFailure(std::string{message});
  }
}

} // namespace ringdown::test

#define RINGDOWN_TEST(name)                                                                    \
  void name();                                                                                 \
  namespace {                                                                                  \
  const bool name##_registered = ::ringdown::test::register_test(#name, &name);                 \
  }                                                                                            \
  void name()

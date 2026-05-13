#include <exception>
#include <functional>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

namespace ringdown::test {

using TestFn = void (*)();

struct TestCase {
  std::string_view name;
  TestFn run;
};

std::vector<TestCase>& registry() {
  static auto tests = std::vector<TestCase>{};
  return tests;
}

bool register_test(std::string_view name, TestFn run) {
  registry().push_back(TestCase{name, run});
  return true;
}

} // namespace ringdown::test

int main() {
  auto failures = 0;

  for (const auto& test : ringdown::test::registry()) {
    try {
      test.run();
      std::cout << "[PASS] " << test.name << '\n';
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
    } catch (...) {
      ++failures;
      std::cerr << "[FAIL] " << test.name << ": unknown exception\n";
    }
  }

  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }

  return 0;
}

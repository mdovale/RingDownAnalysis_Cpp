#include <ringdown/version.hpp>

#include <chrono>
#include <iostream>

int main() {
  const auto start = std::chrono::steady_clock::now();
  volatile auto size = ringdown::version().size();
  const auto stop = std::chrono::steady_clock::now();

  const auto elapsed =
      std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
  std::cout << "version_size=" << size << " elapsed_ns=" << elapsed << '\n';
  return 0;
}

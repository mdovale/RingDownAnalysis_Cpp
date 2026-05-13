#include <ringdown/ringdown.hpp>

#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv) {
  try {
    if (argc == 3 && std::string_view{argv[1]} == "analyze") {
      const auto result = ringdown::RingDownAnalyzer{}.analyze_file(argv[2]);
      std::cout << ringdown::to_json(result);
      return 0;
    }

    if (argc == 2 && std::string_view{argv[1]} == "monte-carlo-smoke") {
      auto options = ringdown::MonteCarloOptions{};
      options.signal.sample_count = 512U;
      options.trial_count = 4U;
      const auto result = ringdown::MonteCarloAnalyzer{}.run(options);
      std::cout << "nls_trials=" << result.nls_frequency_errors.size()
                << " dft_trials=" << result.dft_frequency_errors.size() << '\n';
      return 0;
    }

    std::cout << "RingDownAnalysisCpp " << ringdown::version() << '\n'
              << "Usage:\n"
              << "  ringdown_cli analyze <file.csv>\n"
              << "  ringdown_cli monte-carlo-smoke\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ringdown_cli: " << error.what() << '\n';
    return 1;
  }
}

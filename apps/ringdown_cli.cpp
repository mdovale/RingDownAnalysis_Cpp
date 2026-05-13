#include <ringdown/ringdown.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
  try {
    if (argc == 3 && std::string_view{argv[1]} == "analyze") {
      const auto result = ringdown::RingDownAnalyzer{}.analyze_file(argv[2]);
      std::cout << ringdown::to_json(result);
      return 0;
    }

    if (argc >= 3 && std::string_view{argv[1]} == "batch") {
      auto files = std::vector<std::string>{};
      files.reserve(static_cast<std::size_t>(argc - 2));
      for (auto index = 2; index < argc; ++index) {
        files.emplace_back(argv[index]);
      }
      auto analyzer = ringdown::BatchRingDownAnalyzer{};
      const auto result = analyzer.process_files(files);
      std::cout << ringdown::to_json(result);
      return result.has_failures() ? 1 : 0;
    }

    if (argc >= 2 && std::string_view{argv[1]} == "monte-carlo") {
      auto options = ringdown::MonteCarloOptions{};
      if (argc >= 3) {
        options.trial_count = static_cast<std::size_t>(std::stoull(argv[2]));
      }
      if (argc >= 4) {
        options.signal.sample_count = static_cast<std::size_t>(std::stoull(argv[3]));
      }
      if (argc >= 5) {
        options.worker_count = static_cast<std::size_t>(std::stoull(argv[4]));
      }
      std::cout << ringdown::to_json(ringdown::MonteCarloAnalyzer{}.run(options));
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
              << "  ringdown_cli analyze <file.csv|file.mat|file.zip>\n"
              << "  ringdown_cli batch <file.csv|file.mat|file.zip>...\n"
              << "  ringdown_cli monte-carlo [trials] [samples] [workers]\n"
              << "  ringdown_cli monte-carlo-smoke\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ringdown_cli: " << error.what() << '\n';
    return 1;
  }
}

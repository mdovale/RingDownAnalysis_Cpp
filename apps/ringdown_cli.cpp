#include <ringdown/ringdown.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

class CliUsageError : public std::runtime_error {
public:
  explicit CliUsageError(const std::string& message) : std::runtime_error(message) {}
};

void print_usage(std::ostream& out) {
  out << "RingDownAnalysisCpp " << ringdown::version() << '\n'
      << "Usage:\n"
      << "  ringdown analyze [--json] [--max-file-size-gb <n>|--no-file-size-limit] "
         "<file.csv|file.mat|file.zip>\n"
      << "  ringdown batch [--json] [--workers <n>] [--progress] [--fail-on-error]\n"
      << "                 [--report minimal|full] [--notebook-report]\n"
      << "                 [--file-list <paths.txt>] <path|directory|@paths.txt>...\n"
      << "  ringdown monte-carlo [--trials <n>] [--samples <n>] [--workers <n>]\n"
      << "                       [--seed <n>] [--frequency-hz <hz>] [--sample-rate-hz <hz>]\n"
      << "                       [--snr-db <db>] [--quality-factor <q>] [--amplitude <a>]\n"
      << "  ringdown monte-carlo-smoke\n"
      << "\n"
      << "The ringdown_cli binary is kept as a compatibility alias for ringdown.\n";
}

[[nodiscard]] bool starts_with(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] std::string option_value_error(std::string_view option) {
  return "missing value for " + std::string{option};
}

[[nodiscard]] bool consume_option_value(std::string_view arg,
                                        std::string_view option,
                                        int argc,
                                        char** argv,
                                        int& index,
                                        std::string& value) {
  if (arg == option) {
    if (index + 1 >= argc) {
      throw CliUsageError{option_value_error(option)};
    }
    value = argv[++index];
    return true;
  }

  const auto prefix = std::string{option} + '=';
  if (starts_with(arg, prefix)) {
    value = std::string{arg.substr(prefix.size())};
    if (value.empty()) {
      throw CliUsageError{option_value_error(option)};
    }
    return true;
  }
  return false;
}

[[nodiscard]] std::size_t parse_size(std::string_view text, std::string_view option) {
  auto parsed = std::size_t{0};
  auto value = std::stoull(std::string{text}, &parsed);
  if (parsed != text.size()) {
    throw CliUsageError{"invalid integer for " + std::string{option} + ": " + std::string{text}};
  }
  return static_cast<std::size_t>(value);
}

[[nodiscard]] unsigned long long parse_seed(std::string_view text, std::string_view option) {
  auto parsed = std::size_t{0};
  auto value = std::stoull(std::string{text}, &parsed);
  if (parsed != text.size()) {
    throw CliUsageError{"invalid integer for " + std::string{option} + ": " + std::string{text}};
  }
  return value;
}

[[nodiscard]] double parse_double(std::string_view text, std::string_view option) {
  auto parsed = std::size_t{0};
  const auto value = std::stod(std::string{text}, &parsed);
  if (parsed != text.size() || !std::isfinite(value)) {
    throw CliUsageError{"invalid finite number for " + std::string{option} + ": " + std::string{text}};
  }
  return value;
}

[[nodiscard]] std::optional<std::uintmax_t> parse_max_file_size_gb(std::string_view text) {
  const auto gib = parse_double(text, "--max-file-size-gb");
  if (gib < 0.0) {
    throw CliUsageError{"--max-file-size-gb must be non-negative"};
  }
  return static_cast<std::uintmax_t>(gib * static_cast<double>(1024ULL * 1024ULL * 1024ULL));
}

[[nodiscard]] std::string lower_ascii(std::string value) {
  for (auto& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

[[nodiscard]] bool is_supported_data_file(const std::filesystem::path& path) {
  const auto extension = lower_ascii(path.extension().string());
  return extension == ".csv" || extension == ".mat" || extension == ".zip";
}

[[nodiscard]] std::vector<std::filesystem::path> discover_data_files(const std::filesystem::path& dir) {
  auto paths = std::vector<std::filesystem::path>{};
  for (const auto& entry : std::filesystem::directory_iterator{dir}) {
    if (entry.is_regular_file() && is_supported_data_file(entry.path())) {
      paths.push_back(entry.path());
    }
  }
  std::sort(paths.begin(), paths.end());
  return paths;
}

[[nodiscard]] std::string trim(std::string_view text) {
  auto first = std::size_t{0};
  while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first])) != 0) {
    ++first;
  }
  auto last = text.size();
  while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1U])) != 0) {
    --last;
  }
  return std::string{text.substr(first, last - first)};
}

void add_input_path(std::vector<std::string>& files, const std::filesystem::path& path);

void add_file_list(std::vector<std::string>& files, const std::filesystem::path& list_path) {
  auto input = std::ifstream{list_path};
  if (!input) {
    throw std::runtime_error{"could not open file list: " + list_path.string()};
  }

  auto line = std::string{};
  while (std::getline(input, line)) {
    const auto item = trim(line);
    if (item.empty() || starts_with(item, "#")) {
      continue;
    }
    add_input_path(files, item);
  }
}

void add_input_path(std::vector<std::string>& files, const std::filesystem::path& path) {
  const auto text = path.string();
  if (starts_with(text, "@")) {
    add_file_list(files, text.substr(1U));
    return;
  }
  if (std::filesystem::is_directory(path)) {
    for (const auto& file : discover_data_files(path)) {
      files.push_back(file.string());
    }
    return;
  }
  files.push_back(text);
}

[[nodiscard]] ringdown::BatchProgressCallback make_progress_callback(bool enabled) {
  if (!enabled) {
    return {};
  }
  auto progress_mutex = std::make_shared<std::mutex>();
  return [progress_mutex](const ringdown::BatchProgressEvent& event) {
    auto lock = std::lock_guard<std::mutex>{*progress_mutex};
    std::cerr << "[batch] " << event.stage << " " << (event.index + 1U) << "/" << event.total
              << " elapsed_ms=" << event.elapsed.count() << " path=" << event.filepath;
    if (!event.message.empty()) {
      std::cerr << " message=" << event.message;
    }
    std::cerr << '\n';
  };
}

[[nodiscard]] int run_analyze(int argc, char** argv) {
  auto file = std::optional<std::string>{};
  auto max_file_size_bytes =
      std::optional<std::uintmax_t>{ringdown::RingDownDataLoader::default_max_file_size_bytes};

  for (auto index = 2; index < argc; ++index) {
    const auto arg = std::string_view{argv[index]};
    auto value = std::string{};
    if (arg == "--help" || arg == "-h") {
      print_usage(std::cout);
      return 0;
    }
    if (arg == "--json") {
      continue;
    }
    if (consume_option_value(arg, "--max-file-size-gb", argc, argv, index, value)) {
      max_file_size_bytes = parse_max_file_size_gb(value);
      continue;
    }
    if (arg == "--no-file-size-limit") {
      max_file_size_bytes = std::nullopt;
      continue;
    }
    if (starts_with(arg, "-")) {
      throw CliUsageError{"unknown analyze option: " + std::string{arg}};
    }
    if (file.has_value()) {
      throw CliUsageError{"analyze accepts exactly one input file"};
    }
    file = std::string{arg};
  }

  if (!file.has_value()) {
    throw CliUsageError{"analyze requires an input file"};
  }

  const auto analyzer = ringdown::RingDownAnalyzer{
      ringdown::NLSFrequencyEstimator{}, ringdown::DFTFrequencyEstimator{}, max_file_size_bytes};
  std::cout << ringdown::to_json(analyzer.analyze_file(*file));
  return 0;
}

[[nodiscard]] int run_batch(int argc, char** argv) {
  auto files = std::vector<std::string>{};
  auto process_options = ringdown::BatchProcessOptions{};
  auto export_options = ringdown::BatchExportOptions{};
  export_options.report.include_notebook_results = false;
  auto fail_on_error = false;
  auto progress_enabled = false;
  auto max_file_size_bytes =
      std::optional<std::uintmax_t>{ringdown::RingDownDataLoader::default_max_file_size_bytes};

  for (auto index = 2; index < argc; ++index) {
    const auto arg = std::string_view{argv[index]};
    auto value = std::string{};
    if (arg == "--help" || arg == "-h") {
      print_usage(std::cout);
      return 0;
    }
    if (arg == "--json") {
      continue;
    }
    if (consume_option_value(arg, "--workers", argc, argv, index, value)) {
      process_options.worker_count = parse_size(value, "--workers");
      continue;
    }
    if (arg == "--progress") {
      progress_enabled = true;
      continue;
    }
    if (arg == "--fail-on-error") {
      fail_on_error = true;
      continue;
    }
    if (consume_option_value(arg, "--report", argc, argv, index, value)) {
      if (value == "minimal") {
        export_options.mode = ringdown::BatchExportMode::minimal;
      } else if (value == "full") {
        export_options.mode = ringdown::BatchExportMode::full;
      } else {
        throw CliUsageError{"--report must be minimal or full"};
      }
      continue;
    }
    if (arg == "--notebook-report") {
      export_options.mode = ringdown::BatchExportMode::full;
      export_options.report.include_notebook_results = true;
      continue;
    }
    if (consume_option_value(arg, "--file-list", argc, argv, index, value)) {
      add_file_list(files, value);
      continue;
    }
    if (consume_option_value(arg, "--max-file-size-gb", argc, argv, index, value)) {
      max_file_size_bytes = parse_max_file_size_gb(value);
      continue;
    }
    if (arg == "--no-file-size-limit") {
      max_file_size_bytes = std::nullopt;
      continue;
    }
    if (starts_with(arg, "-")) {
      throw CliUsageError{"unknown batch option: " + std::string{arg}};
    }
    add_input_path(files, std::string{arg});
  }

  if (files.empty()) {
    throw CliUsageError{"batch requires at least one input path, directory, or file list"};
  }

  process_options.progress = make_progress_callback(progress_enabled);
  auto analyzer = ringdown::BatchRingDownAnalyzer{ringdown::RingDownAnalyzer{
      ringdown::NLSFrequencyEstimator{}, ringdown::DFTFrequencyEstimator{}, max_file_size_bytes}};
  const auto result = analyzer.process_files(files, process_options);
  std::cout << ringdown::to_json_batch_export(analyzer, result, export_options);
  if (result.has_failures()) {
    std::cerr << "ringdown: warning: " << result.failed_files.size() << " file(s) failed\n";
  }
  return result.has_failures() && fail_on_error ? 1 : 0;
}

[[nodiscard]] int run_monte_carlo(int argc, char** argv) {
  auto options = ringdown::MonteCarloOptions{};
  auto positional = std::vector<std::string>{};

  for (auto index = 2; index < argc; ++index) {
    const auto arg = std::string_view{argv[index]};
    auto value = std::string{};
    if (arg == "--help" || arg == "-h") {
      print_usage(std::cout);
      return 0;
    }
    if (arg == "--json") {
      continue;
    }
    if (consume_option_value(arg, "--trials", argc, argv, index, value)) {
      options.trial_count = parse_size(value, "--trials");
      continue;
    }
    if (consume_option_value(arg, "--samples", argc, argv, index, value) ||
        consume_option_value(arg, "--sample-count", argc, argv, index, value)) {
      options.signal.sample_count = parse_size(value, "--samples");
      continue;
    }
    if (consume_option_value(arg, "--workers", argc, argv, index, value)) {
      options.worker_count = parse_size(value, "--workers");
      continue;
    }
    if (consume_option_value(arg, "--seed", argc, argv, index, value)) {
      options.seed = parse_seed(value, "--seed");
      continue;
    }
    if (consume_option_value(arg, "--frequency-hz", argc, argv, index, value) ||
        consume_option_value(arg, "--frequency", argc, argv, index, value)) {
      options.signal.frequency_hz = parse_double(value, "--frequency-hz");
      continue;
    }
    if (consume_option_value(arg, "--sample-rate-hz", argc, argv, index, value) ||
        consume_option_value(arg, "--sample-rate", argc, argv, index, value)) {
      options.signal.sample_rate_hz = parse_double(value, "--sample-rate-hz");
      continue;
    }
    if (consume_option_value(arg, "--snr-db", argc, argv, index, value) ||
        consume_option_value(arg, "--snr", argc, argv, index, value)) {
      options.signal.snr_db = parse_double(value, "--snr-db");
      continue;
    }
    if (consume_option_value(arg, "--quality-factor", argc, argv, index, value) ||
        consume_option_value(arg, "--q", argc, argv, index, value)) {
      options.signal.quality_factor = parse_double(value, "--quality-factor");
      continue;
    }
    if (consume_option_value(arg, "--amplitude", argc, argv, index, value)) {
      options.signal.amplitude = parse_double(value, "--amplitude");
      continue;
    }
    if (starts_with(arg, "-")) {
      throw CliUsageError{"unknown monte-carlo option: " + std::string{arg}};
    }
    positional.push_back(std::string{arg});
  }

  if (positional.size() > 3U) {
    throw CliUsageError{"monte-carlo accepts at most three legacy positional values"};
  }
  if (!positional.empty()) {
    options.trial_count = parse_size(positional[0], "trials");
  }
  if (positional.size() >= 2U) {
    options.signal.sample_count = parse_size(positional[1], "samples");
  }
  if (positional.size() >= 3U) {
    options.worker_count = parse_size(positional[2], "workers");
  }

  std::cout << ringdown::to_json(ringdown::MonteCarloAnalyzer{}.run(options));
  return 0;
}

[[nodiscard]] int run_monte_carlo_smoke() {
  auto options = ringdown::MonteCarloOptions{};
  options.signal.sample_count = 512U;
  options.trial_count = 4U;
  const auto result = ringdown::MonteCarloAnalyzer{}.run(options);
  std::cout << "nls_trials=" << result.nls_frequency_errors.size()
            << " dft_trials=" << result.dft_frequency_errors.size() << '\n';
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2 || std::string_view{argv[1]} == "--help" || std::string_view{argv[1]} == "-h") {
      print_usage(std::cout);
      return 0;
    }

    const auto command = std::string_view{argv[1]};
    if (command == "analyze") {
      return run_analyze(argc, argv);
    }
    if (command == "batch") {
      return run_batch(argc, argv);
    }
    if (command == "monte-carlo") {
      return run_monte_carlo(argc, argv);
    }
    if (command == "monte-carlo-smoke") {
      return run_monte_carlo_smoke();
    }

    throw CliUsageError{"unknown command: " + std::string{command}};
  } catch (const CliUsageError& error) {
    std::cerr << "ringdown: " << error.what() << '\n';
    print_usage(std::cerr);
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "ringdown: " << error.what() << '\n';
    return 1;
  }
}

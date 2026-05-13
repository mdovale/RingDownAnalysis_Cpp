#include <ringdown/ringdown.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::string getenv_string(const char* key) {
  if (const char* value = std::getenv(key)) {
    return std::string{value};
  }
  return {};
}

[[nodiscard]] std::string json_escaped_string(const std::string& value) {
  auto out = std::ostringstream{};
  out << '"';
  for (const auto ch : value) {
    if (ch == '"' || ch == '\\') {
      out << '\\' << ch;
    } else if (ch == '\n') {
      out << "\\n";
    } else if (ch == '\r') {
      out << "\\r";
    } else if (ch == '\t') {
      out << "\\t";
    } else {
      out << ch;
    }
  }
  out << '"';
  return out.str();
}

void write_text(const std::filesystem::path& path, const std::string& text) {
  auto parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  auto file = std::ofstream{path};
  if (!file) {
    throw std::runtime_error{"Could not write file: " + path.string()};
  }
  file << text;
}

[[nodiscard]] std::vector<std::filesystem::path> sorted_paths_with_suffix(
    const std::filesystem::path& dir, const char* suffix) {
  auto paths = std::vector<std::filesystem::path>{};
  if (!std::filesystem::exists(dir)) {
    return paths;
  }
  for (const auto& entry : std::filesystem::directory_iterator{dir}) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto ext = entry.path().extension().string();
    if (ext == suffix) {
      paths.push_back(entry.path());
    }
  }
  std::sort(paths.begin(), paths.end());
  return paths;
}

[[nodiscard]] std::filesystem::path default_data_directory() {
  const auto env = getenv_string("RINGDOWN_EXAMPLES_DATA");
  if (!env.empty()) {
    return env;
  }
  const auto candidates = std::vector<std::filesystem::path>{
      std::filesystem::path{".read-only/RingDownAnalysis/data"},
      std::filesystem::path{"../.read-only/RingDownAnalysis/data"},
      std::filesystem::path{"../../.read-only/RingDownAnalysis/data"},
  };
  for (const auto& candidate : candidates) {
    if (std::filesystem::exists(candidate)) {
      return std::filesystem::weakly_canonical(candidate);
    }
  }
  return candidates.front();
}

} // namespace

int main(int argc, char** argv) {
  try {
    auto output_dir = std::filesystem::path{"results/examples/batch_analysis_cpp"};
    auto data_dir = default_data_directory();
    auto worker_count = std::size_t{1U};
    auto max_files = std::optional<std::size_t>{};
    auto fail_on_file_error = false;

    for (auto index = 1; index < argc; ++index) {
      const auto arg = std::string_view{argv[index]};
      if (arg == "--output-dir" && index + 1 < argc) {
        output_dir = argv[++index];
      } else if (arg == "--data-dir" && index + 1 < argc) {
        data_dir = argv[++index];
      } else if (arg == "--workers" && index + 1 < argc) {
        worker_count = static_cast<std::size_t>(std::stoull(argv[++index]));
      } else if (arg == "--max-files" && index + 1 < argc) {
        max_files = static_cast<std::size_t>(std::stoull(argv[++index]));
      } else if (arg == "--fail-on-file-error") {
        fail_on_file_error = true;
      } else if (arg == "--help" || arg == "-h") {
        std::cout << "Usage: batch_analysis_example [options]\n"
                  << "  --output-dir <path>  default: results/examples/batch_analysis_cpp\n"
                  << "  --data-dir <path>    default: .read-only/RingDownAnalysis/data or "
                     "RINGDOWN_EXAMPLES_DATA\n"
                  << "  --workers <n>        default: 1\n"
                  << "  --max-files <n>      optional cap after sorting (CSV then MAT)\n"
                  << "  --fail-on-file-error return nonzero when any file fails\n";
        return 0;
      }
    }

    const auto env_out = getenv_string("RINGDOWN_EXAMPLES_OUTPUT");
    if (!env_out.empty()) {
      output_dir = env_out;
    }

    auto csv_paths = sorted_paths_with_suffix(data_dir, ".csv");
    auto mat_paths = sorted_paths_with_suffix(data_dir, ".mat");
    auto filepaths = std::vector<std::string>{};
    filepaths.reserve(csv_paths.size() + mat_paths.size());
    for (const auto& path : csv_paths) {
      filepaths.push_back(path.string());
    }
    for (const auto& path : mat_paths) {
      filepaths.push_back(path.string());
    }
    if (max_files.has_value() && filepaths.size() > *max_files) {
      filepaths.resize(*max_files);
    }

    auto batch = ringdown::BatchRingDownAnalyzer{};
    const auto processed = batch.process_files(filepaths, worker_count);
    const auto report = ringdown::to_json_batch_report(batch, processed);

    write_text(output_dir / "batch_report.json", report);
    write_text(output_dir / "file_list.json", ringdown::to_json(processed));

    auto meta = std::ostringstream{};
    meta << "{\n";
    meta << "  \"data_dir\": " << json_escaped_string(data_dir.generic_string()) << ",\n";
    meta << "  \"worker_count\": " << worker_count << ",\n";
    if (max_files.has_value()) {
      meta << "  \"max_files\": " << *max_files << ",\n";
    }
    meta << "  \"file_count\": " << filepaths.size() << ",\n";
    meta << "  \"success_count\": " << processed.results.size() << ",\n";
    meta << "  \"failure_count\": " << processed.failed_files.size() << "\n";
    meta << "}\n";
    write_text(output_dir / "meta.json", meta.str());

    std::cout << "Wrote batch analysis artifacts under " << output_dir.string() << '\n';
    if (processed.has_failures()) {
      std::cout << "Warning: " << processed.failed_files.size() << " file(s) failed.\n";
      if (fail_on_file_error) {
        return 1;
      }
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "batch_analysis_example: " << error.what() << '\n';
    return 1;
  }
}

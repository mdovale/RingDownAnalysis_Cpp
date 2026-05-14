#include <ringdown/ringdown.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
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

[[nodiscard]] std::string_view build_type() {
#ifdef NDEBUG
  return "Release";
#else
  return "Debug";
#endif
}

[[nodiscard]] std::string_view compiler_name() {
#if defined(__clang__)
  return "Clang";
#elif defined(__GNUC__)
  return "GCC";
#elif defined(_MSC_VER)
  return "MSVC";
#else
  return "unknown";
#endif
}

[[nodiscard]] double elapsed_milliseconds(std::chrono::steady_clock::time_point start) {
  const auto elapsed = std::chrono::steady_clock::now() - start;
  return std::chrono::duration<double, std::milli>{elapsed}.count();
}

[[nodiscard]] std::string command_line(int argc, char** argv) {
  auto out = std::ostringstream{};
  for (auto index = 0; index < argc; ++index) {
    if (index != 0) {
      out << ' ';
    }
    out << argv[index];
  }
  return out.str();
}

[[nodiscard]] std::string json_number(double value) {
  if (!std::isfinite(value)) {
    return "null";
  }
  auto out = std::ostringstream{};
  out << std::setprecision(17) << value;
  return out.str();
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
      std::filesystem::path{"../RingDownAnalysis/data"},
      std::filesystem::path{"../../RingDownAnalysis/data"},
      std::filesystem::path{"../../../RingDownAnalysis/data"},
      std::filesystem::path{"../../../../RingDownAnalysis/data"},
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
    auto progress_enabled = !getenv_string("RINGDOWN_BATCH_PROGRESS").empty();
    auto notebook_report = !getenv_string("RINGDOWN_BATCH_NOTEBOOK_REPORT").empty();
    auto max_file_size_bytes =
        std::optional<std::uintmax_t>{ringdown::RingDownDataLoader::default_max_file_size_bytes};

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
      } else if (arg == "--max-file-size-gb" && index + 1 < argc) {
        const auto gib = std::stod(argv[++index]);
        if (!std::isfinite(gib) || gib < 0.0) {
          throw std::invalid_argument{"--max-file-size-gb must be finite and non-negative"};
        }
        max_file_size_bytes = static_cast<std::uintmax_t>(
            gib * static_cast<double>(1024ULL * 1024ULL * 1024ULL));
      } else if (arg == "--no-file-size-limit") {
        max_file_size_bytes = std::nullopt;
      } else if (arg == "--fail-on-file-error" || arg == "--fail-on-error") {
        fail_on_file_error = true;
      } else if (arg == "--progress") {
        progress_enabled = true;
      } else if (arg == "--notebook-report") {
        notebook_report = true;
      } else if (arg == "--help" || arg == "-h") {
        std::cout << "Usage: batch_analysis_example [options]\n"
                  << "  --output-dir <path>  default: results/examples/batch_analysis_cpp\n"
                  << "  --data-dir <path>    default: ../RingDownAnalysis/data or "
                     "RINGDOWN_EXAMPLES_DATA\n"
                  << "  --workers <n>        default: 1\n"
                  << "  --max-files <n>      optional cap after sorting (CSV, MAT, then ZIP)\n"
                  << "  --max-file-size-gb <n> default: 1; safety cap before parsing\n"
                  << "  --no-file-size-limit disable the input file-size safety cap\n"
                  << "  --fail-on-error      return nonzero when any file fails\n"
                  << "  --progress           print per-file timing to stderr\n"
                  << "  --notebook-report    include raw waveform arrays in batch_report.json\n";
        return 0;
      }
    }

    const auto env_out = getenv_string("RINGDOWN_EXAMPLES_OUTPUT");
    if (!env_out.empty()) {
      output_dir = env_out;
    }

    auto csv_paths = sorted_paths_with_suffix(data_dir, ".csv");
    auto mat_paths = sorted_paths_with_suffix(data_dir, ".mat");
    auto zip_paths = sorted_paths_with_suffix(data_dir, ".zip");
    auto filepaths = std::vector<std::string>{};
    filepaths.reserve(csv_paths.size() + mat_paths.size() + zip_paths.size());
    for (const auto& path : csv_paths) {
      filepaths.push_back(path.string());
    }
    for (const auto& path : mat_paths) {
      filepaths.push_back(path.string());
    }
    for (const auto& path : zip_paths) {
      filepaths.push_back(path.string());
    }
    if (max_files.has_value() && filepaths.size() > *max_files) {
      filepaths.resize(*max_files);
    }

    auto batch = ringdown::BatchRingDownAnalyzer{ringdown::RingDownAnalyzer{
        ringdown::NLSFrequencyEstimator{}, ringdown::DFTFrequencyEstimator{}, max_file_size_bytes}};
    auto progress_mutex = std::mutex{};
    auto progress = ringdown::BatchProgressCallback{};
    if (progress_enabled) {
      progress = [&](const ringdown::BatchProgressEvent& event) {
        auto lock = std::lock_guard<std::mutex>{progress_mutex};
        std::cerr << "[batch] " << event.stage << " " << (event.index + 1U) << "/" << event.total
                  << " elapsed_ms=" << event.elapsed.count() << " path=" << event.filepath;
        if (!event.message.empty()) {
          std::cerr << " message=" << event.message;
        }
        std::cerr << '\n';
      };
    }

#ifndef NDEBUG
    std::cerr << "Warning: Debug build timings are not production performance data.\n";
#endif

    const auto total_start = std::chrono::steady_clock::now();
    const auto process_start = std::chrono::steady_clock::now();
    const auto processed = batch.process_files(filepaths, ringdown::BatchProcessOptions{worker_count, progress});
    const auto process_ms = elapsed_milliseconds(process_start);

    const auto report_start = std::chrono::steady_clock::now();
    const auto report = ringdown::to_json_batch_report(
        batch, processed, ringdown::BatchReportOptions{notebook_report});
    const auto report_json_ms = elapsed_milliseconds(report_start);

    const auto file_list_start = std::chrono::steady_clock::now();
    const auto file_list = ringdown::to_json(processed);
    const auto file_list_json_ms = elapsed_milliseconds(file_list_start);

    const auto batch_report_write_start = std::chrono::steady_clock::now();
    write_text(output_dir / "batch_report.json", report);
    const auto batch_report_write_ms = elapsed_milliseconds(batch_report_write_start);
    const auto file_list_write_start = std::chrono::steady_clock::now();
    write_text(output_dir / "file_list.json", file_list);
    const auto file_list_write_ms = elapsed_milliseconds(file_list_write_start);
    const auto total_ms = elapsed_milliseconds(total_start);

    auto meta = std::ostringstream{};
    meta << "{\n";
    meta << "  \"data_dir\": " << json_escaped_string(data_dir.generic_string()) << ",\n";
    meta << "  \"build_type\": " << json_escaped_string(std::string{build_type()}) << ",\n";
    meta << "  \"ndebug\": ";
#ifdef NDEBUG
    meta << "true,\n";
#else
    meta << "false,\n";
#endif
    meta << "  \"compiler\": " << json_escaped_string(std::string{compiler_name()}) << ",\n";
    meta << "  \"command_line\": " << json_escaped_string(command_line(argc, argv)) << ",\n";
    meta << "  \"worker_count\": " << worker_count << ",\n";
    meta << "  \"progress_enabled\": " << (progress_enabled ? "true" : "false") << ",\n";
    meta << "  \"notebook_report\": " << (notebook_report ? "true" : "false") << ",\n";
    meta << "  \"max_file_size_bytes\": ";
    if (max_file_size_bytes.has_value()) {
      meta << *max_file_size_bytes;
    } else {
      meta << "null";
    }
    meta << ",\n";
    if (max_files.has_value()) {
      meta << "  \"max_files\": " << *max_files << ",\n";
    }
    meta << "  \"file_count\": " << filepaths.size() << ",\n";
    meta << "  \"success_count\": " << processed.results.size() << ",\n";
    meta << "  \"failure_count\": " << processed.failed_files.size() << ",\n";
    meta << "  \"timings_ms\": {\n";
    meta << "    \"total\": " << json_number(total_ms) << ",\n";
    meta << "    \"process_files\": " << json_number(process_ms) << ",\n";
    meta << "    \"batch_report_json\": " << json_number(report_json_ms) << ",\n";
    meta << "    \"file_list_json\": " << json_number(file_list_json_ms) << ",\n";
    meta << "    \"batch_report_write\": " << json_number(batch_report_write_ms) << ",\n";
    meta << "    \"file_list_write\": " << json_number(file_list_write_ms) << "\n";
    meta << "  }\n";
    meta << "}\n";
    write_text(output_dir / "meta.json", meta.str());

    std::cout << "Wrote batch analysis artifacts under " << output_dir.string() << '\n';
    std::cout << "Timing: process_files_ms=" << json_number(process_ms)
              << " batch_report_json_ms=" << json_number(report_json_ms)
              << " batch_report_write_ms=" << json_number(batch_report_write_ms)
              << " total_ms=" << json_number(total_ms)
              << " notebook_report=" << (notebook_report ? "true" : "false") << '\n';
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

#include <ringdown/ringdown.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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

[[nodiscard]] bool parse_double(const std::string& text, double& value) {
  auto stream = std::istringstream{text};
  stream >> value;
  return !stream.fail();
}

[[nodiscard]] std::vector<std::string> split_csv_line(const std::string& line) {
  auto fields = std::vector<std::string>{};
  auto field = std::string{};
  auto stream = std::istringstream{line};
  while (std::getline(stream, field, ',')) {
    fields.push_back(field);
  }
  return fields;
}

/// Load two-column CSV (time, phase) for parity with Python-exported inputs.
[[nodiscard]] std::pair<std::vector<double>, std::vector<double>> load_two_column_csv(
    const std::filesystem::path& path) {
  auto file = std::ifstream{path};
  if (!file) {
    throw std::runtime_error{"Could not open CSV: " + path.string()};
  }
  auto time = std::vector<double>{};
  auto samples = std::vector<double>{};
  auto line = std::string{};
  while (std::getline(file, line)) {
    const auto first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || line[first] == '%' || line[first] == '#') {
      continue;
    }
    const auto fields = split_csv_line(line);
    if (fields.size() < 2U) {
      continue;
    }
    auto t = 0.0;
    auto x = 0.0;
    if (!parse_double(fields[0], t) || !parse_double(fields[1], x)) {
      if (time.empty()) {
        continue;
      }
      throw std::runtime_error{"Malformed numeric row in CSV: " + path.string()};
    }
    time.push_back(t);
    samples.push_back(x);
  }
  if (time.size() < 2U) {
    throw std::runtime_error{"CSV must contain at least 2 data rows: " + path.string()};
  }
  return {time, samples};
}

} // namespace

int main(int argc, char** argv) {
  try {
    auto output_dir = std::filesystem::path{"results/examples/array_analysis_cpp"};
    auto input_csv = std::string{};
    auto max_tau_multiplier = 1.0;
    for (auto index = 1; index < argc; ++index) {
      const auto arg = std::string_view{argv[index]};
      if (arg == "--output-dir" && index + 1 < argc) {
        output_dir = argv[++index];
      } else if (arg == "--input-csv" && index + 1 < argc) {
        input_csv = argv[++index];
      } else if (arg == "--max-tau-multiplier" && index + 1 < argc) {
        max_tau_multiplier = std::stod(argv[++index]);
      } else if (arg == "--help" || arg == "-h") {
        std::cout << "Usage: array_analysis_example [options]\n"
                  << "  --output-dir <path>   default: results/examples/array_analysis_cpp\n"
                  << "  --input-csv <path>    optional shared t,data CSV (time column 0, data column 1)\n"
                  << "  --max-tau-multiplier <float>  default: 1.0\n";
        return 0;
      }
    }

    const auto env_out = getenv_string("RINGDOWN_EXAMPLES_OUTPUT");
    if (!env_out.empty()) {
      output_dir = env_out;
    }

    auto analyzer = ringdown::RingDownAnalyzer{};
    auto time = std::vector<double>{};
    auto samples = std::vector<double>{};
    auto sample_rate_hz = 0.0;
    auto meta = std::ostringstream{};
    meta << "{\n";
    meta << "  \"source\": " << (input_csv.empty() ? "\"synthetic_cpp\"" : "\"input_csv\"") << ",\n";

    if (!input_csv.empty()) {
      const auto loaded = load_two_column_csv(input_csv);
      time = loaded.first;
      samples = loaded.second;
      const auto dt = time[1] - time[0];
      sample_rate_hz = 1.0 / dt;
      meta << "  \"input_csv\": " << json_escaped_string(input_csv) << ",\n";
    } else {
      auto parameters = ringdown::SignalParameters{};
      parameters.frequency_hz = 5.0;
      parameters.sample_rate_hz = 1000.0;
      parameters.sample_count = 100'000U;
      parameters.amplitude = 0.1;
      parameters.snr_db = 50.0;
      parameters.quality_factor = 500.0;
      sample_rate_hz = parameters.sample_rate_hz;
      const auto signal = ringdown::RingDownSignal{parameters};
      const auto generated = signal.generate(std::nullopt, 42ULL, true);
      time = generated.time;
      samples = generated.samples;
      meta << "  \"f0_hz\": " << parameters.frequency_hz << ",\n";
      meta << "  \"fs_hz\": " << parameters.sample_rate_hz << ",\n";
      meta << "  \"N\": " << parameters.sample_count << ",\n";
      meta << "  \"A0\": " << parameters.amplitude << ",\n";
      meta << "  \"snr_db\": " << parameters.snr_db << ",\n";
      meta << "  \"Q\": " << parameters.quality_factor << ",\n";
      meta << "  \"rng_seed\": 42,\n";
    }

    const auto result_t_data =
        analyzer.analyze_array(time, samples, max_tau_multiplier);
    const auto result_data_fs =
        analyzer.analyze_array(samples, sample_rate_hz, max_tau_multiplier);

    meta << "  \"max_tau_multiplier\": " << max_tau_multiplier << "\n";
    meta << "}\n";

    write_text(output_dir / "meta.json", meta.str());
    write_text(output_dir / "from_t_and_data.json", ringdown::to_json_notebook(result_t_data));
    write_text(output_dir / "from_data_and_fs.json", ringdown::to_json_notebook(result_data_fs));

    auto manifest = std::ostringstream{};
    manifest << "{\n";
    manifest << "  \"meta\": \"meta.json\",\n";
    manifest << "  \"runs\": [\n";
    manifest << "    {\"label\": \"t_and_data\", \"file\": \"from_t_and_data.json\"},\n";
    manifest << "    {\"label\": \"data_and_fs\", \"file\": \"from_data_and_fs.json\"}\n";
    manifest << "  ]\n";
    manifest << "}\n";
    write_text(output_dir / "manifest.json", manifest.str());

    std::cout << "Wrote array analysis artifacts under " << output_dir.string() << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "array_analysis_example: " << error.what() << '\n';
    return 1;
  }
}

#include "test_support.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

[[nodiscard]] std::filesystem::path reference_fixture_path(const std::string& filename) {
  return std::filesystem::path{RINGDOWN_SOURCE_DIR} / "tests" / "fixtures" / "reference" / filename;
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  auto file = std::ifstream{path};
  if (!file) {
    throw std::runtime_error{"failed to open text file: " + path.string()};
  }
  auto buffer = std::ostringstream{};
  buffer << file.rdbuf();
  return buffer.str();
}

void write_text_file(const std::filesystem::path& path, const std::string& text) {
  auto file = std::ofstream{path};
  if (!file) {
    throw std::runtime_error{"failed to write text file: " + path.string()};
  }
  file << text;
}

[[nodiscard]] std::string shell_quote(const std::string& value) {
  auto out = std::string{"'"};
  for (const auto ch : value) {
    if (ch == '\'') {
      out += "'\\''";
    } else {
      out.push_back(ch);
    }
  }
  out += "'";
  return out;
}

[[nodiscard]] int run_command(const std::string& command) {
  return std::system(command.c_str());
}

} // namespace

RINGDOWN_TEST(cli_batch_directory_discovers_zip_and_honors_fail_on_error) {
  const auto workspace = std::filesystem::current_path() / "ringdown_cli_directory_test";
  std::filesystem::remove_all(workspace);
  std::filesystem::create_directories(workspace);
  const auto data_dir = workspace / "data";
  std::filesystem::create_directories(data_dir);

  std::filesystem::copy_file(reference_fixture_path("moku_small.csv"),
                             data_dir / "moku_small.csv",
                             std::filesystem::copy_options::overwrite_existing);
  write_text_file(data_dir / "bad.zip", "not a zip file");

  const auto output = workspace / "batch.json";
  const auto error = workspace / "batch.err";
  const auto command = shell_quote(RINGDOWN_APP_PATH) +
                       " batch --json --report minimal " + shell_quote(data_dir.string()) + " > " +
                       shell_quote(output.string()) + " 2> " + shell_quote(error.string());
  ringdown::test::require(run_command(command) == 0, "batch directory command should warn without failing");

  const auto json = read_text_file(output);
  ringdown::test::require(json.find("\"success_count\": 1") != std::string::npos,
                          "batch directory should analyze the CSV fixture");
  ringdown::test::require(json.find("\"failure_count\": 1") != std::string::npos,
                          "batch directory should include the invalid ZIP failure");
  ringdown::test::require(json.find("bad.zip") != std::string::npos,
                          "batch directory should scan .zip files");

  const auto fail_output = workspace / "batch_fail.json";
  const auto fail_error = workspace / "batch_fail.err";
  const auto fail_command = shell_quote(RINGDOWN_APP_PATH) +
                            " batch --json --fail-on-error --report minimal " +
                            shell_quote(data_dir.string()) + " > " + shell_quote(fail_output.string()) +
                            " 2> " + shell_quote(fail_error.string());
  ringdown::test::require(run_command(fail_command) != 0, "--fail-on-error should make batch failures nonzero");

  std::filesystem::remove_all(workspace);
}

RINGDOWN_TEST(cli_monte_carlo_flags_map_to_options) {
  const auto workspace = std::filesystem::current_path() / "ringdown_cli_monte_carlo_test";
  std::filesystem::remove_all(workspace);
  std::filesystem::create_directories(workspace);
  const auto output = workspace / "monte_carlo.json";
  const auto error = workspace / "monte_carlo.err";

  const auto command = shell_quote(RINGDOWN_APP_PATH) +
                       " monte-carlo --json --trials 1 --samples 64 --workers 1 --seed 99 "
                       "--frequency-hz 4 --sample-rate-hz 80 --snr-db 45 --quality-factor 120 "
                       "--amplitude 0.5 > " +
                       shell_quote(output.string()) + " 2> " + shell_quote(error.string());
  ringdown::test::require(run_command(command) == 0, "monte-carlo flag command should succeed");

  const auto json = read_text_file(output);
  ringdown::test::require(json.find("\"f0\": 4") != std::string::npos,
                          "monte-carlo should use --frequency-hz");
  ringdown::test::require(json.find("\"fs\": 80") != std::string::npos,
                          "monte-carlo should use --sample-rate-hz");
  ringdown::test::require(json.find("\"N\": 64") != std::string::npos,
                          "monte-carlo should use --samples");
  ringdown::test::require(json.find("\"snr_db\": 45") != std::string::npos,
                          "monte-carlo should use --snr-db");
  ringdown::test::require(json.find("\"Q\": 120") != std::string::npos,
                          "monte-carlo should use --quality-factor");

  std::filesystem::remove_all(workspace);
}

#include "test_support.hpp"

#include <ringdown/ringdown.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

namespace {

[[nodiscard]] std::string fixture_text() {
  const auto path = std::filesystem::path{RINGDOWN_SOURCE_DIR} / "tests" / "fixtures" / "reference" /
                    "synthetic_5hz_seed42.json";
  auto file = std::ifstream{path};
  if (!file) {
    throw std::runtime_error{"failed to open fixture: " + path.string()};
  }
  auto buffer = std::ostringstream{};
  buffer << file.rdbuf();
  return buffer.str();
}

[[nodiscard]] std::filesystem::path reference_fixture_path(std::string_view filename) {
  return std::filesystem::path{RINGDOWN_SOURCE_DIR} / "tests" / "fixtures" / "reference" /
         std::string{filename};
}

[[nodiscard]] std::size_t find_key(const std::string& text, std::string_view key, std::size_t start = 0U) {
  const auto needle = std::string{"\""} + std::string{key} + "\"";
  const auto position = text.find(needle, start);
  if (position == std::string::npos) {
    throw std::runtime_error{"missing fixture key: " + std::string{key}};
  }
  return position;
}

[[nodiscard]] double extract_number(const std::string& text, std::string_view key, std::size_t start = 0U) {
  const auto key_position = find_key(text, key, start);
  const auto colon = text.find(':', key_position);
  const auto first = text.find_first_of("-0123456789", colon);
  auto parsed = std::size_t{0};
  const auto value = std::stod(text.substr(first), &parsed);
  (void)parsed;
  return value;
}

[[nodiscard]] bool is_number_char(char value) {
  return (value >= '0' && value <= '9') || value == '-' || value == '+' || value == '.' || value == 'e' ||
         value == 'E';
}

[[nodiscard]] std::vector<double> extract_array(const std::string& text,
                                                std::string_view key,
                                                std::size_t start = 0U) {
  const auto key_position = find_key(text, key, start);
  const auto open = text.find('[', key_position);
  const auto close = text.find(']', open);
  auto values = std::vector<double>{};
  auto cursor = open + 1U;
  while (cursor < close) {
    cursor = text.find_first_of("-0123456789", cursor);
    if (cursor == std::string::npos || cursor >= close) {
      break;
    }
    auto end = cursor;
    while (end < close && is_number_char(text[end])) {
      ++end;
    }
    values.push_back(std::stod(text.substr(cursor, end - cursor)));
    cursor = end;
  }
  return values;
}

void require_near(double actual, double expected, double tolerance, std::string_view message) {
  if (std::abs(actual - expected) > tolerance) {
    auto out = std::ostringstream{};
    out << message << ": actual=" << actual << " expected=" << expected << " tolerance=" << tolerance;
    throw ringdown::test::AssertionFailure{out.str()};
  }
}

template <typename Function>
void require_invalid_argument(Function&& function, std::string_view message) {
  try {
    function();
  } catch (const std::invalid_argument&) {
    return;
  } catch (const std::exception& error) {
    auto out = std::ostringstream{};
    out << message << ": expected invalid_argument, got " << error.what();
    throw ringdown::test::AssertionFailure{out.str()};
  }
  throw ringdown::test::AssertionFailure{std::string{message}};
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

void append_u16(std::vector<unsigned char>& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<unsigned char>(value & 0xFFU));
  bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
}

void append_u32(std::vector<unsigned char>& bytes, std::uint32_t value) {
  bytes.push_back(static_cast<unsigned char>(value & 0xFFU));
  bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
  bytes.push_back(static_cast<unsigned char>((value >> 16U) & 0xFFU));
  bytes.push_back(static_cast<unsigned char>((value >> 24U) & 0xFFU));
}

void append_text(std::vector<unsigned char>& bytes, std::string_view text) {
  for (const auto ch : text) {
    bytes.push_back(static_cast<unsigned char>(ch));
  }
}

void write_deflated_csv_zip(const std::filesystem::path& path,
                            std::string_view entry_name,
                            std::string_view csv_text) {
  if (entry_name.size() > std::numeric_limits<std::uint16_t>::max() ||
      csv_text.size() > std::numeric_limits<std::uint32_t>::max() ||
      csv_text.size() > std::numeric_limits<uInt>::max()) {
    throw std::runtime_error{"ZIP test fixture input is too large"};
  }

  auto compressed = std::vector<unsigned char>(compressBound(static_cast<uLong>(csv_text.size())));
  auto stream = z_stream{};
  stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(csv_text.data()));
  stream.avail_in = static_cast<uInt>(csv_text.size());
  stream.next_out = compressed.data();
  stream.avail_out = static_cast<uInt>(compressed.size());
  if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) !=
      Z_OK) {
    throw std::runtime_error{"failed to initialize ZIP test fixture deflater"};
  }
  const auto compression_status = deflate(&stream, Z_FINISH);
  const auto end_status = deflateEnd(&stream);
  if (compression_status != Z_STREAM_END || end_status != Z_OK ||
      stream.total_out > std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error{"failed to deflate ZIP test fixture"};
  }
  compressed.resize(static_cast<std::size_t>(stream.total_out));

  auto crc = crc32(0L, Z_NULL, 0U);
  crc = crc32(crc, reinterpret_cast<const Bytef*>(csv_text.data()), static_cast<uInt>(csv_text.size()));

  auto bytes = std::vector<unsigned char>{};
  const auto local_header_offset = static_cast<std::uint32_t>(bytes.size());
  append_u32(bytes, 0x04034b50U);
  append_u16(bytes, 20U);
  append_u16(bytes, 0U);
  append_u16(bytes, 8U);
  append_u16(bytes, 0U);
  append_u16(bytes, 0U);
  append_u32(bytes, static_cast<std::uint32_t>(crc));
  append_u32(bytes, static_cast<std::uint32_t>(compressed.size()));
  append_u32(bytes, static_cast<std::uint32_t>(csv_text.size()));
  append_u16(bytes, static_cast<std::uint16_t>(entry_name.size()));
  append_u16(bytes, 0U);
  append_text(bytes, entry_name);
  bytes.insert(bytes.end(), compressed.begin(), compressed.end());

  const auto central_directory_offset = static_cast<std::uint32_t>(bytes.size());
  append_u32(bytes, 0x02014b50U);
  append_u16(bytes, 20U);
  append_u16(bytes, 20U);
  append_u16(bytes, 0U);
  append_u16(bytes, 8U);
  append_u16(bytes, 0U);
  append_u16(bytes, 0U);
  append_u32(bytes, static_cast<std::uint32_t>(crc));
  append_u32(bytes, static_cast<std::uint32_t>(compressed.size()));
  append_u32(bytes, static_cast<std::uint32_t>(csv_text.size()));
  append_u16(bytes, static_cast<std::uint16_t>(entry_name.size()));
  append_u16(bytes, 0U);
  append_u16(bytes, 0U);
  append_u16(bytes, 0U);
  append_u16(bytes, 0U);
  append_u32(bytes, 0U);
  append_u32(bytes, local_header_offset);
  append_text(bytes, entry_name);

  const auto central_directory_size =
      static_cast<std::uint32_t>(bytes.size() - central_directory_offset);
  append_u32(bytes, 0x06054b50U);
  append_u16(bytes, 0U);
  append_u16(bytes, 0U);
  append_u16(bytes, 1U);
  append_u16(bytes, 1U);
  append_u32(bytes, central_directory_size);
  append_u32(bytes, central_directory_offset);
  append_u16(bytes, 0U);

  auto output = std::ofstream{path, std::ios::binary};
  if (!output) {
    throw std::runtime_error{"failed to open ZIP test fixture for writing: " + path.string()};
  }
  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

RINGDOWN_TEST(crlb_matches_python_fixture) {
  const auto text = fixture_text();
  const auto crlb = find_key(text, "crlb");
  const auto inputs = find_key(text, "inputs", crlb);
  const auto variance_f = ringdown::CRLBCalculator::variance(extract_number(text, "A0", inputs),
                                                             extract_number(text, "sigma", inputs),
                                                             extract_number(text, "fs", inputs),
                                                             10000U,
                                                             extract_number(text, "tau", inputs));
  const auto std_f = ringdown::CRLBCalculator::standard_deviation(extract_number(text, "A0", inputs),
                                                                  extract_number(text, "sigma", inputs),
                                                                  extract_number(text, "fs", inputs),
                                                                  10000U,
                                                                  extract_number(text, "tau", inputs));
  const auto variance_q = ringdown::CRLBCalculator::q_variance(extract_number(text, "A0", inputs),
                                                               extract_number(text, "sigma", inputs),
                                                               extract_number(text, "fs", inputs),
                                                               10000U,
                                                               extract_number(text, "tau", inputs),
                                                               extract_number(text, "f0", inputs));

  require_near(variance_f, extract_number(text, "variance_f", crlb), 1.0e-22, "CRLB frequency variance");
  require_near(std_f, extract_number(text, "std_f", crlb), 1.0e-16, "CRLB frequency std");
  require_near(variance_q, extract_number(text, "variance_q", crlb), 1.0e-5, "CRLB Q variance");
}

RINGDOWN_TEST(scientific_inputs_reject_nonfinite_values) {
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  const auto inf = std::numeric_limits<double>::infinity();

  auto signal = ringdown::SignalParameters{};
  signal.frequency_hz = nan;
  require_invalid_argument([&] { ringdown::RingDownSignal{signal}; }, "NaN signal frequency must be rejected");

  require_invalid_argument(
      [&] { (void)ringdown::CRLBCalculator::variance(inf, 0.1, 100.0, 128U, 10.0); },
      "infinite CRLB amplitude must be rejected");
  require_invalid_argument(
      [&] { (void)ringdown::NLSFrequencyEstimator{nan}; },
      "NaN known tau must be rejected");
}

RINGDOWN_TEST(deterministic_signal_matches_python_fixture) {
  const auto text = fixture_text();
  const auto section = find_key(text, "deterministic_signal");
  const auto parameters = find_key(text, "parameters", section);
  auto params = ringdown::SignalParameters{};
  params.frequency_hz = extract_number(text, "f0", parameters);
  params.sample_rate_hz = extract_number(text, "fs", parameters);
  params.sample_count = static_cast<std::size_t>(extract_number(text, "N", parameters));
  params.amplitude = extract_number(text, "A0", parameters);
  params.snr_db = extract_number(text, "snr_db", parameters);
  params.quality_factor = extract_number(text, "Q", parameters);

  const auto samples = ringdown::deterministic_ringdown(params, extract_number(text, "phi0", parameters));
  const auto expected = extract_array(text, "x", find_key(text, "samples", section));
  ringdown::test::require(samples.size() == expected.size(), "deterministic sample size must match fixture");
  for (auto index = std::size_t{0}; index < samples.size(); ++index) {
    require_near(samples[index], expected[index], 1.0e-12, "deterministic sample");
  }
}

RINGDOWN_TEST(estimators_track_python_fixture_signal) {
  const auto text = fixture_text();
  const auto parameters = find_key(text, "parameters", find_key(text, "name"));
  const auto sample_section = find_key(text, "samples", parameters);
  const auto samples = extract_array(text, "x", sample_section);
  const auto fs = extract_number(text, "fs", parameters);
  const auto estimates = find_key(text, "estimates");
  const auto nls_expected = extract_number(text, "f", find_key(text, "nls", estimates));
  const auto dft_expected = extract_number(text, "f", find_key(text, "dft", estimates));

  const auto nls = ringdown::NLSFrequencyEstimator{}.estimate_full(samples, fs);
  const auto dft = ringdown::DFTFrequencyEstimator{}.estimate_full(samples, fs);

  ringdown::test::require(nls.success, "NLS estimator should succeed on fixture");
  ringdown::test::require(dft.success, "DFT estimator should succeed on fixture");
  require_near(nls.frequency_hz, nls_expected, 5.0e-2, "NLS frequency");
  require_near(dft.frequency_hz, dft_expected, 5.0e-2, "DFT frequency");
  ringdown::test::require(nls.tau.has_value(), "NLS should return tau");
  ringdown::test::require(dft.tau.has_value(), "DFT should return tau");
}

RINGDOWN_TEST(analyzer_runs_array_workflow) {
  const auto text = fixture_text();
  const auto parameters = find_key(text, "parameters", find_key(text, "name"));
  const auto sample_section = find_key(text, "samples", parameters);
  const auto samples = extract_array(text, "x", sample_section);
  const auto fs = extract_number(text, "fs", parameters);
  const auto analysis = find_key(text, "array_analysis");

  const auto result = ringdown::RingDownAnalyzer{}.analyze_array(samples, fs);
  require_near(result.nls.frequency_hz, extract_number(text, "f_nls", analysis), 5.0e-2, "analyzer NLS");
  require_near(result.dft.frequency_hz, extract_number(text, "f_dft", analysis), 5.0e-2, "analyzer DFT");
  ringdown::test::require(result.plugin_crlb_std_f > 0.0, "analyzer should compute uncertainty");
  ringdown::test::require(result.cropped_sample_count > 0U, "analyzer should keep samples");
}

RINGDOWN_TEST(loader_and_analyzer_run_file_workflows) {
  const auto text = fixture_text();
  const auto file_fixtures = find_key(text, "file_fixtures");
  const auto csv = find_key(text, "csv", file_fixtures);
  const auto mat = find_key(text, "mat", file_fixtures);
  const auto csv_analysis = find_key(text, "analysis", csv);
  const auto mat_analysis = find_key(text, "analysis", mat);
  const auto zip_path = std::filesystem::current_path() / "moku_small_deflated.zip";
  write_deflated_csv_zip(zip_path,
                         "nested/moku_small.csv",
                         read_text_file(reference_fixture_path("moku_small.csv")));

  const auto csv_loaded = ringdown::RingDownDataLoader::load(reference_fixture_path("moku_small.csv").string());
  const auto mat_loaded = ringdown::RingDownDataLoader::load(reference_fixture_path("moku_small.mat").string());
  const auto zip_loaded = ringdown::RingDownDataLoader::load(zip_path.string());

  ringdown::test::require(csv_loaded.file_type == "CSV", "CSV fixture should load as CSV");
  ringdown::test::require(mat_loaded.file_type == "MAT", "MAT fixture should load as MAT");
  ringdown::test::require(zip_loaded.file_type == "ZIP_CSV", "ZIP fixture should load as ZIP_CSV");
  ringdown::test::require(zip_loaded.samples.size() == csv_loaded.samples.size(),
                          "ZIP CSV sample count should match CSV fixture");
  require_near(zip_loaded.samples.front(), csv_loaded.samples.front(), 1.0e-12, "ZIP CSV first sample");
  ringdown::test::require(mat_loaded.secondary_samples.size() == mat_loaded.samples.size(),
                          "MAT fixture should expose V2");
  require_near(mat_loaded.secondary_samples.front(), extract_number(text, "v2_first", mat), 1.0e-12, "MAT V2");

  const auto analyzer = ringdown::RingDownAnalyzer{};
  const auto csv_result = analyzer.analyze_file(reference_fixture_path("moku_small.csv").string());
  const auto mat_result = analyzer.analyze_file(reference_fixture_path("moku_small.mat").string());
  const auto zip_result = analyzer.analyze_file(zip_path.string());

  require_near(csv_result.nls.frequency_hz, extract_number(text, "f_nls", csv_analysis), 7.5e-2,
               "CSV analyzer NLS");
  require_near(csv_result.dft.frequency_hz, extract_number(text, "f_dft", csv_analysis), 7.5e-2,
               "CSV analyzer DFT");
  require_near(mat_result.nls.frequency_hz, extract_number(text, "f_nls", mat_analysis), 7.5e-2,
               "MAT analyzer NLS");
  require_near(mat_result.dft.frequency_hz, extract_number(text, "f_dft", mat_analysis), 7.5e-2,
               "MAT analyzer DFT");
  require_near(zip_result.nls.frequency_hz, csv_result.nls.frequency_hz, 1.0e-12, "ZIP analyzer NLS");
  require_near(zip_result.dft.frequency_hz, csv_result.dft.frequency_hz, 1.0e-12, "ZIP analyzer DFT");

  std::filesystem::remove(zip_path);
}

RINGDOWN_TEST(batch_workflow_matches_pipeline_fixture) {
  const auto text = fixture_text();
  const auto batch = find_key(text, "batch");
  auto analyzer = ringdown::BatchRingDownAnalyzer{};
  const auto processed = analyzer.process_files({reference_fixture_path("moku_small.csv").string(),
                                                 reference_fixture_path("moku_small.mat").string()});

  ringdown::test::require(processed.results.size() == static_cast<std::size_t>(extract_number(text, "success_count", batch)),
                          "batch success count");
  ringdown::test::require(processed.failed_files.size() == static_cast<std::size_t>(extract_number(text, "failure_count", batch)),
                          "batch failure count");
  const auto nls_stats = analyzer.nls_frequency_statistics();
  const auto dft_stats = analyzer.dft_frequency_statistics();
  const auto summary = analyzer.summary_table();
  const auto consistency = analyzer.consistency_analysis();
  const auto uncertainty = analyzer.uncertainty_comparison();
  require_near(nls_stats.mean, extract_number(text, "nls_mean", batch), 7.5e-2, "batch NLS mean");
  require_near(dft_stats.mean, extract_number(text, "dft_mean", batch), 7.5e-2, "batch DFT mean");
  ringdown::test::require(summary.size() == static_cast<std::size_t>(extract_number(text, "summary_rows", batch)),
                          "batch summary row count");
  ringdown::test::require(consistency.pairwise_comparison_count == 1U, "batch pairwise comparisons");
  ringdown::test::require(uncertainty.frequency_differences.size() ==
                              static_cast<std::size_t>(extract_number(text, "frequency_diff_count", batch)),
                          "batch uncertainty comparison count");

  auto parallel_analyzer = ringdown::BatchRingDownAnalyzer{};
  const auto parallel_processed =
      parallel_analyzer.process_files({reference_fixture_path("moku_small.csv").string(),
                                       reference_fixture_path("moku_small.mat").string()},
                                      2U);
  ringdown::test::require(parallel_processed.results.size() == processed.results.size(),
                          "parallel batch success count");
  ringdown::test::require(parallel_processed.failed_files.empty(), "parallel batch should not fail fixtures");
}

RINGDOWN_TEST(monte_carlo_runs_deterministically) {
  const auto text = fixture_text();
  const auto mc = find_key(text, "monte_carlo");
  const auto python_nls_errors = extract_array(text, "errors_nls", mc);
  auto options = ringdown::MonteCarloOptions{};
  options.signal.sample_count = 256U;
  options.trial_count = 3U;
  options.seed = 7U;
  const auto first = ringdown::MonteCarloAnalyzer{}.run(options);
  const auto second = ringdown::MonteCarloAnalyzer{}.run(options);
  options.worker_count = 2U;
  const auto parallel = ringdown::MonteCarloAnalyzer{}.run(options);
  ringdown::test::require(first.nls_frequency_errors.size() == second.nls_frequency_errors.size(),
                          "NLS trial counts should match");
  ringdown::test::require(first.dft_frequency_errors.size() == second.dft_frequency_errors.size(),
                          "DFT trial counts should match");
  if (!first.dft_frequency_errors.empty()) {
    require_near(first.dft_frequency_errors.front(), second.dft_frequency_errors.front(), 0.0, "DFT seed");
    require_near(first.dft_frequency_errors.front(), parallel.dft_frequency_errors.front(), 0.0,
                 "parallel DFT seed");
  }

  auto fixture_options = ringdown::MonteCarloOptions{};
  fixture_options.signal.sample_count = static_cast<std::size_t>(extract_number(text, "N", find_key(text, "parameters", mc)));
  fixture_options.trial_count = python_nls_errors.size();
  fixture_options.seed = 11U;
  const auto fixture_result = ringdown::MonteCarloAnalyzer{}.run(fixture_options);
  ringdown::test::require(fixture_result.nls_frequency_errors.size() == python_nls_errors.size(),
                          "Monte Carlo fixture trial count");
  ringdown::test::require(!ringdown::to_json(fixture_result).empty(), "Monte Carlo JSON export");
}

RINGDOWN_TEST(json_exports_escape_strings_and_null_nonfinite_values) {
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  const auto inf = std::numeric_limits<double>::infinity();

  auto analysis = ringdown::AnalyzerResult{};
  analysis.filename = "quoted\"file\\name\n.csv";
  analysis.file_type = "C\\SV";
  analysis.sample_rate_hz = inf;
  analysis.nls.tau = nan;
  const auto analysis_json = ringdown::to_json(analysis);
  ringdown::test::require(analysis_json.find("\"filename\": \"quoted\\\"file\\\\name\\n.csv\"") !=
                              std::string::npos,
                          "analysis JSON should escape filenames");
  ringdown::test::require(analysis_json.find("\"fs\": null") != std::string::npos,
                          "analysis JSON should write null for non-finite numbers");
  ringdown::test::require(analysis_json.find("\"tau_nls\": null") != std::string::npos,
                          "analysis JSON should write null for non-finite optionals");

  auto batch_item = ringdown::AnalyzerResult{};
  batch_item.filename = "batch\"file.csv";
  batch_item.plugin_crlb_std_f = inf;
  const auto batch_json = ringdown::to_json(ringdown::ProcessResult{{batch_item}, {}});
  ringdown::test::require(batch_json.find("\"filename\": \"batch\\\"file.csv\"") != std::string::npos,
                          "batch JSON should escape filenames");
  ringdown::test::require(batch_json.find("\"Q_nls\": null") != std::string::npos,
                          "batch JSON should write null for missing optionals");
  ringdown::test::require(batch_json.find("\"plugin_crlb_std_f\": null") != std::string::npos,
                          "batch JSON should write null for non-finite numbers");

  auto monte_carlo = ringdown::MonteCarloResult{};
  monte_carlo.nls_frequency_errors = {nan, inf};
  monte_carlo.nls_statistics = ringdown::ErrorStatistics{nan, inf, 0.0};
  const auto monte_carlo_json = ringdown::to_json(monte_carlo);
  ringdown::test::require(monte_carlo_json.find("[null, null]") != std::string::npos,
                          "Monte Carlo JSON should null non-finite arrays");
  ringdown::test::require(monte_carlo_json.find("\"mean\": null") != std::string::npos,
                          "Monte Carlo JSON should null non-finite stats");
}

RINGDOWN_TEST(to_json_notebook_exports_waveforms_and_estimator_fields) {
  auto result = ringdown::AnalyzerResult{};
  result.filename = "test.csv";
  result.file_type = "CSV";
  result.time = {0.0, 0.5, 1.0};
  result.samples = {1.0, -2.0, 3.0};
  result.cropped_time = {0.0, 0.5};
  result.cropped_samples = {1.1, -2.1};
  result.sample_rate_hz = 2.0;
  result.tau_seed = 0.1;
  result.tau_estimate = 0.2;
  result.tau_model = 0.25;
  result.nls.frequency_hz = 5.5;
  result.dft.frequency_hz = 5.6;
  result.nls.success = true;
  result.dft.success = false;
  result.nls.used_fallback = false;
  result.dft.used_fallback = true;
  result.nls.message = "ok";
  result.dft.message = "warn\"x";
  result.nls.evaluations = 42U;
  result.noise.amplitude = 0.5;
  result.noise.sigma = 0.01;
  result.noise.sigma_mle = 0.011;
  result.noise.degrees_of_freedom = 99U;
  result.noise.success = true;
  result.noise.method = "test";
  result.noise.message = "noise ok";
  result.plugin_crlb_variance_f = 1.0e-6;
  result.plugin_crlb_std_f = 1.0e-3;
  result.uncertainty_valid = true;
  result.sample_count = 3U;
  result.cropped_sample_count = 2U;
  result.observation_time = 1.0;
  result.cropped_observation_time = 0.5;

  const auto text = ringdown::to_json_notebook(result);
  ringdown::test::require(text.find("\"t\": [") != std::string::npos, "notebook JSON should include time array");
  ringdown::test::require(text.find("\"V2\": null") != std::string::npos, "notebook JSON should null V2");
  ringdown::test::require(text.find("\"dft_message\": \"warn\\\"x\"") != std::string::npos,
                          "notebook JSON should escape nested quotes");
  ringdown::test::require(text.find("\"nls_evaluations\": 42") != std::string::npos,
                          "notebook JSON should include evaluations");
  const auto t_arr = extract_array(text, "t", 0U);
  ringdown::test::require(t_arr.size() == 3U, "t array length");
  require_near(t_arr[1], 0.5, 0.0, "t[1]");
}

RINGDOWN_TEST(to_json_batch_report_contains_summary_and_consistency) {
  auto analyzer = ringdown::BatchRingDownAnalyzer{};
  const auto processed = analyzer.process_files({reference_fixture_path("moku_small.csv").string(),
                                                 reference_fixture_path("moku_small.mat").string()},
                                                1U);
  const auto report = ringdown::to_json_batch_report(analyzer, processed);
  ringdown::test::require(report.find("\"q_factors\":") != std::string::npos, "batch report should list Q");
  ringdown::test::require(report.find("\"consistency_analysis\":") != std::string::npos,
                          "batch report should include consistency");
  ringdown::test::require(report.find("\"crlb_comparison_analysis\":") != std::string::npos,
                          "batch report should include CRLB comparison");
  ringdown::test::require(report.find("\"summary_table\":") != std::string::npos,
                          "batch report should include summary table");
  ringdown::test::require(report.find("\"nls_mean\":") != std::string::npos,
                          "consistency should include nls_mean");
  ringdown::test::require(report.find("\"results_notebook\":") != std::string::npos,
                          "batch report should embed notebook results");
}

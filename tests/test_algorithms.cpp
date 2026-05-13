#include "test_support.hpp"

#include <ringdown/ringdown.hpp>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::string fixture_text() {
  const auto path = std::filesystem::path{RINGDOWN_SOURCE_DIR} / "tests" / "fixtures" /
                    "reference" / "synthetic_5hz_seed42.json";
  auto file = std::ifstream{path};
  if (!file) {
    throw std::runtime_error{"failed to open fixture: " + path.string()};
  }
  auto buffer = std::ostringstream{};
  buffer << file.rdbuf();
  return buffer.str();
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

RINGDOWN_TEST(monte_carlo_runs_deterministically) {
  auto options = ringdown::MonteCarloOptions{};
  options.signal.sample_count = 256U;
  options.trial_count = 3U;
  options.seed = 7U;
  const auto first = ringdown::MonteCarloAnalyzer{}.run(options);
  const auto second = ringdown::MonteCarloAnalyzer{}.run(options);
  ringdown::test::require(first.nls_frequency_errors.size() == second.nls_frequency_errors.size(),
                          "NLS trial counts should match");
  ringdown::test::require(first.dft_frequency_errors.size() == second.dft_frequency_errors.size(),
                          "DFT trial counts should match");
  if (!first.dft_frequency_errors.empty()) {
    require_near(first.dft_frequency_errors.front(), second.dft_frequency_errors.front(), 0.0, "DFT seed");
  }
}

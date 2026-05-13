#include <ringdown/analyzer.hpp>

#include <ringdown/crlb.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numbers>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace ringdown {

namespace {

[[nodiscard]] double mean(const std::vector<double>& values) {
  return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}

[[nodiscard]] std::vector<double> inferred_time(std::size_t count, double sample_rate_hz) {
  if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
    throw std::invalid_argument{"Sampling frequency fs must be positive and finite"};
  }
  auto time = std::vector<double>(count);
  for (auto index = std::size_t{0}; index < count; ++index) {
    time[index] = static_cast<double>(index) / sample_rate_hz;
  }
  return time;
}

void validate_samples(const std::vector<double>& samples, const char* source) {
  if (samples.size() < 2U) {
    throw std::invalid_argument{"At least 2 samples required for analysis"};
  }
  for (const auto sample : samples) {
    if (!std::isfinite(sample)) {
      throw std::invalid_argument{std::string{source} + " must contain only finite values"};
    }
  }
}

[[nodiscard]] double validate_uniform_timebase(std::vector<double>& time) {
  if (time.size() < 2U) {
    throw std::invalid_argument{"At least 2 samples required for analysis"};
  }
  for (const auto sample : time) {
    if (!std::isfinite(sample)) {
      throw std::invalid_argument{"Time array must contain only finite values"};
    }
  }
  const auto offset = time.front();
  for (auto& sample : time) {
    sample -= offset;
  }

  auto intervals = std::vector<double>{};
  intervals.reserve(time.size() - 1U);
  for (auto index = std::size_t{1}; index < time.size(); ++index) {
    const auto interval = time[index] - time[index - 1U];
    if (interval <= 0.0 || !std::isfinite(interval)) {
      throw std::invalid_argument{"Time array must be strictly increasing"};
    }
    intervals.push_back(interval);
  }
  auto sorted = intervals;
  std::sort(sorted.begin(), sorted.end());
  const auto dt = sorted[sorted.size() / 2U];
  for (const auto interval : intervals) {
    if (std::abs(interval - dt) > std::max(1.0e-12, std::abs(dt) * 5.0e-3)) {
      throw std::invalid_argument{"Nonuniform timestamps are not supported"};
    }
  }
  return 1.0 / dt;
}

[[nodiscard]] std::pair<std::vector<double>, std::vector<double>> crop_to_tau(
    const std::vector<double>& time,
    const std::vector<double>& samples,
    double tau,
    double max_tau_multiplier) {
  if (!std::isfinite(max_tau_multiplier) || max_tau_multiplier <= 0.0) {
    throw std::invalid_argument{"max_tau_multiplier must be positive and finite"};
  }
  if (!std::isfinite(tau) || tau <= 0.0) {
    throw std::invalid_argument{"tau_est must be positive and finite"};
  }

  const auto t_max = tau * max_tau_multiplier;
  auto cropped_time = std::vector<double>{};
  auto cropped_samples = std::vector<double>{};
  for (auto index = std::size_t{0}; index < time.size(); ++index) {
    if (time[index] <= t_max) {
      cropped_time.push_back(time[index]);
      cropped_samples.push_back(samples[index]);
    }
  }
  if (cropped_time.size() < 100U) {
    return {time, samples};
  }
  return {cropped_time, cropped_samples};
}

[[nodiscard]] std::optional<std::array<double, 3>> solve_3x3(std::array<std::array<double, 4>, 3> matrix) {
  for (auto col = std::size_t{0}; col < 3U; ++col) {
    auto pivot = col;
    for (auto row = col + 1U; row < 3U; ++row) {
      if (std::abs(matrix[row][col]) > std::abs(matrix[pivot][col])) {
        pivot = row;
      }
    }
    if (std::abs(matrix[pivot][col]) < 1.0e-20) {
      return std::nullopt;
    }
    if (pivot != col) {
      std::swap(matrix[pivot], matrix[col]);
    }
    const auto scale = matrix[col][col];
    for (auto item = col; item < 4U; ++item) {
      matrix[col][item] /= scale;
    }
    for (auto row = std::size_t{0}; row < 3U; ++row) {
      if (row == col) {
        continue;
      }
      const auto factor = matrix[row][col];
      for (auto item = col; item < 4U; ++item) {
        matrix[row][item] -= factor * matrix[col][item];
      }
    }
  }
  return std::array<double, 3>{matrix[0][3], matrix[1][3], matrix[2][3]};
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

[[nodiscard]] bool parse_double(const std::string& text, double& value) {
  auto stream = std::istringstream{text};
  stream >> value;
  return !stream.fail();
}

[[nodiscard]] bool parse_csv_data_row(const std::string& line, double& time, double& sample) {
  const auto fields = split_csv_line(line);
  if (fields.size() < 4U) {
    return false;
  }
  return parse_double(fields[0], time) && parse_double(fields[3], sample);
}

[[nodiscard]] std::string optional_number(std::optional<double> value) {
  if (!value.has_value()) {
    return "null";
  }
  auto out = std::ostringstream{};
  out << std::setprecision(17) << *value;
  return out.str();
}

} // namespace

RingDownAnalyzer::RingDownAnalyzer(NLSFrequencyEstimator nls_estimator,
                                   DFTFrequencyEstimator dft_estimator)
    : nls_estimator_{std::move(nls_estimator)}, dft_estimator_{std::move(dft_estimator)} {}

AnalyzerResult RingDownAnalyzer::analyze_array(const std::vector<double>& samples,
                                               double sample_rate_hz,
                                               double max_tau_multiplier) const {
  return analyze_array(inferred_time(samples.size(), sample_rate_hz), samples, max_tau_multiplier);
}

AnalyzerResult RingDownAnalyzer::analyze_array(const std::vector<double>& time,
                                               const std::vector<double>& samples,
                                               double max_tau_multiplier) const {
  validate_samples(samples, "Signal data");
  if (time.size() != samples.size()) {
    throw std::invalid_argument{"t and data must have same length"};
  }

  auto normalized_time = time;
  const auto sample_rate_hz = validate_uniform_timebase(normalized_time);
  const auto initial = estimate_initial_parameters_from_dft(samples, sample_rate_hz);
  const auto tau_seed = estimate_initial_tau_from_envelope(samples, sample_rate_hz);
  auto tau_estimate = estimate_tau(normalized_time, samples, sample_rate_hz);
  if (!std::isfinite(tau_estimate) || tau_estimate <= 0.0) {
    tau_estimate = tau_seed;
  }

  auto [cropped_time, cropped_samples] =
      crop_to_tau(normalized_time, samples, tau_estimate, max_tau_multiplier);
  if (cropped_time.size() < 1000U) {
    cropped_time = normalized_time;
    cropped_samples = samples;
  }

  const auto cropped_tau_seed = std::max(tau_estimate, 1.0 / sample_rate_hz);
  const auto nls = nls_estimator_.estimate_full(cropped_samples, sample_rate_hz, cropped_tau_seed, initial);
  const auto dft = dft_estimator_.estimate_full(cropped_samples, sample_rate_hz);

  const auto tau_model = nls.tau.value_or(dft.tau.value_or(tau_estimate));
  const auto noise = estimate_noise_parameters(cropped_time, cropped_samples, tau_model, nls.frequency_hz);
  const auto crlb_var = CRLBCalculator::variance(
      std::max(noise.amplitude, std::numeric_limits<double>::epsilon()),
      std::max(noise.sigma, std::numeric_limits<double>::epsilon()),
      sample_rate_hz,
      cropped_samples.size(),
      tau_model);
  const auto crlb_std = std::isfinite(crlb_var) ? std::sqrt(crlb_var)
                                                : std::numeric_limits<double>::infinity();

  return AnalyzerResult{normalized_time,
                        samples,
                        cropped_time,
                        cropped_samples,
                        sample_rate_hz,
                        tau_seed,
                        tau_estimate,
                        tau_model,
                        nls,
                        dft,
                        noise,
                        crlb_var,
                        crlb_std,
                        noise.success && nls.success && std::isfinite(crlb_std) && crlb_std > 0.0,
                        normalized_time.size(),
                        cropped_samples.size(),
                        normalized_time.back(),
                        cropped_time.back(),
                        {},
                        {}};
}

AnalyzerResult RingDownAnalyzer::analyze_file(const std::string& filepath,
                                              double max_tau_multiplier) const {
  const auto loaded = RingDownDataLoader::load(filepath);
  auto result = analyze_array(loaded.time, loaded.samples, max_tau_multiplier);
  result.filename = std::filesystem::path{filepath}.filename().string();
  result.file_type = loaded.file_type;
  return result;
}

double RingDownAnalyzer::estimate_tau(const std::vector<double>& time,
                                      const std::vector<double>& samples,
                                      double sample_rate_hz) const {
  (void)time;
  const auto result = NLSFrequencyEstimator{}.estimate_full(
      samples, sample_rate_hz, estimate_initial_tau_from_envelope(samples, sample_rate_hz));
  return result.tau.value_or(estimate_initial_tau_from_envelope(samples, sample_rate_hz));
}

NoiseEstimate RingDownAnalyzer::estimate_noise_parameters(const std::vector<double>& time,
                                                          const std::vector<double>& samples,
                                                          double tau_model,
                                                          double frequency_hz) const {
  if (time.size() != samples.size() || samples.size() < 4U) {
    throw std::invalid_argument{"At least 4 cropped samples are required for noise estimation"};
  }
  if (!std::isfinite(tau_model) || tau_model <= 0.0) {
    throw std::invalid_argument{"tau_model must be positive and finite"};
  }
  if (!std::isfinite(frequency_hz) || frequency_hz < 0.0) {
    throw std::invalid_argument{"f_model must be non-negative and finite"};
  }

  auto normal = std::array<std::array<double, 4>, 3>{};
  for (auto index = std::size_t{0}; index < samples.size(); ++index) {
    const auto t = time[index] - time.front();
    const auto envelope = std::exp(-t / tau_model);
    const auto angle = 2.0 * std::numbers::pi * frequency_hz * t;
    const auto row = std::array<double, 3>{envelope * std::cos(angle), envelope * std::sin(angle), 1.0};
    for (auto lhs = std::size_t{0}; lhs < 3U; ++lhs) {
      for (auto rhs = std::size_t{0}; rhs < 3U; ++rhs) {
        normal[lhs][rhs] += row[lhs] * row[rhs];
      }
      normal[lhs][3] += row[lhs] * samples[index];
    }
  }

  const auto solution = solve_3x3(normal);
  if (!solution.has_value()) {
    return NoiseEstimate{std::numeric_limits<double>::epsilon(),
                         0.0,
                         0.0,
                         0U,
                         false,
                         "tail_std_fallback",
                         "Design matrix is rank-deficient"};
  }

  auto rss = 0.0;
  for (auto index = std::size_t{0}; index < samples.size(); ++index) {
    const auto t = time[index] - time.front();
    const auto envelope = std::exp(-t / tau_model);
    const auto angle = 2.0 * std::numbers::pi * frequency_hz * t;
    const auto fitted = (*solution)[0] * envelope * std::cos(angle) +
                        (*solution)[1] * envelope * std::sin(angle) + (*solution)[2];
    const auto residual = samples[index] - fitted;
    rss += residual * residual;
  }

  const auto dof = samples.size() - 3U;
  return NoiseEstimate{std::max(std::hypot((*solution)[0], (*solution)[1]),
                                std::numeric_limits<double>::epsilon()),
                       std::sqrt(rss / static_cast<double>(dof)),
                       std::sqrt(rss / static_cast<double>(samples.size())),
                       dof,
                       true,
                       "fixed_frequency_tau_linear_lstsq",
                       {}};
}

LoadedData RingDownDataLoader::load(const std::string& filepath) {
  const auto path = std::filesystem::path{filepath};
  const auto extension = path.extension().string();
  if (extension == ".csv" || extension == ".CSV") {
    return load_csv(filepath);
  }
  if (extension == ".mat" || extension == ".MAT") {
    throw std::invalid_argument{"MAT loading requires a MAT/HDF5 dependency that is not yet configured"};
  }
  throw std::invalid_argument{"Unsupported file format: expected .csv or .mat"};
}

LoadedData RingDownDataLoader::load_csv(const std::string& filepath) {
  auto file = std::ifstream{filepath};
  if (!file) {
    throw std::runtime_error{"Could not open CSV file: " + filepath};
  }

  auto time = std::vector<double>{};
  auto samples = std::vector<double>{};
  auto line = std::string{};
  while (std::getline(file, line)) {
    const auto first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || line[first] == '%') {
      continue;
    }
    auto t = 0.0;
    auto sample = 0.0;
    if (!parse_csv_data_row(line, t, sample)) {
      if (time.empty()) {
        continue;
      }
      throw std::invalid_argument{"Malformed numeric data in CSV file: " + filepath};
    }
    time.push_back(t);
    samples.push_back(sample);
  }

  validate_samples(samples, "Signal data");
  const auto t0 = time.front();
  for (auto& sample : time) {
    sample -= t0;
  }
  const auto sample_mean = mean(samples);
  for (auto& sample : samples) {
    sample -= sample_mean;
  }
  return LoadedData{time, samples, {}, "CSV"};
}

std::string to_json(const AnalyzerResult& result) {
  auto out = std::ostringstream{};
  out << std::setprecision(17);
  out << "{\n";
  out << "  \"filename\": \"" << result.filename << "\",\n";
  out << "  \"type\": \"" << result.file_type << "\",\n";
  out << "  \"fs\": " << result.sample_rate_hz << ",\n";
  out << "  \"N\": " << result.sample_count << ",\n";
  out << "  \"N_crop\": " << result.cropped_sample_count << ",\n";
  out << "  \"T\": " << result.observation_time << ",\n";
  out << "  \"T_crop\": " << result.cropped_observation_time << ",\n";
  out << "  \"tau_seed\": " << result.tau_seed << ",\n";
  out << "  \"tau_est\": " << result.tau_estimate << ",\n";
  out << "  \"tau_model\": " << result.tau_model << ",\n";
  out << "  \"f_nls\": " << result.nls.frequency_hz << ",\n";
  out << "  \"f_dft\": " << result.dft.frequency_hz << ",\n";
  out << "  \"tau_nls\": " << optional_number(result.nls.tau) << ",\n";
  out << "  \"tau_dft\": " << optional_number(result.dft.tau) << ",\n";
  out << "  \"Q_nls\": " << optional_number(result.nls.quality_factor) << ",\n";
  out << "  \"Q_dft\": " << optional_number(result.dft.quality_factor) << ",\n";
  out << "  \"A0_est\": " << result.noise.amplitude << ",\n";
  out << "  \"sigma_est\": " << result.noise.sigma << ",\n";
  out << "  \"plugin_crlb_var_f\": " << result.plugin_crlb_variance_f << ",\n";
  out << "  \"plugin_crlb_std_f\": " << result.plugin_crlb_std_f << ",\n";
  out << "  \"uncertainty_valid\": " << (result.uncertainty_valid ? "true" : "false") << "\n";
  out << "}\n";
  return out.str();
}

} // namespace ringdown

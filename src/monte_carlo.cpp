#include <ringdown/monte_carlo.hpp>

#include <algorithm>
#include <cmath>
#include <future>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <utility>

namespace ringdown {

namespace {

[[nodiscard]] ErrorStatistics statistics(const std::vector<double>& values) {
  if (values.empty()) {
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    return ErrorStatistics{nan, nan, nan};
  }
  const auto sum = std::accumulate(values.begin(), values.end(), 0.0);
  const auto avg = sum / static_cast<double>(values.size());
  auto sq = 0.0;
  auto sq_rmse = 0.0;
  for (const auto value : values) {
    const auto delta = value - avg;
    sq += delta * delta;
    sq_rmse += value * value;
  }
  const auto denom = values.size() > 1U ? static_cast<double>(values.size() - 1U)
                                        : static_cast<double>(values.size());
  return ErrorStatistics{avg,
                         std::sqrt(sq / denom),
                         std::sqrt(sq_rmse / static_cast<double>(values.size()))};
}

struct TrialResult {
  std::optional<double> nls_frequency_error;
  std::optional<double> dft_frequency_error;
  std::optional<double> nls_q_error;
  std::optional<double> dft_q_error;
};

void write_vector(std::ostringstream& out, const std::vector<double>& values) {
  out << '[';
  for (auto index = std::size_t{0}; index < values.size(); ++index) {
    if (index != 0U) {
      out << ", ";
    }
    out << values[index];
  }
  out << ']';
}

void write_stats(std::ostringstream& out, const ErrorStatistics& stats) {
  out << "{\"mean\": " << stats.mean << ", \"std\": " << stats.standard_deviation
      << ", \"rmse\": " << stats.rmse << '}';
}

} // namespace

MonteCarloAnalyzer::MonteCarloAnalyzer(NLSFrequencyEstimator nls_estimator,
                                       DFTFrequencyEstimator dft_estimator)
    : nls_estimator_{std::move(nls_estimator)}, dft_estimator_{std::move(dft_estimator)} {}

MonteCarloResult MonteCarloAnalyzer::run(const MonteCarloOptions& options) const {
  options.signal.validate();
  auto result = MonteCarloResult{};
  result.frequency_hz = options.signal.frequency_hz;
  result.quality_factor = options.signal.quality_factor;
  result.tau = options.signal.tau();
  result.sample_rate_hz = options.signal.sample_rate_hz;
  result.sample_count = options.signal.sample_count;
  result.snr_db = options.signal.snr_db;
  result.crlb_std_f = CRLBCalculator::standard_deviation(options.signal.amplitude,
                                                         options.signal.sigma(),
                                                         options.signal.sample_rate_hz,
                                                         options.signal.sample_count,
                                                         options.signal.tau());
  result.crlb_std_q = CRLBCalculator::q_standard_deviation(options.signal.amplitude,
                                                           options.signal.sigma(),
                                                           options.signal.sample_rate_hz,
                                                           options.signal.sample_count,
                                                           options.signal.tau(),
                                                           options.signal.frequency_hz);

  const auto run_trial = [&](std::size_t trial_index) {
    const auto generated =
        RingDownSignal{options.signal}.generate(std::nullopt, options.seed + trial_index, true);
    auto trial = TrialResult{};
    try {
      const auto nls = nls_estimator_.estimate_full(generated.samples, options.signal.sample_rate_hz);
      trial.nls_frequency_error = nls.frequency_hz - options.signal.frequency_hz;
      if (nls.quality_factor.has_value()) {
        trial.nls_q_error = *nls.quality_factor - options.signal.quality_factor;
      }
    } catch (...) {
    }
    try {
      const auto dft = dft_estimator_.estimate_full(generated.samples, options.signal.sample_rate_hz);
      trial.dft_frequency_error = dft.frequency_hz - options.signal.frequency_hz;
      if (dft.quality_factor.has_value()) {
        trial.dft_q_error = *dft.quality_factor - options.signal.quality_factor;
      }
    } catch (...) {
    }
    return trial;
  };

  auto trials = std::vector<TrialResult>{};
  trials.reserve(options.trial_count);
  if (options.worker_count <= 1U || options.trial_count < 2U) {
    for (auto trial = std::size_t{0}; trial < options.trial_count; ++trial) {
      trials.push_back(run_trial(trial));
    }
  } else {
    auto futures = std::vector<std::future<TrialResult>>{};
    futures.reserve(options.trial_count);
    for (auto trial = std::size_t{0}; trial < options.trial_count; ++trial) {
      futures.push_back(std::async(std::launch::async, run_trial, trial));
    }
    for (auto& future : futures) {
      trials.push_back(future.get());
    }
  }

  for (const auto& trial : trials) {
    if (trial.nls_frequency_error.has_value()) {
      result.nls_frequency_errors.push_back(*trial.nls_frequency_error);
    }
    if (trial.dft_frequency_error.has_value()) {
      result.dft_frequency_errors.push_back(*trial.dft_frequency_error);
    }
    if (trial.nls_q_error.has_value()) {
      result.nls_q_errors.push_back(*trial.nls_q_error);
    }
    if (trial.dft_q_error.has_value()) {
      result.dft_q_errors.push_back(*trial.dft_q_error);
    }
  }

  result.nls_statistics = statistics(result.nls_frequency_errors);
  result.dft_statistics = statistics(result.dft_frequency_errors);
  result.nls_q_statistics = statistics(result.nls_q_errors);
  result.dft_q_statistics = statistics(result.dft_q_errors);
  return result;
}

std::string to_json(const MonteCarloResult& result) {
  auto out = std::ostringstream{};
  out << std::setprecision(17);
  out << "{\n";
  out << "  \"f0\": " << result.frequency_hz << ",\n";
  out << "  \"Q\": " << result.quality_factor << ",\n";
  out << "  \"tau\": " << result.tau << ",\n";
  out << "  \"fs\": " << result.sample_rate_hz << ",\n";
  out << "  \"N\": " << result.sample_count << ",\n";
  out << "  \"snr_db\": " << result.snr_db << ",\n";
  out << "  \"crlb_std\": " << result.crlb_std_f << ",\n";
  out << "  \"crlb_std_q\": " << result.crlb_std_q << ",\n";
  out << "  \"errors_nls\": ";
  write_vector(out, result.nls_frequency_errors);
  out << ",\n";
  out << "  \"errors_dft\": ";
  write_vector(out, result.dft_frequency_errors);
  out << ",\n";
  out << "  \"errors_q_nls\": ";
  write_vector(out, result.nls_q_errors);
  out << ",\n";
  out << "  \"errors_q_dft\": ";
  write_vector(out, result.dft_q_errors);
  out << ",\n";
  out << "  \"stats\": {\n";
  out << "    \"nls\": ";
  write_stats(out, result.nls_statistics);
  out << ",\n";
  out << "    \"dft\": ";
  write_stats(out, result.dft_statistics);
  out << ",\n";
  out << "    \"q_nls\": ";
  write_stats(out, result.nls_q_statistics);
  out << ",\n";
  out << "    \"q_dft\": ";
  write_stats(out, result.dft_q_statistics);
  out << "\n";
  out << "  }\n";
  out << "}\n";
  return out.str();
}

} // namespace ringdown

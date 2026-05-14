#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <ringdown/crlb.hpp>
#include <ringdown/estimators.hpp>
#include <ringdown/signal.hpp>

namespace ringdown {

/**
 * @brief Mean, sample standard deviation, and RMSE of estimator errors.
 */
struct ErrorStatistics {
  /// Arithmetic mean of the error samples.
  double mean{0.0};
  /// Sample standard deviation, or NaN when no finite errors are available.
  double standard_deviation{0.0};
  /// Root-mean-square error.
  double rmse{0.0};
};

/**
 * @brief Output from a Monte Carlo estimator comparison.
 *
 * Error arrays contain successful finite trial errors only. The statistics are
 * computed from those arrays independently for each estimator and quantity.
 */
struct MonteCarloResult {
  /// True signal frequency in hertz.
  double frequency_hz{0.0};
  /// True quality factor.
  double quality_factor{0.0};
  /// True decay time in seconds.
  double tau{0.0};
  /// Sampling rate in hertz.
  double sample_rate_hz{0.0};
  /// Number of samples generated per trial.
  std::size_t sample_count{0};
  /// Signal-to-noise ratio in decibels.
  double snr_db{0.0};
  /// Frequency CRLB standard deviation in hertz.
  double crlb_std_f{0.0};
  /// Quality-factor CRLB standard deviation.
  double crlb_std_q{0.0};
  /// NLS frequency errors in hertz.
  std::vector<double> nls_frequency_errors;
  /// DFT frequency errors in hertz.
  std::vector<double> dft_frequency_errors;
  /// NLS quality-factor errors.
  std::vector<double> nls_q_errors;
  /// DFT quality-factor errors.
  std::vector<double> dft_q_errors;
  /// Statistics for NLS frequency errors.
  ErrorStatistics nls_statistics;
  /// Statistics for DFT frequency errors.
  ErrorStatistics dft_statistics;
  /// Statistics for NLS Q errors.
  ErrorStatistics nls_q_statistics;
  /// Statistics for DFT Q errors.
  ErrorStatistics dft_q_statistics;
};

/**
 * @brief Configuration for `MonteCarloAnalyzer::run`.
 */
struct MonteCarloOptions {
  /// Signal model used to generate every trial.
  SignalParameters signal;
  /// Number of independent noisy realizations.
  std::size_t trial_count{100};
  /// Base random seed; each trial uses `seed + trial_index`.
  unsigned long long seed{42};
  /// Number of asynchronous workers; values less than two run serially.
  std::size_t worker_count{1};
};

/**
 * @brief Runs repeated synthetic trials through NLS and DFT estimators.
 *
 * The analyzer owns copies of the estimator objects passed to the constructor.
 * Each trial generates a noisy signal with deterministic per-trial seeding and
 * records finite successful frequency and Q errors.
 */
class MonteCarloAnalyzer {
public:
  /**
   * @brief Constructs a Monte Carlo analyzer with estimator copies.
   */
  MonteCarloAnalyzer(NLSFrequencyEstimator nls_estimator = NLSFrequencyEstimator{},
                     DFTFrequencyEstimator dft_estimator = DFTFrequencyEstimator{});

  /**
   * @brief Runs the configured Monte Carlo experiment.
   *
   * @param options Signal, trial, seed, and worker configuration.
   * @return Aggregated error arrays, statistics, and CRLB reference values.
   *
   * @throws std::invalid_argument if `options.signal.validate()` fails.
   */
  [[nodiscard]] MonteCarloResult run(const MonteCarloOptions& options) const;

private:
  NLSFrequencyEstimator nls_estimator_;
  DFTFrequencyEstimator dft_estimator_;
};

/**
 * @brief Serializes a Monte Carlo result to JSON.
 *
 * Non-finite numeric values are emitted as JSON `null`.
 */
[[nodiscard]] std::string to_json(const MonteCarloResult& result);

} // namespace ringdown

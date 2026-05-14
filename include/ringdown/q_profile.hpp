#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ringdown {

/**
 * @brief Profile-likelihood quality-factor estimate.
 *
 * The profile stores the refined frequency and tau estimates, the derived Q
 * value when it is bounded at the configured threshold, and the sampled
 * profile curve for diagnostics and plotting.
 */
struct QProfileResult {
  /// Frequency used for the fixed-frequency profile, in hertz.
  std::optional<double> f_hat;
  /// Best-fit decay time in seconds.
  std::optional<double> tau_hat;
  /// Profile quality-factor estimate, present only for fully bounded results.
  std::optional<double> Q;
  /// True when both 95% profile crossings are finite and no boundary reason applies.
  bool valid{false};
  /// Machine-readable status such as `valid`, `lower_limit`, or `unbounded`.
  std::string status;
  /// Machine-readable diagnostics explaining invalid or limited estimates.
  std::vector<std::string> reasons;
  /// Lower 95% confidence bound for Q, when both bounds are finite.
  std::optional<double> ci95_lower;
  /// Upper 95% confidence bound for Q, when both bounds are finite.
  std::optional<double> ci95_upper;
  /// One-sided lower 95% limit for Q, when the upper crossing is open.
  std::optional<double> lower_limit_95;
  /// One-sided upper 95% limit for Q, when the lower crossing is open.
  std::optional<double> upper_limit_95;
  /// Tau coordinates of the sampled profile, in seconds.
  std::vector<double> profile_tau;
  /// Quality-factor coordinates corresponding to `profile_tau`.
  std::vector<double> profile_q;
  /// Likelihood-ratio profile values relative to the minimum.
  std::vector<double> profile_delta;
  /// Minimum residual sum of squares found by the profile fit.
  double rss_min{0.0};
  /// Residual standard deviation at the best profile fit.
  double sigma{0.0};
  /// Degrees of freedom used to scale the profile statistic.
  std::size_t dof{0};
  /// Number of points in the returned profile arrays.
  std::size_t n_grid{0};
  /// Name of the profile method that produced the result.
  std::string method;
};

/**
 * @brief Variable-projection profile estimator for quality factor.
 *
 * For a fixed frequency, this estimator profiles over log(tau). At each tau it
 * solves a linear least-squares problem for cosine, sine, and offset terms,
 * then derives confidence limits from the profile statistic.
 */
class ProfileQEstimator {
public:
  /**
   * @brief Constructs a profile-Q estimator.
   *
   * @param n_grid Number of log-spaced tau grid points before refinement.
   * @param chi2_threshold Profile threshold used for confidence-limit crossing.
   * @param tau_max_record_multiplier Minimum tau upper range as a multiple of
   *        the record duration.
   * @param tau_init_span Multiplicative span around the tau seed.
   *
   * @throws std::invalid_argument if the grid is too small or any threshold or
   *         span parameter is invalid.
   */
  explicit ProfileQEstimator(std::size_t n_grid = 161,
                             double chi2_threshold = 3.841458820694124,
                             double tau_max_record_multiplier = 100.0,
                             double tau_init_span = 20.0);

  /**
   * @brief Estimates Q by profiling tau at a fixed frequency.
   *
   * @param time Strictly increasing timestamps.
   * @param samples Input samples corresponding to `time`.
   * @param sample_rate_hz Sampling rate in hertz.
   * @param f_init Optional fixed frequency. If missing or invalid, a DFT seed
   *        is computed from `samples`.
   * @param tau_init Optional tau seed in seconds.
   * @param tau_bounds Optional explicit tau bounds in seconds.
   * @param n_grid_override Optional grid size for this call.
   * @return Profile result with status and diagnostic arrays.
   *
   * @throws std::invalid_argument if vector sizes differ, timestamps are not
   *         finite and strictly increasing, samples are non-finite, sampling
   *         rate is invalid, or the effective grid size is less than 25.
   */
  [[nodiscard]] QProfileResult estimate(const std::vector<double>& time,
                                       const std::vector<double>& samples,
                                       double sample_rate_hz,
                                       std::optional<double> f_init = std::nullopt,
                                       std::optional<double> tau_init = std::nullopt,
                                       std::optional<std::pair<double, double>> tau_bounds = std::nullopt,
                                       std::optional<std::size_t> n_grid_override = std::nullopt) const;

private:
  std::size_t n_grid_;
  double chi2_threshold_;
  double tau_max_record_multiplier_;
  double tau_init_span_;
};

} // namespace ringdown

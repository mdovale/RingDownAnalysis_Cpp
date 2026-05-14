#pragma once

#include <cstddef>

namespace ringdown {

/**
 * @brief Weighted time sums used by the Fisher-information calculations.
 *
 * The weights are `exp(-2*t/tau)` evaluated on a uniform grid. The three sums
 * are `sum(w)`, `sum(t*w)`, and `sum(t*t*w)`.
 */
struct WeightedSums {
  /// Sum of exponential weights.
  double s0{0.0};
  /// Sum of time-weighted exponential weights.
  double s1{0.0};
  /// Sum of squared-time-weighted exponential weights.
  double s2{0.0};
};

/**
 * @brief Cramer-Rao lower-bound utilities for ringdown frequency and Q fits.
 *
 * All functions assume a uniformly sampled exponentially decaying sinusoid and
 * validate finite, positive physical inputs before evaluating the bound.
 */
class CRLBCalculator {
public:
  /**
   * @brief Computes the exponential weighted sums for a uniform sample grid.
   *
   * @param sample_rate_hz Sampling rate in hertz.
   * @param sample_count Number of samples in the record.
   * @param tau Exponential decay time constant in seconds.
   * @return Weighted sums over `index / sample_rate_hz`.
   *
   * @throws std::invalid_argument if `sample_rate_hz`, `sample_count`, or
   *         `tau` is not valid.
   */
  [[nodiscard]] static WeightedSums weighted_sums(double sample_rate_hz, std::size_t sample_count,
                                                  double tau);

  /**
   * @brief Computes the CRLB variance for frequency in hertz squared.
   *
   * @param amplitude Signal amplitude.
   * @param sigma Additive noise standard deviation.
   * @param sample_rate_hz Sampling rate in hertz.
   * @param sample_count Number of samples in the record.
   * @param tau Exponential decay time constant in seconds.
   * @return Frequency variance lower bound, or infinity when the Fisher
   *         information is numerically unresolved.
   *
   * @throws std::invalid_argument if a required positive finite input is
   *         invalid.
   */
  [[nodiscard]] static double variance(double amplitude, double sigma, double sample_rate_hz,
                                       std::size_t sample_count, double tau);

  /**
   * @brief Computes the square root of `variance`.
   *
   * @return Frequency standard deviation lower bound in hertz.
   */
  [[nodiscard]] static double standard_deviation(double amplitude, double sigma,
                                                double sample_rate_hz, std::size_t sample_count,
                                                double tau);

  /**
   * @brief Computes the CRLB variance for quality factor.
   *
   * @param amplitude Signal amplitude.
   * @param sigma Additive noise standard deviation.
   * @param sample_rate_hz Sampling rate in hertz.
   * @param sample_count Number of samples in the record.
   * @param tau Exponential decay time constant in seconds.
   * @param frequency_hz Ringdown frequency in hertz.
   * @return Quality-factor variance lower bound, or infinity when the
   *         time-weighted information is numerically unresolved.
   *
   * @throws std::invalid_argument if a required positive finite input is
   *         invalid.
   */
  [[nodiscard]] static double q_variance(double amplitude, double sigma, double sample_rate_hz,
                                         std::size_t sample_count, double tau,
                                         double frequency_hz);

  /**
   * @brief Computes the square root of `q_variance`.
   *
   * @return Quality-factor standard deviation lower bound.
   */
  [[nodiscard]] static double q_standard_deviation(double amplitude, double sigma,
                                                  double sample_rate_hz,
                                                  std::size_t sample_count, double tau,
                                                  double frequency_hz);
};

} // namespace ringdown

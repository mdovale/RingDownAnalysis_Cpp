#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ringdown {

/**
 * @brief Result returned by frequency estimators.
 *
 * The `frequency_hz` field is always populated. Optional fields are present
 * only when the estimator computes a usable decay-time or quality-factor
 * estimate.
 */
struct EstimationResult {
  /// Estimated frequency in hertz.
  double frequency_hz{0.0};
  /// Estimated decay time in seconds, when available.
  std::optional<double> tau;
  /// Estimated quality factor `pi * frequency_hz * tau`, when available.
  std::optional<double> quality_factor;
  /// When NLS estimates tau, bounds used by the bounded fit (for Q validity / bound-hit flags).
  std::optional<double> tau_lower_bound;
  /// Upper tau bound used by the bounded fit, when a bound was applied.
  std::optional<double> tau_upper_bound;
  /// True when the estimator accepted the fitted result.
  bool success{true};
  /// True when the result represents a fallback or rejected fit.
  bool used_fallback{false};
  /// Human-readable status message describing the estimator path.
  std::string message;
  /// Number of objective evaluations, when tracked by the estimator.
  std::optional<std::size_t> evaluations;
};

/**
 * @brief Initial sinusoid parameters used to seed nonlinear estimation.
 */
struct InitialParameters {
  /// Initial frequency in hertz.
  double frequency_hz{0.0};
  /// Initial phase in radians.
  double phase_rad{0.0};
  /// Initial non-negative amplitude.
  double amplitude{0.0};
  /// Initial additive offset.
  double offset{0.0};
};

/**
 * @brief Window functions supported by `DFTFrequencyEstimator`.
 */
enum class WindowType {
  /// No tapering.
  Rectangular,
  /// Hann taper.
  Hann,
  /// Kaiser taper using `DFTOptions::kaiser_beta`.
  Kaiser,
  /// Blackman taper.
  Blackman,
};

/**
 * @brief Configuration for DFT-based frequency estimation.
 */
struct DFTOptions {
  /// Window applied before the FFT.
  WindowType window{WindowType::Rectangular};
  /// Enables zero-padding before the FFT peak search.
  bool use_zero_padding{true};
  /// Multiplicative padding factor; zero is normalized to one by the estimator.
  std::size_t pad_factor{4};
  /// Kaiser beta parameter used when `window == WindowType::Kaiser`.
  double kaiser_beta{9.0};
  /// Lower frequency cutoff for the peak search, in hertz.
  double minimum_frequency_hz{0.0};
};

/**
 * @brief Frequency estimator based on an interpolated FFT peak.
 *
 * The estimator demeans the input, applies the configured window, searches the
 * positive-frequency power spectrum, and refines the peak using parabolic
 * interpolation. `estimate_full` also estimates tau by fitting a fixed
 * frequency decay model.
 */
class DFTFrequencyEstimator {
public:
  /**
   * @brief Constructs a DFT estimator with the supplied options.
   *
   * @param options DFT configuration. A zero `pad_factor` is treated as one.
   */
  explicit DFTFrequencyEstimator(DFTOptions options = {});

  /**
   * @brief Estimates only the dominant ringdown frequency.
   *
   * @param samples Input samples.
   * @param sample_rate_hz Sampling rate in hertz.
   * @return Estimated frequency in hertz.
   *
   * @throws std::invalid_argument if the samples or sample rate are invalid.
   */
  [[nodiscard]] double estimate(const std::vector<double>& samples, double sample_rate_hz) const;

  /**
   * @brief Estimates frequency and, when possible, tau and quality factor.
   *
   * @param samples Input samples.
   * @param sample_rate_hz Sampling rate in hertz.
   * @return Full estimator result including status diagnostics.
   *
   * @throws std::invalid_argument if the samples or sample rate are invalid.
   */
  [[nodiscard]] EstimationResult estimate_full(const std::vector<double>& samples,
                                               double sample_rate_hz) const;

private:
  DFTOptions options_;
};

/**
 * @brief Analytic bounded nonlinear least-squares frequency estimator.
 *
 * The estimator fits amplitude, frequency, phase, optional tau, and offset to
 * an exponentially decaying cosine model. If a known tau is supplied at
 * construction, tau is held fixed and the quality factor is derived from that
 * value.
 */
class NLSFrequencyEstimator {
public:
  /**
   * @brief Constructs an NLS estimator.
   *
   * @param known_tau Optional fixed decay time in seconds.
   * @throws std::invalid_argument if `known_tau` is present but not positive
   *         and finite.
   */
  explicit NLSFrequencyEstimator(std::optional<double> known_tau = std::nullopt);

  /**
   * @brief Estimates only the ringdown frequency.
   *
   * @param samples Input samples.
   * @param sample_rate_hz Sampling rate in hertz.
   * @return Estimated frequency in hertz.
   *
   * @throws std::invalid_argument if the samples or sample rate are invalid.
   */
  [[nodiscard]] double estimate(const std::vector<double>& samples, double sample_rate_hz) const;

  /**
   * @brief Estimates frequency, tau, and quality factor using bounded NLS.
   *
   * @param samples Input samples.
   * @param sample_rate_hz Sampling rate in hertz.
   * @param tau_initial Optional initial decay-time seed in seconds.
   * @param initial Optional initial sinusoid parameters.
   * @return Full estimator result including fallback and bound diagnostics.
   *
   * @throws std::invalid_argument if the samples or sample rate are invalid.
   */
  [[nodiscard]] EstimationResult estimate_full(const std::vector<double>& samples,
                                               double sample_rate_hz,
                                               std::optional<double> tau_initial = std::nullopt,
                                               std::optional<InitialParameters> initial = std::nullopt) const;

private:
  std::optional<double> known_tau_;
};

/**
 * @brief Builds initial NLS parameters from a coarse Hann-windowed DFT pass.
 *
 * @param samples Input samples.
 * @param sample_rate_hz Sampling rate in hertz.
 * @return Initial frequency, phase, amplitude floor, and offset.
 *
 * @throws std::invalid_argument if the samples or sample rate are invalid.
 */
[[nodiscard]] InitialParameters estimate_initial_parameters_from_dft(
    const std::vector<double>& samples, double sample_rate_hz);

/**
 * @brief Estimates a decay-time seed from windowed RMS envelope decay.
 *
 * @param samples Input samples.
 * @param sample_rate_hz Sampling rate in hertz.
 * @return Positive tau seed in seconds.
 *
 * @throws std::invalid_argument if the samples or sample rate are invalid.
 */
[[nodiscard]] double estimate_initial_tau_from_envelope(const std::vector<double>& samples,
                                                       double sample_rate_hz);

} // namespace ringdown

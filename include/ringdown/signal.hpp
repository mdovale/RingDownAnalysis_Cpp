#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace ringdown {

/**
 * @brief Physical and sampling parameters for a synthetic exponentially
 * decaying ringdown signal.
 *
 * The generated model is
 * `amplitude * exp(-t / tau()) * cos(2*pi*frequency_hz*t + phase_rad)`,
 * optionally with additive Gaussian noise whose standard deviation is derived
 * from `snr_db`.
 */
struct SignalParameters {
  /// Carrier frequency in hertz.
  double frequency_hz{5.0};
  /// Sampling rate in hertz.
  double sample_rate_hz{100.0};
  /// Number of samples to synthesize.
  std::size_t sample_count{1000};
  /// Initial cosine amplitude.
  double amplitude{1.0};
  /// Signal-to-noise ratio in decibels used by `sigma()`.
  double snr_db{60.0};
  /// Dimensionless quality factor used to derive the decay time.
  double quality_factor{10000.0};

  /**
   * @brief Returns the exponential decay time constant.
   *
   * @return `quality_factor / (pi * frequency_hz)`.
   */
  [[nodiscard]] double tau() const;

  /**
   * @brief Returns the Gaussian noise standard deviation implied by `snr_db`.
   *
   * @return Noise standard deviation for additive zero-mean Gaussian noise.
   */
  [[nodiscard]] double sigma() const;

  /**
   * @brief Validates that the signal parameters are finite and usable.
   *
   * @throws std::invalid_argument if frequency, sample rate, sample count,
   *         amplitude, quality factor, or SNR is invalid.
   */
  void validate() const;
};

/**
 * @brief Samples produced by `RingDownSignal::generate`.
 */
struct GeneratedSignal {
  /// Sample timestamps in seconds, starting at zero.
  std::vector<double> time;
  /// Synthetic samples, with noise included when requested.
  std::vector<double> samples;
  /// Phase in radians used for the deterministic carrier.
  double phase_rad{0.0};
};

/**
 * @brief Generator for deterministic or noisy synthetic ringdown records.
 *
 * The constructor validates the supplied `SignalParameters`. Instances are
 * immutable after construction and reuse the stored parameters for all
 * generated records.
 */
class RingDownSignal {
public:
  /**
   * @brief Constructs a signal generator with validated parameters.
   *
   * @param parameters Signal parameters copied into the generator.
   * @throws std::invalid_argument if `parameters.validate()` fails.
   */
  explicit RingDownSignal(SignalParameters parameters);

  /**
   * @brief Returns the validated parameters stored by the generator.
   */
  [[nodiscard]] const SignalParameters& parameters() const noexcept;

  /**
   * @brief Returns the uniformly spaced sample times.
   *
   * @return A vector of length `parameters().sample_count` with
   *         `time[index] == index / parameters().sample_rate_hz`.
   */
  [[nodiscard]] std::vector<double> time() const;

  /**
   * @brief Returns the nominal observation duration.
   *
   * @return `sample_count / sample_rate_hz`.
   */
  [[nodiscard]] double observation_time() const;

  /**
   * @brief Generates one synthetic ringdown realization.
   *
   * @param phase_rad Optional phase in radians. If omitted, a phase is drawn
   *        uniformly from `[-pi, pi]`.
   * @param seed Optional seed for deterministic phase/noise generation.
   * @param include_noise If true, adds Gaussian noise with standard deviation
   *        `parameters().sigma()`.
   * @return Generated timestamps, samples, and the phase that was used.
   */
  [[nodiscard]] GeneratedSignal generate(std::optional<double> phase_rad = std::nullopt,
                                         std::optional<unsigned long long> seed = std::nullopt,
                                         bool include_noise = true) const;

private:
  SignalParameters parameters_;
};

/**
 * @brief Computes the noiseless exponentially decaying cosine signal.
 *
 * @param parameters Signal parameters that define the sample grid and model.
 * @param phase_rad Carrier phase in radians.
 * @return Deterministic samples on the grid implied by `parameters`.
 *
 * @throws std::invalid_argument if `parameters.validate()` fails.
 */
[[nodiscard]] std::vector<double> deterministic_ringdown(const SignalParameters& parameters,
                                                         double phase_rad);

} // namespace ringdown

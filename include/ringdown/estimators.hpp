#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ringdown {

struct EstimationResult {
  double frequency_hz{0.0};
  std::optional<double> tau;
  std::optional<double> quality_factor;
  bool success{true};
  bool used_fallback{false};
  std::string message;
  std::optional<std::size_t> evaluations;
};

struct InitialParameters {
  double frequency_hz{0.0};
  double phase_rad{0.0};
  double amplitude{0.0};
  double offset{0.0};
};

enum class WindowType {
  Rectangular,
  Hann,
  Kaiser,
  Blackman,
};

struct DFTOptions {
  WindowType window{WindowType::Rectangular};
  bool use_zero_padding{true};
  std::size_t pad_factor{4};
  double kaiser_beta{9.0};
  double minimum_frequency_hz{0.0};
};

class DFTFrequencyEstimator {
public:
  explicit DFTFrequencyEstimator(DFTOptions options = {});

  [[nodiscard]] double estimate(const std::vector<double>& samples, double sample_rate_hz) const;
  [[nodiscard]] EstimationResult estimate_full(const std::vector<double>& samples,
                                               double sample_rate_hz) const;

private:
  DFTOptions options_;
};

class NLSFrequencyEstimator {
public:
  explicit NLSFrequencyEstimator(std::optional<double> known_tau = std::nullopt);

  [[nodiscard]] double estimate(const std::vector<double>& samples, double sample_rate_hz) const;
  [[nodiscard]] EstimationResult estimate_full(const std::vector<double>& samples,
                                               double sample_rate_hz,
                                               std::optional<double> tau_initial = std::nullopt,
                                               std::optional<InitialParameters> initial = std::nullopt) const;

private:
  std::optional<double> known_tau_;
};

[[nodiscard]] InitialParameters estimate_initial_parameters_from_dft(
    const std::vector<double>& samples, double sample_rate_hz);
[[nodiscard]] double estimate_initial_tau_from_envelope(const std::vector<double>& samples,
                                                       double sample_rate_hz);

} // namespace ringdown

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace ringdown {

struct SignalParameters {
  double frequency_hz{5.0};
  double sample_rate_hz{100.0};
  std::size_t sample_count{1000};
  double amplitude{1.0};
  double snr_db{60.0};
  double quality_factor{10000.0};

  [[nodiscard]] double tau() const;
  [[nodiscard]] double sigma() const;
  void validate() const;
};

struct GeneratedSignal {
  std::vector<double> time;
  std::vector<double> samples;
  double phase_rad{0.0};
};

class RingDownSignal {
public:
  explicit RingDownSignal(SignalParameters parameters);

  [[nodiscard]] const SignalParameters& parameters() const noexcept;
  [[nodiscard]] std::vector<double> time() const;
  [[nodiscard]] double observation_time() const;

  [[nodiscard]] GeneratedSignal generate(std::optional<double> phase_rad = std::nullopt,
                                         std::optional<unsigned long long> seed = std::nullopt,
                                         bool include_noise = true) const;

private:
  SignalParameters parameters_;
};

[[nodiscard]] std::vector<double> deterministic_ringdown(const SignalParameters& parameters,
                                                         double phase_rad);

} // namespace ringdown

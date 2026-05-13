#include <ringdown/signal.hpp>

#include <cmath>
#include <numbers>
#include <random>
#include <stdexcept>

namespace ringdown {

double SignalParameters::tau() const { return quality_factor / (std::numbers::pi * frequency_hz); }

double SignalParameters::sigma() const {
  const auto rho0 = std::pow(10.0, snr_db / 10.0);
  return std::sqrt(((amplitude * amplitude) / 2.0) / rho0);
}

void SignalParameters::validate() const {
  if (frequency_hz <= 0.0) {
    throw std::invalid_argument{"f0 must be positive"};
  }
  if (sample_rate_hz <= 0.0) {
    throw std::invalid_argument{"fs must be positive"};
  }
  if (sample_count == 0U) {
    throw std::invalid_argument{"N must be positive"};
  }
  if (amplitude <= 0.0) {
    throw std::invalid_argument{"A0 must be positive"};
  }
  if (quality_factor <= 0.0) {
    throw std::invalid_argument{"Q must be positive"};
  }
}

RingDownSignal::RingDownSignal(SignalParameters parameters) : parameters_{parameters} {
  parameters_.validate();
}

const SignalParameters& RingDownSignal::parameters() const noexcept { return parameters_; }

std::vector<double> RingDownSignal::time() const {
  auto values = std::vector<double>(parameters_.sample_count);
  for (auto index = std::size_t{0}; index < values.size(); ++index) {
    values[index] = static_cast<double>(index) / parameters_.sample_rate_hz;
  }
  return values;
}

double RingDownSignal::observation_time() const {
  return static_cast<double>(parameters_.sample_count) / parameters_.sample_rate_hz;
}

std::vector<double> deterministic_ringdown(const SignalParameters& parameters, double phase_rad) {
  parameters.validate();
  auto samples = std::vector<double>(parameters.sample_count);
  const auto tau = parameters.tau();
  for (auto index = std::size_t{0}; index < samples.size(); ++index) {
    const auto t = static_cast<double>(index) / parameters.sample_rate_hz;
    const auto envelope = parameters.amplitude * std::exp(-t / tau);
    samples[index] = envelope * std::cos(2.0 * std::numbers::pi * parameters.frequency_hz * t + phase_rad);
  }
  return samples;
}

GeneratedSignal RingDownSignal::generate(std::optional<double> phase_rad,
                                         std::optional<unsigned long long> seed,
                                         bool include_noise) const {
  auto rng = seed.has_value() ? std::mt19937_64{*seed} : std::mt19937_64{std::random_device{}()};
  auto phase_distribution = std::uniform_real_distribution<double>{-std::numbers::pi, std::numbers::pi};
  const auto phase = phase_rad.value_or(phase_distribution(rng));
  auto generated = GeneratedSignal{time(), deterministic_ringdown(parameters_, phase), phase};

  if (include_noise) {
    auto noise = std::normal_distribution<double>{0.0, parameters_.sigma()};
    for (auto& sample : generated.samples) {
      sample += noise(rng);
    }
  }

  return generated;
}

} // namespace ringdown

#include <ringdown/crlb.hpp>

#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace ringdown {

namespace {

void validate_positive(double value, const char* message) {
  if (value <= 0.0) {
    throw std::invalid_argument{message};
  }
}

} // namespace

WeightedSums CRLBCalculator::weighted_sums(double sample_rate_hz, std::size_t sample_count,
                                           double tau) {
  validate_positive(sample_rate_hz, "fs must be positive");
  if (sample_count == 0U) {
    throw std::invalid_argument{"N must be positive"};
  }
  validate_positive(tau, "tau must be positive");

  auto sums = WeightedSums{};
  for (auto index = std::size_t{0}; index < sample_count; ++index) {
    const auto t = static_cast<double>(index) / sample_rate_hz;
    const auto weight = std::exp(-2.0 * t / tau);
    sums.s0 += weight;
    sums.s1 += t * weight;
    sums.s2 += t * t * weight;
  }
  return sums;
}

double CRLBCalculator::variance(double amplitude, double sigma, double sample_rate_hz,
                                std::size_t sample_count, double tau) {
  validate_positive(amplitude, "A0 must be positive");
  validate_positive(sigma, "sigma must be positive");
  const auto sums = weighted_sums(sample_rate_hz, sample_count, tau);
  const auto delta_s2 = sums.s2 - (sums.s1 * sums.s1 / sums.s0);
  const auto fisher_omega = (amplitude * amplitude / (sigma * sigma)) * delta_s2;
  if (fisher_omega < 1.0e-30) {
    return std::numeric_limits<double>::infinity();
  }
  const auto two_pi = 2.0 * std::numbers::pi;
  return 1.0 / (two_pi * two_pi * fisher_omega);
}

double CRLBCalculator::standard_deviation(double amplitude, double sigma, double sample_rate_hz,
                                          std::size_t sample_count, double tau) {
  const auto var = variance(amplitude, sigma, sample_rate_hz, sample_count, tau);
  return std::isinf(var) ? var : std::sqrt(var);
}

double CRLBCalculator::q_variance(double amplitude, double sigma, double sample_rate_hz,
                                  std::size_t sample_count, double tau, double frequency_hz) {
  validate_positive(amplitude, "A0 must be positive");
  validate_positive(sigma, "sigma must be positive");
  validate_positive(frequency_hz, "f0 must be positive");
  const auto sums = weighted_sums(sample_rate_hz, sample_count, tau);
  const auto delta_s2 = sums.s2 - (sums.s1 * sums.s1 / sums.s0);
  if (delta_s2 < 1.0e-30) {
    return std::numeric_limits<double>::infinity();
  }
  const auto quality_factor = std::numbers::pi * frequency_hz * tau;
  return ((sigma * sigma * tau * tau) / (4.0 * amplitude * amplitude * delta_s2)) *
         (1.0 + 4.0 * quality_factor * quality_factor);
}

double CRLBCalculator::q_standard_deviation(double amplitude, double sigma, double sample_rate_hz,
                                            std::size_t sample_count, double tau,
                                            double frequency_hz) {
  const auto var = q_variance(amplitude, sigma, sample_rate_hz, sample_count, tau, frequency_hz);
  return std::isinf(var) ? var : std::sqrt(var);
}

} // namespace ringdown

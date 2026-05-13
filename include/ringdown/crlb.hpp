#pragma once

#include <cstddef>

namespace ringdown {

struct WeightedSums {
  double s0{0.0};
  double s1{0.0};
  double s2{0.0};
};

class CRLBCalculator {
public:
  [[nodiscard]] static WeightedSums weighted_sums(double sample_rate_hz, std::size_t sample_count,
                                                  double tau);
  [[nodiscard]] static double variance(double amplitude, double sigma, double sample_rate_hz,
                                       std::size_t sample_count, double tau);
  [[nodiscard]] static double standard_deviation(double amplitude, double sigma,
                                                double sample_rate_hz, std::size_t sample_count,
                                                double tau);
  [[nodiscard]] static double q_variance(double amplitude, double sigma, double sample_rate_hz,
                                         std::size_t sample_count, double tau,
                                         double frequency_hz);
  [[nodiscard]] static double q_standard_deviation(double amplitude, double sigma,
                                                  double sample_rate_hz,
                                                  std::size_t sample_count, double tau,
                                                  double frequency_hz);
};

} // namespace ringdown

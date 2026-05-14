#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ringdown {

/// Profile-likelihood / variable-projection Q estimate (Python `QProfileResult`).
struct QProfileResult {
  std::optional<double> f_hat;
  std::optional<double> tau_hat;
  std::optional<double> Q;
  bool valid{false};
  std::string status;
  std::vector<std::string> reasons;
  std::optional<double> ci95_lower;
  std::optional<double> ci95_upper;
  std::optional<double> lower_limit_95;
  std::optional<double> upper_limit_95;
  std::vector<double> profile_tau;
  std::vector<double> profile_q;
  std::vector<double> profile_delta;
  double rss_min{0.0};
  double sigma{0.0};
  std::size_t dof{0};
  std::size_t n_grid{0};
  std::string method;
};

/// Variable-projection profile over log(tau) at fixed frequency (Python `ProfileQEstimator`).
class ProfileQEstimator {
public:
  explicit ProfileQEstimator(std::size_t n_grid = 161,
                             double chi2_threshold = 3.841458820694124,
                             double tau_max_record_multiplier = 100.0,
                             double tau_init_span = 20.0);

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

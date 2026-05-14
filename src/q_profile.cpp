#include <ringdown/q_profile.hpp>

#include <ringdown/estimators.hpp>

#include <array>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>

namespace ringdown {

namespace {

constexpr auto kGoldenRatioConjugate = 0.6180339887498948482;
constexpr auto kGoldenSearchIterations = std::size_t{80U};

[[nodiscard]] double mean(const std::vector<double>& values) {
  auto sum = 0.0;
  for (const auto v : values) {
    sum += v;
  }
  return sum / static_cast<double>(values.size());
}

[[nodiscard]] bool has_resolved_ac_content(const std::vector<double>& data) {
  const auto avg = mean(data);
  auto demeaned_peak = 0.0;
  auto data_peak = 0.0;
  for (const auto sample : data) {
    demeaned_peak = std::max(demeaned_peak, std::abs(sample - avg));
    data_peak = std::max(data_peak, std::abs(sample));
  }
  const auto threshold =
      std::max(data_peak * 1.0e-12, std::numeric_limits<double>::epsilon() * 10.0);
  return demeaned_peak > threshold;
}

[[nodiscard]] double standard_deviation_sample(const std::vector<double>& values) {
  if (values.size() < 2U) {
    return 0.0;
  }
  const auto avg = mean(values);
  auto sum = 0.0;
  for (const auto v : values) {
    const auto d = v - avg;
    sum += d * d;
  }
  return std::sqrt(sum / static_cast<double>(values.size() - 1U));
}

[[nodiscard]] std::optional<std::array<double, 3>> solve_3x3(std::array<std::array<double, 4>, 3> matrix) {
  constexpr auto dimension = 3U;
  for (auto col = std::size_t{0}; col < dimension; ++col) {
    auto pivot = col;
    for (auto row = col + 1U; row < dimension; ++row) {
      if (std::abs(matrix[row][col]) > std::abs(matrix[pivot][col])) {
        pivot = row;
      }
    }
    if (std::abs(matrix[pivot][col]) < 1.0e-24) {
      return std::nullopt;
    }
    if (pivot != col) {
      std::swap(matrix[pivot], matrix[col]);
    }
    const auto scale = matrix[col][col];
    for (auto item = col; item <= dimension; ++item) {
      matrix[col][item] /= scale;
    }
    for (auto row = std::size_t{0}; row < dimension; ++row) {
      if (row == col) {
        continue;
      }
      const auto factor = matrix[row][col];
      for (auto item = col; item <= dimension; ++item) {
        matrix[row][item] -= factor * matrix[col][item];
      }
    }
  }
  return std::array<double, 3>{matrix[0][3], matrix[1][3], matrix[2][3]};
}

struct ProjectionFit {
  double tau{0.0};
  double rss{0.0};
  double sigma{0.0};
  std::size_t dof{0};
  double amplitude{0.0};
};

/**
 * @brief Solves the linear projection problem for a fixed tau.
 *
 * @internal The nonlinear profile varies only tau. For each tau, cosine,
 * sine, and offset coefficients are solved by linear least squares, and the
 * residual sum of squares feeds the profile-likelihood statistic.
 */
[[nodiscard]] std::optional<ProjectionFit> fit_fixed_tau(const std::vector<double>& t_norm,
                                                        const std::vector<double>& data,
                                                        double f_hat,
                                                        double tau) {
  const auto n = data.size();
  if (n < 3U) {
    return std::nullopt;
  }
  auto normal = std::array<std::array<double, 4>, 3>{};
  auto sample_sum_squares = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const auto ti = t_norm[i];
    const auto yi = data[i];
    sample_sum_squares += yi * yi;
    const auto exp_term = std::exp(-ti / tau);
    const auto wt = 2.0 * std::numbers::pi * f_hat * ti;
    const auto c = exp_term * std::cos(wt);
    const auto s = exp_term * std::sin(wt);
    const auto row = std::array<double, 3>{c, s, 1.0};
    for (auto lhs = std::size_t{0}; lhs < 3U; ++lhs) {
      for (auto rhs = std::size_t{0}; rhs < 3U; ++rhs) {
        normal[lhs][rhs] += row[lhs] * row[rhs];
      }
      normal[lhs][3] += row[lhs] * yi;
    }
  }

  const auto solution = solve_3x3(normal);
  if (!solution.has_value()) {
    return std::nullopt;
  }

  const auto explained_sum_squares = (*solution)[0] * normal[0][3] + (*solution)[1] * normal[1][3] +
                                     (*solution)[2] * normal[2][3];
  const auto rss = std::max(0.0, sample_sum_squares - explained_sum_squares);
  const auto dof = n - 3U;
  if (dof == 0U) {
    return std::nullopt;
  }
  const auto sigma = std::sqrt(std::max(rss, 0.0) / static_cast<double>(dof));
  const auto amplitude = std::hypot((*solution)[0], (*solution)[1]);
  return ProjectionFit{tau, rss, sigma, dof, amplitude};
}

/**
 * @brief Interpolates a threshold crossing in log-tau space.
 *
 * @internal Profile points are log-spaced, so confidence-bound interpolation is
 * performed in log(tau) rather than linearly in tau.
 */
[[nodiscard]] double crossing_tau(double tau0,
                                 double delta0,
                                 double tau1,
                                 double delta1,
                                 double threshold) {
  const auto log_tau0 = std::log(tau0);
  const auto log_tau1 = std::log(tau1);
  if (delta1 == delta0) {
    return std::exp(0.5 * (log_tau0 + log_tau1));
  }
  auto frac = (threshold - delta0) / (delta1 - delta0);
  frac = std::clamp(frac, 0.0, 1.0);
  return std::exp(log_tau0 + frac * (log_tau1 - log_tau0));
}

[[nodiscard]] double median_dt(const std::vector<double>& t_norm) {
  if (t_norm.size() < 2U) {
    return 0.0;
  }
  auto dts = std::vector<double>{};
  dts.reserve(t_norm.size() - 1U);
  for (std::size_t i = 1; i < t_norm.size(); ++i) {
    dts.push_back(t_norm[i] - t_norm[i - 1U]);
  }
  std::nth_element(dts.begin(), dts.begin() + static_cast<std::ptrdiff_t>(dts.size() / 2), dts.end());
  return dts[dts.size() / 2];
}

/**
 * @brief Chooses the tau search interval for the profile grid.
 *
 * @internal Explicit bounds are respected after applying the one-sample-period
 * lower floor. Otherwise the interval expands around the tau seed and at least
 * to a record-duration multiple to allow open-limit diagnoses.
 */
[[nodiscard]] std::pair<double, double> tau_bounds_profile(const std::vector<double>& samples,
                                                         double sample_rate_hz,
                                                         const std::vector<double>& t_norm,
                                                         std::optional<double> tau_init,
                                                         std::optional<std::pair<double, double>> tau_bounds,
                                                         double tau_init_span,
                                                         double tau_max_record_multiplier) {
  const auto dt = median_dt(t_norm);
  const auto duration = t_norm.back() - t_norm.front();
  const auto lower_floor = std::max(dt, std::numeric_limits<double>::epsilon());

  if (tau_bounds.has_value()) {
    return {std::max(lower_floor, tau_bounds->first), tau_bounds->second};
  }

  auto tau_seed = tau_init.value_or(std::numeric_limits<double>::quiet_NaN());
  if (!std::isfinite(tau_seed) || tau_seed <= 0.0) {
    tau_seed = estimate_initial_tau_from_envelope(samples, sample_rate_hz);
  }
  const auto tau_min = std::max(lower_floor, tau_seed / tau_init_span);
  const auto tau_max = std::max({tau_min * 10.0, tau_seed * tau_init_span, duration * tau_max_record_multiplier});
  if (!std::isfinite(tau_min) || !std::isfinite(tau_max) || tau_min <= 0.0 || tau_max <= tau_min) {
    throw std::invalid_argument{"Invalid tau bounds for profile Q"};
  }
  return {std::max(tau_min, lower_floor), tau_max};
}

template <typename Function>
[[nodiscard]] double minimize_golden(double lower, double upper, Function objective, std::size_t iterations) {
  auto left = lower;
  auto right = upper;
  auto c = right - kGoldenRatioConjugate * (right - left);
  auto d = left + kGoldenRatioConjugate * (right - left);
  auto fc = objective(c);
  auto fd = objective(d);

  for (auto iteration = std::size_t{0}; iteration < iterations; ++iteration) {
    if (fc < fd) {
      right = d;
      d = c;
      fd = fc;
      c = right - kGoldenRatioConjugate * (right - left);
      fc = objective(c);
    } else {
      left = c;
      c = d;
      fc = fd;
      d = left + kGoldenRatioConjugate * (right - left);
      fd = objective(d);
    }
  }
  return 0.5 * (left + right);
}

/**
 * @brief Builds a consistently shaped invalid profile-Q result.
 *
 * @internal Keeping invalid results centralized avoids partially populated
 * diagnostics that would complicate JSON exports and batch summaries.
 */
[[nodiscard]] QProfileResult invalid_result(std::string status,
                                           std::vector<std::string> reasons,
                                           std::string method,
                                           std::optional<double> f_hat = std::nullopt,
                                           std::optional<double> tau_hat = std::nullopt) {
  return QProfileResult{f_hat,
                        tau_hat,
                        std::nullopt,
                        false,
                        std::move(status),
                        std::move(reasons),
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        {},
                        {},
                        {},
                        std::numeric_limits<double>::quiet_NaN(),
                        std::numeric_limits<double>::quiet_NaN(),
                        0U,
                        0U,
                        std::move(method)};
}

} // namespace

ProfileQEstimator::ProfileQEstimator(std::size_t n_grid,
                                     double chi2_threshold,
                                     double tau_max_record_multiplier,
                                     double tau_init_span)
    : n_grid_{n_grid},
      chi2_threshold_{chi2_threshold},
      tau_max_record_multiplier_{tau_max_record_multiplier},
      tau_init_span_{tau_init_span} {
  if (n_grid_ < 25U) {
    throw std::invalid_argument{"n_grid must be at least 25"};
  }
  if (!std::isfinite(chi2_threshold_) || chi2_threshold_ <= 0.0) {
    throw std::invalid_argument{"chi2_threshold must be positive and finite"};
  }
  if (!std::isfinite(tau_max_record_multiplier_) || tau_max_record_multiplier_ <= 1.0) {
    throw std::invalid_argument{"tau_max_record_multiplier must be finite and greater than 1"};
  }
  if (!std::isfinite(tau_init_span_) || tau_init_span_ <= 1.0) {
    throw std::invalid_argument{"tau_init_span must be finite and greater than 1"};
  }
}

QProfileResult ProfileQEstimator::estimate(const std::vector<double>& time,
                                          const std::vector<double>& samples,
                                          const double sample_rate_hz,
                                          const std::optional<double> f_init,
                                          const std::optional<double> tau_init,
                                          const std::optional<std::pair<double, double>> tau_bounds,
                                          const std::optional<std::size_t> n_grid_override) const {
  const std::string method_name = "fixed_frequency_log_tau_variable_projection";

  if (time.size() != samples.size()) {
    throw std::invalid_argument{"t and data must have same length"};
  }
  if (time.size() < 5U) {
    return invalid_result("invalid", {"profile_insufficient_samples"}, method_name);
  }
  for (const auto v : time) {
    if (!std::isfinite(v)) {
      throw std::invalid_argument{"t must contain only finite values"};
    }
  }
  for (const auto v : samples) {
    if (!std::isfinite(v)) {
      throw std::invalid_argument{"data must contain only finite values"};
    }
  }
  if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
    throw std::invalid_argument{"Sampling frequency fs must be positive and finite"};
  }

  auto t_norm = time;
  const auto t0 = t_norm.front();
  for (auto& v : t_norm) {
    v -= t0;
  }
  for (std::size_t i = 1; i < t_norm.size(); ++i) {
    if (t_norm[i] <= t_norm[i - 1U]) {
      throw std::invalid_argument{"t must be strictly increasing"};
    }
  }

  if (!has_resolved_ac_content(samples)) {
    return invalid_result("invalid", {"profile_no_resolved_ac_content"}, method_name);
  }

  double f_hat = 0.0;
  if (!f_init.has_value() || !std::isfinite(*f_init) || *f_init <= 0.0) {
    f_hat = estimate_initial_parameters_from_dft(samples, sample_rate_hz).frequency_hz;
  } else {
    f_hat = *f_init;
  }
  if (!std::isfinite(f_hat) || f_hat <= 0.0 || f_hat >= 0.5 * sample_rate_hz) {
    return invalid_result("failed", {"profile_frequency_missing_or_out_of_range"}, method_name, f_hat);
  }

  const auto [tau_min, tau_max] = tau_bounds_profile(samples,
                                                    sample_rate_hz,
                                                    t_norm,
                                                    tau_init,
                                                    tau_bounds,
                                                    tau_init_span_,
                                                    tau_max_record_multiplier_);

  const auto grid_n = n_grid_override.value_or(n_grid_);
  if (grid_n < 25U) {
    throw std::invalid_argument{"n_grid must be at least 25"};
  }
  const auto n_profile_grid = static_cast<int>(grid_n);

  const auto log_tau_min = std::log(tau_min);
  const auto log_tau_max = std::log(tau_max);
  auto tau_grid = std::vector<double>(static_cast<std::size_t>(n_profile_grid));
  for (int i = 0; i < n_profile_grid; ++i) {
    const auto alpha = static_cast<double>(i) / static_cast<double>(n_profile_grid - 1);
    tau_grid[static_cast<std::size_t>(i)] = std::exp(log_tau_min + alpha * (log_tau_max - log_tau_min));
  }

  auto fits = std::vector<ProjectionFit>{};
  fits.reserve(tau_grid.size());
  for (const auto tau : tau_grid) {
    const auto fit = fit_fixed_tau(t_norm, samples, f_hat, tau);
    if (!fit.has_value()) {
      return invalid_result("failed", {"profile_lstsq_failed:rank_deficient"}, method_name, f_hat);
    }
    fits.push_back(*fit);
  }

  auto rss_grid = std::vector<double>(fits.size());
  for (std::size_t i = 0; i < fits.size(); ++i) {
    rss_grid[i] = fits[i].rss;
  }
  const auto best_grid_idx =
      static_cast<std::size_t>(std::distance(rss_grid.begin(), std::min_element(rss_grid.begin(), rss_grid.end())));
  auto best_fit = fits[best_grid_idx];

  const auto rss_at_log = [&](double log_tau) -> double {
    const auto tau = std::exp(log_tau);
    const auto f = fit_fixed_tau(t_norm, samples, f_hat, tau);
    return f.has_value() ? f->rss : std::numeric_limits<double>::infinity();
  };

  const auto log_opt = minimize_golden(log_tau_min, log_tau_max, rss_at_log, kGoldenSearchIterations);
  const auto refined = fit_fixed_tau(t_norm, samples, f_hat, std::exp(log_opt));
  if (refined.has_value() && std::isfinite(refined->rss)) {
    best_fit = *refined;
  }

  auto tau_profile = std::vector<double>{};
  auto rss_profile = std::vector<double>{};
  tau_profile.reserve(fits.size() + 1U);
  rss_profile.reserve(fits.size() + 1U);
  for (const auto& f : fits) {
    tau_profile.push_back(f.tau);
    rss_profile.push_back(f.rss);
  }
  tau_profile.push_back(best_fit.tau);
  rss_profile.push_back(best_fit.rss);

  auto order = std::vector<std::size_t>(tau_profile.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    return tau_profile[a] < tau_profile[b];
  });
  auto tau_sorted = std::vector<double>(order.size());
  auto rss_sorted = std::vector<double>(order.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    tau_sorted[i] = tau_profile[order[i]];
    rss_sorted[i] = rss_profile[order[i]];
  }
  tau_profile = std::move(tau_sorted);
  rss_profile = std::move(rss_sorted);

  const auto demeaned_sum_sq = [&]() {
    const auto m = mean(samples);
    auto s = 0.0;
    for (const auto v : samples) {
      const auto d = v - m;
      s += d * d;
    }
    return s;
  }();
  const auto rss_scale = std::max(demeaned_sum_sq, 1.0);
  const auto rss_floor = std::numeric_limits<double>::min() * rss_scale;
  const auto rss_min = std::max(best_fit.rss, rss_floor);

  auto profile_delta = std::vector<double>(tau_profile.size());
  for (std::size_t i = 0; i < tau_profile.size(); ++i) {
    const auto r = std::max(rss_profile[i], rss_floor);
    profile_delta[i] = static_cast<double>(best_fit.dof) * std::log(r / rss_min);
    profile_delta[i] = std::max(profile_delta[i], 0.0);
  }

  auto profile_q = std::vector<double>(tau_profile.size());
  for (std::size_t i = 0; i < tau_profile.size(); ++i) {
    profile_q[i] = std::numbers::pi * f_hat * tau_profile[i];
  }

  auto best_index = std::size_t{0};
  auto best_dist = std::numeric_limits<double>::infinity();
  for (std::size_t i = 0; i < tau_profile.size(); ++i) {
    const auto d = std::abs(tau_profile[i] - best_fit.tau);
    if (d < best_dist) {
      best_dist = d;
      best_index = i;
    }
  }

  auto reasons = std::vector<std::string>{};
  if (best_index == 0U) {
    reasons.push_back("profile_min_at_lower_tau_bound");
  }
  if (best_index + 1U == tau_profile.size()) {
    reasons.push_back("profile_min_at_upper_tau_bound");
  }

  const auto amp_threshold =
      std::max(standard_deviation_sample(samples) * 1.0e-12, std::numeric_limits<double>::epsilon());
  if (best_fit.amplitude <= amp_threshold) {
    return invalid_result("invalid", {"profile_amplitude_unresolved"}, method_name, f_hat, best_fit.tau);
  }

  auto inside = std::vector<bool>(profile_delta.size());
  for (std::size_t i = 0; i < profile_delta.size(); ++i) {
    inside[i] = profile_delta[i] <= chi2_threshold_;
  }
  if (!inside[best_index]) {
    return invalid_result("failed", {"profile_minimum_not_inside_threshold_region"}, method_name, f_hat, best_fit.tau);
  }

  auto left = best_index;
  while (left > 0U && inside[left - 1U]) {
    --left;
  }
  auto right = best_index;
  while (right + 1U < inside.size() && inside[right + 1U]) {
    ++right;
  }

  std::optional<double> lower_tau_95;
  std::optional<double> upper_tau_95;
  if (left > 0U) {
    lower_tau_95 = crossing_tau(tau_profile[left - 1U],
                                 profile_delta[left - 1U],
                                 tau_profile[left],
                                 profile_delta[left],
                                 chi2_threshold_);
  }
  if (right + 1U < profile_delta.size()) {
    upper_tau_95 = crossing_tau(tau_profile[right],
                               profile_delta[right],
                               tau_profile[right + 1U],
                               profile_delta[right + 1U],
                               chi2_threshold_);
  }

  const auto q_hat = std::numbers::pi * f_hat * best_fit.tau;
  const auto lower_q_95 =
      lower_tau_95.has_value() ? std::optional<double>{std::numbers::pi * f_hat * *lower_tau_95} : std::nullopt;
  const auto upper_q_95 =
      upper_tau_95.has_value() ? std::optional<double>{std::numbers::pi * f_hat * *upper_tau_95} : std::nullopt;

  const auto valid = lower_q_95.has_value() && upper_q_95.has_value() && reasons.empty();
  std::string status;
  std::optional<double> q_value;
  std::optional<double> ci_lo;
  std::optional<double> ci_hi;
  std::optional<double> lower_limit_95;
  std::optional<double> upper_limit_95;

  if (valid) {
    status = "valid";
    q_value = q_hat;
    ci_lo = *lower_q_95;
    ci_hi = *upper_q_95;
  } else if (lower_q_95.has_value() && !upper_q_95.has_value()) {
    status = "lower_limit";
    reasons.push_back("profile_open_high");
    lower_limit_95 = *lower_q_95;
  } else if (!lower_q_95.has_value() && upper_q_95.has_value()) {
    status = "upper_limit";
    reasons.push_back("profile_open_low");
    upper_limit_95 = *upper_q_95;
  } else {
    status = "unbounded";
    reasons.push_back("profile_does_not_cross_threshold");
  }

  const auto profile_point_count = tau_profile.size();
  return QProfileResult{std::optional<double>{f_hat},
                        std::optional<double>{best_fit.tau},
                        q_value,
                        valid,
                        std::move(status),
                        std::move(reasons),
                        ci_lo,
                        ci_hi,
                        lower_limit_95,
                        upper_limit_95,
                        std::move(tau_profile),
                        std::move(profile_q),
                        std::move(profile_delta),
                        rss_min,
                        best_fit.sigma,
                        best_fit.dof,
                        profile_point_count,
                        method_name};
}

} // namespace ringdown

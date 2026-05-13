#include <ringdown/estimators.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>

namespace ringdown {

namespace {

constexpr auto kGoldenRatioConjugate = 0.6180339887498948482;
constexpr auto kGoldenSearchIterations = std::size_t{32U};
constexpr auto kAlternatingFitIterations = std::size_t{3U};

[[nodiscard]] double mean(const std::vector<double>& values) {
  return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}

[[nodiscard]] double standard_deviation(const std::vector<double>& values) {
  const auto avg = mean(values);
  auto sum = 0.0;
  for (const auto value : values) {
    const auto delta = value - avg;
    sum += delta * delta;
  }
  return std::sqrt(sum / static_cast<double>(values.size()));
}

[[nodiscard]] double max_abs(const std::vector<double>& values) {
  auto result = 0.0;
  for (const auto value : values) {
    result = std::max(result, std::abs(value));
  }
  return result;
}

void validate_signal_input(const std::vector<double>& samples) {
  if (samples.empty()) {
    throw std::invalid_argument{"Signal x cannot be empty; need at least one sample for estimation"};
  }
  if (samples.size() == 1U) {
    throw std::invalid_argument{
        "Signal x must have at least 2 samples; single-sample signals cannot be used"};
  }
  for (const auto sample : samples) {
    if (std::isnan(sample)) {
      throw std::invalid_argument{"Signal x contains NaN; cannot perform frequency estimation"};
    }
    if (std::isinf(sample)) {
      throw std::invalid_argument{"Signal x contains Inf; cannot perform frequency estimation"};
    }
  }
}

void validate_sample_rate(double sample_rate_hz) {
  if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
    throw std::invalid_argument{"Sampling frequency fs must be positive and finite"};
  }
}

[[nodiscard]] bool has_resolved_ac_content(const std::vector<double>& samples) {
  const auto avg = mean(samples);
  auto demeaned_peak = 0.0;
  for (const auto sample : samples) {
    demeaned_peak = std::max(demeaned_peak, std::abs(sample - avg));
  }
  const auto threshold =
      std::max(max_abs(samples) * 1.0e-12, std::numeric_limits<double>::epsilon() * 10.0);
  return demeaned_peak > threshold;
}

[[nodiscard]] double amplitude_floor(const std::vector<double>& samples) {
  return std::max({standard_deviation(samples) * std::sqrt(2.0),
                   max_abs(samples) * 1.0e-6,
                   std::numeric_limits<double>::epsilon()});
}

[[nodiscard]] std::size_t next_power_of_two(std::size_t value) {
  auto result = std::size_t{1};
  while (result < value) {
    result <<= 1U;
  }
  return result;
}

[[nodiscard]] bool is_power_of_two(std::size_t value) { return value != 0U && (value & (value - 1U)) == 0U; }

[[nodiscard]] double modified_bessel_i0(double value) {
  auto term = 1.0;
  auto sum = 1.0;
  const auto quarter_x2 = (value * value) / 4.0;
  for (auto order = std::size_t{1}; order < 32U; ++order) {
    const auto order_f = static_cast<double>(order);
    term *= quarter_x2 / (order_f * order_f);
    sum += term;
    if (std::abs(term) < std::abs(sum) * 1.0e-15) {
      break;
    }
  }
  return sum;
}

[[nodiscard]] std::vector<double> window_values(WindowType window, std::size_t count, double beta) {
  auto values = std::vector<double>(count, 1.0);
  if (count == 1U || window == WindowType::Rectangular) {
    return values;
  }

  const auto denominator = static_cast<double>(count - 1U);
  for (auto index = std::size_t{0}; index < count; ++index) {
    const auto ratio = static_cast<double>(index) / denominator;
    if (window == WindowType::Hann) {
      values[index] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * ratio);
    } else if (window == WindowType::Blackman) {
      values[index] = 0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * ratio) +
                      0.08 * std::cos(4.0 * std::numbers::pi * ratio);
    } else if (window == WindowType::Kaiser) {
      const auto arg = beta * std::sqrt(std::max(0.0, 1.0 - std::pow(2.0 * ratio - 1.0, 2.0)));
      values[index] = modified_bessel_i0(arg) / modified_bessel_i0(beta);
    }
  }
  return values;
}

void fft(std::vector<std::complex<double>>& values) {
  const auto count = values.size();
  auto reversed = std::size_t{0};
  for (auto index = std::size_t{1}; index < count; ++index) {
    auto bit = count >> 1U;
    while ((reversed & bit) != 0U) {
      reversed ^= bit;
      bit >>= 1U;
    }
    reversed ^= bit;
    if (index < reversed) {
      std::swap(values[index], values[reversed]);
    }
  }

  for (auto length = std::size_t{2}; length <= count; length <<= 1U) {
    const auto angle = -2.0 * std::numbers::pi / static_cast<double>(length);
    const auto step = std::complex<double>{std::cos(angle), std::sin(angle)};
    for (auto start = std::size_t{0}; start < count; start += length) {
      auto factor = std::complex<double>{1.0, 0.0};
      for (auto offset = std::size_t{0}; offset < length / 2U; ++offset) {
        const auto even = values[start + offset];
        const auto odd = factor * values[start + offset + length / 2U];
        values[start + offset] = even + odd;
        values[start + offset + length / 2U] = even - odd;
        factor *= step;
      }
    }
  }
}

[[nodiscard]] std::vector<double> power_spectrum(const std::vector<double>& samples,
                                                 double sample_rate_hz,
                                                 const DFTOptions& options,
                                                 std::size_t& dft_size) {
  const auto count = samples.size();
  auto target_size = options.use_zero_padding ? options.pad_factor * count : count;
  if (target_size < count) {
    target_size = count;
  }
  dft_size = is_power_of_two(target_size) ? target_size : next_power_of_two(target_size);

  const auto avg = mean(samples);
  const auto window = window_values(options.window, count, options.kaiser_beta);
  auto values = std::vector<std::complex<double>>(dft_size, {0.0, 0.0});
  for (auto index = std::size_t{0}; index < count; ++index) {
    values[index] = std::complex<double>{(samples[index] - avg) * window[index], 0.0};
  }

  fft(values);

  const auto output_size = (dft_size / 2U) + 1U;
  auto power = std::vector<double>(output_size);
  for (auto index = std::size_t{0}; index < output_size; ++index) {
    power[index] = std::norm(values[index]);
  }
  (void)sample_rate_hz;
  return power;
}

[[nodiscard]] double parabolic_peak_delta(const std::vector<double>& power, std::size_t peak) {
  if (peak == 0U || peak + 1U >= power.size()) {
    return 0.0;
  }
  const auto left = std::log(std::max(power[peak - 1U], std::numeric_limits<double>::min()));
  const auto center = std::log(std::max(power[peak], std::numeric_limits<double>::min()));
  const auto right = std::log(std::max(power[peak + 1U], std::numeric_limits<double>::min()));
  const auto denominator = left - 2.0 * center + right;
  if (std::abs(denominator) < std::numeric_limits<double>::epsilon()) {
    return 0.0;
  }
  return std::clamp(0.5 * (left - right) / denominator, -1.0, 1.0);
}

[[nodiscard]] EstimationResult dft_frequency_result(const std::vector<double>& samples,
                                                    double sample_rate_hz,
                                                    const DFTOptions& options) {
  validate_signal_input(samples);
  validate_sample_rate(sample_rate_hz);

  if (!has_resolved_ac_content(samples)) {
    return EstimationResult{sample_rate_hz / static_cast<double>(std::max<std::size_t>(samples.size(), 2U)),
                            std::nullopt,
                            std::nullopt,
                            false,
                            true,
                            "Signal has no resolved AC content after demeaning",
                            0U};
  }

  auto dft_size = std::size_t{0};
  const auto power = power_spectrum(samples, sample_rate_hz, options, dft_size);
  auto min_bin = std::size_t{1};
  if (options.minimum_frequency_hz > 0.0) {
    min_bin = std::max<std::size_t>(
        1U, static_cast<std::size_t>(std::ceil(options.minimum_frequency_hz *
                                               static_cast<double>(dft_size) / sample_rate_hz)));
  }
  if (min_bin >= power.size()) {
    min_bin = power.size() - 1U;
  }

  auto peak = min_bin;
  for (auto index = min_bin + 1U; index < power.size(); ++index) {
    if (power[index] > power[peak]) {
      peak = index;
    }
  }

  if (peak == 0U || peak + 1U >= power.size()) {
    const auto frequency = static_cast<double>(peak) * sample_rate_hz / static_cast<double>(dft_size);
    return EstimationResult{frequency,
                            std::nullopt,
                            std::nullopt,
                            false,
                            true,
                            "DFT peak occurred at the FFT edge; skipping interpolation",
                            std::nullopt};
  }

  const auto delta = parabolic_peak_delta(power, peak);
  const auto frequency = (static_cast<double>(peak) + delta) * sample_rate_hz / static_cast<double>(dft_size);
  return EstimationResult{frequency,
                          std::nullopt,
                          std::nullopt,
                          true,
                          false,
                          "DFT frequency estimated via interpolated FFT peak",
                          std::nullopt};
}

struct LinearFit {
  double amplitude{0.0};
  double phase{0.0};
  double offset{0.0};
  double rss{std::numeric_limits<double>::infinity()};
  bool success{false};
};

[[nodiscard]] std::optional<std::array<double, 3>> solve_3x3(std::array<std::array<double, 4>, 3> matrix) {
  for (auto col = std::size_t{0}; col < 3U; ++col) {
    auto pivot = col;
    for (auto row = col + 1U; row < 3U; ++row) {
      if (std::abs(matrix[row][col]) > std::abs(matrix[pivot][col])) {
        pivot = row;
      }
    }
    if (std::abs(matrix[pivot][col]) < 1.0e-20) {
      return std::nullopt;
    }
    if (pivot != col) {
      std::swap(matrix[pivot], matrix[col]);
    }
    const auto scale = matrix[col][col];
    for (auto item = col; item < 4U; ++item) {
      matrix[col][item] /= scale;
    }
    for (auto row = std::size_t{0}; row < 3U; ++row) {
      if (row == col) {
        continue;
      }
      const auto factor = matrix[row][col];
      for (auto item = col; item < 4U; ++item) {
        matrix[row][item] -= factor * matrix[col][item];
      }
    }
  }
  return std::array<double, 3>{matrix[0][3], matrix[1][3], matrix[2][3]};
}

[[nodiscard]] LinearFit fit_fixed_frequency_tau(const std::vector<double>& samples,
                                                double sample_rate_hz,
                                                double frequency_hz,
                                                double tau) {
  if (!std::isfinite(frequency_hz) || frequency_hz < 0.0 || !std::isfinite(tau) || tau <= 0.0) {
    return {};
  }

  auto normal = std::array<std::array<double, 4>, 3>{};
  auto sample_sum_squares = 0.0;
  const auto dt = 1.0 / sample_rate_hz;
  const auto envelope_step = std::exp(-dt / tau);
  const auto angle_step = 2.0 * std::numbers::pi * frequency_hz * dt;
  const auto cos_step = std::cos(angle_step);
  const auto sin_step = std::sin(angle_step);
  auto envelope = 1.0;
  auto cos_value = 1.0;
  auto sin_value = 0.0;
  for (auto index = std::size_t{0}; index < samples.size(); ++index) {
    const auto row = std::array<double, 3>{envelope * cos_value, envelope * sin_value, 1.0};
    sample_sum_squares += samples[index] * samples[index];
    for (auto lhs = std::size_t{0}; lhs < 3U; ++lhs) {
      for (auto rhs = std::size_t{0}; rhs < 3U; ++rhs) {
        normal[lhs][rhs] += row[lhs] * row[rhs];
      }
      normal[lhs][3] += row[lhs] * samples[index];
    }
    const auto next_cos = cos_value * cos_step - sin_value * sin_step;
    const auto next_sin = sin_value * cos_step + cos_value * sin_step;
    cos_value = next_cos;
    sin_value = next_sin;
    envelope *= envelope_step;
  }

  const auto solution = solve_3x3(normal);
  if (!solution.has_value()) {
    return {};
  }

  const auto explained_sum_squares = (*solution)[0] * normal[0][3] + (*solution)[1] * normal[1][3] +
                                     (*solution)[2] * normal[2][3];
  const auto rss = std::max(0.0, sample_sum_squares - explained_sum_squares);

  return LinearFit{std::hypot((*solution)[0], (*solution)[1]),
                   std::atan2(-(*solution)[1], (*solution)[0]),
                   (*solution)[2],
                   rss,
                   true};
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
  return (left + right) / 2.0;
}

[[nodiscard]] double sanitize_tau_guess(std::optional<double> tau_guess,
                                        double sample_rate_hz,
                                        std::size_t sample_count,
                                        double& lower,
                                        double& upper) {
  lower = std::max(1.0 / sample_rate_hz, std::numeric_limits<double>::epsilon());
  const auto t_last = static_cast<double>(sample_count - 1U) / sample_rate_hz;
  const auto default_upper = std::max(10.0 * t_last, lower * 10.0);
  auto guess = tau_guess.value_or(std::max(0.5 * t_last, lower * 2.0));
  if (!std::isfinite(guess) || guess <= 0.0) {
    guess = std::max(0.5 * t_last, lower * 2.0);
  }
  upper = std::max(default_upper, guess * 1.1);
  return std::clamp(guess, lower * 1.01, upper * 0.99);
}

[[nodiscard]] EstimationResult estimate_with_separable_nls(const std::vector<double>& samples,
                                                           double sample_rate_hz,
                                                           std::optional<double> known_tau,
                                                           std::optional<double> tau_initial,
                                                           std::optional<InitialParameters> initial) {
  validate_signal_input(samples);
  validate_sample_rate(sample_rate_hz);
  const auto init = initial.value_or(estimate_initial_parameters_from_dft(samples, sample_rate_hz));

  if (!has_resolved_ac_content(samples)) {
    const auto q = known_tau.has_value() ? std::optional<double>{std::numbers::pi * init.frequency_hz * *known_tau}
                                        : std::nullopt;
    return EstimationResult{init.frequency_hz,
                            known_tau,
                            q,
                            false,
                            true,
                            "Signal has no resolved AC content after demeaning",
                            0U};
  }

  const auto df = sample_rate_hz / static_cast<double>(samples.size());
  const auto f_low = std::max(0.0, init.frequency_hz - std::max(0.2 * init.frequency_hz, 2.0 * df));
  const auto f_high = std::min(0.5 * sample_rate_hz,
                               init.frequency_hz + std::max(0.2 * init.frequency_hz, 2.0 * df));

  auto tau_lower = 0.0;
  auto tau_upper = 0.0;
  auto tau = known_tau.value_or(sanitize_tau_guess(tau_initial, sample_rate_hz, samples.size(), tau_lower, tau_upper));
  if (known_tau.has_value()) {
    tau_lower = *known_tau;
    tau_upper = *known_tau;
  }

  auto frequency = init.frequency_hz;
  const auto optimize_frequency = [&]() {
    frequency = minimize_golden(
        f_low,
        f_high,
        [&](double candidate) { return fit_fixed_frequency_tau(samples, sample_rate_hz, candidate, tau).rss; },
        kGoldenSearchIterations);
  };
  const auto optimize_tau = [&]() {
    tau = minimize_golden(
        tau_lower,
        tau_upper,
        [&](double candidate) {
          return fit_fixed_frequency_tau(samples, sample_rate_hz, frequency, candidate).rss;
        },
        kGoldenSearchIterations);
  };

  if (known_tau.has_value()) {
    optimize_frequency();
  } else {
    for (auto iteration = std::size_t{0}; iteration < kAlternatingFitIterations; ++iteration) {
      optimize_frequency();
      optimize_tau();
    }
    optimize_frequency();
  }

  const auto fit = fit_fixed_frequency_tau(samples, sample_rate_hz, frequency, tau);
  if (!fit.success || !std::isfinite(frequency) || frequency < 0.0 || frequency > 0.5 * sample_rate_hz) {
    return EstimationResult{init.frequency_hz,
                            known_tau,
                            known_tau.has_value()
                                ? std::optional<double>{std::numbers::pi * init.frequency_hz * *known_tau}
                                : std::nullopt,
                            false,
                            true,
                            "Separable NLS fit failed frequency sanity check",
                            std::nullopt};
  }

  if (!known_tau.has_value() && (!std::isfinite(tau) || tau <= 0.0 || tau < tau_lower || tau > tau_upper)) {
    return EstimationResult{frequency,
                            std::nullopt,
                            std::nullopt,
                            false,
                            true,
                            "Separable NLS fit failed tau sanity check",
                            std::nullopt};
  }

  return EstimationResult{frequency,
                          tau,
                          std::numbers::pi * frequency * tau,
                          true,
                          false,
                          "Separable nonlinear least-squares fit converged",
                          std::nullopt};
}

} // namespace

DFTFrequencyEstimator::DFTFrequencyEstimator(DFTOptions options) : options_{options} {
  if (options_.pad_factor == 0U) {
    options_.pad_factor = 1U;
  }
}

double DFTFrequencyEstimator::estimate(const std::vector<double>& samples, double sample_rate_hz) const {
  return estimate_full(samples, sample_rate_hz).frequency_hz;
}

EstimationResult DFTFrequencyEstimator::estimate_full(const std::vector<double>& samples,
                                                      double sample_rate_hz) const {
  auto result = dft_frequency_result(samples, sample_rate_hz, options_);
  if (!result.success) {
    return result;
  }

  auto tau_lower = 0.0;
  auto tau_upper = 0.0;
  auto tau = sanitize_tau_guess(estimate_initial_tau_from_envelope(samples, sample_rate_hz),
                                sample_rate_hz,
                                samples.size(),
                                tau_lower,
                                tau_upper);
  tau = minimize_golden(
      tau_lower,
      tau_upper,
      [&](double candidate) {
        return fit_fixed_frequency_tau(samples, sample_rate_hz, result.frequency_hz, candidate).rss;
      },
      kGoldenSearchIterations);
  if (!std::isfinite(tau) || tau <= 0.0) {
    result.success = false;
    result.used_fallback = true;
    result.message = "DFT tau fit failed tau sanity check";
    return result;
  }

  result.tau = tau;
  result.quality_factor = std::numbers::pi * result.frequency_hz * tau;
  return result;
}

NLSFrequencyEstimator::NLSFrequencyEstimator(std::optional<double> known_tau) : known_tau_{known_tau} {
  if (known_tau_.has_value() && (!std::isfinite(*known_tau_) || *known_tau_ <= 0.0)) {
    throw std::invalid_argument{"tau_known must be positive and finite"};
  }
}

double NLSFrequencyEstimator::estimate(const std::vector<double>& samples, double sample_rate_hz) const {
  return estimate_full(samples, sample_rate_hz).frequency_hz;
}

EstimationResult NLSFrequencyEstimator::estimate_full(const std::vector<double>& samples,
                                                      double sample_rate_hz,
                                                      std::optional<double> tau_initial,
                                                      std::optional<InitialParameters> initial) const {
  return estimate_with_separable_nls(samples, sample_rate_hz, known_tau_, tau_initial, initial);
}

InitialParameters estimate_initial_parameters_from_dft(const std::vector<double>& samples,
                                                       double sample_rate_hz) {
  validate_signal_input(samples);
  validate_sample_rate(sample_rate_hz);
  auto options = DFTOptions{};
  options.window = WindowType::Hann;
  options.use_zero_padding = false;
  const auto dft = dft_frequency_result(samples, sample_rate_hz, options);
  const auto avg = mean(samples);
  return InitialParameters{dft.frequency_hz, 0.0, std::max(amplitude_floor(samples), 1.0e-12), avg};
}

double estimate_initial_tau_from_envelope(const std::vector<double>& samples, double sample_rate_hz) {
  validate_signal_input(samples);
  validate_sample_rate(sample_rate_hz);
  if (samples.size() < 10U) {
    return std::max((static_cast<double>(samples.size() - 1U) / sample_rate_hz) / 2.0,
                    1.0 / sample_rate_hz);
  }

  const auto window_size = std::max<std::size_t>(1U, std::min<std::size_t>(1000U, samples.size() / 10U));
  const auto window_count = samples.size() / window_size;
  auto rms_values = std::vector<double>(window_count);
  for (auto window = std::size_t{0}; window < window_count; ++window) {
    auto chunk = std::vector<double>{};
    chunk.reserve(window_size);
    const auto start = window * window_size;
    for (auto offset = std::size_t{0}; offset < window_size; ++offset) {
      chunk.push_back(samples[start + offset]);
    }
    rms_values[window] = standard_deviation(chunk);
  }

  const auto peak = *std::max_element(rms_values.begin(), rms_values.end());
  for (auto index = std::size_t{0}; index < rms_values.size(); ++index) {
    if (rms_values[index] < peak * std::exp(-1.0) && index > 0U) {
      return std::max(static_cast<double>(index * window_size) / sample_rate_hz, 1.0 / sample_rate_hz);
    }
  }

  return std::max((static_cast<double>(samples.size() - 1U) / sample_rate_hz) / 2.0,
                  1.0 / sample_rate_hz);
}

} // namespace ringdown

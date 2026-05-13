#include <ringdown/ringdown.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

namespace {

template <typename Function>
auto measure(std::string_view name, Function&& function) {
  const auto start = std::chrono::steady_clock::now();
  auto result = function();
  const auto stop = std::chrono::steady_clock::now();
  const auto elapsed_ms =
      std::chrono::duration<double, std::milli>(stop - start).count();
  std::cout << name << "_ms=" << elapsed_ms << '\n';
  return result;
}

template <typename Function>
void measure_void(std::string_view name, Function&& function) {
  const auto start = std::chrono::steady_clock::now();
  function();
  const auto stop = std::chrono::steady_clock::now();
  const auto elapsed_ms =
      std::chrono::duration<double, std::milli>(stop - start).count();
  std::cout << name << "_ms=" << elapsed_ms << '\n';
}

[[nodiscard]] std::string_view build_type() {
#ifdef NDEBUG
  return "Release";
#else
  return "Debug";
#endif
}

[[nodiscard]] std::string_view compiler_name() {
#if defined(__clang__)
  return "Clang";
#elif defined(__GNUC__)
  return "GCC";
#elif defined(_MSC_VER)
  return "MSVC";
#else
  return "unknown";
#endif
}

[[nodiscard]] std::string command_line(int argc, char** argv) {
  auto out = std::ostringstream{};
  for (auto index = 0; index < argc; ++index) {
    if (index != 0) {
      out << ' ';
    }
    out << argv[index];
  }
  return out.str();
}

[[nodiscard]] std::optional<std::filesystem::path> first_file_with_suffix(
    const std::filesystem::path& dir,
    std::string_view suffix) {
  if (!std::filesystem::exists(dir)) {
    return std::nullopt;
  }

  auto paths = std::vector<std::filesystem::path>{};
  for (const auto& entry : std::filesystem::directory_iterator{dir}) {
    if (entry.is_regular_file() && entry.path().extension() == suffix) {
      paths.push_back(entry.path());
    }
  }
  if (paths.empty()) {
    return std::nullopt;
  }
  std::sort(paths.begin(), paths.end());
  return paths.front();
}

[[nodiscard]] std::optional<std::filesystem::path> first_data_file(std::string_view suffix) {
  const auto candidates = std::vector<std::filesystem::path>{
      std::filesystem::path{"tests/fixtures/reference"},
      std::filesystem::path{"../tests/fixtures/reference"},
      std::filesystem::path{"../../tests/fixtures/reference"},
      std::filesystem::path{".read-only/RingDownAnalysis/data"},
      std::filesystem::path{"../.read-only/RingDownAnalysis/data"},
      std::filesystem::path{"../../.read-only/RingDownAnalysis/data"},
  };
  for (const auto& candidate : candidates) {
    if (const auto path = first_file_with_suffix(candidate, suffix)) {
      return path;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::pair<std::vector<double>, std::vector<double>> crop_to_tau(
    const std::vector<double>& time,
    const std::vector<double>& samples,
    double tau) {
  auto cropped_time = std::vector<double>{};
  auto cropped_samples = std::vector<double>{};
  for (auto index = std::size_t{0}; index < time.size(); ++index) {
    if (time[index] <= tau) {
      cropped_time.push_back(time[index]);
      cropped_samples.push_back(samples[index]);
    }
  }
  if (cropped_time.size() < 1000U) {
    return {time, samples};
  }
  return {cropped_time, cropped_samples};
}

} // namespace

int main(int argc, char** argv) {
  std::cout << "build_type=" << build_type() << '\n';
  std::cout << "ndebug=";
#ifdef NDEBUG
  std::cout << "true\n";
#else
  std::cout << "false\n";
  std::cout << "warning=Debug build timings are not production performance data\n";
#endif
  std::cout << "compiler=" << compiler_name() << '\n';
  std::cout << "command_line=" << command_line(argc, argv) << '\n';

  auto parameters = ringdown::SignalParameters{};
  parameters.sample_count = 10000U;
  parameters.snr_db = 60.0;

  auto generated = measure("signal_generation_10k", [&] {
    return ringdown::RingDownSignal{parameters}.generate(0.2, 42U);
  });

  const auto full_dft = measure("dft_estimate_10k", [&] {
    return ringdown::DFTFrequencyEstimator{}.estimate_full(generated.samples, parameters.sample_rate_hz);
  });

  const auto full_nls = measure("nls_estimate_10k", [&] {
    return ringdown::NLSFrequencyEstimator{}.estimate_full(generated.samples, parameters.sample_rate_hz);
  });

  auto initial = ringdown::InitialParameters{};
  measure_void("stage_initial_dft_10k", [&] {
    initial = ringdown::estimate_initial_parameters_from_dft(generated.samples, parameters.sample_rate_hz);
  });

  auto tau_seed = 0.0;
  measure_void("stage_envelope_tau_seed_10k", [&] {
    tau_seed = ringdown::estimate_initial_tau_from_envelope(generated.samples, parameters.sample_rate_hz);
  });

  auto analyzer = ringdown::RingDownAnalyzer{};
  auto tau_estimate = 0.0;
  measure_void("stage_full_record_tau_estimation_10k", [&] {
    tau_estimate = analyzer.estimate_tau(generated.time, generated.samples, parameters.sample_rate_hz, tau_seed, initial);
  });

  const auto [cropped_time, cropped_samples] = crop_to_tau(generated.time, generated.samples, tau_estimate);
  const auto cropped_tau_seed = std::max(tau_estimate, 1.0 / parameters.sample_rate_hz);

  const auto nls = measure("stage_cropped_nls_10k", [&] {
    return ringdown::NLSFrequencyEstimator{}.estimate_full(
        cropped_samples, parameters.sample_rate_hz, cropped_tau_seed, initial);
  });

  const auto dft = measure("stage_cropped_dft_tau_fit_10k", [&] {
    return ringdown::DFTFrequencyEstimator{}.estimate_full(cropped_samples, parameters.sample_rate_hz);
  });

  measure_void("stage_noise_fit_10k", [&] {
    const auto tau_model = nls.tau.value_or(dft.tau.value_or(tau_estimate));
    (void)analyzer.estimate_noise_parameters(cropped_time, cropped_samples, tau_model, nls.frequency_hz);
  });

  auto analysis = measure("array_analysis_10k", [&] {
    return ringdown::RingDownAnalyzer{}.analyze_array(generated.time, generated.samples);
  });

  auto mc_options = ringdown::MonteCarloOptions{};
  mc_options.signal.sample_count = 2048U;
  mc_options.trial_count = 8U;
  mc_options.worker_count = 2U;
  auto mc = measure("monte_carlo_8x2048", [&] {
    return ringdown::MonteCarloAnalyzer{}.run(mc_options);
  });

  const auto process = ringdown::ProcessResult{{analysis}, {}};
  measure_void("batch_json_summary_one_result", [&] {
    const auto json = ringdown::to_json(process);
    (void)json;
  });
  measure_void("batch_notebook_json_one_result", [&] {
    const auto json = ringdown::to_json_notebook(analysis);
    (void)json;
  });

  if (const auto csv_path = first_data_file(".csv")) {
    try {
      measure_void("csv_load_only", [&] {
        const auto loaded = ringdown::RingDownDataLoader::load_csv(csv_path->string());
        (void)loaded;
      });
      std::cout << "csv_load_path=" << csv_path->generic_string() << '\n';
    } catch (const std::exception& error) {
      std::cout << "csv_load_skipped=1\n";
      std::cout << "csv_load_skip_reason=" << error.what() << '\n';
    }
  } else {
    std::cout << "csv_load_skipped=1\n";
  }
  if (const auto mat_path = first_data_file(".mat")) {
    try {
      measure_void("mat_load_only", [&] {
        const auto loaded = ringdown::RingDownDataLoader::load_mat(mat_path->string());
        (void)loaded;
      });
      std::cout << "mat_load_path=" << mat_path->generic_string() << '\n';
    } catch (const std::exception& error) {
      std::cout << "mat_load_skipped=1\n";
      std::cout << "mat_load_skip_reason=" << error.what() << '\n';
    }
  } else {
    std::cout << "mat_load_skipped=1\n";
  }

  std::cout << "dft_frequency_hz=" << dft.frequency_hz << '\n';
  std::cout << "dft_evaluations=" << dft.evaluations.value_or(0U) << '\n';
  std::cout << "nls_frequency_hz=" << nls.frequency_hz << '\n';
  std::cout << "nls_evaluations=" << nls.evaluations.value_or(0U) << '\n';
  std::cout << "full_dft_evaluations=" << full_dft.evaluations.value_or(0U) << '\n';
  std::cout << "full_nls_evaluations=" << full_nls.evaluations.value_or(0U) << '\n';
  std::cout << "analysis_frequency_hz=" << analysis.nls.frequency_hz << '\n';
  std::cout << "mc_nls_trials=" << mc.nls_frequency_errors.size() << '\n';
  return 0;
}

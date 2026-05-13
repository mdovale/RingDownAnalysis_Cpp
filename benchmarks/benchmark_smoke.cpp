#include <ringdown/ringdown.hpp>

#include <chrono>
#include <iostream>
#include <string_view>

namespace {

template <typename Function>
void measure(std::string_view name, Function&& function) {
  const auto start = std::chrono::steady_clock::now();
  function();
  const auto stop = std::chrono::steady_clock::now();
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
  std::cout << name << "_ms=" << elapsed_ms << '\n';
}

} // namespace

int main() {
  auto parameters = ringdown::SignalParameters{};
  parameters.sample_count = 10000U;
  parameters.snr_db = 60.0;

  auto generated = ringdown::GeneratedSignal{};
  measure("signal_generation_10k", [&] {
    generated = ringdown::RingDownSignal{parameters}.generate(0.2, 42U);
  });

  auto dft = ringdown::EstimationResult{};
  measure("dft_estimate_10k", [&] {
    dft = ringdown::DFTFrequencyEstimator{}.estimate_full(generated.samples, parameters.sample_rate_hz);
  });

  auto nls = ringdown::EstimationResult{};
  measure("nls_estimate_10k", [&] {
    nls = ringdown::NLSFrequencyEstimator{}.estimate_full(generated.samples, parameters.sample_rate_hz);
  });

  auto analysis = ringdown::AnalyzerResult{};
  measure("array_analysis_10k", [&] {
    analysis = ringdown::RingDownAnalyzer{}.analyze_array(generated.time, generated.samples);
  });

  auto mc_options = ringdown::MonteCarloOptions{};
  mc_options.signal.sample_count = 2048U;
  mc_options.trial_count = 8U;
  mc_options.worker_count = 2U;
  auto mc = ringdown::MonteCarloResult{};
  measure("monte_carlo_8x2048", [&] {
    mc = ringdown::MonteCarloAnalyzer{}.run(mc_options);
  });

  std::cout << "dft_frequency_hz=" << dft.frequency_hz << '\n';
  std::cout << "nls_frequency_hz=" << nls.frequency_hz << '\n';
  std::cout << "analysis_frequency_hz=" << analysis.nls.frequency_hz << '\n';
  std::cout << "mc_nls_trials=" << mc.nls_frequency_errors.size() << '\n';
  return 0;
}

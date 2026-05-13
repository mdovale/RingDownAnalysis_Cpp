#pragma once

#include <cstddef>
#include <vector>

#include <ringdown/crlb.hpp>
#include <ringdown/estimators.hpp>
#include <ringdown/signal.hpp>

namespace ringdown {

struct ErrorStatistics {
  double mean{0.0};
  double standard_deviation{0.0};
  double rmse{0.0};
};

struct MonteCarloResult {
  double frequency_hz{0.0};
  double quality_factor{0.0};
  double tau{0.0};
  double sample_rate_hz{0.0};
  std::size_t sample_count{0};
  double snr_db{0.0};
  double crlb_std_f{0.0};
  double crlb_std_q{0.0};
  std::vector<double> nls_frequency_errors;
  std::vector<double> dft_frequency_errors;
  std::vector<double> nls_q_errors;
  std::vector<double> dft_q_errors;
  ErrorStatistics nls_statistics;
  ErrorStatistics dft_statistics;
  ErrorStatistics nls_q_statistics;
  ErrorStatistics dft_q_statistics;
};

struct MonteCarloOptions {
  SignalParameters signal;
  std::size_t trial_count{100};
  unsigned long long seed{42};
  std::size_t worker_count{1};
};

class MonteCarloAnalyzer {
public:
  MonteCarloAnalyzer(NLSFrequencyEstimator nls_estimator = NLSFrequencyEstimator{},
                     DFTFrequencyEstimator dft_estimator = DFTFrequencyEstimator{});

  [[nodiscard]] MonteCarloResult run(const MonteCarloOptions& options) const;

private:
  NLSFrequencyEstimator nls_estimator_;
  DFTFrequencyEstimator dft_estimator_;
};

} // namespace ringdown

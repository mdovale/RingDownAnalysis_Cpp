#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <ringdown/estimators.hpp>

namespace ringdown {

struct NoiseEstimate {
  double amplitude{0.0};
  double sigma{0.0};
  double sigma_mle{0.0};
  std::size_t degrees_of_freedom{0};
  bool success{false};
  std::string method;
  std::string message;
};

struct AnalyzerResult {
  std::vector<double> time;
  std::vector<double> samples;
  std::vector<double> cropped_time;
  std::vector<double> cropped_samples;
  std::vector<double> secondary_samples;
  double sample_rate_hz{0.0};
  double tau_seed{0.0};
  double tau_estimate{0.0};
  double tau_model{0.0};
  EstimationResult nls;
  EstimationResult dft;
  NoiseEstimate noise;
  double plugin_crlb_variance_f{0.0};
  double plugin_crlb_std_f{0.0};
  bool uncertainty_valid{false};
  std::size_t sample_count{0};
  std::size_t cropped_sample_count{0};
  double observation_time{0.0};
  double cropped_observation_time{0.0};
  std::string filename;
  std::string file_type;
};

class RingDownAnalyzer {
public:
  RingDownAnalyzer(NLSFrequencyEstimator nls_estimator = NLSFrequencyEstimator{},
                   DFTFrequencyEstimator dft_estimator = DFTFrequencyEstimator{});

  [[nodiscard]] AnalyzerResult analyze_array(const std::vector<double>& samples,
                                             double sample_rate_hz,
                                             double max_tau_multiplier = 1.0) const;
  [[nodiscard]] AnalyzerResult analyze_array(const std::vector<double>& time,
                                             const std::vector<double>& samples,
                                             double max_tau_multiplier = 1.0) const;
  [[nodiscard]] AnalyzerResult analyze_file(const std::string& filepath,
                                            double max_tau_multiplier = 1.0) const;

  [[nodiscard]] double estimate_tau(const std::vector<double>& time,
                                    const std::vector<double>& samples,
                                    double sample_rate_hz) const;
  [[nodiscard]] NoiseEstimate estimate_noise_parameters(const std::vector<double>& time,
                                                       const std::vector<double>& samples,
                                                       double tau_model,
                                                       double frequency_hz) const;

private:
  NLSFrequencyEstimator nls_estimator_;
  DFTFrequencyEstimator dft_estimator_;
};

struct LoadedData {
  std::vector<double> time;
  std::vector<double> samples;
  std::vector<double> secondary_samples;
  std::string file_type;
};

class RingDownDataLoader {
public:
  [[nodiscard]] static LoadedData load(const std::string& filepath);
  [[nodiscard]] static LoadedData load_csv(const std::string& filepath);
  [[nodiscard]] static LoadedData load_mat(const std::string& filepath);
  [[nodiscard]] static LoadedData load_zip(const std::string& filepath);
};

[[nodiscard]] std::string to_json(const AnalyzerResult& result);

/// JSON export aligned with Python `RingDownAnalyzer.analyze_array` / `analyze_file`
/// result dicts (waveforms, estimator diagnostics, uncertainty aliases) for notebooks
/// and downstream tooling. Arrays may be large.
[[nodiscard]] std::string to_json_notebook(const AnalyzerResult& result);

} // namespace ringdown

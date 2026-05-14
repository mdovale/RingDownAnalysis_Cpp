#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <ringdown/estimators.hpp>
#include <ringdown/q_profile.hpp>

namespace ringdown {

struct AnalysisTimingsMs {
  double total{0.0};
  double load{0.0};
  double normalize{0.0};
  double initial_dft{0.0};
  double tau_seed{0.0};
  double full_record_tau{0.0};
  double crop{0.0};
  double cropped_nls{0.0};
  double cropped_dft_tau{0.0};
  double profile_q{0.0};
  double noise_fit{0.0};
  double crlb{0.0};
};

struct NoiseEstimate {
  double amplitude{0.0};
  double sigma{0.0};
  double sigma_mle{0.0};
  std::size_t degrees_of_freedom{0};
  bool success{false};
  std::string method;
  std::string message;
};

/// Validated vs raw Q diagnostics for one method (Python `QAssessment` fields on the result dict).
struct QEstimateDiagnostics {
  std::optional<double> value;
  std::optional<double> raw;
  bool valid{false};
  std::string status;
  std::vector<std::string> reasons;
  std::optional<double> raw_to_pre_crop_ratio;
  bool tau_at_lower_bound{false};
  bool tau_at_upper_bound{false};
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
  AnalysisTimingsMs timings;

  QEstimateDiagnostics nls_q;
  QEstimateDiagnostics dft_q;
  QProfileResult profile_q;
  std::optional<double> Q_pre_crop;
  bool tau_est_at_lower_bound{false};
  bool tau_est_at_upper_bound{false};
  bool tau_est_low_confidence{false};
  bool tau_nls_at_lower_bound{false};
  bool tau_nls_at_upper_bound{false};
  bool tau_dft_at_lower_bound{false};
  bool tau_dft_at_upper_bound{false};

  /// Filled by `BatchRingDownAnalyzer::calculate_q_factors` / `get_q_factor_statistics`.
  std::optional<double> batch_Q;
  bool batch_Q_valid{false};
  std::string batch_Q_status;
};

struct LoadedData {
  std::vector<double> time;
  std::vector<double> samples;
  std::vector<double> secondary_samples;
  std::string file_type;
};

class RingDownDataLoader {
public:
  static constexpr std::uintmax_t default_max_file_size_bytes = 1024ULL * 1024ULL * 1024ULL;

  [[nodiscard]] static LoadedData load(
      const std::string& filepath,
      std::optional<std::uintmax_t> max_file_size_bytes = default_max_file_size_bytes);
  [[nodiscard]] static LoadedData load_csv(
      const std::string& filepath,
      std::optional<std::uintmax_t> max_file_size_bytes = default_max_file_size_bytes);
  [[nodiscard]] static LoadedData load_mat(
      const std::string& filepath,
      std::optional<std::uintmax_t> max_file_size_bytes = default_max_file_size_bytes);
  [[nodiscard]] static LoadedData load_zip(
      const std::string& filepath,
      std::optional<std::uintmax_t> max_file_size_bytes = default_max_file_size_bytes);
};

/// One record from `RingDownAnalyzer::q_sensitivity` (Python `q_sensitivity` rows).
struct QSensitivityRow {
  double start_offset{0.0};
  double duration{0.0};
  double max_tau_multiplier{3.0};
  AnalyzerResult analysis;
};

class RingDownAnalyzer {
public:
  RingDownAnalyzer(NLSFrequencyEstimator nls_estimator = NLSFrequencyEstimator{},
                   DFTFrequencyEstimator dft_estimator = DFTFrequencyEstimator{},
                   std::optional<std::uintmax_t> max_file_size_bytes =
                       RingDownDataLoader::default_max_file_size_bytes,
                   ProfileQEstimator profile_q_estimator = ProfileQEstimator{});

  [[nodiscard]] AnalyzerResult analyze_array(const std::vector<double>& samples,
                                             double sample_rate_hz,
                                             double max_tau_multiplier = 3.0) const;
  [[nodiscard]] AnalyzerResult analyze_array(const std::vector<double>& time,
                                             const std::vector<double>& samples,
                                             double max_tau_multiplier = 3.0) const;
  [[nodiscard]] AnalyzerResult analyze_file(const std::string& filepath,
                                            double max_tau_multiplier = 3.0) const;

  [[nodiscard]] std::vector<QSensitivityRow> q_sensitivity(const std::vector<double>& time,
                                                           const std::vector<double>& samples,
                                                           const std::vector<double>& start_offsets,
                                                           const std::vector<double>& durations,
                                                           const std::vector<double>& max_tau_multipliers =
                                                               {}) const;

  [[nodiscard]] double estimate_tau(const std::vector<double>& time,
                                    const std::vector<double>& samples,
                                    double sample_rate_hz,
                                    std::optional<double> tau_initial = std::nullopt,
                                    std::optional<InitialParameters> initial = std::nullopt) const;
  [[nodiscard]] NoiseEstimate estimate_noise_parameters(const std::vector<double>& time,
                                                       const std::vector<double>& samples,
                                                       double tau_model,
                                                       double frequency_hz) const;

private:
  NLSFrequencyEstimator nls_estimator_;
  DFTFrequencyEstimator dft_estimator_;
  ProfileQEstimator profile_q_estimator_;
  std::optional<std::uintmax_t> max_file_size_bytes_;
};

[[nodiscard]] std::string to_json(const AnalyzerResult& result);

/// JSON export aligned with Python `RingDownAnalyzer.analyze_array` / `analyze_file`
/// result dicts (waveforms, estimator diagnostics, uncertainty aliases) for notebooks
/// and downstream tooling. Arrays may be large.
[[nodiscard]] std::string to_json_notebook(const AnalyzerResult& result);

} // namespace ringdown

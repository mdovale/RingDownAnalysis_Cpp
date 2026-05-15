#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <ringdown/estimators.hpp>
#include <ringdown/q_profile.hpp>

namespace ringdown {

/**
 * @brief Per-stage wall-clock timings for one analysis, in milliseconds.
 */
struct AnalysisTimingsMs {
  /// Total analysis time.
  double total{0.0};
  /// File loading time.
  double load{0.0};
  /// Input validation and timebase normalization time.
  double normalize{0.0};
  /// Initial DFT seed time.
  double initial_dft{0.0};
  /// Initial tau seed time.
  double tau_seed{0.0};
  /// Full-record tau estimation time.
  double full_record_tau{0.0};
  /// Cropping decision time.
  double crop{0.0};
  /// Cropped-record NLS fit time.
  double cropped_nls{0.0};
  /// Cropped-record DFT and tau fit time.
  double cropped_dft_tau{0.0};
  /// Profile-Q estimation time.
  double profile_q{0.0};
  /// Noise-parameter fit time.
  double noise_fit{0.0};
  /// Plug-in CRLB calculation time.
  double crlb{0.0};
};

/**
 * @brief Linear fixed-frequency noise estimate for a cropped ringdown record.
 */
struct NoiseEstimate {
  /// Estimated ringdown amplitude, floored to a positive value on success.
  double amplitude{0.0};
  /// Residual standard deviation using the model degrees of freedom.
  double sigma{0.0};
  /// Maximum-likelihood residual standard deviation using `N` samples.
  double sigma_mle{0.0};
  /// Residual degrees of freedom.
  std::size_t degrees_of_freedom{0};
  /// True when the fixed-frequency linear least-squares fit succeeded.
  bool success{false};
  /// Machine-readable method identifier.
  std::string method;
  /// Diagnostic message for fallback or failed fits.
  std::string message;
};

/**
 * @brief Validated versus raw Q diagnostics for one estimator.
 *
 * `raw` preserves the direct estimator output. `value` is populated only when
 * the validation logic accepts the estimate as user-facing.
 */
struct QEstimateDiagnostics {
  /// Validated Q value, when accepted.
  std::optional<double> value;
  /// Direct estimator Q value before validation.
  std::optional<double> raw;
  /// True when `value` is suitable for user-facing summaries.
  bool valid{false};
  /// Machine-readable status such as `valid`, `warning`, or `invalid`.
  std::string status;
  /// Machine-readable validation reasons and warnings.
  std::vector<std::string> reasons;
  /// Ratio of raw Q to the pre-crop Q seed, when both are finite.
  std::optional<double> raw_to_pre_crop_ratio;
  /// True when the estimator tau is near its lower fitted bound.
  bool tau_at_lower_bound{false};
  /// True when the estimator tau is near its upper fitted bound.
  bool tau_at_upper_bound{false};
};

/**
 * @brief Complete result from array or file analysis.
 *
 * The result preserves normalized input arrays, cropped arrays, estimator
 * outputs, Q diagnostics, profile-Q diagnostics, uncertainty estimates, and
 * metadata needed by JSON exports and batch summaries.
 */
struct AnalyzerResult {
  /// Normalized timestamps in seconds, starting at zero.
  std::vector<double> time;
  /// Primary input samples as supplied to the analysis pipeline.
  std::vector<double> samples;
  /// Cropped timestamps used for final NLS/DFT fits.
  std::vector<double> cropped_time;
  /// Cropped primary samples used for final NLS/DFT fits.
  std::vector<double> cropped_samples;
  /// Optional secondary channel from MAT files, mean-normalized when present.
  std::vector<double> secondary_samples;
  /// Inferred or supplied sampling rate in hertz.
  double sample_rate_hz{0.0};
  /// Tau seed from envelope analysis.
  double tau_seed{0.0};
  /// Full-record tau estimate used to define the cropped window.
  double tau_estimate{0.0};
  /// Tau selected for noise and plug-in CRLB calculations.
  double tau_model{0.0};
  /// Cropped-record NLS estimator result.
  EstimationResult nls;
  /// Cropped-record DFT estimator result.
  EstimationResult dft;
  /// Fixed-frequency noise estimate.
  NoiseEstimate noise;
  /// Plug-in CRLB variance for frequency.
  double plugin_crlb_variance_f{0.0};
  /// Plug-in CRLB standard deviation for frequency.
  double plugin_crlb_std_f{0.0};
  /// True when the uncertainty estimate passed basic validity checks.
  bool uncertainty_valid{false};
  /// Number of normalized input samples.
  std::size_t sample_count{0};
  /// Number of cropped samples.
  std::size_t cropped_sample_count{0};
  /// Normalized record end time in seconds.
  double observation_time{0.0};
  /// Cropped record end time in seconds.
  double cropped_observation_time{0.0};
  /// Basename of the analyzed file, or empty for array analysis.
  std::string filename;
  /// Loader type label such as `CSV`, `MAT`, or `ZIP_CSV`.
  std::string file_type;
  /// Stage timings for the analysis.
  AnalysisTimingsMs timings;

  /// NLS Q validation diagnostics.
  QEstimateDiagnostics nls_q;
  /// DFT Q validation diagnostics.
  QEstimateDiagnostics dft_q;
  /// Profile-likelihood Q diagnostics.
  QProfileResult profile_q;
  /// Pre-crop Q seed derived from NLS frequency and full-record tau.
  std::optional<double> Q_pre_crop;
  /// True when the full-record tau estimate is near its lower bound.
  bool tau_est_at_lower_bound{false};
  /// True when the full-record tau estimate is near its upper bound.
  bool tau_est_at_upper_bound{false};
  /// True when the full-record tau estimate has low support in the record.
  bool tau_est_low_confidence{false};
  /// True when the cropped NLS tau is near its lower fitted bound.
  bool tau_nls_at_lower_bound{false};
  /// True when the cropped NLS tau is near its upper fitted bound.
  bool tau_nls_at_upper_bound{false};
  /// True when the cropped DFT tau fit is near its lower fitted bound.
  bool tau_dft_at_lower_bound{false};
  /// True when the cropped DFT tau fit is near its upper fitted bound.
  bool tau_dft_at_upper_bound{false};

  /// Filled by `BatchRingDownAnalyzer::calculate_q_factors` / `get_q_factor_statistics`.
  std::optional<double> batch_Q;
  bool batch_Q_valid{false};
  std::string batch_Q_status;
};

/**
 * @brief Loaded input data normalized for analysis.
 */
struct LoadedData {
  /// Time array in seconds, normalized to start at zero.
  std::vector<double> time;
  /// Primary signal samples, mean-normalized by file loaders.
  std::vector<double> samples;
  /// Optional secondary channel from MAT input.
  std::vector<double> secondary_samples;
  /// Loader type label.
  std::string file_type;
};

/**
 * @brief File loader for supported ringdown data formats.
 *
 * The loader supports CSV, MAT v5, and ZIP archives containing exactly one CSV
 * file.
 */
class RingDownDataLoader {
public:
  /**
   * @brief Loads a supported file by extension.
   *
   * @param filepath Path to a `.csv`, `.mat`, or `.zip` file.
   * @return Normalized loaded data.
   *
   * @throws std::invalid_argument for unsupported formats or invalid file
   *         structure.
   * @throws std::runtime_error if the file cannot be opened.
   */
  [[nodiscard]] static LoadedData load(const std::string& filepath);

  /**
   * @brief Loads a Moku-style CSV file.
   *
   * CSV parsing uses the first column as time and the fourth column as the
   * primary sample channel. Header/comment rows are skipped before data begins.
   */
  [[nodiscard]] static LoadedData load_csv(const std::string& filepath);

  /**
   * @brief Loads a little-endian MAT v5 file containing `moku.data`.
   *
   * The first column is interpreted as time, the fourth as primary phase data,
   * and the ninth as an optional secondary channel when present.
   */
  [[nodiscard]] static LoadedData load_mat(const std::string& filepath);

  /**
   * @brief Loads a ZIP archive containing exactly one CSV entry.
   *
   * Stored and deflated CSV entries are supported. Encrypted, multi-file,
   * ZIP64, and unsupported-compression archives are rejected.
   */
  [[nodiscard]] static LoadedData load_zip(const std::string& filepath);
};

/**
 * @brief One windowed analysis row from `RingDownAnalyzer::q_sensitivity`.
 */
struct QSensitivityRow {
  /// Window start offset in seconds.
  double start_offset{0.0};
  /// Window duration in seconds.
  double duration{0.0};
  /// Crop multiplier used for this window analysis.
  double max_tau_multiplier{3.0};
  /// Analysis result for the selected window.
  AnalyzerResult analysis;
};

/**
 * @brief High-level single-record ringdown analysis pipeline.
 *
 * The analyzer validates and normalizes the timebase, estimates a full-record
 * tau seed, crops to a tau-scaled window, runs NLS and DFT estimators, computes
 * Q diagnostics, estimates noise, and evaluates a plug-in CRLB.
 */
class RingDownAnalyzer {
public:
  /**
   * @brief Constructs an analyzer with estimator objects.
   *
   * @param nls_estimator NLS estimator copied into the analyzer.
   * @param dft_estimator DFT estimator copied into the analyzer.
   * @param profile_q_estimator Profile-Q estimator copied into the analyzer.
   */
  RingDownAnalyzer(NLSFrequencyEstimator nls_estimator = NLSFrequencyEstimator{},
                   DFTFrequencyEstimator dft_estimator = DFTFrequencyEstimator{},
                   ProfileQEstimator profile_q_estimator = ProfileQEstimator{});

  /**
   * @brief Analyzes samples on an inferred uniform time grid.
   *
   * @param samples Input samples.
   * @param sample_rate_hz Sampling rate in hertz.
   * @param max_tau_multiplier Maximum crop duration as a multiple of tau.
   * @return Complete analysis result.
   *
   * @throws std::invalid_argument for invalid samples, sample rate, or crop
   *         multiplier.
   */
  [[nodiscard]] AnalyzerResult analyze_array(const std::vector<double>& samples,
                                             double sample_rate_hz,
                                             double max_tau_multiplier = 3.0) const;

  /**
   * @brief Analyzes samples with explicit timestamps.
   *
   * The time array is normalized to start at zero and must be strictly
   * increasing with approximately uniform spacing.
   *
   * @param time Input timestamps.
   * @param samples Input samples.
   * @param max_tau_multiplier Maximum crop duration as a multiple of tau.
   * @return Complete analysis result.
   *
   * @throws std::invalid_argument for mismatched sizes, invalid samples,
   *         nonuniform timestamps, or invalid crop multiplier.
   */
  [[nodiscard]] AnalyzerResult analyze_array(const std::vector<double>& time,
                                             const std::vector<double>& samples,
                                             double max_tau_multiplier = 3.0) const;

  /**
   * @brief Loads and analyzes a supported data file.
   *
   * @param filepath Path to a `.csv`, `.mat`, or `.zip` input.
   * @param max_tau_multiplier Maximum crop duration as a multiple of tau.
   * @return Complete analysis result with filename and file type populated.
   */
  [[nodiscard]] AnalyzerResult analyze_file(const std::string& filepath,
                                            double max_tau_multiplier = 3.0) const;

  /**
   * @brief Runs analyses over start/duration/crop multiplier combinations.
   *
   * @param time Input timestamps.
   * @param samples Input samples.
   * @param start_offsets Window start offsets in seconds.
   * @param durations Window durations in seconds.
   * @param max_tau_multipliers Crop multipliers; defaults to `{3.0}` when
   *        empty.
   * @return One row for each start, duration, and multiplier combination.
   *
   * @throws std::invalid_argument for invalid inputs or windows with fewer
   *         than two samples.
   */
  [[nodiscard]] std::vector<QSensitivityRow> q_sensitivity(const std::vector<double>& time,
                                                           const std::vector<double>& samples,
                                                           const std::vector<double>& start_offsets,
                                                           const std::vector<double>& durations,
                                                           const std::vector<double>& max_tau_multipliers =
                                                               {}) const;

  /**
   * @brief Estimates a tau seed for the analysis crop.
   *
   * @param time Input timestamps. Currently accepted for API parity; the
   *        implementation uses `sample_rate_hz` and `samples`.
   * @param samples Input samples.
   * @param sample_rate_hz Sampling rate in hertz.
   * @param tau_initial Optional envelope-derived tau seed.
   * @param initial Optional initial sinusoid parameters.
   * @return Positive tau estimate or fallback seed.
   */
  [[nodiscard]] double estimate_tau(const std::vector<double>& time,
                                    const std::vector<double>& samples,
                                    double sample_rate_hz,
                                    std::optional<double> tau_initial = std::nullopt,
                                    std::optional<InitialParameters> initial = std::nullopt) const;

  /**
   * @brief Estimates amplitude and noise for a fixed tau/frequency model.
   *
   * @param time Timestamps for the cropped record.
   * @param samples Samples for the cropped record.
   * @param tau_model Decay time used by the fixed model.
   * @param frequency_hz Frequency used by the fixed model.
   * @return Noise estimate and least-squares diagnostics.
   *
   * @throws std::invalid_argument if fewer than four samples are supplied, the
   *         vector sizes differ, or the fixed model parameters are invalid.
   */
  [[nodiscard]] NoiseEstimate estimate_noise_parameters(const std::vector<double>& time,
                                                       const std::vector<double>& samples,
                                                       double tau_model,
                                                       double frequency_hz) const;

private:
  NLSFrequencyEstimator nls_estimator_;
  DFTFrequencyEstimator dft_estimator_;
  ProfileQEstimator profile_q_estimator_;
};

/**
 * @brief Serializes a compact analysis result to JSON.
 *
 * Non-finite numeric values and missing optionals are emitted as JSON `null`.
 */
[[nodiscard]] std::string to_json(const AnalyzerResult& result);

} // namespace ringdown

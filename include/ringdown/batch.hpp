#pragma once

#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include <ringdown/analyzer.hpp>

namespace ringdown {

/**
 * @brief File-level failure captured during batch processing.
 */
struct FailedFile {
  /// Input path that failed to analyze.
  std::string filepath;
  /// Exception message reported for the failure.
  std::string message;
};

/**
 * @brief Results and failures from `BatchRingDownAnalyzer::process_files`.
 */
struct ProcessResult {
  /// Successful per-file analysis results, in input order.
  std::vector<AnalyzerResult> results;
  /// Files that failed analysis, in input order.
  std::vector<FailedFile> failed_files;

  /// Returns true when at least one file failed.
  [[nodiscard]] bool has_failures() const noexcept { return !failed_files.empty(); }
};

/**
 * @brief Progress notification emitted before or after each file analysis.
 */
struct BatchProgressEvent {
  /// Zero-based input file index.
  std::size_t index{0};
  /// Total number of input files.
  std::size_t total{0};
  /// Input filepath for this event.
  std::string filepath;
  /// Stage label such as `analyze_file_start`, `analyze_file_done`, or `analyze_file_failed`.
  std::string stage;
  /// True for successful completion events.
  bool success{false};
  /// Elapsed time since the file stage began.
  std::chrono::milliseconds elapsed{0};
  /// Optional diagnostic message.
  std::string message;
};

/// Callback type for receiving batch progress events.
using BatchProgressCallback = std::function<void(const BatchProgressEvent&)>;

/**
 * @brief Basic descriptive statistics for a collection of values.
 */
struct SummaryStatistics {
  /// Arithmetic mean.
  double mean{0.0};
  /// Median value.
  double median{0.0};
  /// Population standard deviation.
  double standard_deviation{0.0};
  /// Minimum value.
  double minimum{0.0};
  /// Maximum value.
  double maximum{0.0};
};

/**
 * @brief One row in the batch summary table.
 */
struct BatchSummaryRow {
  /// Input basename.
  std::string filename;
  /// Loader type label.
  std::string file_type;
  /// Original sample count.
  std::size_t sample_count{0};
  /// Cropped sample count.
  std::size_t cropped_sample_count{0};
  /// Original observation end time in seconds.
  double observation_time{0.0};
  /// Cropped observation end time in seconds.
  double cropped_observation_time{0.0};
  /// Sampling rate in hertz.
  double sample_rate_hz{0.0};
  /// Full-record tau estimate in seconds.
  double tau_estimate{0.0};
  /// Cropped NLS tau estimate in seconds, or NaN when missing.
  double tau_nls{0.0};
  /// Cropped DFT tau estimate in seconds, or NaN when missing.
  double tau_dft{0.0};
  /// NLS frequency estimate in hertz.
  double frequency_nls{0.0};
  /// DFT frequency estimate in hertz.
  double frequency_dft{0.0};
  /// Absolute NLS/DFT frequency difference in hertz.
  double frequency_difference{0.0};
  /// Validated NLS quality factor, or NaN when invalid.
  double quality_factor_nls{0.0};
  /// Validated DFT quality factor, or NaN when invalid.
  double quality_factor_dft{0.0};
  /// Plug-in CRLB frequency standard deviation in hertz.
  double plugin_crlb_std_f{0.0};
  /// Estimated amplitude.
  double amplitude{0.0};
  /// Estimated noise standard deviation.
  double sigma{0.0};
  /// NLS estimator success flag.
  bool nls_success{false};
  /// DFT estimator success flag.
  bool dft_success{false};
  /// True when the uncertainty estimate passed validity checks.
  bool uncertainty_valid{false};
};

/**
 * @brief Options controlling `to_json_batch_report` output.
 */
struct BatchReportOptions {
  /// Include notebook-style per-file results with waveform arrays.
  bool include_notebook_results{true};
};

/**
 * @brief Cross-file consistency metrics for frequency estimates.
 */
struct ConsistencyAnalysis {
  /// Number of successful realizations.
  std::size_t realization_count{0};
  /// Number of pairwise comparisons.
  std::size_t pairwise_comparison_count{0};
  /// Pairwise absolute NLS frequency differences.
  std::vector<double> nls_pairwise_differences;
  /// Pairwise absolute DFT frequency differences.
  std::vector<double> dft_pairwise_differences;
  /// Statistics for pairwise NLS differences.
  SummaryStatistics nls_pairwise_statistics;
  /// Statistics for pairwise DFT differences.
  SummaryStatistics dft_pairwise_statistics;
  /// Statistics for NLS frequencies across realizations.
  SummaryStatistics nls_frequency_statistics;
  /// Statistics for DFT frequencies across realizations.
  SummaryStatistics dft_frequency_statistics;
};

/**
 * @brief Comparison between estimator disagreement and plug-in CRLB scale.
 */
struct UncertaintyComparison {
  /// Absolute NLS/DFT frequency differences.
  std::vector<double> frequency_differences;
  /// Per-result plug-in CRLB standard deviations.
  std::vector<double> plugin_crlb_stds;
  /// Frequency difference divided by plug-in CRLB standard deviation.
  std::vector<double> ratios;
  /// Statistics for frequency differences.
  SummaryStatistics difference_statistics;
  /// Statistics for plug-in CRLB standard deviations.
  SummaryStatistics plugin_crlb_statistics;
  /// Statistics for finite ratios.
  SummaryStatistics ratio_statistics;
};

/**
 * @brief Aggregate statistics for selected batch quality factors.
 */
struct QFactorStatistics {
  /// Selected finite Q values.
  std::vector<double> values;
  /// Mean selected Q value, or NaN when no values are available.
  double mean{std::numeric_limits<double>::quiet_NaN()};
  /// Standard deviation of selected Q values, or NaN when unavailable.
  double std_dev{std::numeric_limits<double>::quiet_NaN()};
  /// Minimum selected Q value, or NaN when unavailable.
  double minimum{std::numeric_limits<double>::quiet_NaN()};
  /// Maximum selected Q value, or NaN when unavailable.
  double maximum{std::numeric_limits<double>::quiet_NaN()};
  /// Difference between maximum and minimum Q, or NaN when unavailable.
  double range{std::numeric_limits<double>::quiet_NaN()};
  /// Number of analyzed results.
  std::size_t n_total{0};
  /// Number of selected finite Q values.
  std::size_t n_valid{0};
  /// Number of results skipped because no selected finite Q was available.
  std::size_t n_skipped{0};
  /// Number of results classified as invalid.
  std::size_t n_invalid{0};
  /// Number of profile-Q results classified as one-sided or unbounded limits.
  std::size_t n_profile_limits{0};
  /// True when invalid raw Q fallbacks were allowed.
  bool include_invalid{false};
};

/**
 * @brief Batch processor and report helper for ringdown files.
 *
 * A batch analyzer owns an underlying `RingDownAnalyzer` and stores the most
 * recent successful results. Summary and comparison methods operate on that
 * stored result set.
 */
class BatchRingDownAnalyzer {
public:
  /**
   * @brief Constructs a batch analyzer around a single-record analyzer.
   */
  explicit BatchRingDownAnalyzer(RingDownAnalyzer analyzer = RingDownAnalyzer{});

  /**
   * @brief Analyzes each filepath and records successes and failures.
   *
   * @param filepaths Input files to process.
   * @param worker_count Number of asynchronous workers; values less than two
   *        run serially.
   * @param progress Optional progress callback invoked from worker threads.
   * @return Successful results and per-file failures.
   *
   * @note Successful results and failures preserve input ordering.
   */
  [[nodiscard]] ProcessResult process_files(const std::vector<std::string>& filepaths,
                                            std::size_t worker_count = 1,
                                            BatchProgressCallback progress = {});

  /**
   * @brief Selects preferred Q values from the stored results.
   *
   * @param include_invalid If true, use finite raw fallback Q values when the
   *        preferred validated value is unavailable.
   * @return Selected finite Q values.
   *
   * @post Updates each stored result's `batch_Q` fields.
   */
  [[nodiscard]] std::vector<double> calculate_q_factors(bool include_invalid = false);

  /**
   * @brief Computes aggregate statistics for selected Q values.
   *
   * @param include_invalid Whether raw invalid fallback Q values may be used.
   * @return Q-factor statistics and classification counts.
   *
   * @post Updates each stored result's `batch_Q` fields.
   */
  [[nodiscard]] QFactorStatistics get_q_factor_statistics(bool include_invalid = false);

  /// Builds one summary-table row for each stored result.
  [[nodiscard]] std::vector<BatchSummaryRow> summary_table() const;
  /// Computes pairwise and across-realization frequency consistency metrics.
  [[nodiscard]] ConsistencyAnalysis consistency_analysis() const;
  /// Compares NLS/DFT frequency disagreement with plug-in CRLB scale.
  [[nodiscard]] UncertaintyComparison uncertainty_comparison() const;
  /// Computes descriptive statistics for stored NLS frequencies.
  [[nodiscard]] SummaryStatistics nls_frequency_statistics() const;
  /// Computes descriptive statistics for stored DFT frequencies.
  [[nodiscard]] SummaryStatistics dft_frequency_statistics() const;

  /// Returns the stored successful results from the most recent processing run.
  [[nodiscard]] const std::vector<AnalyzerResult>& results() const noexcept;

private:
  RingDownAnalyzer analyzer_;
  std::vector<AnalyzerResult> results_;
};

/**
 * @brief Serializes process successes and failures to JSON.
 */
[[nodiscard]] std::string to_json(const ProcessResult& result);

/**
 * @brief Serializes a full batch report to JSON.
 *
 * The report contains failures, optional notebook-shaped per-file results,
 * timing summaries, selected Q values, summary rows, consistency analysis, and
 * plug-in CRLB comparison blocks.
 */
[[nodiscard]] std::string to_json_batch_report(BatchRingDownAnalyzer& batch,
                                               const ProcessResult& process,
                                               BatchReportOptions options = {});

} // namespace ringdown

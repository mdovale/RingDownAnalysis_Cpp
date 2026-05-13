#pragma once

#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <string>
#include <vector>

#include <ringdown/analyzer.hpp>

namespace ringdown {

struct FailedFile {
  std::string filepath;
  std::string message;
};

struct ProcessResult {
  std::vector<AnalyzerResult> results;
  std::vector<FailedFile> failed_files;

  [[nodiscard]] bool has_failures() const noexcept { return !failed_files.empty(); }
};

struct BatchProgressEvent {
  std::size_t index{0};
  std::size_t total{0};
  std::string filepath;
  std::string stage;
  bool success{false};
  std::chrono::milliseconds elapsed{0};
  std::string message;
};

using BatchProgressCallback = std::function<void(const BatchProgressEvent&)>;

struct SummaryStatistics {
  double mean{0.0};
  double median{0.0};
  double standard_deviation{0.0};
  double minimum{0.0};
  double maximum{0.0};
};

struct BatchSummaryRow {
  std::string filename;
  std::string file_type;
  std::size_t sample_count{0};
  std::size_t cropped_sample_count{0};
  double observation_time{0.0};
  double cropped_observation_time{0.0};
  double sample_rate_hz{0.0};
  double tau_estimate{0.0};
  double tau_nls{0.0};
  double tau_dft{0.0};
  double frequency_nls{0.0};
  double frequency_dft{0.0};
  double frequency_difference{0.0};
  double quality_factor_nls{0.0};
  double quality_factor_dft{0.0};
  double plugin_crlb_std_f{0.0};
  double amplitude{0.0};
  double sigma{0.0};
  bool nls_success{false};
  bool dft_success{false};
  bool uncertainty_valid{false};
};

struct BatchReportOptions {
  bool include_notebook_results{true};
};

struct ConsistencyAnalysis {
  std::size_t realization_count{0};
  std::size_t pairwise_comparison_count{0};
  std::vector<double> nls_pairwise_differences;
  std::vector<double> dft_pairwise_differences;
  SummaryStatistics nls_pairwise_statistics;
  SummaryStatistics dft_pairwise_statistics;
  SummaryStatistics nls_frequency_statistics;
  SummaryStatistics dft_frequency_statistics;
};

struct UncertaintyComparison {
  std::vector<double> frequency_differences;
  std::vector<double> plugin_crlb_stds;
  std::vector<double> ratios;
  SummaryStatistics difference_statistics;
  SummaryStatistics plugin_crlb_statistics;
  SummaryStatistics ratio_statistics;
};

class BatchRingDownAnalyzer {
public:
  explicit BatchRingDownAnalyzer(RingDownAnalyzer analyzer = RingDownAnalyzer{});

  [[nodiscard]] ProcessResult process_files(const std::vector<std::string>& filepaths,
                                            std::size_t worker_count = 1,
                                            BatchProgressCallback progress = {});
  [[nodiscard]] std::vector<double> calculate_q_factors() const;
  [[nodiscard]] std::vector<BatchSummaryRow> summary_table() const;
  [[nodiscard]] ConsistencyAnalysis consistency_analysis() const;
  [[nodiscard]] UncertaintyComparison uncertainty_comparison() const;
  [[nodiscard]] SummaryStatistics nls_frequency_statistics() const;
  [[nodiscard]] SummaryStatistics dft_frequency_statistics() const;

  [[nodiscard]] const std::vector<AnalyzerResult>& results() const noexcept;

private:
  RingDownAnalyzer analyzer_;
  std::vector<AnalyzerResult> results_;
};

[[nodiscard]] std::string to_json(const ProcessResult& result);

/// Full batch report: per-file notebook-shaped results, failures, summary rows,
/// Q factors, consistency and plug-in / CRLB comparison blocks (Python-notebook
/// compatible field names where practical).
[[nodiscard]] std::string to_json_batch_report(const BatchRingDownAnalyzer& batch,
                                               const ProcessResult& process,
                                               BatchReportOptions options = {});

} // namespace ringdown

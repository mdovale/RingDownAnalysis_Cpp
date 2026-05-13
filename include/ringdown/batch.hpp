#pragma once

#include <cstddef>
#include <exception>
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

struct SummaryStatistics {
  double mean{0.0};
  double median{0.0};
  double standard_deviation{0.0};
  double minimum{0.0};
  double maximum{0.0};
};

class BatchRingDownAnalyzer {
public:
  explicit BatchRingDownAnalyzer(RingDownAnalyzer analyzer = RingDownAnalyzer{});

  [[nodiscard]] ProcessResult process_files(const std::vector<std::string>& filepaths,
                                            std::size_t worker_count = 1);
  [[nodiscard]] std::vector<double> calculate_q_factors() const;
  [[nodiscard]] SummaryStatistics nls_frequency_statistics() const;
  [[nodiscard]] SummaryStatistics dft_frequency_statistics() const;

  [[nodiscard]] const std::vector<AnalyzerResult>& results() const noexcept;

private:
  RingDownAnalyzer analyzer_;
  std::vector<AnalyzerResult> results_;
};

} // namespace ringdown

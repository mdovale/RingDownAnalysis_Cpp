#include <ringdown/batch.hpp>

#include <algorithm>
#include <cmath>
#include <future>
#include <numeric>
#include <utility>

namespace ringdown {

namespace {

[[nodiscard]] SummaryStatistics statistics(std::vector<double> values) {
  if (values.empty()) {
    return {};
  }
  std::sort(values.begin(), values.end());
  const auto sum = std::accumulate(values.begin(), values.end(), 0.0);
  const auto avg = sum / static_cast<double>(values.size());
  auto sq = 0.0;
  for (const auto value : values) {
    const auto delta = value - avg;
    sq += delta * delta;
  }
  const auto median = values.size() % 2U == 0U
                          ? (values[values.size() / 2U - 1U] + values[values.size() / 2U]) / 2.0
                          : values[values.size() / 2U];
  return SummaryStatistics{avg,
                           median,
                           std::sqrt(sq / static_cast<double>(values.size())),
                           values.front(),
                           values.back()};
}

} // namespace

BatchRingDownAnalyzer::BatchRingDownAnalyzer(RingDownAnalyzer analyzer) : analyzer_{std::move(analyzer)} {}

ProcessResult BatchRingDownAnalyzer::process_files(const std::vector<std::string>& filepaths,
                                                   std::size_t worker_count) {
  results_.clear();
  auto failed = std::vector<FailedFile>{};
  if (filepaths.empty()) {
    return ProcessResult{};
  }

  if (worker_count <= 1U) {
    for (const auto& filepath : filepaths) {
      try {
        results_.push_back(analyzer_.analyze_file(filepath));
      } catch (const std::exception& error) {
        failed.push_back(FailedFile{filepath, error.what()});
      }
    }
    return ProcessResult{results_, failed};
  }

  auto futures = std::vector<std::future<AnalyzerResult>>{};
  futures.reserve(filepaths.size());
  for (const auto& filepath : filepaths) {
    futures.push_back(
        std::async(std::launch::async, [this, filepath] { return analyzer_.analyze_file(filepath); }));
  }

  for (auto index = std::size_t{0}; index < futures.size(); ++index) {
    try {
      results_.push_back(futures[index].get());
    } catch (const std::exception& error) {
      failed.push_back(FailedFile{filepaths[index], error.what()});
    }
  }
  return ProcessResult{results_, failed};
}

std::vector<double> BatchRingDownAnalyzer::calculate_q_factors() const {
  auto values = std::vector<double>{};
  values.reserve(results_.size());
  for (const auto& result : results_) {
    values.push_back(
        result.nls.quality_factor.value_or(result.tau_model * result.nls.frequency_hz * std::acos(-1.0)));
  }
  return values;
}

SummaryStatistics BatchRingDownAnalyzer::nls_frequency_statistics() const {
  auto values = std::vector<double>{};
  values.reserve(results_.size());
  for (const auto& result : results_) {
    values.push_back(result.nls.frequency_hz);
  }
  return statistics(std::move(values));
}

SummaryStatistics BatchRingDownAnalyzer::dft_frequency_statistics() const {
  auto values = std::vector<double>{};
  values.reserve(results_.size());
  for (const auto& result : results_) {
    values.push_back(result.dft.frequency_hz);
  }
  return statistics(std::move(values));
}

const std::vector<AnalyzerResult>& BatchRingDownAnalyzer::results() const noexcept { return results_; }

} // namespace ringdown

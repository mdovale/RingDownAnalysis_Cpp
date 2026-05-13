#include <ringdown/batch.hpp>

#include <algorithm>
#include <cmath>
#include <future>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
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

[[nodiscard]] double optional_or_nan(std::optional<double> value) {
  return value.value_or(std::numeric_limits<double>::quiet_NaN());
}

[[nodiscard]] std::string json_string(const std::string& value) {
  auto out = std::ostringstream{};
  out << '"';
  for (const auto ch : value) {
    if (ch == '"' || ch == '\\') {
      out << '\\';
    }
    out << ch;
  }
  out << '"';
  return out.str();
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

std::vector<BatchSummaryRow> BatchRingDownAnalyzer::summary_table() const {
  auto rows = std::vector<BatchSummaryRow>{};
  rows.reserve(results_.size());
  for (const auto& result : results_) {
    rows.push_back(BatchSummaryRow{result.filename,
                                   result.file_type,
                                   result.sample_count,
                                   result.cropped_sample_count,
                                   result.observation_time,
                                   result.cropped_observation_time,
                                   result.sample_rate_hz,
                                   result.tau_estimate,
                                   optional_or_nan(result.nls.tau),
                                   optional_or_nan(result.dft.tau),
                                   result.nls.frequency_hz,
                                   result.dft.frequency_hz,
                                   std::abs(result.nls.frequency_hz - result.dft.frequency_hz),
                                   optional_or_nan(result.nls.quality_factor),
                                   optional_or_nan(result.dft.quality_factor),
                                   result.plugin_crlb_std_f,
                                   result.noise.amplitude,
                                   result.noise.sigma,
                                   result.nls.success,
                                   result.dft.success,
                                   result.uncertainty_valid});
  }
  return rows;
}

ConsistencyAnalysis BatchRingDownAnalyzer::consistency_analysis() const {
  auto analysis = ConsistencyAnalysis{};
  analysis.realization_count = results_.size();
  auto nls_frequencies = std::vector<double>{};
  auto dft_frequencies = std::vector<double>{};
  nls_frequencies.reserve(results_.size());
  dft_frequencies.reserve(results_.size());
  for (const auto& result : results_) {
    nls_frequencies.push_back(result.nls.frequency_hz);
    dft_frequencies.push_back(result.dft.frequency_hz);
  }

  for (auto lhs = std::size_t{0}; lhs < results_.size(); ++lhs) {
    for (auto rhs = lhs + 1U; rhs < results_.size(); ++rhs) {
      analysis.nls_pairwise_differences.push_back(
          std::abs(results_[lhs].nls.frequency_hz - results_[rhs].nls.frequency_hz));
      analysis.dft_pairwise_differences.push_back(
          std::abs(results_[lhs].dft.frequency_hz - results_[rhs].dft.frequency_hz));
    }
  }
  analysis.pairwise_comparison_count = analysis.nls_pairwise_differences.size();
  analysis.nls_pairwise_statistics = statistics(analysis.nls_pairwise_differences);
  analysis.dft_pairwise_statistics = statistics(analysis.dft_pairwise_differences);
  analysis.nls_frequency_statistics = statistics(std::move(nls_frequencies));
  analysis.dft_frequency_statistics = statistics(std::move(dft_frequencies));
  return analysis;
}

UncertaintyComparison BatchRingDownAnalyzer::uncertainty_comparison() const {
  auto comparison = UncertaintyComparison{};
  comparison.frequency_differences.reserve(results_.size());
  comparison.plugin_crlb_stds.reserve(results_.size());
  comparison.ratios.reserve(results_.size());
  for (const auto& result : results_) {
    const auto difference = std::abs(result.nls.frequency_hz - result.dft.frequency_hz);
    comparison.frequency_differences.push_back(difference);
    comparison.plugin_crlb_stds.push_back(result.plugin_crlb_std_f);
    if (result.plugin_crlb_std_f > 0.0 && std::isfinite(result.plugin_crlb_std_f)) {
      comparison.ratios.push_back(difference / result.plugin_crlb_std_f);
    }
  }
  comparison.difference_statistics = statistics(comparison.frequency_differences);
  comparison.plugin_crlb_statistics = statistics(comparison.plugin_crlb_stds);
  comparison.ratio_statistics = statistics(comparison.ratios);
  return comparison;
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

std::string to_json(const ProcessResult& result) {
  auto out = std::ostringstream{};
  out << std::setprecision(17);
  out << "{\n";
  out << "  \"success_count\": " << result.results.size() << ",\n";
  out << "  \"failure_count\": " << result.failed_files.size() << ",\n";
  out << "  \"results\": [\n";
  for (auto index = std::size_t{0}; index < result.results.size(); ++index) {
    const auto& item = result.results[index];
    out << "    {\"filename\": " << json_string(item.filename) << ", \"type\": "
        << json_string(item.file_type) << ", \"f_nls\": " << item.nls.frequency_hz
        << ", \"f_dft\": " << item.dft.frequency_hz << ", \"tau_est\": " << item.tau_estimate
        << ", \"Q_nls\": " << optional_or_nan(item.nls.quality_factor)
        << ", \"Q_dft\": " << optional_or_nan(item.dft.quality_factor)
        << ", \"plugin_crlb_std_f\": " << item.plugin_crlb_std_f << "}";
    if (index + 1U != result.results.size()) {
      out << ',';
    }
    out << '\n';
  }
  out << "  ],\n";
  out << "  \"failures\": [\n";
  for (auto index = std::size_t{0}; index < result.failed_files.size(); ++index) {
    const auto& item = result.failed_files[index];
    out << "    {\"filepath\": " << json_string(item.filepath)
        << ", \"message\": " << json_string(item.message) << "}";
    if (index + 1U != result.failed_files.size()) {
      out << ',';
    }
    out << '\n';
  }
  out << "  ]\n";
  out << "}\n";
  return out.str();
}

} // namespace ringdown

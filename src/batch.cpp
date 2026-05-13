#include <ringdown/batch.hpp>

#include <algorithm>
#include <atomic>
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

[[nodiscard]] std::string json_number(double value) {
  if (!std::isfinite(value)) {
    return "null";
  }
  auto out = std::ostringstream{};
  out << std::setprecision(17) << value;
  return out.str();
}

[[nodiscard]] std::string optional_json_number(std::optional<double> value) {
  return value.has_value() ? json_number(*value) : "null";
}

[[nodiscard]] std::string json_string(const std::string& value) {
  auto out = std::ostringstream{};
  out << '"';
  for (const auto ch : value) {
    if (ch == '"' || ch == '\\') {
      out << '\\';
      out << ch;
    } else if (ch == '\n') {
      out << "\\n";
    } else if (ch == '\r') {
      out << "\\r";
    } else if (ch == '\t') {
      out << "\\t";
    } else if (static_cast<unsigned char>(ch) < 0x20U) {
      out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
          << static_cast<int>(static_cast<unsigned char>(ch)) << std::dec << std::setfill(' ');
    } else {
      out << ch;
    }
  }
  out << '"';
  return out.str();
}

struct ProcessingSlot {
  std::optional<AnalyzerResult> result;
  std::string error_message;
};

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

  const auto bounded_worker_count = std::min(worker_count, filepaths.size());
  auto slots = std::vector<ProcessingSlot>(filepaths.size());
  auto next_index = std::atomic<std::size_t>{0U};
  auto workers = std::vector<std::future<void>>{};
  workers.reserve(bounded_worker_count);

  for (auto worker = std::size_t{0}; worker < bounded_worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [this, &filepaths, &slots, &next_index] {
      while (true) {
        const auto index = next_index.fetch_add(1U);
        if (index >= filepaths.size()) {
          return;
        }
        try {
          slots[index].result = analyzer_.analyze_file(filepaths[index]);
        } catch (const std::exception& error) {
          slots[index].error_message = error.what();
        }
      }
    }));
  }
  for (auto& worker : workers) {
    worker.get();
  }

  for (auto index = std::size_t{0}; index < slots.size(); ++index) {
    if (slots[index].result.has_value()) {
      results_.push_back(std::move(*slots[index].result));
    } else {
      failed.push_back(FailedFile{filepaths[index], slots[index].error_message});
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
        << json_string(item.file_type) << ", \"f_nls\": " << json_number(item.nls.frequency_hz)
        << ", \"f_dft\": " << json_number(item.dft.frequency_hz)
        << ", \"tau_est\": " << json_number(item.tau_estimate)
        << ", \"Q_nls\": " << optional_json_number(item.nls.quality_factor)
        << ", \"Q_dft\": " << optional_json_number(item.dft.quality_factor)
        << ", \"plugin_crlb_std_f\": " << json_number(item.plugin_crlb_std_f) << "}";
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

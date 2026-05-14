#include <ringdown/batch.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
#include <iomanip>
#include <limits>
#include <numeric>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
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

void append_json_double_array(std::ostringstream& out, const std::vector<double>& values) {
  out << '[';
  for (auto index = std::size_t{0}; index < values.size(); ++index) {
    if (index != 0U) {
      out << ',';
    }
    out << json_number(values[index]);
  }
  out << ']';
}

void append_summary_statistics_object(std::ostringstream& out, const SummaryStatistics& s) {
  out << "{\"mean\":" << json_number(s.mean) << ",\"median\":" << json_number(s.median)
      << ",\"standard_deviation\":" << json_number(s.standard_deviation)
      << ",\"std\":" << json_number(s.standard_deviation) << ",\"minimum\":" << json_number(s.minimum)
      << ",\"maximum\":" << json_number(s.maximum) << '}';
}

void append_json_timings_object(std::ostringstream& out, const AnalysisTimingsMs& timings) {
  out << "{\"total\":" << json_number(timings.total) << ",\"load\":" << json_number(timings.load)
      << ",\"normalize\":" << json_number(timings.normalize)
      << ",\"initial_dft\":" << json_number(timings.initial_dft)
      << ",\"tau_seed\":" << json_number(timings.tau_seed)
      << ",\"full_record_tau\":" << json_number(timings.full_record_tau)
      << ",\"crop\":" << json_number(timings.crop)
      << ",\"cropped_nls\":" << json_number(timings.cropped_nls)
      << ",\"cropped_dft_tau\":" << json_number(timings.cropped_dft_tau)
      << ",\"profile_q\":" << json_number(timings.profile_q)
      << ",\"noise_fit\":" << json_number(timings.noise_fit)
      << ",\"crlb\":" << json_number(timings.crlb) << '}';
}

struct ProcessingSlot {
  std::optional<AnalyzerResult> result;
  std::string error_message;
};

/**
 * @brief Emits a progress callback event if a callback was supplied.
 *
 * @internal This helper centralizes elapsed-time construction so serial and
 * parallel processing report the same event shape.
 */
void report_progress(const BatchProgressCallback& progress,
                     std::size_t index,
                     std::size_t total,
                     const std::string& filepath,
                     std::string stage,
                     bool success,
                     std::chrono::steady_clock::time_point start,
                     std::string message = {}) {
  if (!progress) {
    return;
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  progress(BatchProgressEvent{index, total, filepath, std::move(stage), success, elapsed, std::move(message)});
}

struct SelectedQ {
  std::optional<double> q;
  bool q_valid{false};
  std::string q_status;
};

/**
 * @brief Chooses the Q value that batch summaries should aggregate.
 *
 * @internal Profile-Q is preferred when available. If profile-Q is unavailable
 * or invalid, finite raw NLS values are used only when `include_invalid` asks
 * for diagnostic rather than strictly validated summaries.
 */
[[nodiscard]] SelectedQ select_preferred_q(const AnalyzerResult& result, bool include_invalid) {
  const auto has_profile = !result.profile_q.method.empty();
  if (has_profile) {
    if (result.profile_q.Q.has_value() && std::isfinite(*result.profile_q.Q)) {
      return {result.profile_q.Q, result.profile_q.valid, result.profile_q.status};
    }
    if (!include_invalid) {
      return {std::nullopt, false, result.profile_q.status};
    }
    if (result.nls_q.raw.has_value() && std::isfinite(*result.nls_q.raw)) {
      return {result.nls_q.raw, false, result.profile_q.status};
    }
    return {std::nullopt, false, result.profile_q.status};
  }

  if (!result.nls_q.valid && !include_invalid) {
    return {std::nullopt, false, result.nls_q.status};
  }
  auto q = result.nls_q.value;
  if (!q.has_value() && include_invalid) {
    q = result.nls_q.raw;
  }
  if (!q.has_value()) {
    const auto tau_for_q = result.nls.tau.value_or(result.tau_model);
    if (std::isfinite(result.nls.frequency_hz) && std::isfinite(tau_for_q) && tau_for_q > 0.0) {
      q = std::numbers::pi * result.nls.frequency_hz * tau_for_q;
    }
  }
  if (!q.has_value() || !std::isfinite(*q)) {
    return {std::nullopt, false, result.nls_q.status};
  }
  return {q, result.nls_q.valid, result.nls_q.status};
}

} // namespace

BatchRingDownAnalyzer::BatchRingDownAnalyzer(RingDownAnalyzer analyzer) : analyzer_{std::move(analyzer)} {}

ProcessResult BatchRingDownAnalyzer::process_files(const std::vector<std::string>& filepaths,
                                                   std::size_t worker_count,
                                                   BatchProgressCallback progress) {
  results_.clear();
  auto failed = std::vector<FailedFile>{};
  if (filepaths.empty()) {
    return ProcessResult{};
  }

  if (worker_count <= 1U) {
    for (auto index = std::size_t{0}; index < filepaths.size(); ++index) {
      const auto& filepath = filepaths[index];
      const auto start = std::chrono::steady_clock::now();
      report_progress(progress, index, filepaths.size(), filepath, "analyze_file_start", false, start);
      try {
        results_.push_back(analyzer_.analyze_file(filepath));
        report_progress(progress, index, filepaths.size(), filepath, "analyze_file_done", true, start);
      } catch (const std::exception& error) {
        failed.push_back(FailedFile{filepath, error.what()});
        report_progress(progress,
                        index,
                        filepaths.size(),
                        filepath,
                        "analyze_file_failed",
                        false,
                        start,
                        error.what());
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
    workers.push_back(std::async(std::launch::async, [this, &filepaths, &slots, &next_index, &progress] {
      while (true) {
        const auto index = next_index.fetch_add(1U);
        if (index >= filepaths.size()) {
          return;
        }
        const auto start = std::chrono::steady_clock::now();
        report_progress(progress, index, filepaths.size(), filepaths[index], "analyze_file_start", false, start);
        try {
          slots[index].result = analyzer_.analyze_file(filepaths[index]);
          report_progress(progress, index, filepaths.size(), filepaths[index], "analyze_file_done", true, start);
        } catch (const std::exception& error) {
          slots[index].error_message = error.what();
          report_progress(progress,
                          index,
                          filepaths.size(),
                          filepaths[index],
                          "analyze_file_failed",
                          false,
                          start,
                          error.what());
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

ProcessResult BatchRingDownAnalyzer::process_files(const std::vector<std::string>& filepaths,
                                                   const BatchProcessOptions& options) {
  return process_files(filepaths, options.worker_count, options.progress);
}

std::vector<double> BatchRingDownAnalyzer::calculate_q_factors(const bool include_invalid) {
  auto values = std::vector<double>{};
  values.reserve(results_.size());
  for (auto& result : results_) {
    const auto sel = select_preferred_q(result, include_invalid);
    result.batch_Q = sel.q;
    result.batch_Q_valid = sel.q_valid;
    result.batch_Q_status = sel.q_status;
    if (sel.q.has_value() && std::isfinite(*sel.q)) {
      values.push_back(*sel.q);
    }
  }
  return values;
}

QFactorStatistics BatchRingDownAnalyzer::get_q_factor_statistics(const bool include_invalid) {
  auto stats = QFactorStatistics{};
  stats.include_invalid = include_invalid;
  if (results_.empty()) {
    return stats;
  }
  (void)calculate_q_factors(include_invalid);
  stats.n_total = results_.size();
  for (const auto& r : results_) {
    if (r.batch_Q.has_value() && std::isfinite(*r.batch_Q)) {
      stats.values.push_back(*r.batch_Q);
    }
  }
  stats.n_skipped = stats.n_total - stats.values.size();
  stats.n_valid = stats.values.size();

  for (const auto& r : results_) {
    const auto has_profile = !r.profile_q.method.empty();
    if (has_profile && !r.profile_q.valid) {
      const auto& st = r.profile_q.status;
      if (st == "lower_limit" || st == "upper_limit" || st == "unbounded") {
        ++stats.n_profile_limits;
      } else {
        ++stats.n_invalid;
      }
    } else if (!has_profile && !r.nls_q.valid) {
      ++stats.n_invalid;
    }
  }

  if (stats.values.empty()) {
    return stats;
  }
  const auto s = statistics(std::vector<double>(stats.values.begin(), stats.values.end()));
  stats.mean = s.mean;
  stats.std_dev = s.standard_deviation;
  stats.minimum = s.minimum;
  stats.maximum = s.maximum;
  stats.range = s.maximum - s.minimum;
  return stats;
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
                                   optional_or_nan(result.nls_q.value),
                                   optional_or_nan(result.dft_q.value),
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
        << ", \"Q_nls\": " << optional_json_number(item.nls_q.value)
        << ", \"Q_dft\": " << optional_json_number(item.dft_q.value)
        << ", \"plugin_crlb_std_f\": " << json_number(item.plugin_crlb_std_f)
        << ", \"timings_ms\": ";
    append_json_timings_object(out, item.timings);
    out << "}";
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

std::string to_json_batch_report(BatchRingDownAnalyzer& batch,
                                 const ProcessResult& process,
                                 BatchReportOptions options) {
  auto out = std::ostringstream{};
  out << std::setprecision(17);
  out << "{\n";
  out << "  \"success_count\": " << process.results.size() << ",\n";
  out << "  \"failure_count\": " << process.failed_files.size() << ",\n";

  out << "  \"failures\": [\n";
  for (auto index = std::size_t{0}; index < process.failed_files.size(); ++index) {
    const auto& item = process.failed_files[index];
    out << "    {\"filepath\": " << json_string(item.filepath) << ", \"message\": "
        << json_string(item.message) << "}";
    if (index + 1U != process.failed_files.size()) {
      out << ',';
    }
    out << '\n';
  }
  out << "  ],\n";

  out << "  \"results_notebook\": [\n";
  if (options.include_notebook_results) {
    for (auto index = std::size_t{0}; index < process.results.size(); ++index) {
      const auto chunk = to_json_notebook(process.results[index]);
      auto stream = std::istringstream{chunk};
      auto line = std::string{};
      auto first_line = true;
      while (std::getline(stream, line)) {
        if (line.empty()) {
          continue;
        }
        if (!first_line) {
          out << '\n';
        }
        first_line = false;
        out << "    " << line;
      }
      if (index + 1U != process.results.size()) {
        out << ',';
      }
      out << '\n';
    }
  }
  out << "  ],\n";

  out << "  \"file_timings_ms\": [\n";
  for (auto index = std::size_t{0}; index < process.results.size(); ++index) {
    const auto& item = process.results[index];
    out << "    {\"filename\": " << json_string(item.filename) << ", \"type\": "
        << json_string(item.file_type) << ", \"N\": " << item.sample_count
        << ", \"N_crop\": " << item.cropped_sample_count << ", \"timings\": ";
    append_json_timings_object(out, item.timings);
    out << "}";
    if (index + 1U != process.results.size()) {
      out << ',';
    }
    out << '\n';
  }
  out << "  ],\n";

  const auto q_stats_full = batch.get_q_factor_statistics(false);
  const auto summary = batch.summary_table();

  out << "  \"q_factors\": ";
  append_json_double_array(out, q_stats_full.values);
  out << ",\n";

  out << "  \"q_factor_statistics\": {\n";
  out << "    \"values\": ";
  append_json_double_array(out, q_stats_full.values);
  out << ",\n";
  out << "    \"mean\": " << json_number(q_stats_full.mean) << ",\n";
  out << "    \"std\": " << json_number(q_stats_full.std_dev) << ",\n";
  out << "    \"min\": " << json_number(q_stats_full.minimum) << ",\n";
  out << "    \"max\": " << json_number(q_stats_full.maximum) << ",\n";
  out << "    \"range\": " << json_number(q_stats_full.range) << ",\n";
  out << "    \"n_total\": " << q_stats_full.n_total << ",\n";
  out << "    \"n_valid\": " << q_stats_full.n_valid << ",\n";
  out << "    \"n_skipped\": " << q_stats_full.n_skipped << ",\n";
  out << "    \"n_invalid\": " << q_stats_full.n_invalid << ",\n";
  out << "    \"n_profile_limits\": " << q_stats_full.n_profile_limits << ",\n";
  out << "    \"include_invalid\": " << (q_stats_full.include_invalid ? "true" : "false") << "\n";
  out << "  },\n";

  out << "  \"summary_table\": [\n";
  for (auto index = std::size_t{0}; index < summary.size(); ++index) {
    const auto& row = summary[index];
    const auto q = index < process.results.size() ? optional_or_nan(process.results[index].batch_Q)
                                                    : std::numeric_limits<double>::quiet_NaN();
    out << "    {\n";
    out << "      \"Filename\": " << json_string(row.filename) << ",\n";
    out << "      \"Type\": " << json_string(row.file_type) << ",\n";
    out << "      \"N (samples)\": " << row.sample_count << ",\n";
    out << "      \"N_crop (samples)\": " << row.cropped_sample_count << ",\n";
    out << "      \"T (s)\": " << json_number(row.observation_time) << ",\n";
    out << "      \"T_crop (s)\": " << json_number(row.cropped_observation_time) << ",\n";
    out << "      \"fs (Hz)\": " << json_number(row.sample_rate_hz) << ",\n";
    out << "      \"tau_est (s)\": " << json_number(row.tau_estimate) << ",\n";
    out << "      \"tau_nls (s)\": " << json_number(row.tau_nls) << ",\n";
    out << "      \"tau_dft (s)\": " << json_number(row.tau_dft) << ",\n";
    out << "      \"f_NLS (Hz)\": " << json_number(row.frequency_nls) << ",\n";
    out << "      \"f_DFT (Hz)\": " << json_number(row.frequency_dft) << ",\n";
    out << "      \"|f_NLS - f_DFT| (Hz)\": " << json_number(row.frequency_difference) << ",\n";
    out << "      \"Q_NLS\": " << json_number(row.quality_factor_nls) << ",\n";
    out << "      \"Q_DFT\": " << json_number(row.quality_factor_dft) << ",\n";
    out << "      \"Q\": " << json_number(q) << ",\n";
    out << "      \"Plugin bound std (Hz)\": " << json_number(row.plugin_crlb_std_f) << ",\n";
    out << "      \"uncertainty_valid\": " << (row.uncertainty_valid ? "true" : "false") << ",\n";
    out << "      \"NLS success\": " << (row.nls_success ? "true" : "false") << ",\n";
    out << "      \"DFT success\": " << (row.dft_success ? "true" : "false") << ",\n";
    out << "      \"A0_est\": " << json_number(row.amplitude) << ",\n";
    out << "      \"sigma_est\": " << json_number(row.sigma) << "\n";
    out << "    }";
    if (index + 1U != summary.size()) {
      out << ',';
    }
    out << '\n';
  }
  out << "  ],\n";

  const auto& results = batch.results();
  if (results.empty()) {
    out << "  \"consistency_analysis\": null,\n";
    out << "  \"crlb_comparison_analysis\": null\n";
    out << "}\n";
    return out.str();
  }

  const auto consistency = batch.consistency_analysis();
  const auto nls_mean = consistency.nls_frequency_statistics.mean;
  const auto dft_mean = consistency.dft_frequency_statistics.mean;
  const auto nls_std_across = consistency.nls_frequency_statistics.standard_deviation;
  const auto dft_std_across = consistency.dft_frequency_statistics.standard_deviation;
  const auto nls_cv =
      nls_mean > 0.0 && std::isfinite(nls_mean) ? nls_std_across / nls_mean
                                                 : std::numeric_limits<double>::infinity();
  const auto dft_cv =
      dft_mean > 0.0 && std::isfinite(dft_mean) ? dft_std_across / dft_mean
                                                 : std::numeric_limits<double>::infinity();
  const auto nls_span =
      consistency.nls_frequency_statistics.maximum - consistency.nls_frequency_statistics.minimum;
  const auto dft_span =
      consistency.dft_frequency_statistics.maximum - consistency.dft_frequency_statistics.minimum;

  out << "  \"consistency_analysis\": {\n";
  out << "    \"n_realizations\": " << consistency.realization_count << ",\n";
  out << "    \"n_pairwise_comparisons\": " << consistency.pairwise_comparison_count << ",\n";
  out << "    \"nls_pairwise_diffs\": ";
  append_json_double_array(out, consistency.nls_pairwise_differences);
  out << ",\n";
  out << "    \"dft_pairwise_diffs\": ";
  append_json_double_array(out, consistency.dft_pairwise_differences);
  out << ",\n";
  out << "    \"nls_pairwise_indices\": [\n";
  {
    auto first = true;
    for (auto lhs = std::size_t{0}; lhs < results.size(); ++lhs) {
      for (auto rhs = lhs + 1U; rhs < results.size(); ++rhs) {
        if (!first) {
          out << ",\n";
        }
        first = false;
        out << "      [" << lhs << ", " << rhs << ']';
      }
    }
    if (!first) {
      out << '\n';
    }
  }
  out << "    ],\n";
  out << "    \"dft_pairwise_indices\": [\n";
  {
    auto first = true;
    for (auto lhs = std::size_t{0}; lhs < results.size(); ++lhs) {
      for (auto rhs = lhs + 1U; rhs < results.size(); ++rhs) {
        if (!first) {
          out << ",\n";
        }
        first = false;
        out << "      [" << lhs << ", " << rhs << ']';
      }
    }
    if (!first) {
      out << '\n';
    }
  }
  out << "    ],\n";
  out << "    \"nls_statistics\": ";
  append_summary_statistics_object(out, consistency.nls_pairwise_statistics);
  out << ",\n";
  out << "    \"dft_statistics\": ";
  append_summary_statistics_object(out, consistency.dft_pairwise_statistics);
  out << ",\n";
  out << "    \"nls_mean\": " << json_number(nls_mean) << ",\n";
  out << "    \"dft_mean\": " << json_number(dft_mean) << ",\n";
  out << "    \"nls_std_across_realizations\": " << json_number(nls_std_across) << ",\n";
  out << "    \"dft_std_across_realizations\": " << json_number(dft_std_across) << ",\n";
  out << "    \"nls_cv\": " << json_number(nls_cv) << ",\n";
  out << "    \"dft_cv\": " << json_number(dft_cv) << ",\n";
  out << "    \"nls_span\": " << json_number(nls_span) << ",\n";
  out << "    \"dft_span\": " << json_number(dft_span) << ",\n";
  out << "    \"nls_range\": [" << json_number(consistency.nls_frequency_statistics.minimum) << ", "
      << json_number(consistency.nls_frequency_statistics.maximum) << "],\n";
  out << "    \"dft_range\": [" << json_number(consistency.dft_frequency_statistics.minimum) << ", "
      << json_number(consistency.dft_frequency_statistics.maximum) << "]\n";
  out << "  },\n";

  auto frequency_diffs = std::vector<double>{};
  auto plugin_stds = std::vector<double>{};
  auto ratios = std::vector<double>{};
  auto valid_ratios = std::vector<double>{};
  frequency_diffs.reserve(results.size());
  plugin_stds.reserve(results.size());
  ratios.reserve(results.size());
  auto finite_stds = std::vector<double>{};
  for (const auto& result : results) {
    const auto diff = std::abs(result.nls.frequency_hz - result.dft.frequency_hz);
    frequency_diffs.push_back(diff);
    plugin_stds.push_back(result.plugin_crlb_std_f);
    if (result.plugin_crlb_std_f > 0.0 && std::isfinite(result.plugin_crlb_std_f)) {
      const auto ratio = diff / result.plugin_crlb_std_f;
      ratios.push_back(ratio);
      valid_ratios.push_back(ratio);
    } else {
      ratios.push_back(std::numeric_limits<double>::quiet_NaN());
    }
    if (std::isfinite(result.plugin_crlb_std_f)) {
      finite_stds.push_back(result.plugin_crlb_std_f);
    }
  }

  auto crlb_stats_mean = std::numeric_limits<double>::quiet_NaN();
  auto crlb_stats_min = std::numeric_limits<double>::quiet_NaN();
  auto crlb_stats_max = std::numeric_limits<double>::quiet_NaN();
  if (!finite_stds.empty()) {
    const auto st = statistics(std::move(finite_stds));
    crlb_stats_mean = st.mean;
    crlb_stats_min = st.minimum;
    crlb_stats_max = st.maximum;
  }

  auto ratio_stats_mean = std::numeric_limits<double>::quiet_NaN();
  auto ratio_stats_median = std::numeric_limits<double>::quiet_NaN();
  auto ratio_stats_min = std::numeric_limits<double>::quiet_NaN();
  auto ratio_stats_max = std::numeric_limits<double>::quiet_NaN();
  if (!valid_ratios.empty()) {
    const auto rs = statistics(std::vector<double>(valid_ratios.begin(), valid_ratios.end()));
    ratio_stats_mean = rs.mean;
    ratio_stats_median = rs.median;
    ratio_stats_min = rs.minimum;
    ratio_stats_max = rs.maximum;
  }

  out << "  \"crlb_comparison_analysis\": {\n";
  out << "    \"frequency_diffs\": ";
  append_json_double_array(out, frequency_diffs);
  out << ",\n";
  out << "    \"plugin_crlb_stds\": ";
  append_json_double_array(out, plugin_stds);
  out << ",\n";
  out << "    \"crlb_stds\": ";
  append_json_double_array(out, plugin_stds);
  out << ",\n";
  out << "    \"ratios\": ";
  append_json_double_array(out, ratios);
  out << ",\n";
  out << "    \"valid_ratios\": ";
  append_json_double_array(out, valid_ratios);
  out << ",\n";
  out << "    \"crlb_statistics\": {\"mean\": " << json_number(crlb_stats_mean) << ", \"min\": "
      << json_number(crlb_stats_min) << ", \"max\": " << json_number(crlb_stats_max) << "},\n";
  out << "    \"ratio_statistics\": {\"mean\": " << json_number(ratio_stats_mean) << ", \"median\": "
      << json_number(ratio_stats_median) << ", \"min\": " << json_number(ratio_stats_min)
      << ", \"max\": " << json_number(ratio_stats_max) << "}\n";
  out << "  }\n";
  out << "}\n";
  return out.str();
}

std::string to_json_batch_export(BatchRingDownAnalyzer& batch,
                                 const ProcessResult& process,
                                 BatchExportOptions options) {
  if (options.mode == BatchExportMode::full) {
    return to_json_batch_report(batch, process, options.report);
  }
  return to_json(process);
}

} // namespace ringdown

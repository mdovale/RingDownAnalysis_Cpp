#include <ringdown/analyzer.hpp>

#include <ringdown/crlb.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <numbers>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

#include <zlib.h>

namespace ringdown {

namespace {

[[nodiscard]] double mean(const std::vector<double>& values) {
  return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}

[[nodiscard]] std::vector<double> inferred_time(std::size_t count, double sample_rate_hz) {
  if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
    throw std::invalid_argument{"Sampling frequency fs must be positive and finite"};
  }
  auto time = std::vector<double>(count);
  for (auto index = std::size_t{0}; index < count; ++index) {
    time[index] = static_cast<double>(index) / sample_rate_hz;
  }
  return time;
}

void validate_samples(const std::vector<double>& samples, const char* source) {
  if (samples.size() < 2U) {
    throw std::invalid_argument{"At least 2 samples required for analysis"};
  }
  for (const auto sample : samples) {
    if (!std::isfinite(sample)) {
      throw std::invalid_argument{std::string{source} + " must contain only finite values"};
    }
  }
}

[[nodiscard]] double validate_uniform_timebase(std::vector<double>& time) {
  if (time.size() < 2U) {
    throw std::invalid_argument{"At least 2 samples required for analysis"};
  }
  for (const auto sample : time) {
    if (!std::isfinite(sample)) {
      throw std::invalid_argument{"Time array must contain only finite values"};
    }
  }
  const auto offset = time.front();
  for (auto& sample : time) {
    sample -= offset;
  }

  auto intervals = std::vector<double>{};
  intervals.reserve(time.size() - 1U);
  for (auto index = std::size_t{1}; index < time.size(); ++index) {
    const auto interval = time[index] - time[index - 1U];
    if (interval <= 0.0 || !std::isfinite(interval)) {
      throw std::invalid_argument{"Time array must be strictly increasing"};
    }
    intervals.push_back(interval);
  }
  auto sorted = intervals;
  std::sort(sorted.begin(), sorted.end());
  const auto dt = sorted[sorted.size() / 2U];
  for (const auto interval : intervals) {
    if (std::abs(interval - dt) > std::max(1.0e-12, std::abs(dt) * 5.0e-3)) {
      throw std::invalid_argument{"Nonuniform timestamps are not supported"};
    }
  }
  return 1.0 / dt;
}

[[nodiscard]] std::pair<std::vector<double>, std::vector<double>> crop_to_tau(
    const std::vector<double>& time,
    const std::vector<double>& samples,
    double tau,
    double max_tau_multiplier) {
  if (!std::isfinite(max_tau_multiplier) || max_tau_multiplier <= 0.0) {
    throw std::invalid_argument{"max_tau_multiplier must be positive and finite"};
  }
  if (!std::isfinite(tau) || tau <= 0.0) {
    throw std::invalid_argument{"tau_est must be positive and finite"};
  }

  const auto t_max = tau * max_tau_multiplier;
  auto cropped_time = std::vector<double>{};
  auto cropped_samples = std::vector<double>{};
  for (auto index = std::size_t{0}; index < time.size(); ++index) {
    if (time[index] <= t_max) {
      cropped_time.push_back(time[index]);
      cropped_samples.push_back(samples[index]);
    }
  }
  if (cropped_time.size() < 100U) {
    return {time, samples};
  }
  return {cropped_time, cropped_samples};
}

[[nodiscard]] std::optional<std::array<double, 3>> solve_3x3(std::array<std::array<double, 4>, 3> matrix) {
  for (auto col = std::size_t{0}; col < 3U; ++col) {
    auto pivot = col;
    for (auto row = col + 1U; row < 3U; ++row) {
      if (std::abs(matrix[row][col]) > std::abs(matrix[pivot][col])) {
        pivot = row;
      }
    }
    if (std::abs(matrix[pivot][col]) < 1.0e-20) {
      return std::nullopt;
    }
    if (pivot != col) {
      std::swap(matrix[pivot], matrix[col]);
    }
    const auto scale = matrix[col][col];
    for (auto item = col; item < 4U; ++item) {
      matrix[col][item] /= scale;
    }
    for (auto row = std::size_t{0}; row < 3U; ++row) {
      if (row == col) {
        continue;
      }
      const auto factor = matrix[row][col];
      for (auto item = col; item < 4U; ++item) {
        matrix[row][item] -= factor * matrix[col][item];
      }
    }
  }
  return std::array<double, 3>{matrix[0][3], matrix[1][3], matrix[2][3]};
}

[[nodiscard]] bool parse_csv_data_row(const std::string& line, double& time, double& sample) {
  const auto* cursor = line.c_str();
  auto* end = const_cast<char*>(cursor);
  time = std::strtod(cursor, &end);
  if (end == cursor) {
    return false;
  }

  cursor = std::strchr(end, ',');
  if (cursor == nullptr) {
    return false;
  }
  for (auto column = 1U; column < 3U; ++column) {
    cursor = std::strchr(cursor + 1, ',');
    if (cursor == nullptr) {
      return false;
    }
  }

  const auto* sample_start = cursor + 1;
  sample = std::strtod(sample_start, &end);
  return end != sample_start;
}

[[nodiscard]] std::string format_byte_count(std::uintmax_t value) {
  auto digits = std::to_string(value);
  for (auto insert_at = static_cast<std::ptrdiff_t>(digits.size()) - 3; insert_at > 0; insert_at -= 3) {
    digits.insert(static_cast<std::size_t>(insert_at), ",");
  }
  return digits;
}

void check_file_size(const std::string& filepath, std::uintmax_t max_file_size_bytes) {
  auto error = std::error_code{};
  const auto size = std::filesystem::file_size(filepath, error);
  if (error) {
    return;
  }
  if (size <= max_file_size_bytes) {
    return;
  }
  throw std::invalid_argument{"File size (" + format_byte_count(size) +
                              " bytes) exceeds maximum allowed (" +
                              format_byte_count(max_file_size_bytes) + " bytes): " + filepath};
}

[[nodiscard]] std::string lower_ascii(std::string value) {
  for (auto& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

[[nodiscard]] std::uint16_t read_u16(const std::vector<unsigned char>& bytes, std::size_t offset) {
  if (offset + sizeof(std::uint16_t) > bytes.size()) {
    throw std::invalid_argument{"Invalid ZIP file structure: truncated uint16"};
  }
  return static_cast<std::uint16_t>(bytes[offset]) |
         static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] std::uint32_t read_u32(const std::vector<unsigned char>& bytes, std::size_t offset) {
  if (offset + sizeof(std::uint32_t) > bytes.size()) {
    throw std::invalid_argument{"Invalid ZIP file structure: truncated uint32"};
  }
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

struct ZipEntry {
  std::string name;
  std::uint16_t flags{0U};
  std::uint16_t method{0U};
  std::uint32_t compressed_size{0U};
  std::uint32_t uncompressed_size{0U};
  std::uint32_t local_header_offset{0U};
};

[[nodiscard]] bool is_directory_entry(std::string_view name) {
  return !name.empty() && (name.back() == '/' || name.back() == '\\');
}

[[nodiscard]] bool is_csv_entry(std::string_view name) {
  return lower_ascii(std::filesystem::path{std::string{name}}.extension().string()) == ".csv";
}

[[nodiscard]] std::size_t find_zip_eocd(const std::vector<unsigned char>& bytes) {
  constexpr auto eocd_signature = std::uint32_t{0x06054b50U};
  constexpr auto min_eocd_size = std::size_t{22U};
  if (bytes.size() < min_eocd_size) {
    throw std::invalid_argument{"Invalid ZIP file structure: file is too small"};
  }
  const auto search_start =
      bytes.size() - std::min<std::size_t>(bytes.size(), min_eocd_size + 0xFFFFU);
  for (auto offset = bytes.size() - min_eocd_size + 1U; offset > search_start; --offset) {
    const auto candidate = offset - 1U;
    if (read_u32(bytes, candidate) == eocd_signature) {
      return candidate;
    }
  }
  throw std::invalid_argument{"Invalid ZIP file structure: missing end-of-central-directory record"};
}

[[nodiscard]] std::vector<ZipEntry> read_zip_entries(const std::vector<unsigned char>& bytes) {
  constexpr auto central_signature = std::uint32_t{0x02014b50U};
  const auto eocd = find_zip_eocd(bytes);
  if (read_u16(bytes, eocd + 4U) != 0U || read_u16(bytes, eocd + 6U) != 0U) {
    throw std::invalid_argument{"Invalid ZIP file structure: multi-disk archives are not supported"};
  }

  const auto entry_count = read_u16(bytes, eocd + 10U);
  const auto central_size = read_u32(bytes, eocd + 12U);
  const auto central_offset = read_u32(bytes, eocd + 16U);
  if (static_cast<std::size_t>(central_offset) + static_cast<std::size_t>(central_size) > bytes.size()) {
    throw std::invalid_argument{"Invalid ZIP file structure: central directory exceeds file size"};
  }

  auto entries = std::vector<ZipEntry>{};
  entries.reserve(entry_count);
  auto cursor = static_cast<std::size_t>(central_offset);
  for (auto index = std::uint16_t{0U}; index < entry_count; ++index) {
    if (cursor + 46U > bytes.size() || read_u32(bytes, cursor) != central_signature) {
      throw std::invalid_argument{"Invalid ZIP file structure: malformed central directory"};
    }
    const auto compressed_size = read_u32(bytes, cursor + 20U);
    const auto uncompressed_size = read_u32(bytes, cursor + 24U);
    const auto name_size = read_u16(bytes, cursor + 28U);
    const auto extra_size = read_u16(bytes, cursor + 30U);
    const auto comment_size = read_u16(bytes, cursor + 32U);
    const auto local_header_offset = read_u32(bytes, cursor + 42U);
    if (compressed_size == 0xFFFFFFFFU || uncompressed_size == 0xFFFFFFFFU ||
        local_header_offset == 0xFFFFFFFFU) {
      throw std::invalid_argument{"Invalid ZIP file structure: ZIP64 archives are not supported"};
    }
    const auto name_offset = cursor + 46U;
    const auto next = name_offset + static_cast<std::size_t>(name_size) +
                      static_cast<std::size_t>(extra_size) + static_cast<std::size_t>(comment_size);
    if (next > bytes.size()) {
      throw std::invalid_argument{"Invalid ZIP file structure: central directory entry exceeds file size"};
    }
    entries.push_back(ZipEntry{
        std::string{reinterpret_cast<const char*>(bytes.data() + name_offset), name_size},
        read_u16(bytes, cursor + 8U),
        read_u16(bytes, cursor + 10U),
        compressed_size,
        uncompressed_size,
        local_header_offset});
    cursor = next;
  }
  return entries;
}

[[nodiscard]] const ZipEntry& select_single_csv_entry(const std::vector<ZipEntry>& entries,
                                                     const std::string& filepath) {
  auto selected = static_cast<const ZipEntry*>(nullptr);
  auto non_directory_count = std::size_t{0};
  for (const auto& entry : entries) {
    if (is_directory_entry(entry.name)) {
      continue;
    }
    ++non_directory_count;
    if (!is_csv_entry(entry.name)) {
      throw std::invalid_argument{"ZIP file must contain exactly one .csv file: " + filepath};
    }
    if (selected != nullptr) {
      throw std::invalid_argument{"ZIP file must contain exactly one .csv file: " + filepath};
    }
    selected = &entry;
  }
  if (selected == nullptr || non_directory_count != 1U) {
    throw std::invalid_argument{"ZIP file must contain exactly one .csv file: " + filepath};
  }
  return *selected;
}

[[nodiscard]] std::string inflate_raw_deflate(const unsigned char* compressed,
                                             std::size_t compressed_size,
                                             std::uint32_t uncompressed_size) {
  if (compressed_size > static_cast<std::size_t>(std::numeric_limits<uInt>::max())) {
    throw std::invalid_argument{"Invalid ZIP file structure: compressed entry is too large"};
  }
  auto output = std::string(uncompressed_size, '\0');
  auto stream = z_stream{};
  stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed));
  stream.avail_in = static_cast<uInt>(compressed_size);
  stream.next_out = reinterpret_cast<Bytef*>(output.data());
  stream.avail_out = static_cast<uInt>(output.size());

  if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
    throw std::runtime_error{"Failed to initialize ZIP deflate decompressor"};
  }
  const auto status = inflate(&stream, Z_FINISH);
  const auto end_status = inflateEnd(&stream);
  if (status != Z_STREAM_END || end_status != Z_OK || stream.total_out != output.size()) {
    throw std::invalid_argument{"Invalid ZIP file structure: deflate payload could not be decompressed"};
  }
  return output;
}

[[nodiscard]] std::string extract_zip_entry(const std::vector<unsigned char>& bytes,
                                           const ZipEntry& entry) {
  constexpr auto local_signature = std::uint32_t{0x04034b50U};
  if ((entry.flags & 0x1U) != 0U) {
    throw std::invalid_argument{"Invalid ZIP file structure: encrypted entries are not supported"};
  }
  const auto local_offset = static_cast<std::size_t>(entry.local_header_offset);
  if (local_offset + 30U > bytes.size() || read_u32(bytes, local_offset) != local_signature) {
    throw std::invalid_argument{"Invalid ZIP file structure: malformed local file header"};
  }
  const auto local_name_size = read_u16(bytes, local_offset + 26U);
  const auto local_extra_size = read_u16(bytes, local_offset + 28U);
  const auto data_offset = local_offset + 30U + static_cast<std::size_t>(local_name_size) +
                           static_cast<std::size_t>(local_extra_size);
  const auto compressed_size = static_cast<std::size_t>(entry.compressed_size);
  if (data_offset + compressed_size > bytes.size()) {
    throw std::invalid_argument{"Invalid ZIP file structure: compressed entry exceeds file size"};
  }

  if (entry.method == 0U) {
    if (entry.compressed_size != entry.uncompressed_size) {
      throw std::invalid_argument{"Invalid ZIP file structure: stored entry size mismatch"};
    }
    return std::string{reinterpret_cast<const char*>(bytes.data() + data_offset), compressed_size};
  }
  if (entry.method == 8U) {
    return inflate_raw_deflate(bytes.data() + data_offset, compressed_size, entry.uncompressed_size);
  }
  throw std::invalid_argument{"Invalid ZIP file structure: only stored and deflated CSV entries are supported"};
}

enum class MatType : std::uint32_t {
  Int8 = 1U,
  UInt32 = 6U,
  Double = 9U,
  Matrix = 14U,
  Compressed = 15U,
};

enum class MatClass : std::uint32_t {
  Struct = 2U,
  Double = 6U,
};

struct MatElement {
  std::uint32_t type{0U};
  std::uint32_t byte_count{0U};
  std::size_t data_offset{0U};
  std::array<unsigned char, 4> small_data{};
  bool small{false};
};

struct MatMatrix {
  std::uint32_t class_type{0U};
  std::vector<std::int32_t> dimensions;
  std::string name;
  std::vector<double> values;
  std::size_t rows{0U};
  std::size_t columns{0U};
  std::vector<std::string> field_names;
  std::vector<MatMatrix> fields;
};

template <typename T>
[[nodiscard]] T read_little_endian(const std::vector<unsigned char>& bytes, std::size_t offset) {
  if (offset + sizeof(T) > bytes.size()) {
    throw std::invalid_argument{"Invalid MAT file structure: truncated data element"};
  }
  auto value = T{};
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}

[[nodiscard]] std::size_t padded_size(std::size_t byte_count) {
  return ((byte_count + 7U) / 8U) * 8U;
}

[[nodiscard]] MatElement read_mat_element(const std::vector<unsigned char>& bytes,
                                          std::size_t& cursor,
                                          std::size_t limit) {
  if (cursor + 8U > limit || limit > bytes.size()) {
    throw std::invalid_argument{"Invalid MAT file structure: truncated tag"};
  }
  const auto first = read_little_endian<std::uint32_t>(bytes, cursor);
  const auto second = read_little_endian<std::uint32_t>(bytes, cursor + 4U);
  cursor += 8U;

  auto element = MatElement{};
  const auto small_type = first & 0xFFFFU;
  const auto small_count = first >> 16U;
  if (small_count != 0U) {
    element.type = small_type;
    element.byte_count = small_count;
    element.small = true;
    std::memcpy(element.small_data.data(), &second, sizeof(second));
    return element;
  }

  element.type = first;
  element.byte_count = second;
  element.data_offset = cursor;
  const auto padded = padded_size(element.byte_count);
  if (cursor + padded > limit) {
    throw std::invalid_argument{"Invalid MAT file structure: element exceeds file size"};
  }
  cursor += padded;
  return element;
}

[[nodiscard]] unsigned char mat_byte_at(const std::vector<unsigned char>& bytes,
                                        const MatElement& element,
                                        std::size_t offset) {
  if (offset >= element.byte_count) {
    throw std::invalid_argument{"Invalid MAT file structure: element byte offset out of range"};
  }
  return element.small ? element.small_data[offset] : bytes[element.data_offset + offset];
}

[[nodiscard]] std::int32_t mat_i32_at(const std::vector<unsigned char>& bytes,
                                      const MatElement& element,
                                      std::size_t offset) {
  if (offset + sizeof(std::int32_t) > element.byte_count) {
    throw std::invalid_argument{"Invalid MAT file structure: int32 element is truncated"};
  }
  if (element.small) {
    auto value = std::int32_t{};
    std::memcpy(&value, element.small_data.data() + offset, sizeof(value));
    return value;
  }
  return read_little_endian<std::int32_t>(bytes, element.data_offset + offset);
}

[[nodiscard]] std::uint32_t mat_u32_at(const std::vector<unsigned char>& bytes,
                                       const MatElement& element,
                                       std::size_t offset) {
  if (offset + sizeof(std::uint32_t) > element.byte_count) {
    throw std::invalid_argument{"Invalid MAT file structure: uint32 element is truncated"};
  }
  if (element.small) {
    auto value = std::uint32_t{};
    std::memcpy(&value, element.small_data.data() + offset, sizeof(value));
    return value;
  }
  return read_little_endian<std::uint32_t>(bytes, element.data_offset + offset);
}

[[nodiscard]] double mat_double_at(const std::vector<unsigned char>& bytes,
                                   const MatElement& element,
                                   std::size_t offset) {
  if (offset + sizeof(double) > element.byte_count || element.small) {
    throw std::invalid_argument{"Invalid MAT file structure: double element is truncated"};
  }
  return read_little_endian<double>(bytes, element.data_offset + offset);
}

[[nodiscard]] std::string mat_string(const std::vector<unsigned char>& bytes,
                                     const MatElement& element) {
  auto value = std::string{};
  value.reserve(element.byte_count);
  for (auto index = std::size_t{0}; index < element.byte_count; ++index) {
    const auto ch = static_cast<char>(mat_byte_at(bytes, element, index));
    if (ch != '\0') {
      value.push_back(ch);
    }
  }
  return value;
}

[[nodiscard]] MatMatrix parse_mat_matrix(const std::vector<unsigned char>& bytes,
                                         const MatElement& matrix_element);

[[nodiscard]] MatMatrix parse_mat_matrix_payload(const std::vector<unsigned char>& bytes,
                                                 std::size_t cursor,
                                                 std::size_t limit) {
  const auto flags = read_mat_element(bytes, cursor, limit);
  if (flags.type != static_cast<std::uint32_t>(MatType::UInt32) || flags.byte_count < 8U) {
    throw std::invalid_argument{"Invalid MAT file structure: missing array flags"};
  }
  auto matrix = MatMatrix{};
  matrix.class_type = mat_u32_at(bytes, flags, 0U) & 0xFFU;

  const auto dimensions = read_mat_element(bytes, cursor, limit);
  matrix.dimensions.reserve(dimensions.byte_count / sizeof(std::int32_t));
  for (auto offset = std::size_t{0}; offset + sizeof(std::int32_t) <= dimensions.byte_count;
       offset += sizeof(std::int32_t)) {
    matrix.dimensions.push_back(mat_i32_at(bytes, dimensions, offset));
  }
  if (matrix.dimensions.size() >= 2U) {
    matrix.rows = static_cast<std::size_t>(matrix.dimensions[0]);
    matrix.columns = static_cast<std::size_t>(matrix.dimensions[1]);
  }

  const auto name = read_mat_element(bytes, cursor, limit);
  matrix.name = mat_string(bytes, name);

  if (matrix.class_type == static_cast<std::uint32_t>(MatClass::Double)) {
    const auto real = read_mat_element(bytes, cursor, limit);
    if (real.type != static_cast<std::uint32_t>(MatType::Double)) {
      throw std::invalid_argument{"Invalid MAT file structure: expected double payload"};
    }
    matrix.values.reserve(real.byte_count / sizeof(double));
    for (auto offset = std::size_t{0}; offset + sizeof(double) <= real.byte_count; offset += sizeof(double)) {
      matrix.values.push_back(mat_double_at(bytes, real, offset));
    }
    return matrix;
  }

  if (matrix.class_type == static_cast<std::uint32_t>(MatClass::Struct)) {
    const auto field_name_length = read_mat_element(bytes, cursor, limit);
    const auto field_width = static_cast<std::size_t>(mat_i32_at(bytes, field_name_length, 0U));
    const auto field_names = read_mat_element(bytes, cursor, limit);
    if (field_width == 0U) {
      throw std::invalid_argument{"Invalid MAT file structure: zero field-name width"};
    }
    const auto field_count = field_names.byte_count / field_width;
    matrix.field_names.reserve(field_count);
    for (auto field = std::size_t{0}; field < field_count; ++field) {
      auto name_value = std::string{};
      for (auto offset = std::size_t{0}; offset < field_width; ++offset) {
        const auto ch = static_cast<char>(mat_byte_at(bytes, field_names, field * field_width + offset));
        if (ch != '\0') {
          name_value.push_back(ch);
        }
      }
      matrix.field_names.push_back(name_value);
    }

    const auto struct_elements = std::max<std::size_t>(1U, matrix.rows * matrix.columns);
    matrix.fields.reserve(field_count * struct_elements);
    for (auto index = std::size_t{0}; index < field_count * struct_elements; ++index) {
      const auto field_matrix = read_mat_element(bytes, cursor, limit);
      if (field_matrix.type != static_cast<std::uint32_t>(MatType::Matrix)) {
        throw std::invalid_argument{"Invalid MAT file structure: struct field is not a matrix"};
      }
      matrix.fields.push_back(parse_mat_matrix(bytes, field_matrix));
    }
  }

  return matrix;
}

[[nodiscard]] MatMatrix parse_mat_matrix(const std::vector<unsigned char>& bytes,
                                         const MatElement& matrix_element) {
  if (matrix_element.type != static_cast<std::uint32_t>(MatType::Matrix) || matrix_element.small) {
    throw std::invalid_argument{"Invalid MAT file structure: expected matrix element"};
  }
  return parse_mat_matrix_payload(bytes,
                                  matrix_element.data_offset,
                                  matrix_element.data_offset + matrix_element.byte_count);
}

[[nodiscard]] const MatMatrix& struct_field(const MatMatrix& matrix, std::string_view field_name) {
  const auto found = std::find(matrix.field_names.begin(), matrix.field_names.end(), field_name);
  if (found == matrix.field_names.end()) {
    throw std::invalid_argument{"Invalid MAT file structure: missing moku.data"};
  }
  const auto index = static_cast<std::size_t>(std::distance(matrix.field_names.begin(), found));
  if (index >= matrix.fields.size()) {
    throw std::invalid_argument{"Invalid MAT file structure: missing struct field payload"};
  }
  return matrix.fields[index];
}

void validate_finite_channel(const std::vector<double>& values,
                             std::string_view channel,
                             const std::string& filepath) {
  if (values.empty()) {
    throw std::invalid_argument{std::string{channel} + " channel in " + filepath + " contains no samples"};
  }
  for (auto index = std::size_t{0}; index < values.size(); ++index) {
    if (!std::isfinite(values[index])) {
      throw std::invalid_argument{std::string{channel} + " channel in " + filepath +
                                  " must contain only finite values"};
    }
  }
}

void remove_mean(std::vector<double>& values) {
  const auto value_mean = mean(values);
  for (auto& value : values) {
    value -= value_mean;
  }
}

[[nodiscard]] std::string optional_number(std::optional<double> value) {
  if (!value.has_value()) {
    return "null";
  }
  if (!std::isfinite(*value)) {
    return "null";
  }
  auto out = std::ostringstream{};
  out << std::setprecision(17) << *value;
  return out.str();
}

[[nodiscard]] std::string json_number(double value) {
  if (!std::isfinite(value)) {
    return "null";
  }
  auto out = std::ostringstream{};
  out << std::setprecision(17) << value;
  return out.str();
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

[[nodiscard]] std::string optional_size_t_json(const std::optional<std::size_t>& value) {
  if (!value.has_value()) {
    return "null";
  }
  return std::to_string(*value);
}

} // namespace

RingDownAnalyzer::RingDownAnalyzer(NLSFrequencyEstimator nls_estimator,
                                   DFTFrequencyEstimator dft_estimator)
    : nls_estimator_{std::move(nls_estimator)}, dft_estimator_{std::move(dft_estimator)} {}

AnalyzerResult RingDownAnalyzer::analyze_array(const std::vector<double>& samples,
                                               double sample_rate_hz,
                                               double max_tau_multiplier) const {
  return analyze_array(inferred_time(samples.size(), sample_rate_hz), samples, max_tau_multiplier);
}

AnalyzerResult RingDownAnalyzer::analyze_array(const std::vector<double>& time,
                                               const std::vector<double>& samples,
                                               double max_tau_multiplier) const {
  validate_samples(samples, "Signal data");
  if (time.size() != samples.size()) {
    throw std::invalid_argument{"t and data must have same length"};
  }

  auto normalized_time = time;
  const auto sample_rate_hz = validate_uniform_timebase(normalized_time);
  const auto initial = estimate_initial_parameters_from_dft(samples, sample_rate_hz);
  const auto tau_seed = estimate_initial_tau_from_envelope(samples, sample_rate_hz);
  auto tau_estimate = estimate_tau(normalized_time, samples, sample_rate_hz, tau_seed, initial);
  if (!std::isfinite(tau_estimate) || tau_estimate <= 0.0) {
    tau_estimate = tau_seed;
  }

  auto [cropped_time, cropped_samples] =
      crop_to_tau(normalized_time, samples, tau_estimate, max_tau_multiplier);
  if (cropped_time.size() < 1000U) {
    cropped_time = normalized_time;
    cropped_samples = samples;
  }

  const auto cropped_tau_seed = std::max(tau_estimate, 1.0 / sample_rate_hz);
  const auto nls = nls_estimator_.estimate_full(cropped_samples, sample_rate_hz, cropped_tau_seed, initial);
  const auto dft = dft_estimator_.estimate_full(cropped_samples, sample_rate_hz);

  const auto tau_model = nls.tau.value_or(dft.tau.value_or(tau_estimate));
  const auto noise = estimate_noise_parameters(cropped_time, cropped_samples, tau_model, nls.frequency_hz);
  const auto crlb_var = CRLBCalculator::variance(
      std::max(noise.amplitude, std::numeric_limits<double>::epsilon()),
      std::max(noise.sigma, std::numeric_limits<double>::epsilon()),
      sample_rate_hz,
      cropped_samples.size(),
      tau_model);
  const auto crlb_std = std::isfinite(crlb_var) ? std::sqrt(crlb_var)
                                                : std::numeric_limits<double>::infinity();

  return AnalyzerResult{normalized_time,
                        samples,
                        cropped_time,
                        cropped_samples,
                        {},
                        sample_rate_hz,
                        tau_seed,
                        tau_estimate,
                        tau_model,
                        nls,
                        dft,
                        noise,
                        crlb_var,
                        crlb_std,
                        noise.success && nls.success && std::isfinite(crlb_std) && crlb_std > 0.0,
                        normalized_time.size(),
                        cropped_samples.size(),
                        normalized_time.back(),
                        cropped_time.back(),
                        {},
                        {}};
}

AnalyzerResult RingDownAnalyzer::analyze_file(const std::string& filepath,
                                              double max_tau_multiplier) const {
  const auto loaded = RingDownDataLoader::load(filepath);
  auto result = analyze_array(loaded.time, loaded.samples, max_tau_multiplier);
  result.secondary_samples = loaded.secondary_samples;
  result.filename = std::filesystem::path{filepath}.filename().string();
  result.file_type = loaded.file_type;
  return result;
}

double RingDownAnalyzer::estimate_tau(const std::vector<double>& time,
                                      const std::vector<double>& samples,
                                      double sample_rate_hz,
                                      std::optional<double> tau_initial,
                                      std::optional<InitialParameters> initial) const {
  (void)time;
  const auto tau_seed = tau_initial.value_or(estimate_initial_tau_from_envelope(samples, sample_rate_hz));
  const auto result = NLSFrequencyEstimator{}.estimate_full(samples, sample_rate_hz, tau_seed, initial);
  return result.tau.value_or(tau_seed);
}

NoiseEstimate RingDownAnalyzer::estimate_noise_parameters(const std::vector<double>& time,
                                                          const std::vector<double>& samples,
                                                          double tau_model,
                                                          double frequency_hz) const {
  if (time.size() != samples.size() || samples.size() < 4U) {
    throw std::invalid_argument{"At least 4 cropped samples are required for noise estimation"};
  }
  if (!std::isfinite(tau_model) || tau_model <= 0.0) {
    throw std::invalid_argument{"tau_model must be positive and finite"};
  }
  if (!std::isfinite(frequency_hz) || frequency_hz < 0.0) {
    throw std::invalid_argument{"f_model must be non-negative and finite"};
  }

  auto normal = std::array<std::array<double, 4>, 3>{};
  auto sample_sum_squares = 0.0;
  for (auto index = std::size_t{0}; index < samples.size(); ++index) {
    const auto t = time[index] - time.front();
    const auto envelope = std::exp(-t / tau_model);
    const auto angle = 2.0 * std::numbers::pi * frequency_hz * t;
    const auto row = std::array<double, 3>{envelope * std::cos(angle), envelope * std::sin(angle), 1.0};
    sample_sum_squares += samples[index] * samples[index];
    for (auto lhs = std::size_t{0}; lhs < 3U; ++lhs) {
      for (auto rhs = std::size_t{0}; rhs < 3U; ++rhs) {
        normal[lhs][rhs] += row[lhs] * row[rhs];
      }
      normal[lhs][3] += row[lhs] * samples[index];
    }
  }

  const auto solution = solve_3x3(normal);
  if (!solution.has_value()) {
    return NoiseEstimate{std::numeric_limits<double>::epsilon(),
                         0.0,
                         0.0,
                         0U,
                         false,
                         "tail_std_fallback",
                         "Design matrix is rank-deficient"};
  }

  const auto explained_sum_squares = (*solution)[0] * normal[0][3] + (*solution)[1] * normal[1][3] +
                                     (*solution)[2] * normal[2][3];
  const auto rss = std::max(0.0, sample_sum_squares - explained_sum_squares);

  const auto dof = samples.size() - 3U;
  return NoiseEstimate{std::max(std::hypot((*solution)[0], (*solution)[1]),
                                std::numeric_limits<double>::epsilon()),
                       std::sqrt(rss / static_cast<double>(dof)),
                       std::sqrt(rss / static_cast<double>(samples.size())),
                       dof,
                       true,
                       "fixed_frequency_tau_linear_lstsq",
                       {}};
}

LoadedData RingDownDataLoader::load(const std::string& filepath,
                                    std::optional<std::uintmax_t> max_file_size_bytes) {
  if (max_file_size_bytes.has_value()) {
    check_file_size(filepath, *max_file_size_bytes);
  }
  const auto path = std::filesystem::path{filepath};
  const auto extension = lower_ascii(path.extension().string());
  if (extension == ".csv") {
    return load_csv(filepath, max_file_size_bytes);
  }
  if (extension == ".mat") {
    return load_mat(filepath, max_file_size_bytes);
  }
  if (extension == ".zip") {
    return load_zip(filepath, max_file_size_bytes);
  }
  throw std::invalid_argument{"Unsupported file format: expected .csv, .mat, or .zip"};
}

namespace {

LoadedData load_csv_stream(std::istream& file,
                           const std::string& source,
                           std::string file_type,
                           std::size_t reserve_count = 0U) {
  auto time = std::vector<double>{};
  auto samples = std::vector<double>{};
  if (reserve_count > 0U) {
    time.reserve(reserve_count);
    samples.reserve(reserve_count);
  }
  auto line = std::string{};
  while (std::getline(file, line)) {
    const auto first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || line[first] == '%') {
      continue;
    }
    auto t = 0.0;
    auto sample = 0.0;
    if (!parse_csv_data_row(line, t, sample)) {
      if (time.empty()) {
        continue;
      }
      throw std::invalid_argument{"Malformed numeric data in CSV file: " + source};
    }
    time.push_back(t);
    samples.push_back(sample);
  }

  validate_samples(samples, "Signal data");
  const auto t0 = time.front();
  for (auto& sample : time) {
    sample -= t0;
  }
  const auto sample_mean = mean(samples);
  for (auto& sample : samples) {
    sample -= sample_mean;
  }
  return LoadedData{time, samples, {}, std::move(file_type)};
}

} // namespace

LoadedData RingDownDataLoader::load_csv(const std::string& filepath,
                                        std::optional<std::uintmax_t> max_file_size_bytes) {
  if (max_file_size_bytes.has_value()) {
    check_file_size(filepath, *max_file_size_bytes);
  }
  const auto reserve_count =
      static_cast<std::size_t>(std::min<std::uintmax_t>(std::filesystem::file_size(filepath) / 64U,
                                                       std::numeric_limits<std::size_t>::max()));
  auto file = std::ifstream{filepath};
  if (!file) {
    throw std::runtime_error{"Could not open CSV file: " + filepath};
  }
  return load_csv_stream(file, filepath, "CSV", reserve_count);
}

LoadedData RingDownDataLoader::load_zip(const std::string& filepath,
                                        std::optional<std::uintmax_t> max_file_size_bytes) {
  if (max_file_size_bytes.has_value()) {
    check_file_size(filepath, *max_file_size_bytes);
  }
  auto file = std::ifstream{filepath, std::ios::binary};
  if (!file) {
    throw std::runtime_error{"Could not open ZIP file: " + filepath};
  }
  auto bytes = std::vector<unsigned char>((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());
  const auto entries = read_zip_entries(bytes);
  const auto& csv_entry = select_single_csv_entry(entries, filepath);
  const auto csv_text = extract_zip_entry(bytes, csv_entry);
  auto stream = std::istringstream{csv_text};
  return load_csv_stream(stream, filepath + ":" + csv_entry.name, "ZIP_CSV", csv_text.size() / 64U);
}

LoadedData RingDownDataLoader::load_mat(const std::string& filepath,
                                        std::optional<std::uintmax_t> max_file_size_bytes) {
  if (max_file_size_bytes.has_value()) {
    check_file_size(filepath, *max_file_size_bytes);
  }
  auto file = std::ifstream{filepath, std::ios::binary};
  if (!file) {
    throw std::runtime_error{"Could not open MAT file: " + filepath};
  }
  auto bytes = std::vector<unsigned char>((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());
  if (bytes.size() < 136U) {
    throw std::invalid_argument{"Invalid MAT file structure: file is too small"};
  }
  if (bytes[126] != 'I' || bytes[127] != 'M') {
    throw std::invalid_argument{"Invalid MAT file structure: only little-endian MAT v5 files are supported"};
  }

  auto cursor = std::size_t{128U};
  auto moku = std::optional<MatMatrix>{};
  while (cursor + 8U <= bytes.size()) {
    auto element = read_mat_element(bytes, cursor, bytes.size());
    if (element.type == static_cast<std::uint32_t>(MatType::Compressed)) {
      throw std::invalid_argument{"Invalid MAT file structure: compressed MAT elements are not supported"};
    }
    if (element.type != static_cast<std::uint32_t>(MatType::Matrix)) {
      continue;
    }
    auto matrix = parse_mat_matrix(bytes, element);
    if (matrix.name == "moku") {
      moku = std::move(matrix);
      break;
    }
  }
  if (!moku.has_value()) {
    throw std::invalid_argument{"Invalid MAT file structure: missing moku variable"};
  }

  const auto& data = struct_field(*moku, "data");
  if (data.class_type != static_cast<std::uint32_t>(MatClass::Double) || data.rows == 0U || data.columns < 4U) {
    throw std::invalid_argument{
        "Invalid MAT file structure: moku.data must be a non-empty 2D array with at least 4 columns"};
  }
  if (data.values.size() < data.rows * data.columns) {
    throw std::invalid_argument{"Invalid MAT file structure: moku.data payload is truncated"};
  }

  auto time = std::vector<double>(data.rows);
  auto samples = std::vector<double>(data.rows);
  auto secondary = std::vector<double>{};
  if (data.columns > 8U) {
    secondary.resize(data.rows);
  }

  for (auto row = std::size_t{0}; row < data.rows; ++row) {
    time[row] = data.values[row];
    samples[row] = data.values[3U * data.rows + row];
    if (!secondary.empty()) {
      secondary[row] = data.values[8U * data.rows + row];
    }
  }

  validate_finite_channel(time, "time", filepath);
  validate_finite_channel(samples, "phase", filepath);
  if (!secondary.empty()) {
    validate_finite_channel(secondary, "V2", filepath);
  }

  const auto t0 = time.front();
  for (auto& value : time) {
    value -= t0;
  }
  remove_mean(samples);
  if (!secondary.empty()) {
    remove_mean(secondary);
  }

  return LoadedData{time, samples, secondary, "MAT"};
}

std::string to_json(const AnalyzerResult& result) {
  auto out = std::ostringstream{};
  out << std::setprecision(17);
  out << "{\n";
  out << "  \"filename\": " << json_string(result.filename) << ",\n";
  out << "  \"type\": " << json_string(result.file_type) << ",\n";
  out << "  \"fs\": " << json_number(result.sample_rate_hz) << ",\n";
  out << "  \"N\": " << result.sample_count << ",\n";
  out << "  \"N_crop\": " << result.cropped_sample_count << ",\n";
  out << "  \"T\": " << json_number(result.observation_time) << ",\n";
  out << "  \"T_crop\": " << json_number(result.cropped_observation_time) << ",\n";
  out << "  \"tau_seed\": " << json_number(result.tau_seed) << ",\n";
  out << "  \"tau_est\": " << json_number(result.tau_estimate) << ",\n";
  out << "  \"tau_model\": " << json_number(result.tau_model) << ",\n";
  out << "  \"f_nls\": " << json_number(result.nls.frequency_hz) << ",\n";
  out << "  \"f_dft\": " << json_number(result.dft.frequency_hz) << ",\n";
  out << "  \"tau_nls\": " << optional_number(result.nls.tau) << ",\n";
  out << "  \"tau_dft\": " << optional_number(result.dft.tau) << ",\n";
  out << "  \"Q_nls\": " << optional_number(result.nls.quality_factor) << ",\n";
  out << "  \"Q_dft\": " << optional_number(result.dft.quality_factor) << ",\n";
  out << "  \"A0_est\": " << json_number(result.noise.amplitude) << ",\n";
  out << "  \"sigma_est\": " << json_number(result.noise.sigma) << ",\n";
  out << "  \"plugin_crlb_var_f\": " << json_number(result.plugin_crlb_variance_f) << ",\n";
  out << "  \"plugin_crlb_std_f\": " << json_number(result.plugin_crlb_std_f) << ",\n";
  out << "  \"uncertainty_valid\": " << (result.uncertainty_valid ? "true" : "false") << "\n";
  out << "}\n";
  return out.str();
}

std::string to_json_notebook(const AnalyzerResult& result) {
  auto out = std::ostringstream{};
  out << std::setprecision(17);
  out << "{\n";
  out << "  \"filename\": " << json_string(result.filename) << ",\n";
  out << "  \"type\": " << json_string(result.file_type) << ",\n";
  out << "  \"t\": ";
  append_json_double_array(out, result.time);
  out << ",\n";
  out << "  \"data\": ";
  append_json_double_array(out, result.samples);
  out << ",\n";
  if (result.secondary_samples.empty()) {
    out << "  \"V2\": null,\n";
  } else {
    out << "  \"V2\": ";
    append_json_double_array(out, result.secondary_samples);
    out << ",\n";
  }
  out << "  \"t_crop\": ";
  append_json_double_array(out, result.cropped_time);
  out << ",\n";
  out << "  \"data_cropped\": ";
  append_json_double_array(out, result.cropped_samples);
  out << ",\n";
  out << "  \"fs\": " << json_number(result.sample_rate_hz) << ",\n";
  out << "  \"tau_seed\": " << json_number(result.tau_seed) << ",\n";
  out << "  \"tau_est\": " << json_number(result.tau_estimate) << ",\n";
  out << "  \"tau_model\": " << json_number(result.tau_model) << ",\n";
  out << "  \"f_nls\": " << json_number(result.nls.frequency_hz) << ",\n";
  out << "  \"f_dft\": " << json_number(result.dft.frequency_hz) << ",\n";
  out << "  \"tau_nls\": " << optional_number(result.nls.tau) << ",\n";
  out << "  \"tau_dft\": " << optional_number(result.dft.tau) << ",\n";
  out << "  \"Q_nls\": " << optional_number(result.nls.quality_factor) << ",\n";
  out << "  \"Q_dft\": " << optional_number(result.dft.quality_factor) << ",\n";
  out << "  \"nls_evaluations\": " << optional_size_t_json(result.nls.evaluations) << ",\n";
  out << "  \"dft_evaluations\": " << optional_size_t_json(result.dft.evaluations) << ",\n";
  const auto q_pre =
      std::isfinite(result.nls.frequency_hz) && std::isfinite(result.tau_estimate) && result.tau_estimate > 0.0
          ? std::numbers::pi * result.nls.frequency_hz * result.tau_estimate
          : std::numeric_limits<double>::quiet_NaN();
  out << "  \"Q_pre_crop\": " << json_number(q_pre) << ",\n";
  out << "  \"nls_success\": " << (result.nls.success ? "true" : "false") << ",\n";
  out << "  \"dft_success\": " << (result.dft.success ? "true" : "false") << ",\n";
  out << "  \"nls_used_fallback\": " << (result.nls.used_fallback ? "true" : "false") << ",\n";
  out << "  \"dft_used_fallback\": " << (result.dft.used_fallback ? "true" : "false") << ",\n";
  out << "  \"nls_message\": " << json_string(result.nls.message) << ",\n";
  out << "  \"dft_message\": " << json_string(result.dft.message) << ",\n";
  out << "  \"A0_est\": " << json_number(result.noise.amplitude) << ",\n";
  out << "  \"sigma_est\": " << json_number(result.noise.sigma) << ",\n";
  out << "  \"sigma_mle_est\": " << json_number(result.noise.sigma_mle) << ",\n";
  out << "  \"noise_dof\": " << result.noise.degrees_of_freedom << ",\n";
  out << "  \"noise_estimation_success\": " << (result.noise.success ? "true" : "false") << ",\n";
  out << "  \"noise_estimation_method\": " << json_string(result.noise.method) << ",\n";
  out << "  \"noise_estimation_message\": " << json_string(result.noise.message) << ",\n";
  out << "  \"plugin_crlb_var_f\": " << json_number(result.plugin_crlb_variance_f) << ",\n";
  out << "  \"plugin_crlb_std_f\": " << json_number(result.plugin_crlb_std_f) << ",\n";
  out << "  \"uncertainty_std_f\": " << json_number(result.plugin_crlb_std_f) << ",\n";
  out << "  \"crlb_std_f\": " << json_number(result.plugin_crlb_std_f) << ",\n";
  out << "  \"crlb_std_f_is_alias\": true,\n";
  out << "  \"uncertainty_valid\": " << (result.uncertainty_valid ? "true" : "false") << ",\n";
  out << "  \"N\": " << result.sample_count << ",\n";
  out << "  \"N_crop\": " << result.cropped_sample_count << ",\n";
  out << "  \"T\": " << json_number(result.observation_time) << ",\n";
  out << "  \"T_crop\": " << json_number(result.cropped_observation_time) << "\n";
  out << "}\n";
  return out.str();
}

} // namespace ringdown

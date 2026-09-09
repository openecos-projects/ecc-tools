#include "lef/LefExportFormat.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <ostream>
#include <stdexcept>

namespace eccdb::lef_export_detail {
namespace {

std::string decimal(int64_t value, int64_t denominator)
{
  if (denominator <= 0) {
    throw std::invalid_argument("LEF database units must be positive");
  }

  const bool negative = value < 0;
  const auto magnitude = negative ? -static_cast<__int128>(value) : static_cast<__int128>(value);
  const auto whole = magnitude / denominator;
  auto remainder = magnitude % denominator;

  std::string result;
  if (negative) {
    result.push_back('-');
  }
  result += std::to_string(static_cast<int64_t>(whole));
  if (remainder == 0) {
    return result;
  }

  result.push_back('.');
  // Legal LEF DATABASE MICRONS values yield a terminating decimal. Keep a
  // defensive bound so an invalid programmatic database cannot loop forever.
  for (uint32_t digit_count = 0; remainder != 0 && digit_count != 18u; ++digit_count) {
    remainder *= 10;
    result.push_back(static_cast<char>('0' + remainder / denominator));
    remainder %= denominator;
  }
  while (result.back() == '0') {
    result.pop_back();
  }
  return result;
}

}  // namespace

std::string distance(int64_t value, int32_t database_units_per_micron)
{
  return decimal(value, database_units_per_micron);
}

std::string area(int64_t value, int32_t database_units_per_micron)
{
  const auto units = static_cast<int64_t>(database_units_per_micron);
  if (units <= 0 || units > std::numeric_limits<int64_t>::max() / units) {
    throw std::invalid_argument("LEF database units cannot represent area");
  }
  return decimal(value, units * units);
}

std::string number(double value)
{
  if (!std::isfinite(value)) {
    throw std::invalid_argument("LEF cannot represent a non-finite number");
  }
  std::ostringstream stream;
  stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
  return stream.str();
}

void writeQuoted(std::ostream& output, std::string_view value)
{
  output.put('"');
  for (const char character : value) {
    if (character == '"' || character == '\\') {
      output.put('\\');
    }
    output.put(character);
  }
  output.put('"');
}

}  // namespace eccdb::lef_export_detail

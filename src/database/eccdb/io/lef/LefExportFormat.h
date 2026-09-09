#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>

namespace eccdb::lef_export_detail {

// All geometry in ECCDB is integer DBU. Formatting through
// this helper keeps LEF text stable and avoids silently rounding coordinates.
[[nodiscard]] std::string distance(int64_t value, int32_t database_units_per_micron);
[[nodiscard]] std::string area(int64_t value, int32_t database_units_per_micron);
[[nodiscard]] std::string number(double value);

void writeQuoted(std::ostream& output, std::string_view value);

}  // namespace eccdb::lef_export_detail

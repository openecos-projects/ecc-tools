#pragma once

#include <cstdint>
#include <string>

namespace eccdb {

// These values are not database objects. They are ordinary rows inside the
// variable-length vectors owned by their complete Rule component.
struct TechCutArraySpacingItem
{
  uint32_t array_cut_count = 0;
  int32_t spacing = 0;
};

struct TechCutOrthogonalSpacingTableItem
{
  int32_t within = 0;
  int32_t spacing = 0;
};

// One nested "TOCLASS class spacing1 [spacing2]" entry of LEF58_EOLSPACING.
struct TechCutLef58EolSpacingToClass
{
  std::string cutclass_name;
  int32_t cut_spacing1 = 0;
  int32_t cut_spacing2 = 0;
};

// One row/column intersection of a LEF58_SPACINGTABLE CUTCLASS matrix.
struct TechCutLef58SpacingTableCell
{
  int32_t cut_spacing1 = 0;
  int32_t cut_spacing2 = 0;
  bool has_cut_spacing1 = false;
  bool has_cut_spacing2 = false;
};

struct TechCutLef58SpacingTableClassPair
{
  std::string from;
  std::string to;
};

struct TechCutLef58SpacingTablePrlEntry
{
  std::string from;
  std::string to;
  int32_t prl = 0;
};

}  // namespace eccdb

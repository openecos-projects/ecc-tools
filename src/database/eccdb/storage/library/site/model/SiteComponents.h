#pragma once

#include <cstdint>
#include <string>

namespace eccdb {

enum class LibrarySiteClass : uint8_t
{
  kNone = 0,
  kCore,
  kPad,
  kCorner,
  kVirtual
};

// LEF 5.8 SITE subset represented by this component:
//   SITE name
//     CLASS {CORE | PAD | VIRTUAL} ;
//     [SYMMETRY {X | Y | R90} ... ;]
//     SIZE width BY height ;
//   END name
// Row orientation belongs to the design-side Row object. ROWPATTERN is not
// represented yet; kCorner and overlap preserve iDB extensions.
struct LibrarySite
{
  std::string name;
  int32_t width = -1;
  int32_t height = -1;
  bool overlap = false;
  LibrarySiteClass site_class = LibrarySiteClass::kNone;
  bool symmetry_x = false;
  bool symmetry_y = false;
  bool symmetry_r90 = false;
};

}  // namespace eccdb

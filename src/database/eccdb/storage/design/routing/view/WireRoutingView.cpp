#include "design/routing/view/WireRoutingView.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace eccdb {

DesignWirePoint DesignWirePointRange::operator[](std::size_t index) const
{
  if (index >= size()) {
    throw std::out_of_range("wire point index is out of range");
  }
  if (!_owned.empty()) {
    return _owned[index];
  }

  DesignWirePoint result{.position = _compact[index].position};
  if (!_extras.empty()) {
    const auto global_index = static_cast<uint64_t>(_global_begin) + index;
    const auto found = std::lower_bound(_extras.begin(), _extras.end(), global_index,
                                        [](const auto& entry, uint64_t point_index) {
                                          return entry.point_index < point_index;
                                        });
    if (found != _extras.end() && found->point_index == global_index) {
      result.flags = found->flags;
      result.extension = found->extension;
    }
  }
  return result;
}

DesignWireVia DesignWireViaRange::operator[](std::size_t index) const
{
  if (index >= size()) {
    throw std::out_of_range("wire VIA index is out of range");
  }
  if (!_owned.empty()) {
    return _owned[index];
  }

  const auto& record = _compact[index];
  DesignWireVia result{.point_index = record.point_index,
                       .orientation = static_cast<DesignOrientation>(record.meta.orientation),
                       .flags = record.meta.flags};
  if (record.meta.reference_kind == DesignRoutingViaReferenceKind::kTech) {
    if (record.reference > std::numeric_limits<uint32_t>::max()) {
      throw std::logic_error("compact technology VIA reference exceeds uint32_t");
    }
    result.tech_via = TechViaMasterId{static_cast<TechEntity>(static_cast<uint32_t>(record.reference))};
  } else if (record.meta.reference_kind == DesignRoutingViaReferenceKind::kDesign) {
    result.design_via = DesignViaId{static_cast<DesignEntity>(record.reference)};
  } else {
    throw std::logic_error("compact wire VIA reference kind is invalid");
  }

  if (!_extras.empty()) {
    const auto global_index = static_cast<uint64_t>(_global_begin) + index;
    const auto found = std::lower_bound(_extras.begin(), _extras.end(), global_index,
                                        [](const auto& entry, uint64_t via_index) {
                                          return entry.via_index < via_index;
                                        });
    if (found != _extras.end() && found->via_index == global_index) {
      result.top_mask = found->top_mask;
      result.cut_mask = found->cut_mask;
      result.bottom_mask = found->bottom_mask;
      result.rows = found->rows;
      result.columns = found->columns;
      result.step_x = found->step_x;
      result.step_y = found->step_y;
    }
  }
  return result;
}

}  // namespace eccdb

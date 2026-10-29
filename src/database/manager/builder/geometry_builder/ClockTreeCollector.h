#pragma once

#include "ClockTreeSnapshot.h"

#include <optional>
#include <string>

namespace idb {
class IdbDesign;
class IdbLayout;
}  // namespace idb

namespace ecc::geometry {

class GeometryStore;

struct ClockTreeCollectOptions
{
  ClockTreeStage stage = ClockTreeStage::kUnknown;
  ClockTreeAnalysisSource analysis_source = ClockTreeAnalysisSource::kNone;
  std::string timing_unavailable_reason = "timing analysis was not requested";
  std::string rc_corner = "";
  std::string liberty_corner = "";
  double temperature_c = 0.0;
};

std::optional<ClockTreeSnapshot> collect_clock_tree_snapshot(idb::IdbDesign& design, idb::IdbLayout& layout,
                                                             const GeometryStore& store,
                                                             const ClockTreeCollectOptions& options);

}  // namespace ecc::geometry

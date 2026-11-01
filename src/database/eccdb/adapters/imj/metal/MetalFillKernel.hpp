// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "MIModel.hpp"

namespace imj {

// One physical bounding box projected by the EccDB adapter. A single object
// can contribute to density, blockage, or both.
struct MetalFillOccupancy
{
  MIRect bounds;
  bool contributes_density = false;
  bool blocks_fill = false;
};

class MetalFillSource
{
 public:
  virtual ~MetalFillSource() = default;

  // Return every relevant physical object on mi_layer intersecting region.
  // The caller owns the returned snapshot and can freely sort or clip it.
  [[nodiscard]] virtual std::vector<MetalFillOccupancy> query(const MILayer& mi_layer, const MIRect& region) const = 0;

  // Must implement the legacy rule composition for this backend:
  // max(default/RANGE spacing, PRL spacing).
  [[nodiscard]] virtual int32_t spacing(const MILayer& mi_layer, int32_t wire_width, int32_t parallel_length) const = 0;
};

// EccDB-adapter-only numerical planner. Legacy MetalInserter retains its
// original iDB implementation and never includes or calls this planner.
// It preserves the existing valid-input fill algorithm: density is reported
// and updated but does not terminate creation based on min/max density.
class MetalFillKernel
{
 public:
  static void build(MIModel& mi_model, const MetalFillSource& source);

  [[nodiscard]] static double unionArea(const std::vector<MIRect>& rectangles);

 private:
  static void buildLayer(MIModel& mi_model, MILayer& mi_layer, const MetalFillSource& source);
  static void buildDensityWindows(MIModel& mi_model, MILayer& mi_layer, const MetalFillSource& source);
  static double metalArea(const MetalFillSource& source, const MILayer& mi_layer, const MIRect& region);
  static void buildLayerFill(MIModel& mi_model, MILayer& mi_layer, const MetalFillSource& source);
  [[nodiscard]] static std::vector<int32_t> trackCoordinates(const MILayer& mi_layer, const MIRect& region);
  [[nodiscard]] static int32_t trackStep(const MILayer& mi_layer, const MetalFillSource& source);
  [[nodiscard]] static int32_t firstTrackCoordinate(int32_t coordinate, int32_t track_start, int32_t track_pitch);
  [[nodiscard]] static std::vector<MIRect> fillRectangles(MIModel& mi_model,
                                                            const MILayer& mi_layer,
                                                            const MetalFillSource& source,
                                                            const MIRect& fill_region,
                                                            int32_t track_coordinate);
  [[nodiscard]] static std::vector<std::pair<int32_t, int32_t>> blockedIntervals(const MetalFillSource& source,
                                                                                   const MILayer& mi_layer,
                                                                                   const MIRect& fill_rect);
  [[nodiscard]] static MIRect fillRect(const MIModel& mi_model,
                                       const MILayer& mi_layer,
                                       const MIRect& fill_region,
                                       int32_t track_coordinate);
  [[nodiscard]] static std::vector<MIRect> uniformFillRectangles(const MIModel& mi_model,
                                                                   const MILayer& mi_layer,
                                                                   const MetalFillSource& source,
                                                                   const MIRect& free_fill_rect);
  [[nodiscard]] static int32_t fillSpacing(const MILayer& mi_layer,
                                            const MetalFillSource& source,
                                            int32_t parallel_length);
  [[nodiscard]] static int32_t requiredSpacing(const MILayer& mi_layer,
                                                const MetalFillSource& source,
                                                const MIRect& first,
                                                const MIRect& second);
  [[nodiscard]] static int32_t parallelLength(const MIRect& first, const MIRect& second);
  [[nodiscard]] static std::vector<int32_t> affectedDensityWindowIndices(const MIModel& mi_model,
                                                                           const MILayer& mi_layer,
                                                                           const MIRect& rectangle);
  static void addFillRect(MIModel& mi_model, MILayer& mi_layer, const MIRect& fill_rect);
  static void updateDensityWindows(MIModel& mi_model, MILayer& mi_layer, const MIRect& fill_rect);
  [[nodiscard]] static int32_t ceilDiv(int32_t dividend, int32_t divisor);
  [[nodiscard]] static int32_t alignUp(int32_t coordinate, int32_t grid);
  [[nodiscard]] static int32_t alignDown(int32_t coordinate, int32_t grid);
};

}  // namespace imj

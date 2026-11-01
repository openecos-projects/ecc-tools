// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eccdb {

// Shared LEF/DEF antenna value types.  These are payload types, not ECS
// components; owner components live with their Library/Tech/Design owners.
enum class AntennaMetric : uint8_t
{
  kUnknown = 0,
  kSize,
  kLengthFactor,
  kMetalArea,
  kMetalLength,
  kGateArea,
  kDiffArea,
  kPartialMetalArea,
  kPartialMetalSideArea,
  kPartialCutArea,
  kMaxAreaCar,
  kMaxSideAreaCar,
  kMaxCutCar,
  kAreaRatio,
  kDiffAreaRatio,
  kCumAreaRatio,
  kCumDiffAreaRatio,
  kDiffAreaRatioPwl,
  kCumDiffAreaRatioPwl,
  kAreaFactor,
  kSideAreaRatio,
  kDiffSideAreaRatio,
  kCumSideAreaRatio,
  kCumDiffSideAreaRatio,
  kDiffSideAreaRatioPwl,
  kCumDiffSideAreaRatioPwl,
  kSideAreaFactor,
  kCumRoutingPlusCut,
  kGatePlusDiff,
  kAreaMinusDiff,
  kCutAreaFactor,
  kAreaDiffReducePwl,
};

struct AntennaPwlPoint
{
  double x = 0.0;
  double y = 0.0;
};

struct AntennaPwl
{
  AntennaMetric metric = AntennaMetric::kUnknown;
  std::vector<AntennaPwlPoint> points;
};

namespace AntennaValueFlag {
constexpr uint32_t kDiffuseOnly = 1u << 0;
constexpr uint32_t kBoolean = 1u << 1;
}  // namespace AntennaValueFlag

struct AntennaValue
{
  AntennaMetric metric = AntennaMetric::kUnknown;
  uint32_t flags = 0;
  double value = 0.0;
};

struct AntennaLayerValue
{
  AntennaMetric metric = AntennaMetric::kUnknown;
  uint32_t flags = 0;
  std::string layer;
  double value = 0.0;
};

struct AntennaModel
{
  uint32_t flags = 0;
  std::string oxide;
  std::vector<AntennaValue> values;
  std::vector<AntennaLayerValue> layer_values;
  std::vector<AntennaPwl> pwls;
};

namespace AntennaModelFlag {
constexpr uint32_t kHasValues = 1u << 0;
constexpr uint32_t kHasLayerValues = 1u << 1;
constexpr uint32_t kHasPwls = 1u << 2;
constexpr uint32_t kHasBooleanValues = 1u << 3;
}  // namespace AntennaModelFlag

}  // namespace eccdb

// SPDX-License-Identifier: MulanPSL-2.0
//
// iCTS' native EccDB boundary.  This adapter deliberately keeps iCTS' local
// clock-tree model intact: it only projects physical EccDB objects in and
// commits the completed tree through EccDB services.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "CtsDesignSource.hh"

namespace eccdb {
class DatabaseContext;
}

namespace idb {
class LibCell;
}

namespace icts {
class Clock;
class Design;
class Pin;
}

namespace eccdb::adapter::icts {

using CtsLibertyCellLookup = std::function<idb::LibCell*(const std::string&)>;

class CtsEccdbAdapter final : public ::icts::CtsDesignSource
{
 public:
  explicit CtsEccdbAdapter(DatabaseContext& context, CtsLibertyCellLookup liberty_cell_lookup = {}, std::string sdc_path = {});

  CtsEccdbAdapter(const CtsEccdbAdapter&) = delete;
  CtsEccdbAdapter(CtsEccdbAdapter&&) = delete;
  auto operator=(const CtsEccdbAdapter&) -> CtsEccdbAdapter& = delete;
  auto operator=(CtsEccdbAdapter&&) -> CtsEccdbAdapter& = delete;
  ~CtsEccdbAdapter() = default;

  void setLibertyCellLookup(CtsLibertyCellLookup liberty_cell_lookup);

  [[nodiscard]] auto isDesignReady() const noexcept -> bool override;
  [[nodiscard]] auto isLayoutReady() const noexcept -> bool override;
  [[nodiscard]] auto readClockData() const -> ::icts::SdcClockData override;
  [[nodiscard]] auto findLibertyCell(const std::string& cell_master) const -> idb::LibCell* override;
  [[nodiscard]] auto queryDbUnit() const -> std::optional<int32_t> override;
  [[nodiscard]] auto withinCore(int32_t point_x, int32_t point_y) const -> std::optional<bool> override;
  [[nodiscard]] auto queryInstGeometry(const std::string& inst_name) const -> std::optional<::icts::WrapperCellGeometry> override;
  [[nodiscard]] auto collectLogicCellGeometries() const -> std::vector<::icts::WrapperCellGeometry> override;
  [[nodiscard]] auto queryCellHeightUm(const std::string& cell_master) const -> std::optional<double> override;
  [[nodiscard]] auto queryCellAreaUm2(const std::string& cell_master) const -> std::optional<double> override;
  [[nodiscard]] std::optional<double> querySupplyVoltage() const;
  [[nodiscard]] auto queryLibertyUnits() const -> std::optional<::icts::SdcUnits> override;
  [[nodiscard]] auto queryWireResistance(int routing_layer, double length_um, std::optional<double> wire_width_um) const
      -> std::optional<double> override;
  [[nodiscard]] auto queryWireCapacitanceProfile(int routing_layer, double length_um, std::optional<double> wire_width_um) const
      -> std::optional<::icts::Wrapper::WireCapacitanceProfile> override;

  [[nodiscard]] auto traceSdcClocks(const ::icts::SdcClockTraceInput& input) const -> ::icts::ClockTraceBuild override;
  // Build the iCTS/FastSTA input graph directly from the borrowed EccDB.
  // The graph is a derived view; EccDB remains the source of truth.  The
  // optional anchor is reserved for callers that need an explicit signal
  // routing layer; an empty value selects the first valid routing layer.
  [[nodiscard]] auto collectTimingGraph(std::string_view routing_anchor_name, std::optional<unsigned> routing_anchor_level) const
      -> ::icts::WrapperTimingGraph override;
  auto readTraceClockTargets(::icts::Design& design, const std::vector<::icts::ClockTraceClockTarget>& clock_targets) -> bool override;
  [[nodiscard]] auto writeClocks(::icts::Design& design, const std::vector<::icts::Clock*>& clocks) -> ::icts::WrapperWriteSummary override;

 private:
  DatabaseContext* _context = nullptr;
  CtsLibertyCellLookup _liberty_cell_lookup;
  std::string _sdc_path;
};

}  // namespace eccdb::adapter::icts

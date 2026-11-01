// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include "Wrapper.hh"
#include "adapter/sdc/SDCClockReader.hh"

namespace icts {

// A backend supplies the existing CTS models and query results. It owns no
// CTS algorithm state; its database must outlive the active CTS session.
// Source configuration and Liberty data stay fixed during a session. Queries
// return validated values in the units documented by Wrapper, or nullopt.
class CtsDesignSource
{
 public:
  virtual ~CtsDesignSource() = default;

  virtual auto isDesignReady() const noexcept -> bool = 0;
  virtual auto isLayoutReady() const noexcept -> bool = 0;
  virtual auto readClockData() const -> SdcClockData = 0;
  virtual auto findLibertyCell(const std::string& master) const -> idb::LibCell* = 0;
  virtual auto queryDbUnit() const -> std::optional<int32_t> = 0;
  virtual auto withinCore(int32_t x, int32_t y) const -> std::optional<bool> = 0;
  virtual auto queryInstGeometry(const std::string& name) const -> std::optional<WrapperCellGeometry> = 0;
  virtual auto collectLogicCellGeometries() const -> std::vector<WrapperCellGeometry> = 0;
  virtual auto queryCellHeightUm(const std::string& master) const -> std::optional<double> = 0;
  virtual auto queryCellAreaUm2(const std::string& master) const -> std::optional<double> = 0;
  virtual auto queryLibertyUnits() const -> std::optional<SdcUnits> = 0;
  virtual auto queryWireResistance(int layer, double length_um, std::optional<double> width_um) const -> std::optional<double> = 0;
  virtual auto queryWireCapacitanceProfile(int layer, double length_um, std::optional<double> width_um) const
      -> std::optional<Wrapper::WireCapacitanceProfile> = 0;
  virtual auto traceSdcClocks(const SdcClockTraceInput& input) const -> ClockTraceBuild = 0;
  virtual auto collectTimingGraph(std::string_view routing_anchor_name, std::optional<unsigned> routing_layer) const -> WrapperTimingGraph = 0;
  virtual auto readTraceClockTargets(Design& design, const std::vector<ClockTraceClockTarget>& targets) -> bool = 0;
  virtual auto writeClocks(Design& design, const std::vector<Clock*>& clocks) -> WrapperWriteSummary = 0;
};

}  // namespace icts

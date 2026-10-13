// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// ***************************************************************************************
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
#include <vector>

#include "adapter/sdc/SDCClockReader.hh"

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

struct CtsEccdbCellGeometry
{
  std::string name;
  std::string cell_master;
  int32_t origin_x = -1;
  int32_t origin_y = -1;
  int32_t width_dbu = 0;
  int32_t height_dbu = 0;
};

struct CtsEccdbWireCapacitanceProfile
{
  double area_cap_pf = 0.0;
  double edge_cap_pf = 0.0;
  double ground_cap_pf = 0.0;
  double coupling_cap_pf = 0.0;
  double timing_coupling_factor = 0.0;
  double total_cap_pf = 0.0;
  double timing_effective_cap_pf = 0.0;
};

struct CtsEccdbWriteSummary
{
  bool success = false;
  std::string failed_clock;
  std::string failed_net;
  std::size_t inserted_inst_count = 0U;
  std::size_t inserted_net_count = 0U;
  std::string reason;
};

class CtsEccdbAdapter
{
 public:
  explicit CtsEccdbAdapter(DatabaseContext& context, CtsLibertyCellLookup liberty_cell_lookup = {});

  CtsEccdbAdapter(const CtsEccdbAdapter&) = delete;
  CtsEccdbAdapter(CtsEccdbAdapter&&) = delete;
  auto operator=(const CtsEccdbAdapter&) -> CtsEccdbAdapter& = delete;
  auto operator=(CtsEccdbAdapter&&) -> CtsEccdbAdapter& = delete;
  ~CtsEccdbAdapter() = default;

  void setLibertyCellLookup(CtsLibertyCellLookup liberty_cell_lookup);

  [[nodiscard]] bool isDesignReady() const noexcept;
  [[nodiscard]] bool isLayoutReady() const noexcept;
  [[nodiscard]] std::optional<int32_t> queryDbUnit() const;
  [[nodiscard]] std::optional<bool> withinCore(int32_t point_x, int32_t point_y) const;
  [[nodiscard]] std::optional<CtsEccdbCellGeometry> queryInstGeometry(const std::string& inst_name) const;
  [[nodiscard]] std::vector<CtsEccdbCellGeometry> collectLogicCellGeometries() const;
  [[nodiscard]] std::optional<double> queryCellHeightUm(const std::string& cell_master) const;
  [[nodiscard]] std::optional<double> queryCellAreaUm2(const std::string& cell_master) const;
  [[nodiscard]] std::optional<double> queryWireResistance(int routing_layer, double length_um,
                                                            std::optional<double> wire_width_um) const;
  [[nodiscard]] std::optional<CtsEccdbWireCapacitanceProfile> queryWireCapacitanceProfile(
      int routing_layer, double length_um, std::optional<double> wire_width_um) const;

  [[nodiscard]] ::icts::ClockTraceBuild traceSdcClocks(const ::icts::SdcClockTraceInput& input) const;
  bool readTraceClockTargets(::icts::Design& design, const std::vector<::icts::ClockTraceClockTarget>& clock_targets);
  [[nodiscard]] CtsEccdbWriteSummary writeClocks(::icts::Design& design, const std::vector<::icts::Clock*>& clocks);

 private:
  DatabaseContext* _context = nullptr;
  CtsLibertyCellLookup _liberty_cell_lookup;
};

}  // namespace eccdb::adapter::icts

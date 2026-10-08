// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include "NativeDatabaseSnapshot.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "design/DesignStore.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/netlist/storage/DesignNetlistStorage.h"
#include "odb/db.h"
#include "service/geometry/PhysicalDesignService.h"

namespace drt::io {
namespace {

std::string quotedName(const std::string& value)
{
  std::ostringstream output;
  output << std::quoted(value);
  return output.str();
}

std::string placementStatus(eccdb::DesignPlacementStatus status)
{
  switch (status) {
    case eccdb::DesignPlacementStatus::kUnplaced:
      return "UNPLACED";
    case eccdb::DesignPlacementStatus::kPlaced:
      return "PLACED";
    case eccdb::DesignPlacementStatus::kFixed:
      return "FIXED";
    case eccdb::DesignPlacementStatus::kCover:
      return "COVER";
  }
  throw std::runtime_error("unsupported EccDB placement status");
}

std::string placementStatus(odb::dbPlacementStatus status)
{
  switch (status.getValue()) {
    case odb::dbPlacementStatus::NONE:
    case odb::dbPlacementStatus::UNPLACED:
      return "UNPLACED";
    case odb::dbPlacementStatus::PLACED:
      return "PLACED";
    // OpenDB's DEF reader represents DEF FIXED as FIRM.
    case odb::dbPlacementStatus::FIRM:
      return "FIXED";
    case odb::dbPlacementStatus::COVER:
      return "COVER";
    case odb::dbPlacementStatus::SUGGESTED:
    case odb::dbPlacementStatus::LOCKED:
      throw std::runtime_error("unsupported OpenDB placement status");
  }
  throw std::runtime_error("unsupported OpenDB placement status");
}

void writeRecords(std::vector<std::string> records, const std::filesystem::path& path)
{
  std::sort(records.begin(), records.end());
  std::ofstream output(path);
  for (const auto& record : records) {
    output << record << '\n';
  }
  if (!output) {
    throw std::runtime_error("cannot write native placement snapshot");
  }
}

}  // namespace

void writeNativePlacementSnapshot(const eccdb::PhysicalDesignService& physical,
                                  const std::filesystem::path& path)
{
  std::vector<std::string> records;
  const auto& netlist = physical.design().netlistStorage();
  for (const auto id : netlist.instances()) {
    const auto& instance = netlist.instance(id);
    records.push_back("instance " + quotedName(instance.name) + " " + placementStatus(instance.placement_status));
  }
  for (const auto id : netlist.ioPins()) {
    const auto& pin = netlist.ioPin(id);
    // Port order is intentionally ignored. Use this status multiset with the
    // geometry snapshot to identify ports; it is not a full DEF metadata audit.
    for (const auto bpin : pin.pins) {
      const auto& port = netlist.blockPin(bpin);
      records.push_back("io " + quotedName(pin.name) + " port " + placementStatus(port.placement_status));
    }
  }
  writeRecords(std::move(records), path);
}

void writeNativePlacementSnapshot(odb::dbBlock& block, const std::filesystem::path& path)
{
  std::vector<std::string> records;
  for (auto* instance : block.getInsts()) {
    records.push_back("instance " + quotedName(instance->getName()) + " " + placementStatus(instance->getPlacementStatus()));
  }
  for (auto* term : block.getBTerms()) {
    // Port order is intentionally ignored. Use this status multiset with the
    // geometry snapshot to identify ports; it is not a full DEF metadata audit.
    for (auto* pin : term->getBPins()) {
      records.push_back("io " + quotedName(term->getName()) + " port " + placementStatus(pin->getPlacementStatus()));
    }
  }
  writeRecords(std::move(records), path);
}

}  // namespace drt::io

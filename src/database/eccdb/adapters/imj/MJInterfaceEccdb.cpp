// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "MJInterfaceEccdb.h"

#include "MJInterface.hpp"

#include "AntennaEccdb.h"
#include "MetalFillEccdb.h"

#include <algorithm>
#include <any>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/box.hpp>
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/index/predicates.hpp>
#include <boost/geometry/index/rtree.hpp>
#include <boost/iterator/function_output_iterator.hpp>

#include "context/DatabaseContext.h"
#include "design/DesignStore.h"
#include "design/floorplan/model/FloorplanComponents.h"
#include "design/global/model/DesignGlobalComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "library/LibraryStore.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/site/model/SiteComponents.h"
#include "service/instance/InstanceService.h"
#include "tech/TechStore.h"

namespace imj {
namespace {

struct Box
{
  int64_t ll_x = 0;
  int64_t ll_y = 0;
  int64_t ur_x = 0;
  int64_t ur_y = 0;
};

using PlacementIndexPoint = boost::geometry::model::point<int64_t, 2, boost::geometry::cs::cartesian>;
using PlacementIndexBox = boost::geometry::model::box<PlacementIndexPoint>;
using PlacementIndexValue = std::pair<PlacementIndexBox, uint32_t>;
using PlacementIndex = boost::geometry::index::rtree<PlacementIndexValue, boost::geometry::index::quadratic<16>>;

PlacementIndexBox indexBox(const Box& bounds) noexcept
{
  return PlacementIndexBox{PlacementIndexPoint{bounds.ll_x, bounds.ll_y}, PlacementIndexPoint{bounds.ur_x, bounds.ur_y}};
}

struct FillerMaster
{
  eccdb::LibraryCellMasterId id;
  std::string name;
  int32_t site_count = 0;
};

struct FillerPlacement
{
  eccdb::LibraryCellMasterId master;
  std::string name;
  eccdb::Point origin;
  eccdb::DesignOrientation orientation = eccdb::DesignOrientation::kN;
};

std::string configString(const std::map<std::string, std::any>& config, std::string_view key)
{
  const auto found = config.find(std::string{key});
  if (found == config.end()) {
    return {};
  }
  if (const auto* value = std::any_cast<std::string>(&found->second); value != nullptr) {
    return *value;
  }
  throw std::invalid_argument("iMJ native option '" + std::string{key} + "' must be a string");
}

int32_t configPositiveInt(const std::map<std::string, std::any>& config, std::string_view key, int32_t default_value)
{
  const auto found = config.find(std::string{key});
  if (found == config.end()) {
    return default_value;
  }
  if (const auto* value = std::any_cast<int32_t>(&found->second); value != nullptr) {
    if (*value <= 0) {
      throw std::invalid_argument("iMJ native option '" + std::string{key} + "' must be positive");
    }
    return *value;
  }
  throw std::invalid_argument("iMJ native option '" + std::string{key} + "' must be an integer");
}

std::vector<std::string> splitNames(std::string names)
{
  for (char& character : names) {
    if (std::string_view{" \t\r\n,;{}[]"}.find(character) != std::string_view::npos) {
      character = ' ';
    }
  }

  std::vector<std::string> result;
  std::size_t begin = 0;
  while (begin < names.size()) {
    begin = names.find_first_not_of(' ', begin);
    if (begin == std::string::npos) {
      break;
    }
    const auto end = names.find(' ', begin);
    result.push_back(names.substr(begin, end - begin));
    begin = end;
  }
  return result;
}

bool isCoreFiller(const eccdb::LibraryStore& library, const eccdb::LibraryCellMaster& master)
{
  if (master.type < eccdb::LibraryCellMasterType::kCore || master.width == 0u || master.height == 0u) {
    return false;
  }
  if (master.terms.empty()) {
    return master.core_filler || master.type == eccdb::LibraryCellMasterType::kCoreSpacer;
  }
  // Some PDKs declare fillers as CLASS CORE with PG terminals rather than
  // CLASS CORE SPACER. Use the same electrical criterion as iMJ's iDB source.
  return std::all_of(master.terms.begin(), master.terms.end(), [&](const auto term_id) {
    const auto use = library.masterTermStorage().masterTerm(term_id).use;
    return use == eccdb::LibraryMasterTermUse::kPower || use == eccdb::LibraryMasterTermUse::kGround;
  });
}

bool isPlaced(eccdb::DesignPlacementStatus status) noexcept
{
  return status != eccdb::DesignPlacementStatus::kUnplaced;
}

bool blocksPostPlacementFiller(const eccdb::DesignBlockage& blockage) noexcept
{
  if (blockage.kind != eccdb::DesignBlockageKind::kPlacement) {
    return false;
  }

  // DEF defines SOFT and PARTIAL placement blockages for initial placement.
  // Filler insertion is a later placement operation, so only a hard placement
  // blockage removes row sites here. PUSHDOWN and COMPONENT only affect where
  // the absolute blockage geometry originates; both retain hard semantics.
  constexpr uint32_t kInitialPlacementOnly = eccdb::DesignBlockageFlag::kSoft | eccdb::DesignBlockageFlag::kHasPartial;
  return (blockage.flags & kInitialPlacementOnly) == 0u;
}

Box instanceBounds(const eccdb::DesignInstance& instance, const eccdb::LibraryCellMaster& master)
{
  const bool rotated = eccdb::isQuarterTurnOrientation(instance.orientation);
  const int64_t width = rotated ? static_cast<int64_t>(master.height) : static_cast<int64_t>(master.width);
  const int64_t height = rotated ? static_cast<int64_t>(master.width) : static_cast<int64_t>(master.height);
  return {.ll_x = instance.origin.x, .ll_y = instance.origin.y, .ur_x = static_cast<int64_t>(instance.origin.x) + width,
          .ur_y = static_cast<int64_t>(instance.origin.y) + height};
}

Box rectangleBounds(eccdb::Rect rectangle) noexcept
{
  rectangle = rectangle.normalized();
  return {.ll_x = rectangle.ll_x, .ll_y = rectangle.ll_y, .ur_x = rectangle.ur_x, .ur_y = rectangle.ur_y};
}

void markBlockedSites(std::vector<bool>& available,
                      int64_t row_x,
                      int64_t row_y,
                      int64_t site_width,
                      int64_t site_height,
                      const Box& blockage)
{
  const int64_t row_ur_x = row_x + site_width * static_cast<int64_t>(available.size());
  const int64_t row_ur_y = row_y + site_height;
  if (blockage.ur_x <= row_x || blockage.ll_x >= row_ur_x || blockage.ur_y <= row_y || blockage.ll_y >= row_ur_y) {
    return;
  }

  const int64_t clipped_ll_x = std::max(blockage.ll_x, row_x);
  const int64_t clipped_ur_x = std::min(blockage.ur_x, row_ur_x);
  const auto begin = static_cast<std::size_t>((clipped_ll_x - row_x) / site_width);
  const auto end = static_cast<std::size_t>((clipped_ur_x - row_x + site_width - 1) / site_width);
  for (std::size_t index = begin; index < std::min(end, available.size()); ++index) {
    available[index] = false;
  }
}

std::vector<FillerMaster> collectFillerMasters(const eccdb::LibraryStore& library,
                                                int32_t site_width,
                                                const std::vector<std::string>& requested)
{
  std::vector<FillerMaster> result;
  const auto add_master = [&](eccdb::LibraryCellMasterId id) {
    const auto& master = library.cellMasterStorage().cellMaster(id);
    if (!isCoreFiller(library, master)) {
      throw std::invalid_argument("iMJ native filler master is not a core filler: " + master.name);
    }
    if (master.width == 0u || master.width > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
        || static_cast<int32_t>(master.width) % site_width != 0) {
      throw std::invalid_argument("iMJ native filler master width is not aligned to the row site: " + master.name);
    }
    result.push_back({.id = id, .name = master.name, .site_count = static_cast<int32_t>(master.width) / site_width});
  };

  if (requested.empty()) {
    for (const auto id : library.cellMasterStorage().cellMasters()) {
      if (isCoreFiller(library, library.cellMasterStorage().cellMaster(id))) {
        add_master(id);
      }
    }
  } else {
    for (const auto& name : requested) {
      const auto id = library.cellMasterStorage().findCellMaster(name);
      if (!id) {
        throw std::invalid_argument("iMJ native filler master does not exist: " + name);
      }
      add_master(id);
    }
  }
  std::sort(result.begin(), result.end(), [](const FillerMaster& lhs, const FillerMaster& rhs) {
    if (lhs.site_count != rhs.site_count) {
      return lhs.site_count > rhs.site_count;
    }
    return lhs.name < rhs.name;
  });
  if (result.empty()) {
    throw std::invalid_argument("iMJ native filler insertion requires at least one core filler master");
  }
  return result;
}

std::string nextFillerName(std::set<std::string, std::less<>>& occupied, const std::string& master_name, uint32_t& sequence)
{
  const auto base = master_name + "_" + std::to_string(sequence++);
  if (occupied.insert(base).second) {
    return base;
  }
  for (uint64_t suffix = 0;; ++suffix) {
    auto candidate = base + std::to_string(suffix);
    if (occupied.insert(candidate).second) return candidate;
  }
}

}  // namespace

namespace detail {

void insertFillerEccdb(eccdb::DatabaseContext& context, MJConfigMap& config_map)
{
  if (context.library() == nullptr) {
    throw std::logic_error("iMJ native filler insertion requires a complete DatabaseContext");
  }
  auto& design = context.design();
  const auto& library = *context.library();
  const auto min_filler_sites = configPositiveInt(config_map, "-min_filler_width", 1);
  const auto requested = splitNames(configString(config_map, "-filler"));
  const auto& floorplan = design.floorplanStorage();
  auto rows = floorplan.rows();
  if (rows.empty()) {
    throw std::invalid_argument("iMJ native filler insertion requires placement rows");
  }

  std::vector<FillerPlacement> placements;
  // Filler numbering follows physical row order and is independent of ECS
  // storage traversal. Each master has its own insertion ordinal.
  std::sort(rows.begin(), rows.end(), [&](const auto lhs_id, const auto rhs_id) {
    const auto& lhs = floorplan.row(lhs_id);
    const auto& rhs = floorplan.row(rhs_id);
    return std::tie(lhs.origin.y, lhs.origin.x, lhs.name) < std::tie(rhs.origin.y, rhs.origin.x, rhs.name);
  });
  std::map<std::string, uint32_t> sequences;
  std::set<std::string, std::less<>> occupied;
  const auto& netlist = design.netlistStorage();
  std::vector<Box> placement_obstacles;
  netlist.forEachInstance([&](eccdb::DesignInstanceId,
                              const eccdb::DesignInstance& instance,
                              std::span<const eccdb::DesignInstanceTermId>) {
    occupied.insert(instance.name);
    if (!isPlaced(instance.placement_status)) {
      return;
    }
    const auto& master = library.cellMasterStorage().cellMaster(instance.master);
    placement_obstacles.push_back(instanceBounds(instance, master));
  });
  const auto& constraints = design.constraintStorage();
  for (const auto blockage_id : constraints.blockages()) {
    const auto& blockage = constraints.blockage(blockage_id);
    if (!blocksPostPlacementFiller(blockage)) {
      continue;
    }
    for (const auto rectangle_id : blockage.rectangles) {
      placement_obstacles.push_back(rectangleBounds(constraints.blockageRectangle(rectangle_id)));
    }
  }
  std::vector<PlacementIndexValue> placement_index_values;
  placement_index_values.reserve(placement_obstacles.size());
  for (uint32_t index = 0; index < placement_obstacles.size(); ++index) {
    placement_index_values.emplace_back(indexBox(placement_obstacles[index]), index);
  }
  const PlacementIndex placement_index{placement_index_values.begin(), placement_index_values.end()};

  std::unordered_map<int32_t, std::vector<FillerMaster>> masters_by_site_width;
  for (const auto row_id : rows) {
    const auto& row = floorplan.row(row_id);
    const auto& site = library.siteStorage().site(row.site);
    const auto site_width = row.step_x > 0 ? row.step_x : site.width;
    if (site_width <= 0 || site.height <= 0 || row.repeat_count_x == 0u) {
      throw std::invalid_argument("iMJ native filler insertion found an invalid row site");
    }
    auto masters = masters_by_site_width.find(site_width);
    if (masters == masters_by_site_width.end()) {
      masters = masters_by_site_width.emplace(site_width, collectFillerMasters(library, site_width, requested)).first;
    }
    const auto repeat_y = std::max<uint32_t>(row.repeat_count_y, 1u);
    for (uint32_t y_index = 0; y_index < repeat_y; ++y_index) {
      const int64_t row_y = static_cast<int64_t>(row.origin.y) + static_cast<int64_t>(y_index) * row.step_y;
      std::vector<bool> available(row.repeat_count_x, true);
      const Box row_bounds{.ll_x = row.origin.x,
                           .ll_y = row_y,
                           .ur_x = static_cast<int64_t>(row.origin.x) + static_cast<int64_t>(site_width) * row.repeat_count_x,
                           .ur_y = row_y + site.height};
      // The R-tree may return a box merely touching this row. The canonical
      // half-open boundary rule remains in markBlockedSites, so selection is
      // an acceleration only and does not alter filler placement semantics.
      const auto mark_candidate = [&](const PlacementIndexValue& candidate) {
        markBlockedSites(available, row.origin.x, row_y, site_width, site.height, placement_obstacles[candidate.second]);
      };
      placement_index.query(boost::geometry::index::intersects(indexBox(row_bounds)), boost::make_function_output_iterator(mark_candidate));

      std::size_t begin = 0;
      while (begin < available.size()) {
        while (begin < available.size() && !available[begin]) {
          ++begin;
        }
        std::size_t end = begin;
        while (end < available.size() && available[end]) {
          ++end;
        }
        std::size_t cursor = begin;
        while (end > cursor && static_cast<int64_t>(end - cursor) >= min_filler_sites) {
          const int32_t free_sites = static_cast<int32_t>(end - cursor);
          const FillerMaster* chosen = nullptr;
          for (const auto& master : masters->second) {
            const int32_t remaining = free_sites - master.site_count;
            if (remaining >= 0 && (remaining == 0 || remaining >= min_filler_sites)) {
              chosen = &master;
              break;
            }
          }
          if (chosen == nullptr) {
            break;
          }
          placements.push_back({.master = chosen->id,
                                .name = nextFillerName(occupied, chosen->name, sequences[chosen->name]),
                                .origin = {.x = static_cast<int32_t>(static_cast<int64_t>(row.origin.x) + cursor * site_width),
                                           .y = static_cast<int32_t>(row_y)},
                                .orientation = row.orientation});
          cursor += static_cast<std::size_t>(chosen->site_count);
        }
        begin = end;
      }
    }
  }

  std::vector<eccdb::DesignInstanceId> created;
  created.reserve(placements.size());
  eccdb::InstanceService instances{context};
  try {
    for (const auto& placement : placements) {
      created.push_back(instances.create(eccdb::DesignInstance{.name = placement.name,
                                                                 .master = placement.master,
                                                                 .origin = placement.origin,
                                                                 .orientation = placement.orientation,
                                                                 .placement_status = eccdb::DesignPlacementStatus::kPlaced,
                                                                 .source = eccdb::DesignInstanceSource::kDist}));
    }
  } catch (...) {
    for (auto it = created.rbegin(); it != created.rend(); ++it) {
      static_cast<void>(instances.destroy(*it));
    }
    throw;
  }
}

void insertMetalEccdb(eccdb::DatabaseContext& context, MJConfigMap& config_map)
{
  imj::insertMetalEccdb(context, config_map);
}

NativeAntennaSummary checkAntennaEccdb(eccdb::DatabaseContext& context, MJConfigMap& config_map)
{
  return imj::checkAntennaEccdb(context, config_map);
}

}  // namespace detail

}  // namespace imj

namespace eccdb::adapters {

void bindMjSource(imj::MJInterface& interface, DatabaseContext* context)
{
  if (context == nullptr) {
    interface.clearDesignOperations();
    return;
  }

  interface.setDesignOperations({
      .insert_filler = [context](imj::MJConfigMap& config) { imj::detail::insertFillerEccdb(*context, config); },
      .insert_metal = [context](imj::MJConfigMap& config) { imj::detail::insertMetalEccdb(*context, config); },
      .check_antenna = [context](imj::MJConfigMap& config) { return imj::detail::checkAntennaEccdb(*context, config); },
  });
}

}  // namespace eccdb::adapters

// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
//
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "IOCellPlacer.hpp"

#include <cctype>
#include <numeric>
#include <tuple>

#include "DataManager.hpp"
#include "IdbOrientTransform.h"
#include "Logger.hpp"

namespace ifp {

namespace {

bool matchPattern(const std::string& value, const std::string& pattern)
{
  const size_t star = pattern.find('*');
  if (star == std::string::npos) {
    return value == pattern;
  }
  const std::string prefix = pattern.substr(0, star);
  const std::string suffix = pattern.substr(star + 1);
  return value.size() >= prefix.size() + suffix.size() && value.compare(0, prefix.size(), prefix) == 0
         && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool hasMatchingNet(const Database& database, const std::vector<std::string>& patterns)
{
  for (const Net& net : database.get_net_list()) {
    for (const std::string& pattern : patterns) {
      if (matchPattern(net.get_name(), pattern)) {
        return true;
      }
    }
  }
  return false;
}

std::string resolvePowerNet(const Database& database, const std::vector<std::string>& patterns)
{
  std::set<std::string> matches;
  for (const auto& net : database.get_net_list()) {
    for (const auto& pattern : patterns) {
      if (matchPattern(net.get_name(), pattern)) {
        matches.insert(net.get_name());
      }
    }
  }
  if (matches.size() != 1) {
    FPLOG.error(Loc::current(), "IO power definition must match exactly one net; matched ", matches.size(), ".");
  }
  return *matches.begin();
}

std::string getPowerPrefix(const std::string& pin_name)
{
  std::string prefix = pin_name;
  while (!prefix.empty() && std::isdigit(static_cast<unsigned char>(prefix.back()))) {
    prefix.pop_back();
  }
  std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return prefix;
}

}  // namespace

bool IOCellPlacer::isPowerMaster(const IOCellDefinition& definition, const std::string& master)
{
  for (const IOCellPowerMember& member : definition.power_members) {
    if (member.master_name == master) {
      return true;
    }
  }
  return false;
}

bool IOCellPlacer::isSignalMaster(const IOCellDefinition& definition, const std::string& master)
{
  return std::find(definition.signal_master_names.begin(), definition.signal_master_names.end(), master) != definition.signal_master_names.end();
}

bool IOCellPlacer::place()
{
  IOCellDefinition definition;
  if (!loadDefinition(definition) || !validateDefinition(definition)) {
    return false;
  }

  resetExistingPlacement(definition);
  bool placed = false;
  if (_config.io_cell_mode == PlacementMode::kFile) {
    placed = placeFromFile(definition);
  } else {
    placed = placeAuto(definition);
  }
  if (!placed || !normalizePowerInstanceNames(definition)) {
    return false;
  }
  return bindIOPins(definition) && updateIOPinPorts();
}

bool IOCellPlacer::loadDefinition(IOCellDefinition& definition)
{
  const std::string& file_path = _config.io_cell_definition_file;
  if (file_path.empty()) {
    FPLOG.error(Loc::current(), "io_cell.definition_file_path must not be empty.");
    return false;
  }

  std::ifstream stream(file_path);
  if (!stream.is_open()) {
    FPLOG.error(Loc::current(), "Failed to open IO Cell definition file '", file_path, "'.");
    return false;
  }

  nlohmann::json root;
  try {
    stream >> root;
  } catch (const std::exception& e) {
    FPLOG.error(Loc::current(), "Failed to parse IO Cell definition file '", file_path, "': ", e.what());
    return false;
  }

  definition = IOCellDefinition();
  nlohmann::json corner = root.value("corner", nlohmann::json::object());
  definition.corner_master_name = corner.value("master", "");
  for (const nlohmann::json& master : root.value("signal_masters", nlohmann::json::array())) {
    definition.signal_master_names.push_back(master.get<std::string>());
  }
  auto parse_power_member = [&](const nlohmann::json& member_json) {
    IOCellPowerMember member;
    member.master_name = member_json.value("master", "");
    member.pin_name = member_json.value("pin", "");
    if (member_json.contains("net_pattern")) {
      if (member_json["net_pattern"].is_array()) {
        for (const nlohmann::json& pattern : member_json["net_pattern"]) {
          member.net_patterns.push_back(pattern.get<std::string>());
        }
      } else {
        member.net_patterns.push_back(member_json["net_pattern"].get<std::string>());
      }
    }
    if (member.master_name.empty()) {
      FPLOG.error(Loc::current(), "IO Cell power member is missing 'master'.");
      return false;
    }
    definition.power_members.push_back(member);
    return true;
  };

  if (root.contains("power_members")) {
    for (const nlohmann::json& member_json : root["power_members"]) {
      if (!parse_power_member(member_json)) {
        return false;
      }
    }
  } else {
    // Backward compatibility: merge all members from the old power_groups schema.
    for (const nlohmann::json& group_json : root.value("power_groups", nlohmann::json::array())) {
      for (const nlohmann::json& member_json : group_json.value("members", nlohmann::json::array())) {
        if (!parse_power_member(member_json)) {
          return false;
        }
      }
    }
  }

  for (const nlohmann::json& filler_json : root.value("filler_masters", nlohmann::json::array())) {
    IOCellFillerMaster filler;
    filler.master_name = filler_json.value("master", "");
    filler.width_micron = filler_json.value("width_micron", 0.0);
    if (filler.master_name.empty() || filler.width_micron <= 0.0) {
      FPLOG.error(Loc::current(), "Invalid filler master in IO Cell definition file.");
      return false;
    }
    definition.filler_masters.push_back(filler);
  }

  std::sort(definition.signal_master_names.begin(), definition.signal_master_names.end());
  definition.signal_master_names.erase(std::unique(definition.signal_master_names.begin(), definition.signal_master_names.end()),
                                       definition.signal_master_names.end());
  if (definition.corner_master_name.empty()) {
    FPLOG.error(Loc::current(), "IO Cell definition does not specify corner.master.");
    return false;
  }
  if (definition.power_members.empty()) {
    FPLOG.error(Loc::current(), "IO Cell definition must contain at least one power member.");
    return false;
  }
  if (definition.filler_masters.empty()) {
    FPLOG.error(Loc::current(), "IO Cell definition must contain filler_masters.");
    return false;
  }
  return true;
}

bool IOCellPlacer::validateDefinition(const IOCellDefinition& definition)
{
  Database& database = FPDM.getDatabase();
  if (definition.corner_master_name.empty() || database.get_cell_master_map().find(definition.corner_master_name) == database.get_cell_master_map().end()) {
    FPLOG.error(Loc::current(), "IO Cell corner master '", definition.corner_master_name, "' is not present in LEF/database.");
    return false;
  }
  for (const std::string& master : definition.signal_master_names) {
    if (database.get_cell_master_map().find(master) == database.get_cell_master_map().end()) {
      FPLOG.error(Loc::current(), "IO Cell signal master '", master, "' is not present in LEF/database.");
      return false;
    }
  }
  std::set<std::string> role_prefixes;
  for (const IOCellPowerMember& member : definition.power_members) {
    if (database.get_cell_master_map().find(member.master_name) == database.get_cell_master_map().end()) {
      FPLOG.error(Loc::current(), "IO Cell power master '", member.master_name, "' is not present in LEF/database.");
      return false;
    }
    if (member.net_patterns.empty()) {
      FPLOG.error(Loc::current(), "IO Cell power member '", member.master_name, "' has no net_pattern.");
      return false;
    }
    if (!hasMatchingNet(database, member.net_patterns)) {
      FPLOG.error(Loc::current(), "No net matches power Cell '", member.master_name, "' definition patterns.");
      return false;
    }
    if (member.pin_name.empty()) {
      FPLOG.error(Loc::current(), "IO Cell power member '", member.master_name, "' has no pin field.");
      return false;
    }
    const std::string role = getPowerPrefix(member.pin_name);
    if (role.empty() || !role_prefixes.insert(role).second) {
      FPLOG.error(Loc::current(), "IO Cell power members have duplicate or empty power role prefix.");
      return false;
    }
  }
  for (const IOCellFillerMaster& filler : definition.filler_masters) {
    if (database.get_cell_master_map().find(filler.master_name) == database.get_cell_master_map().end()) {
      FPLOG.error(Loc::current(), "IO Cell filler master '", filler.master_name, "' is not present in LEF/database.");
      return false;
    }
  }
  if (_config.io_cell_power_spacing_micron <= 0.0) {
    FPLOG.error(Loc::current(), "io_cell.power.target_group_spacing must be greater than zero.");
    return false;
  }
  return true;
}

bool IOCellPlacer::normalizePowerInstanceNames(const IOCellDefinition& definition)
{
  Database& database = FPDM.getDatabase();

  // Build the role-to-name mapping from the technology definition.  Digits in
  // a role distinguish a LEF pin variant (VDD1/VDD2) but do not change the
  // externally visible prefix used for the physical side/index name.
  std::map<std::string, std::string> master_to_prefix;
  std::map<std::string, std::string> prefix_to_master;
  for (const IOCellPowerMember& member : definition.power_members) {
    const std::string role = getPowerPrefix(member.pin_name);
    if (role.empty()) {
      FPLOG.error(Loc::current(), "Cannot derive a power instance prefix for master '", member.master_name, "'.");
      return false;
    }
    auto [iter, inserted] = master_to_prefix.emplace(member.master_name, role);
    if (!inserted && iter->second != role) {
      FPLOG.error(Loc::current(), "Power master '", member.master_name, "' has conflicting role prefixes.");
      return false;
    }
    auto [prefix_iter, prefix_inserted] = prefix_to_master.emplace(role, member.master_name);
    if (!prefix_inserted && prefix_iter->second != member.master_name) {
      FPLOG.error(Loc::current(), "Power role prefix '", role, "' is assigned to multiple masters.");
      return false;
    }
  }

  // A re-run can leave power cells that are no longer needed after the die
  // size changes (for example die_size -> die_util).  They are unplaced but
  // keep their previously normalized names, which can collide with the new
  // target names.  Move them to unique temporary names and mark them deleted
  // so the IDB output removes them as well.
  int32_t stale_power_index = 0;
  const int32_t instance_count = static_cast<int32_t>(database.get_instance_list().size());
  for (int32_t index = 0; index < instance_count; ++index) {
    Instance& instance = database.get_instance_list()[index];
    if (!isPowerMaster(definition, instance.get_master_name()) || instance.get_placed() || instance.is_delete_instance()) {
      continue;
    }
    // Only clean up instances that carry a name produced by an earlier
    // normalization pass or by createPhysicalInstance; leave user-named
    // supply pads alone.
    bool generated_name = instance.get_name().rfind("IO_POWER_", 0) == 0;
    for (const auto& [prefix, master] : prefix_to_master) {
      if (instance.get_name().rfind(prefix + "_", 0) == 0) {
        generated_name = true;
        break;
      }
    }
    if (!generated_name) {
      continue;
    }
    std::string temporary;
    do {
      temporary = "__IFP_UNUSED_POWER_" + std::to_string(stale_power_index++);
    } while (database.get_instance_name_to_idx_map().contains(temporary));
    const std::string old_name = instance.get_name();
    auto old_iter = database.get_instance_name_to_idx_map().find(old_name);
    if (old_iter == database.get_instance_name_to_idx_map().end()) {
      continue;
    }
    database.get_instance_name_to_idx_map().erase(old_iter);
    instance.set_name(temporary);
    database.get_instance_name_to_idx_map()[temporary] = index;
    instance.set_delete_instance(true);
    for (Net& net : database.get_net_list()) {
      for (NetPin& pin : net.get_net_pin_list()) {
        if (!pin.get_io() && pin.get_instance_name() == old_name) {
          pin.set_instance_name(temporary);
        }
      }
    }
    for (IOPin& io_pin : database.get_io_pin_list()) {
      if (io_pin.get_instance_name() == old_name) {
        io_pin.set_instance_name(temporary);
      }
    }
    if (!instance.is_new_instance()) {
      database.get_instance_rename_map()[old_name] = temporary;
    }
  }

  using PowerByRole = std::map<std::string, std::vector<Instance*>>;
  std::map<IOEdgeType, PowerByRole> power_by_edge;
  const Die& die = database.get_die();
  const int32_t die_ll_x = die.get_ll_x();
  const int32_t die_ll_y = die.get_ll_y();
  const int32_t die_ur_x = die.get_ur_x();
  const int32_t die_ur_y = die.get_ur_y();

  auto oriented_edge = [](PlacementOrientation orient) {
    switch (orient) {
      case PlacementOrientation::kN:
        return IOEdgeType::kBottom;
      case PlacementOrientation::kW:
        return IOEdgeType::kRight;
      case PlacementOrientation::kS:
        return IOEdgeType::kTop;
      case PlacementOrientation::kE:
        return IOEdgeType::kLeft;
      default:
        return IOEdgeType::kNone;
    }
  };

  for (Instance& instance : database.get_instance_list()) {
    if (!isPowerMaster(definition, instance.get_master_name()) || !instance.get_placed() || instance.is_delete_instance()) {
      continue;
    }
    const PlanarRect& rect = instance.get_bounding_rect();
    std::vector<IOEdgeType> candidates;
    if (rect.get_ll_y() == die_ll_y) {
      candidates.push_back(IOEdgeType::kBottom);
    }
    if (rect.get_ur_x() == die_ur_x) {
      candidates.push_back(IOEdgeType::kRight);
    }
    if (rect.get_ur_y() == die_ur_y) {
      candidates.push_back(IOEdgeType::kTop);
    }
    if (rect.get_ll_x() == die_ll_x) {
      candidates.push_back(IOEdgeType::kLeft);
    }
    const IOEdgeType by_orientation = oriented_edge(instance.get_orient());
    IOEdgeType edge = IOEdgeType::kNone;
    if (std::find(candidates.begin(), candidates.end(), by_orientation) != candidates.end()) {
      edge = by_orientation;
    } else if (candidates.size() == 1) {
      edge = candidates.front();
    }
    if (edge == IOEdgeType::kNone) {
      FPLOG.error(Loc::current(), "Placed IO power Cell '", instance.get_name(), "' does not lie on exactly one die edge.");
      return false;
    }

    auto prefix_iter = master_to_prefix.find(instance.get_master_name());
    if (prefix_iter == master_to_prefix.end()) {
      FPLOG.error(Loc::current(), "No role prefix is defined for IO power master '", instance.get_master_name(), "'.");
      return false;
    }
    power_by_edge[edge][prefix_iter->second].push_back(&instance);
  }

  const IOEdgeType edges[] = {IOEdgeType::kBottom, IOEdgeType::kRight, IOEdgeType::kTop, IOEdgeType::kLeft};
  const char* edge_names[] = {"bottom", "right", "top", "left"};
  struct Rename {
    Instance* instance = nullptr;
    std::string old_name;
    std::string new_name;
  };
  std::vector<Rename> renames;
  std::map<std::string, int32_t> target_owner;

  for (size_t edge_index = 0; edge_index < 4; ++edge_index) {
    const IOEdgeType edge = edges[edge_index];
    auto edge_iter = power_by_edge.find(edge);
    if (edge_iter == power_by_edge.end() || edge_iter->second.empty()) {
      FPLOG.error(Loc::current(), "No placed IO power group was found on ", edge_names[edge_index], " edge.");
      return false;
    }
    for (const auto& [prefix, master] : prefix_to_master) {
      if (edge_iter->second.find(prefix) == edge_iter->second.end()) {
        FPLOG.error(Loc::current(), "IO power role '", prefix, "' is missing on ", edge_names[edge_index], " edge.");
        return false;
      }
    }
    size_t expected_count = 0;
    for (auto& [prefix, instances] : edge_iter->second) {
      if (expected_count == 0) {
        expected_count = instances.size();
      } else if (instances.size() != expected_count) {
        FPLOG.error(Loc::current(), "IO power role '", prefix, "' has ", instances.size(), " cells on ", edge_names[edge_index],
                    "; expected ", expected_count, " for complete power groups.");
        return false;
      }
      std::sort(instances.begin(), instances.end(), [edge](const Instance* lhs, const Instance* rhs) {
        const PlanarRect& lhs_rect = lhs->get_bounding_rect();
        const PlanarRect& rhs_rect = rhs->get_bounding_rect();
        if (edge == IOEdgeType::kBottom || edge == IOEdgeType::kTop) {
          if (lhs_rect.get_ll_x() != rhs_rect.get_ll_x()) {
            return lhs_rect.get_ll_x() < rhs_rect.get_ll_x();
          }
        } else if (lhs_rect.get_ur_y() != rhs_rect.get_ur_y()) {
          // Physical top-to-bottom order on the vertical edges.
          return lhs_rect.get_ur_y() > rhs_rect.get_ur_y();
        }
        return lhs->get_name() < rhs->get_name();
      });
      for (size_t index = 0; index < instances.size(); ++index) {
        const std::string target = prefix + "_" + edge_names[edge_index] + "_" + std::to_string(index);
        const auto existing = database.get_instance_name_to_idx_map().find(target);
        const int32_t instance_index = static_cast<int32_t>(instances[index] - database.get_instance_list().data());
        if (existing != database.get_instance_name_to_idx_map().end() && existing->second != instance_index) {
          // A name collision is allowed only when that instance is itself one
          // of the placed power cells being changed in this pass.  Check the
          // instance directly so a collision with a later role in the sorted
          // traversal is handled as well.
          const Instance& existing_instance = database.get_instance_list()[existing->second];
          const bool moving_existing_name = isPowerMaster(definition, existing_instance.get_master_name())
                                            && existing_instance.get_placed() && !existing_instance.is_delete_instance();
          if (!moving_existing_name) {
            FPLOG.error(Loc::current(), "Target IO power instance name '", target, "' is already used by another instance.");
            return false;
          }
        }
        auto [owner_iter, inserted] = target_owner.emplace(target, instance_index);
        if (!inserted && owner_iter->second != instance_index) {
          FPLOG.error(Loc::current(), "Duplicate target IO power instance name '", target, "'.");
          return false;
        }
        if (instances[index]->get_name() != target) {
          renames.push_back({instances[index], instances[index]->get_name(), target});
        }
      }
    }
  }

  if (renames.empty()) {
    return true;
  }

  auto rename_database_instance = [&](Instance& instance, const std::string& old_name, const std::string& new_name) {
    auto old_iter = database.get_instance_name_to_idx_map().find(old_name);
    if (old_iter == database.get_instance_name_to_idx_map().end()) {
      FPLOG.error(Loc::current(), "Cannot rename unknown IO power instance '", old_name, "'.");
      return false;
    }
    const int32_t index = old_iter->second;
    database.get_instance_name_to_idx_map().erase(old_iter);
    instance.set_name(new_name);
    database.get_instance_name_to_idx_map()[new_name] = index;
    for (Net& net : database.get_net_list()) {
      for (NetPin& pin : net.get_net_pin_list()) {
        if (!pin.get_io() && pin.get_instance_name() == old_name) {
          pin.set_instance_name(new_name);
        }
      }
    }
    for (IOPin& io_pin : database.get_io_pin_list()) {
      if (io_pin.get_instance_name() == old_name) {
        io_pin.set_instance_name(new_name);
      }
    }
    return true;
  };

  // Two phases keep the map valid even if an old source name happens to be a
  // target name of another power cell.
  std::vector<std::string> temporary_names;
  temporary_names.reserve(renames.size());
  for (size_t index = 0; index < renames.size(); ++index) {
    std::string temporary = "__IFP_IO_POWER_RENAME_TMP_" + std::to_string(index);
    while (database.get_instance_name_to_idx_map().contains(temporary)) {
      temporary += "_";
    }
    temporary_names.push_back(temporary);
    if (!rename_database_instance(*renames[index].instance, renames[index].old_name, temporary)) {
      return false;
    }
  }
  for (size_t index = 0; index < renames.size(); ++index) {
    if (!rename_database_instance(*renames[index].instance, temporary_names[index], renames[index].new_name)) {
      return false;
    }
    // Newly created ring cells do not exist in IDB yet; their final name is
    // consumed directly by outputNewInstanceList().  Only source-netlist
    // instances need a deferred IDB rename.
    if (!renames[index].instance->is_new_instance()) {
      database.get_instance_rename_map()[renames[index].old_name] = renames[index].new_name;
    }
  }
  return true;
}

void IOCellPlacer::resetExistingPlacement(const IOCellDefinition& definition)
{
  Database& database = FPDM.getDatabase();
  _power_instance_indices.clear();
  _used_instance_names.clear();
  _next_power_temp_id = 0;
  database.get_instance_list().reserve(database.get_instance_list().size() + 1024);
  const int32_t instance_count = static_cast<int32_t>(database.get_instance_list().size());
  for (int32_t index = 0; index < instance_count; ++index) {
    Instance& instance = database.get_instance_list()[index];
    _used_instance_names.insert(instance.get_name());
    bool generated_ring_cell
        = instance.get_name().rfind("IO_CORNER_", 0) == 0 || instance.get_name().rfind("IO_FILL_", 0) == 0 || instance.get_name().rfind("IO_POWER_", 0) == 0;
    bool physical_only_master = instance.get_master_name() == definition.corner_master_name;
    for (const IOCellFillerMaster& filler : definition.filler_masters) {
      physical_only_master = physical_only_master || instance.get_master_name() == filler.master_name;
    }
    if (generated_ring_cell || physical_only_master) {
      instance.set_delete_instance(true);
    }
    if (isSignalMaster(definition, instance.get_master_name()) || isPowerMaster(definition, instance.get_master_name())) {
      instance.set_coord(-1, -1);
      instance.set_placed(false);
      instance.set_fixed(false);
      instance.set_placement_updated(false);
      if (isPowerMaster(definition, instance.get_master_name())) {
        _power_instance_indices.push_back(index);
      }
    }
  }
}

bool IOCellPlacer::placeAuto(const IOCellDefinition& definition)
{
  auto signal_indices = buildSignalSideIndices(definition);
  if (!placeCorners(definition)) {
    return false;
  }
  const IOEdgeType edges[] = {IOEdgeType::kBottom, IOEdgeType::kRight, IOEdgeType::kTop, IOEdgeType::kLeft};
  for (int32_t side = 0; side < 4; ++side) {
    if (!placeSide(edges[side], definition, signal_indices[side])) {
      return false;
    }
  }
  return true;
}

bool IOCellPlacer::placeFromFile(const IOCellDefinition& definition)
{
  const std::string& path = _config.io_cell_file;
  if (path.empty()) {
    FPLOG.error(Loc::current(), "io_cell.mode is 'file', but io_cell.file_path is empty.");
    return false;
  }
  if (!placeCorners(definition)) {
    return false;
  }
  std::ifstream stream(path);
  if (!stream.is_open()) {
    FPLOG.error(Loc::current(), "Failed to open IO Cell placement file '", path, "'.");
    return false;
  }
  std::string line;
  int32_t line_number = 0;
  std::vector<Segment> occupied[4];
  std::vector<std::string> side_power_masters[4];
  std::vector<int32_t> side_group_starts[4];
  const IOEdgeType edges[] = {IOEdgeType::kBottom, IOEdgeType::kRight, IOEdgeType::kTop, IOEdgeType::kLeft};
  while (std::getline(stream, line)) {
    ++line_number;
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream input(line);
    std::string instance_name, edge_name, master_name, orient_name;
    double offset = 0.0;
    if (!(input >> instance_name >> edge_name >> offset)) {
      FPLOG.error(Loc::current(), "Invalid IO Cell placement at line ", line_number, ".");
      return false;
    }
    input >> master_name >> orient_name;
    IOEdgeType edge = IOEdgeType::kNone;
    if (edge_name == "bottom") {
      edge = IOEdgeType::kBottom;
    }
    if (edge_name == "right") {
      edge = IOEdgeType::kRight;
    }
    if (edge_name == "top") {
      edge = IOEdgeType::kTop;
    }
    if (edge_name == "left") {
      edge = IOEdgeType::kLeft;
    }
    if (edge == IOEdgeType::kNone) {
      FPLOG.error(Loc::current(), "Invalid IO Cell edge '", edge_name, "' at line ", line_number, ".");
      return false;
    }
    Database& database = FPDM.getDatabase();
    auto index_iter = database.get_instance_name_to_idx_map().find(instance_name);
    Instance* instance = nullptr;
    if (index_iter == database.get_instance_name_to_idx_map().end()) {
      if (master_name.empty() || (isSignalMaster(definition, master_name) && !isPowerMaster(definition, master_name))) {
        FPLOG.error(Loc::current(), "IO Cell placement refers to unknown instance '", instance_name, "' without a physical-only master.");
        return false;
      }
      if (database.get_cell_master_map().find(master_name) == database.get_cell_master_map().end()) {
        FPLOG.error(Loc::current(), "IO Cell placement uses unknown master '", master_name, "'.");
        return false;
      }
      instance = createPhysicalInstance(instance_name, master_name);
    } else {
      instance = &database.get_instance_list()[index_iter->second];
    }
    if (!master_name.empty() && master_name != instance->get_master_name()) {
      FPLOG.error(Loc::current(), "Changing IO Cell master is not supported for '", instance_name, "'.");
      return false;
    }
    PlacementOrientation orient = orient_name.empty() ? edgeOrientation(edge) : GetPlacementOrientationByName()(orient_name);
    if (!placeCell(*instance, edge, sideStart(edge) + micronToDbu(offset), orient)) {
      return false;
    }
    int32_t side_index = static_cast<int32_t>(std::find(std::begin(edges), std::end(edges), edge) - std::begin(edges));
    int32_t begin = sideStart(edge) + micronToDbu(offset);
    if (isPowerMaster(definition, instance->get_master_name())) {
      side_power_masters[side_index].push_back(instance->get_master_name());
      for (const IOCellPowerMember& member : definition.power_members) {
        if (member.master_name == instance->get_master_name()) {
          instance->set_io_cell_pin_name(member.pin_name);
          instance->set_io_cell_net_name(resolvePowerNet(database, member.net_patterns));
        }
      }
    }
    if (!definition.power_members.empty() && instance->get_master_name() == definition.power_members.front().master_name) {
      side_group_starts[side_index].push_back(begin);
    }
    occupied[side_index].push_back({begin, begin + getCellSpan(*instance)});
  }
  const CellMaster& corner_master = FPDM.getDatabase().get_cell_master_map().at(definition.corner_master_name);
  for (int32_t side_index = 0; side_index < 4; ++side_index) {
    const IOEdgeType edge = edges[side_index];
    std::vector<Segment> sorted;
    sorted = occupied[side_index];
    std::sort(sorted.begin(), sorted.end(), [](const Segment& lhs, const Segment& rhs) { return lhs.begin < rhs.begin; });
    const int32_t begin = sideStart(edge) + (edge == IOEdgeType::kBottom || edge == IOEdgeType::kTop ? corner_master.get_width() : corner_master.get_height());
    const int32_t end = sideStart(edge) + sideLength(edge)
                        - (edge == IOEdgeType::kBottom || edge == IOEdgeType::kTop ? corner_master.get_width() : corner_master.get_height());
    if (sorted.empty() || sorted.front().begin != begin || sorted.back().end != end) {
      FPLOG.error(Loc::current(), "IO Cell placement file does not exactly cover ", edgeName(edge), " edge.");
      return false;
    }
    for (const IOCellPowerMember& member : definition.power_members) {
      if (std::find(side_power_masters[side_index].begin(), side_power_masters[side_index].end(), member.master_name)
          == side_power_masters[side_index].end()) {
        FPLOG.error(Loc::current(), "IO Cell placement file has no complete power package on ", edgeName(edge), ".");
        return false;
      }
    }
    std::sort(side_group_starts[side_index].begin(), side_group_starts[side_index].end());
    const int32_t max_spacing = micronToDbu(_config.io_cell_power_spacing_micron);
    for (size_t i = 1; i < side_group_starts[side_index].size(); ++i) {
      if (side_group_starts[side_index][i] - side_group_starts[side_index][i - 1] > max_spacing) {
        FPLOG.error(Loc::current(), "IO Cell power groups exceed the maximum spacing on ", edgeName(edge), ".");
        return false;
      }
    }
    for (size_t i = 1; i < sorted.size(); ++i) {
      if (sorted[i - 1].end != sorted[i].begin) {
        FPLOG.error(Loc::current(), "IO Cell placement file has a gap or overlap on ", edgeName(edge), ".");
        return false;
      }
    }
  }
  return true;
}

bool IOCellPlacer::bindIOPins(const IOCellDefinition& definition)
{
  const Database& database = FPDM.getDatabase();
  _port_bindings.clear();
  for (const Net& net : database.get_net_list()) {
    std::vector<std::string> io_names;
    for (const NetPin& pin : net.get_net_pin_list()) {
      if (pin.get_io()) {
        io_names.push_back(pin.get_pin_name());
      }
    }
    if (io_names.empty()) {
      continue;
    }

    std::vector<PortBinding> bindings;
    std::set<std::pair<std::string, std::string>> seen;
    bool all_power = true;
    for (const NetPin& pin : net.get_net_pin_list()) {
      if (pin.get_io()) {
        continue;
      }
      auto iter = database.get_instance_name_to_idx_map().find(pin.get_instance_name());
      if (iter == database.get_instance_name_to_idx_map().end()) {
        continue;
      }
      const Instance& instance = database.get_instance_list()[iter->second];
      const auto master = database.get_cell_master_map().find(instance.get_master_name());
      if (instance.get_macro() || master == database.get_cell_master_map().end() || !master->second.get_pad()) {
        continue;
      }
      const bool power = isPowerMaster(definition, instance.get_master_name());
      if (!power && !isSignalMaster(definition, instance.get_master_name())) {
        continue;
      }
      // Generated supply cells feed the ring; they are not additional package terminals.
      if (instance.get_name().rfind("IO_POWER_", 0) == 0 || instance.get_name().rfind("IO_POWER_TMP_", 0) == 0 || instance.is_delete_instance()) {
        continue;
      }
      if (seen.emplace(instance.get_name(), pin.get_pin_name()).second) {
        bindings.push_back({instance.get_name(), pin.get_pin_name()});
        all_power = all_power && power;
      }
    }
    if (bindings.empty() || (bindings.size() > 1 && !all_power)) {
      FPLOG.error(Loc::current(), "IO net '", net.get_name(), "' has ", bindings.size(),
                  " eligible PAD terminals. Expected one signal PAD or one/more supply PADs.");
      return false;
    }
    std::sort(bindings.begin(), bindings.end(),
              [](const PortBinding& a, const PortBinding& b) { return std::tie(a.instance_name, a.pin_name) < std::tie(b.instance_name, b.pin_name); });
    for (const std::string& name : io_names) {
      if (!_port_bindings.emplace(name, bindings).second) {
        FPLOG.error(Loc::current(), "IO pin '", name, "' is present on multiple nets.");
        return false;
      }
    }
  }
  for (const IOPin& io_pin : database.get_io_pin_list()) {
    if (_port_bindings.find(io_pin.get_name()) == _port_bindings.end()) {
      FPLOG.error(Loc::current(), "No connected IO Cell terminal for IO pin '", io_pin.get_name(), "'.");
      return false;
    }
  }
  return true;
}

bool IOCellPlacer::updateIOPinPorts()
{
  Database& database = FPDM.getDatabase();
  for (IOPin& io_pin : database.get_io_pin_list()) {
    std::vector<IOPort> absolute_ports;
    std::vector<IOPort> relative_ports;
    const auto& bindings = _port_bindings.at(io_pin.get_name());
    for (const PortBinding& binding : bindings) {
      const Instance& instance = database.get_instance_list().at(database.get_instance_name_to_idx_map().at(binding.instance_name));
      if (!instance.get_placed() || instance.is_delete_instance()) {
        FPLOG.error(Loc::current(), "IO pin '", io_pin.get_name(), "' refers to unplaced PAD '", binding.instance_name, "'.");
        return false;
      }
      // Use one access rectangle on the highest available routing layer, rather
      // than copying all of the cell's internal power-ring metal into the top port.
      const InstancePinShape* access = nullptr;
      int access_order = -1;
      int64_t access_area = -1;
      for (const InstancePinShape& shape : instance.get_pin_shape_list()) {
        if (shape.get_pin_name() != binding.pin_name) {
          continue;
        }
        const RoutingLayer* layer = nullptr;
        for (const RoutingLayer& candidate : database.get_routing_layer_list()) {
          if (candidate.get_name() == shape.get_layer_name()) {
            layer = &candidate;
            break;
          }
        }
        if (layer == nullptr) {
          continue;
        }
        const int64_t width = static_cast<int64_t>(shape.get_ur_x()) - shape.get_ll_x();
        const int64_t height = static_cast<int64_t>(shape.get_ur_y()) - shape.get_ll_y();
        const int64_t area = width * height;
        const int64_t thickness = std::min(width, height);
        if (width <= 0 || height <= 0 || thickness < layer->get_min_width() || area < layer->get_min_area()
            || (layer->get_max_width() > 0 && thickness > layer->get_max_width())) {
          continue;
        }
        if (layer->get_order() > access_order || (layer->get_order() == access_order && area > access_area)) {
          access = &shape;
          access_order = layer->get_order();
          access_area = area;
        }
      }
      if (access == nullptr) {
        FPLOG.error(Loc::current(), "No legal routing access rectangle for IO pin '", io_pin.get_name(), "' on PAD '", binding.instance_name, "/",
                    binding.pin_name, "'.");
        return false;
      }
      idb::IdbOrient orient = idb::IdbOrient::kNone;
      switch (instance.get_orient()) {
        case PlacementOrientation::kN:
          orient = idb::IdbOrient::kN_R0;
          break;
        case PlacementOrientation::kE:
          orient = idb::IdbOrient::kE_R270;
          break;
        case PlacementOrientation::kS:
          orient = idb::IdbOrient::kS_R180;
          break;
        case PlacementOrientation::kW:
          orient = idb::IdbOrient::kW_R90;
          break;
        case PlacementOrientation::kFN:
          orient = idb::IdbOrient::kFN_MY;
          break;
        case PlacementOrientation::kFS:
          orient = idb::IdbOrient::kFS_MX;
          break;
        case PlacementOrientation::kFE:
          orient = idb::IdbOrient::kFE_MY90;
          break;
        case PlacementOrientation::kFW:
          orient = idb::IdbOrient::kFW_MX90;
          break;
        default:
          FPLOG.error(Loc::current(), "Invalid orientation for IO Cell '", binding.instance_name, "'.");
          return false;
      }
      // IDB already normalizes LEF ORIGIN when loading the master. Its rectangles
      // are bbox-local here, including for previously placed PAD instances.
      idb::IdbCoordinate<int32_t> origin(instance.get_x(), instance.get_y());
      idb::IdbRect rect(access->get_ll_x() + instance.get_x(), access->get_ll_y() + instance.get_y(), access->get_ur_x() + instance.get_x(),
                        access->get_ur_y() + instance.get_y());
      idb::IdbOrientTransform transform(orient, &origin, instance.get_width(), instance.get_height());
      transform.transformRect(&rect);
      const int32_t x = rect.get_low_x() + (rect.get_high_x() - rect.get_low_x()) / 2;
      const int32_t y = rect.get_low_y() + (rect.get_high_y() - rect.get_low_y()) / 2;
      IOPort port;
      port.set_layer_name(access->get_layer_name());
      port.set_coord(x, y);
      port.set_placed(true);
      port.set_rect(rect.get_low_x(), rect.get_low_y(), rect.get_high_x(), rect.get_high_y());
      absolute_ports.push_back(port);
      port.set_rect(rect.get_low_x() - x, rect.get_low_y() - y, rect.get_high_x() - x, rect.get_high_y() - y);
      relative_ports.push_back(port);
    }
    io_pin.set_instance_name(bindings.size() == 1 ? bindings.front().instance_name : "");
    io_pin.set_port_list(absolute_ports);
    io_pin.set_new_port_list(relative_ports);
    io_pin.set_coord(absolute_ports.front().get_x(), absolute_ports.front().get_y());
    io_pin.set_orient(PlacementOrientation::kN);
    io_pin.set_direct_location(true);
    io_pin.set_port_exist(true);
    io_pin.set_port_exist_updated(true);
    io_pin.set_placed(true);
    io_pin.set_fixed(true);
    io_pin.set_updated(true);
  }
  return true;
}

bool IOCellPlacer::placeCorners(const IOCellDefinition& definition)
{
  Database& database = FPDM.getDatabase();
  const CellMaster& corner_master = database.get_cell_master_map().at(definition.corner_master_name);
  const int32_t cw = corner_master.get_width();
  const int32_t ch = corner_master.get_height();
  const int32_t x0 = database.get_die().get_ll_x();
  const int32_t y0 = database.get_die().get_ll_y();
  const int32_t x1 = database.get_die().get_ur_x();
  const int32_t y1 = database.get_die().get_ur_y();
  struct Corner
  {
    const char* name;
    int32_t x;
    int32_t y;
    PlacementOrientation orient;
  };
  const Corner corners[] = {{"IO_CORNER_BL", x0, y0, PlacementOrientation::kN},
                            {"IO_CORNER_BR", x1 - cw, y0, PlacementOrientation::kW},
                            {"IO_CORNER_TR", x1 - cw, y1 - ch, PlacementOrientation::kS},
                            {"IO_CORNER_TL", x0, y1 - ch, PlacementOrientation::kE}};
  for (const Corner& corner : corners) {
    Instance* instance = nullptr;
    auto index_iter = database.get_instance_name_to_idx_map().find(corner.name);
    if (index_iter != database.get_instance_name_to_idx_map().end()) {
      instance = &database.get_instance_list()[index_iter->second];
      if (instance->get_master_name() != definition.corner_master_name) {
        FPLOG.error(Loc::current(), "Existing corner instance '", corner.name, "' has unexpected master.");
        return false;
      }
    } else {
      instance = createPhysicalInstance(corner.name, definition.corner_master_name);
    }
    instance->set_coord(corner.x, corner.y);
    instance->set_orient(corner.orient);
    const int32_t corner_width = getOrientedWidth(*instance, corner.orient);
    const int32_t corner_height = getOrientedHeight(*instance, corner.orient);
    instance->set_bounding_rect(corner.x, corner.y, corner.x + corner_width, corner.y + corner_height);
    instance->set_fixed(true);
    instance->set_placed(true);
    instance->set_placement_updated(!instance->is_new_instance());
    instance->set_delete_instance(false);
  }
  return true;
}

bool IOCellPlacer::placeSide(IOEdgeType edge, const IOCellDefinition& definition, const std::vector<int32_t>& signal_indices)
{
  Database& database = FPDM.getDatabase();
  SideLayout layout;
  if (!buildSideLayout(edge, definition, layout)) {
    FPLOG.error(Loc::current(), layout.error);
    return false;
  }

  for (int32_t group_idx = 0; group_idx < layout.group_count; ++group_idx) {
    int32_t offset = layout.anchors[group_idx];
    for (const IOCellPowerMember* member : layout.package) {
      Instance* instance = findOrCreatePowerInstance(*member);
      if (instance == nullptr || !placeCell(*instance, edge, offset, edgeOrientation(edge))) {
        return false;
      }
      offset += getCellSpan(*instance);
    }
  }

  // Pack the signal instances into free intervals in netlist order.
  size_t signal_cursor = 0;
  int32_t filler_index = 0;
  for (const Segment& segment : layout.free_segments) {
    int32_t cursor_coord = segment.begin;
    while (signal_cursor < signal_indices.size()) {
      Instance& signal = database.get_instance_list()[signal_indices[signal_cursor]];
      const int32_t span = getCellSpan(signal);
      if (cursor_coord + span > segment.end) {
        break;
      }
      if (!placeCell(signal, edge, cursor_coord, edgeOrientation(edge))) {
        return false;
      }
      cursor_coord += span;
      ++signal_cursor;
    }
    if (!fillSegment(edge, {cursor_coord, segment.end}, definition, filler_index)) {
      return false;
    }
  }
  if (signal_cursor != signal_indices.size()) {
    FPLOG.error(Loc::current(), "Signal IO Cells do not fit on ", edgeName(edge), ".");
    return false;
  }
  return true;
}

std::array<std::vector<int32_t>, 4> IOCellPlacer::buildSignalSideIndices(const IOCellDefinition& definition) const
{
  static constexpr int32_t kSideCount = 4;
  std::array<std::vector<int32_t>, kSideCount> signal_indices;
  const Database& database = FPDM.getDatabase();
  int32_t next_side = 0;
  for (int32_t i = 0; i < static_cast<int32_t>(database.get_instance_list().size()); ++i) {
    const Instance& instance = database.get_instance_list()[i];
    if (!isSignalMaster(definition, instance.get_master_name())) {
      continue;
    }
    signal_indices[next_side].push_back(i);
    next_side = (next_side + 1) % kSideCount;
  }
  return signal_indices;
}

bool IOCellPlacer::buildSideLayout(IOEdgeType edge, const IOCellDefinition& definition, SideLayout& layout) const
{
  layout = SideLayout();
  const Database& database = FPDM.getDatabase();
  const bool horizontal = edge == IOEdgeType::kBottom || edge == IOEdgeType::kTop;
  const CellMaster& corner_master = database.get_cell_master_map().at(definition.corner_master_name);
  layout.corner_span = horizontal ? corner_master.get_width() : corner_master.get_height();
  layout.line_begin = sideStart(edge) + layout.corner_span;
  layout.line_end = sideStart(edge) + sideLength(edge) - layout.corner_span;
  layout.line_length = layout.line_end - layout.line_begin;

  layout.dbu_spacing = micronToDbu(_config.io_cell_power_spacing_micron);
  if (layout.dbu_spacing <= 0) {
    layout.error = "IO Cell power group spacing is below one DBU on " + edgeName(edge) + ".";
    return false;
  }

  for (const IOCellPowerMember& member : definition.power_members) {
    layout.package.push_back(&member);
  }
  for (const IOCellPowerMember* member : layout.package) {
    layout.package_span += database.get_cell_master_map().at(member->master_name).get_width();
  }
  if (layout.package.empty() || layout.package_span <= 0) {
    layout.error = "IO Cell power package has zero placement span on " + edgeName(edge) + ".";
    return false;
  }
  if (layout.line_length < layout.package_span) {
    layout.error = "IO Cell power package does not fit on " + edgeName(edge) + ".";
    return false;
  }
  layout.group_count = std::max<int32_t>(1, (layout.line_length - layout.package_span + layout.dbu_spacing - 1) / layout.dbu_spacing + 1);
  if (layout.group_count * layout.package_span > layout.line_length) {
    layout.error = "IO Cell power packages do not fit on " + edgeName(edge) + ".";
    return false;
  }

  // Keep the first/last power group away from the corner cells.  A single
  // group is centered; multiple groups are distributed over the reduced
  // usable span instead of starting/ending flush against the corners.
  int32_t corner_clearance = micronToDbu(_config.io_cell_corner_clearance_micron);
  if (corner_clearance < 0) {
    corner_clearance = 0;
  }
  const int32_t max_clearance = (layout.line_length - layout.group_count * layout.package_span) / 2;
  if (corner_clearance > max_clearance) {
    corner_clearance = max_clearance;
  }
  layout.clearance = corner_clearance;
  layout.usable_length = layout.line_length - 2 * layout.clearance - layout.package_span;

  int32_t previous = layout.line_begin;
  for (int32_t group_idx = 0; group_idx < layout.group_count; ++group_idx) {
    int32_t anchor = layout.line_begin + layout.clearance;
    if (layout.group_count == 1) {
      anchor += layout.usable_length / 2;
    } else {
      anchor += static_cast<int32_t>((static_cast<int64_t>(layout.usable_length) * group_idx) / (layout.group_count - 1));
    }
    if (anchor < previous) {
      anchor = previous;
    }
    if (anchor > layout.line_end - layout.package_span) {
      anchor = layout.line_end - layout.package_span;
    }
    if (anchor > previous) {
      layout.free_segments.push_back({previous, anchor});
    }
    layout.anchors.push_back(anchor);
    previous = anchor + layout.package_span;
  }
  if (previous < layout.line_end) {
    layout.free_segments.push_back({previous, layout.line_end});
  }

  for (const IOCellFillerMaster& filler : definition.filler_masters) {
    layout.filler_widths.push_back(micronToDbu(filler.width_micron));
  }
  return true;
}

bool IOCellPlacer::canFillGap(int32_t gap, const std::vector<int32_t>& filler_widths) const
{
  if (gap <= 0) {
    return true;
  }
  if (filler_widths.empty()) {
    return false;
  }
  int32_t gcd = 0;
  int32_t min_width = std::numeric_limits<int32_t>::max();
  for (int32_t width : filler_widths) {
    if (width <= 0) {
      continue;
    }
    gcd = std::gcd(gcd, width);
    min_width = std::min(min_width, width);
  }
  if (gcd <= 0 || gap % gcd != 0) {
    return false;
  }
  // When the smallest filler equals the gcd, any multiple of the gcd is
  // representable.  Otherwise fall back to the exact solver for safety.
  return min_width == gcd || !solveFiller(gap, filler_widths).empty();
}

bool IOCellPlacer::packSignalsDryRun(const std::vector<int32_t>& signal_indices, const SideLayout& layout, int32_t& deficit) const
{
  const Database& database = FPDM.getDatabase();
  deficit = 0;
  int32_t filler_deficit = 0;
  size_t signal_cursor = 0;
  for (const Segment& segment : layout.free_segments) {
    int32_t cursor_coord = segment.begin;
    while (signal_cursor < signal_indices.size()) {
      const Instance& signal = database.get_instance_list()[signal_indices[signal_cursor]];
      const int32_t span = getCellSpan(signal);
      if (cursor_coord + span > segment.end) {
        break;
      }
      cursor_coord += span;
      ++signal_cursor;
    }
    const int32_t gap = segment.end - cursor_coord;
    if (gap > 0 && !canFillGap(gap, layout.filler_widths)) {
      filler_deficit += gap;
    }
  }
  if (signal_cursor != signal_indices.size()) {
    int32_t remaining = 0;
    for (size_t index = signal_cursor; index < signal_indices.size(); ++index) {
      remaining += getCellSpan(database.get_instance_list()[signal_indices[index]]);
    }
    deficit = std::max(remaining, filler_deficit);
    return false;
  }
  if (filler_deficit > 0) {
    deficit = filler_deficit;
    return false;
  }
  return true;
}

int32_t IOCellPlacer::requiredLineLength(int32_t signal_length, int32_t package_span, int32_t spacing) const
{
  if (package_span <= 0) {
    return 0;
  }
  spacing = std::max<int32_t>(1, spacing);
  int32_t line = signal_length + package_span;
  for (int32_t iter = 0; iter < 16; ++iter) {
    const int32_t groups = std::max<int32_t>(1, (line - package_span + spacing - 1) / spacing + 1);
    const int32_t need = groups * package_span + signal_length;
    if (need <= line) {
      return line;
    }
    line = need;
  }
  return line;
}

IOCapacityReport IOCellPlacer::checkCapacity()
{
  IOCapacityReport report;
  IOCellDefinition definition;
  if (!loadDefinition(definition) || !validateDefinition(definition)) {
    report.fits = false;
    report.error = "Invalid IO cell definition.";
    return report;
  }

  const auto signal_indices = buildSignalSideIndices(definition);
  const IOEdgeType edges[] = {IOEdgeType::kBottom, IOEdgeType::kRight, IOEdgeType::kTop, IOEdgeType::kLeft};
  const Database& database = FPDM.getDatabase();
  const CellMaster& corner_master = database.get_cell_master_map().at(definition.corner_master_name);
  int32_t required_width = 0;
  int32_t required_height = 0;

  for (int32_t side_idx = 0; side_idx < 4; ++side_idx) {
    const IOEdgeType edge = edges[side_idx];
    IOSideCapacity& side = report.sides[side_idx];
    side.edge = edge;
    side.edge_name = edgeName(edge);

    int32_t signal_length = 0;
    for (int32_t instance_idx : signal_indices[side_idx]) {
      signal_length += getCellSpan(database.get_instance_list()[instance_idx]);
    }
    side.signal_length = signal_length;

    SideLayout layout;
    if (!buildSideLayout(edge, definition, layout)) {
      side.error = layout.error;
      side.line_length = layout.line_length;
      side.package_span = layout.package_span;
      side.group_count = layout.group_count;
      side.clearance = layout.clearance;
      side.power_length = layout.group_count * layout.package_span;
      side.free_length = std::max<int32_t>(0, layout.line_length - side.power_length);
      side.deficit = std::max<int32_t>(signal_length + side.power_length - layout.line_length, 0);
      report.fits = false;
      const int32_t required_line = requiredLineLength(signal_length, layout.package_span, layout.dbu_spacing);
      if (edge == IOEdgeType::kBottom || edge == IOEdgeType::kTop) {
        required_width = std::max(required_width, required_line + 2 * corner_master.get_width());
      } else {
        required_height = std::max(required_height, required_line + 2 * corner_master.get_height());
      }
      continue;
    }

    side.line_length = layout.line_length;
    side.package_span = layout.package_span;
    side.group_count = layout.group_count;
    side.power_length = layout.group_count * layout.package_span;
    side.clearance = layout.clearance;
    side.free_length = layout.line_length - side.power_length;

    int32_t deficit = 0;
    const bool fits = packSignalsDryRun(signal_indices[side_idx], layout, deficit);
    side.deficit = fits ? 0 : std::max<int32_t>(deficit, side.signal_length + side.power_length - layout.line_length);
    if (!fits) {
      report.fits = false;
    }

    const int32_t required_line = requiredLineLength(signal_length, layout.package_span, layout.dbu_spacing);
    if (edge == IOEdgeType::kBottom || edge == IOEdgeType::kTop) {
      required_width = std::max(required_width, required_line + 2 * corner_master.get_width());
    } else {
      required_height = std::max(required_height, required_line + 2 * corner_master.get_height());
    }
  }

  report.required_die_width_dbu = required_width > 0 ? required_width : -1;
  report.required_die_height_dbu = required_height > 0 ? required_height : -1;
  return report;
}

bool IOCellPlacer::placeCell(Instance& instance, IOEdgeType edge, int32_t along, PlacementOrientation orient, bool fixed)
{
  Database& database = FPDM.getDatabase();
  const int32_t x0 = database.get_die().get_ll_x();
  const int32_t y0 = database.get_die().get_ll_y();
  const int32_t x1 = database.get_die().get_ur_x();
  const int32_t y1 = database.get_die().get_ur_y();
  const int32_t oriented_width = getOrientedWidth(instance, orient);
  const int32_t oriented_height = getOrientedHeight(instance, orient);
  int32_t x = x0;
  int32_t y = y0;
  const int32_t along_offset = along - sideStart(edge);
  switch (edge) {
    case IOEdgeType::kBottom:
      x = along;
      y = y0;
      break;
    case IOEdgeType::kRight:
      x = x1 - oriented_width;
      y = y0 + along_offset;
      break;
    case IOEdgeType::kTop:
      x = x1 - along_offset - oriented_width;
      y = y1 - oriented_height;
      break;
    case IOEdgeType::kLeft:
      x = x0;
      y = y1 - along_offset - oriented_height;
      break;
    default:
      return false;
  }
  instance.set_coord(x, y);
  instance.set_orient(orient);
  instance.set_bounding_rect(x, y, x + oriented_width, y + oriented_height);
  instance.set_fixed(fixed);
  instance.set_placed(true);
  instance.set_delete_instance(false);
  instance.set_placement_updated(!instance.is_new_instance());
  return x >= x0 && y >= y0 && x + oriented_width <= x1 && y + oriented_height <= y1;
}

Instance* IOCellPlacer::findOrCreatePowerInstance(const IOCellPowerMember& member)
{
  Database& database = FPDM.getDatabase();
  for (int32_t index : _power_instance_indices) {
    Instance& instance = database.get_instance_list()[index];
    if (instance.get_master_name() == member.master_name && !instance.get_placed()) {
      instance.set_io_cell_net_name(resolvePowerNet(database, member.net_patterns));
      instance.set_io_cell_pin_name(member.pin_name);
      return &instance;
    }
  }
  std::string name;
  do {
    name = "IO_POWER_TMP_" + std::to_string(_next_power_temp_id++);
  } while (_used_instance_names.contains(name));
  Instance* instance = createPhysicalInstance(name, member.master_name);
  instance->set_io_cell_net_name(resolvePowerNet(database, member.net_patterns));
  instance->set_io_cell_pin_name(member.pin_name);
  return instance;
}

Instance* IOCellPlacer::createPhysicalInstance(const std::string& name, const std::string& master_name)
{
  Database& database = FPDM.getDatabase();
  Instance instance;
  instance.set_name(name);
  instance.set_master_name(master_name);
  const CellMaster& master = database.get_cell_master_map().at(master_name);
  instance.set_width(master.get_width());
  instance.set_height(master.get_height());
  instance.set_new_instance(true);
  instance.set_fixed(true);
  database.get_instance_name_to_idx_map()[name] = static_cast<int32_t>(database.get_instance_list().size());
  database.get_instance_list().push_back(instance);
  _used_instance_names.insert(name);
  return &database.get_instance_list().back();
}

bool IOCellPlacer::fillSegment(IOEdgeType edge, const Segment& segment, const IOCellDefinition& definition, int32_t& filler_index)
{
  if (segment.end <= segment.begin) {
    return true;
  }
  std::vector<int32_t> widths;
  for (const IOCellFillerMaster& filler : definition.filler_masters) {
    widths.push_back(micronToDbu(filler.width_micron));
  }
  std::vector<int32_t> solution = solveFiller(segment.end - segment.begin, widths);
  if (solution.empty()) {
    FPLOG.error(Loc::current(), "Cannot exactly fill IO Cell gap on ", edgeName(edge), ": ", segment.end - segment.begin, " DBU.");
    return false;
  }
  int32_t coordinate = segment.begin;
  for (int32_t width : solution) {
    int32_t selected = -1;
    for (int32_t i = 0; i < static_cast<int32_t>(widths.size()); ++i) {
      if (widths[i] == width) {
        selected = i;
      }
    }
    if (selected < 0) {
      return false;
    }
    Instance* instance
        = createPhysicalInstance("IO_FILL_" + edgeName(edge) + "_" + std::to_string(filler_index++), definition.filler_masters[selected].master_name);
    if (!placeCell(*instance, edge, coordinate, edgeOrientation(edge))) {
      return false;
    }
    coordinate += width;
  }
  return coordinate == segment.end;
}

std::vector<int32_t> IOCellPlacer::solveFiller(int32_t gap, const std::vector<int32_t>& widths) const
{
  if (gap < 0 || widths.empty()) {
    return {};
  }
  const int32_t inf = std::numeric_limits<int32_t>::max() / 4;
  std::vector<int32_t> count(gap + 1, inf), previous(gap + 1, -1);
  count[0] = 0;
  for (int32_t amount = 1; amount <= gap; ++amount) {
    for (int32_t i = 0; i < static_cast<int32_t>(widths.size()); ++i) {
      if (widths[i] <= amount && count[amount - widths[i]] != inf && count[amount - widths[i]] + 1 < count[amount]) {
        count[amount] = count[amount - widths[i]] + 1;
        previous[amount] = i;
      }
    }
  }
  if (count[gap] == inf) {
    return {};
  }
  std::vector<int32_t> result;
  for (int32_t amount = gap; amount > 0;) {
    int32_t index = previous[amount];
    if (index < 0) {
      return {};
    }
    result.push_back(widths[index]);
    amount -= widths[index];
  }
  return result;
}

int32_t IOCellPlacer::getCellSpan(const Instance& instance) const
{
  // IO edge cells are placed with N/E/S/W. Their tangent dimension is the
  // unrotated master width (65um for the CX55 sample), including on left/right.
  return instance.get_width();
}

int32_t IOCellPlacer::getOrientedWidth(const Instance& instance, PlacementOrientation orient) const
{
  return orient == PlacementOrientation::kE || orient == PlacementOrientation::kW || orient == PlacementOrientation::kFE || orient == PlacementOrientation::kFW
             ? instance.get_height()
             : instance.get_width();
}

int32_t IOCellPlacer::getOrientedHeight(const Instance& instance, PlacementOrientation orient) const
{
  return orient == PlacementOrientation::kE || orient == PlacementOrientation::kW || orient == PlacementOrientation::kFE || orient == PlacementOrientation::kFW
             ? instance.get_width()
             : instance.get_height();
}

int32_t IOCellPlacer::micronToDbu(double micron) const
{
  return static_cast<int32_t>(std::llround(micron * FPDM.getDatabase().get_micron_dbu()));
}

PlacementOrientation IOCellPlacer::edgeOrientation(IOEdgeType edge) const
{
  // In N orientation the CORE side faces +Y. Rotate that side toward the die center.
  switch (edge) {
    case IOEdgeType::kBottom:
      return PlacementOrientation::kN;
    case IOEdgeType::kRight:
      return PlacementOrientation::kW;
    case IOEdgeType::kTop:
      return PlacementOrientation::kS;
    case IOEdgeType::kLeft:
      return PlacementOrientation::kE;
    default:
      return PlacementOrientation::kN;
  }
}

int32_t IOCellPlacer::sideLength(IOEdgeType edge) const
{
  const Die& die = FPDM.getDatabase().get_die();
  return edge == IOEdgeType::kBottom || edge == IOEdgeType::kTop ? die.get_ur_x() - die.get_ll_x() : die.get_ur_y() - die.get_ll_y();
}

int32_t IOCellPlacer::sideStart(IOEdgeType edge) const
{
  const Die& die = FPDM.getDatabase().get_die();
  return edge == IOEdgeType::kBottom || edge == IOEdgeType::kTop ? die.get_ll_x() : die.get_ll_y();
}

std::string IOCellPlacer::edgeName(IOEdgeType edge) const
{
  switch (edge) {
    case IOEdgeType::kBottom:
      return "bottom";
    case IOEdgeType::kRight:
      return "right";
    case IOEdgeType::kTop:
      return "top";
    case IOEdgeType::kLeft:
      return "left";
    default:
      return "none";
  }
}

}  // namespace ifp

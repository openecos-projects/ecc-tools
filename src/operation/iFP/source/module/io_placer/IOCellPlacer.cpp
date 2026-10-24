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
  size_t value_idx = 0;
  size_t pattern_idx = 0;
  size_t star_idx = std::string::npos;
  size_t retry_idx = 0;
  while (value_idx < value.size()) {
    if (pattern_idx < pattern.size() && (pattern[pattern_idx] == '?' || pattern[pattern_idx] == value[value_idx])) {
      ++value_idx;
      ++pattern_idx;
    } else if (pattern_idx < pattern.size() && pattern[pattern_idx] == '*') {
      star_idx = pattern_idx++;
      retry_idx = value_idx;
    } else if (star_idx != std::string::npos) {
      pattern_idx = star_idx + 1;
      value_idx = ++retry_idx;
    } else {
      return false;
    }
  }
  while (pattern_idx < pattern.size() && pattern[pattern_idx] == '*') {
    ++pattern_idx;
  }
  return pattern_idx == pattern.size();
}

int32_t alignUp(int32_t value, int32_t step)
{
  return step <= 1 ? value : ((value + step - 1) / step) * step;
}

// Minimum number of identical power packages on a line of line_length.
// Returns -1 when target_group_spacing cannot be met.
int32_t packageCount(int32_t line_length, int32_t package_span, int32_t spacing)
{
  if (package_span <= 0) {
    return 0;
  }
  if (line_length < package_span) {
    return -1;
  }
  if (line_length < 2 * package_span) {
    return 1;
  }
  const int64_t l = line_length;
  const int64_t p = package_span;
  const int64_t s = std::max<int64_t>(1, spacing);
  // n >= ceil((L + S) / (P + S)) <=> (L - nP) / (n - 1) <= S
  const int64_t n = (l + s + p + s - 1) / (p + s);
  return n * p > l ? -1 : static_cast<int32_t>(n);
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
  const CellMaster& corner_master = database.get_cell_master_map().at(definition.corner_master_name);
  if (corner_master.get_width() != corner_master.get_height()) {
    FPLOG.error(Loc::current(), "IO Cell corner master '", definition.corner_master_name, "' must be square (width ",
                corner_master.get_width(), " != height ", corner_master.get_height(), ").");
    return false;
  }
  for (const IOCellFillerMaster& filler : definition.filler_masters) {
    auto filler_iter = database.get_cell_master_map().find(filler.master_name);
    if (filler_iter == database.get_cell_master_map().end()) {
      FPLOG.error(Loc::current(), "IO Cell filler master '", filler.master_name, "' is not present in LEF/database.");
      return false;
    }
    const int32_t width = filler_iter->second.get_width();
    if (width <= 0) {
      FPLOG.error(Loc::current(), "IO Cell filler master '", filler.master_name, "' has invalid width ", width, ".");
      return false;
    }
    // Widths come from LEF only; a conflicting width_micron would silently shift
    // every following cell and overlap the ring.
    if (filler.width_micron > 0.0 && micronToDbu(filler.width_micron) != width) {
      FPLOG.error(Loc::current(), "IO Cell filler '", filler.master_name, "' width_micron=", filler.width_micron, " um (",
                  micronToDbu(filler.width_micron), " DBU) does not match LEF width ", width, " DBU.");
      return false;
    }
  }
  if (_config.io_cell_power_spacing_micron <= 0.0) {
    FPLOG.error(Loc::current(), "io_cell.power.target_group_spacing must be greater than zero.");
    return false;
  }
  return true;
}

int32_t IOCellPlacer::fillerWidth(const IOCellFillerMaster& filler) const
{
  return FPDM.getDatabase().get_cell_master_map().at(filler.master_name).get_width();
}

int32_t IOCellPlacer::fillerGcd(const IOCellDefinition& definition) const
{
  int32_t gcd = 0;
  for (const IOCellFillerMaster& filler : definition.filler_masters) {
    gcd = std::gcd(gcd, fillerWidth(filler));
  }
  return gcd;
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
  // so the IDB output removes them as well.  Deleted records are included:
  // resetExistingPlacement marks old ring cells for removal but they still own
  // their names until this pass moves them away.
  // Only cells generated by iFP carry the IO_POWER_ prefix; a user supply pad
  // (even one named "vdd_*") must never be removed.
  int32_t stale_power_index = 0;
  const int32_t instance_count = static_cast<int32_t>(database.get_instance_list().size());
  for (int32_t index = 0; index < instance_count; ++index) {
    Instance& instance = database.get_instance_list()[index];
    if (!isPowerMaster(definition, instance.get_master_name()) || instance.get_placed()) {
      continue;
    }
    if (instance.get_name().rfind("IO_POWER_", 0) != 0) {
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
        // Keep the IO_POWER_ prefix on cells generated by iFP so later runs can
        // tell them apart from supply pads that came from the netlist.
        const bool generated = !instances[index]->is_from_netlist() || instances[index]->get_name().rfind("IO_POWER_", 0) == 0;
        const std::string target = (generated ? "IO_POWER_" : "") + prefix + "_" + edge_names[edge_index] + "_"
                                   + std::to_string(index);
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
  std::set<std::string> required_signals;
  for (const Instance& instance : FPDM.getDatabase().get_instance_list()) {
    if (isSignalMaster(definition, instance.get_master_name()) && !instance.is_delete_instance()) {
      required_signals.insert(instance.get_name());
    }
  }
  std::set<std::string> placed_signals;
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
    if (!std::isfinite(offset) || offset < 0.0) {
      FPLOG.error(Loc::current(), "Invalid IO Cell offset at line ", line_number, ".");
      return false;
    }
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
      FPLOG.error(Loc::current(), "IO Cell '", instance_name, "' does not fit on ", edgeName(edge), " at line ", line_number, ".");
      return false;
    }
    int32_t side_index = static_cast<int32_t>(std::find(std::begin(edges), std::end(edges), edge) - std::begin(edges));
    int32_t begin = sideStart(edge) + micronToDbu(offset);
    if (isSignalMaster(definition, instance->get_master_name())) {
      placed_signals.insert(instance->get_name());
    }
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
    occupied[side_index].push_back({begin, begin + getCellSpan(*instance, edge, orient)});
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
    const size_t member_count = definition.power_members.size();
    std::vector<std::string>& masters = side_power_masters[side_index];
    if (member_count == 0 || masters.empty() || masters.size() % member_count != 0) {
      FPLOG.error(Loc::current(), "IO Cell placement file has an incomplete power package on ", edgeName(edge), ".");
      return false;
    }
    size_t rotation = 0;
    while (rotation < member_count && masters[rotation] != definition.power_members[0].master_name) {
      ++rotation;
    }
    if (rotation == member_count) {
      FPLOG.error(Loc::current(), "IO Cell placement file has no complete power package on ", edgeName(edge), ".");
      return false;
    }
    for (size_t i = 0; i < masters.size(); ++i) {
      if (masters[i] != definition.power_members[(rotation + i) % member_count].master_name) {
        FPLOG.error(Loc::current(), "IO Cell power groups are not complete or ordered on ", edgeName(edge), ".");
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
  if (placed_signals != required_signals) {
    std::string missing;
    for (const std::string& name : required_signals) {
      if (placed_signals.count(name) == 0) {
        if (!missing.empty()) {
          missing += ", ";
        }
        missing += name;
      }
    }
    FPLOG.error(Loc::current(), "IO Cell placement file is missing signal PADs: ", missing, ".");
    return false;
  }
  return true;
}

bool IOCellPlacer::bindIOPins(const IOCellDefinition& definition)
{
  const Database& database = FPDM.getDatabase();
  _port_bindings.clear();

  std::string unplaced_signals;
  for (const Instance& instance : database.get_instance_list()) {
    if (!instance.get_placed() && !instance.is_delete_instance() && isSignalMaster(definition, instance.get_master_name())) {
      if (!unplaced_signals.empty()) {
        unplaced_signals += ", ";
      }
      unplaced_signals += instance.get_name();
    }
  }
  if (!unplaced_signals.empty()) {
    FPLOG.error(Loc::current(), "These signal PADs are not placed: ", unplaced_signals,
                ". Add them to io_cell.file_path or use io_cell.mode=auto.");
    return false;
  }

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
      if (!instance.get_placed() || instance.get_macro() || master == database.get_cell_master_map().end() || !master->second.get_pad()) {
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
        auto layer_iter = database.get_routing_layer_name_to_idx_map().find(shape.get_layer_name());
        if (layer_iter != database.get_routing_layer_name_to_idx_map().end()) {
          layer = &database.get_routing_layer_list()[layer_iter->second];
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
      offset += getCellSpan(*instance, edge, edgeOrientation(edge));
    }
  }

  // Pack the signal instances into free intervals in netlist order.
  size_t signal_cursor = 0;
  int32_t filler_index = 0;
  for (const Segment& segment : layout.free_segments) {
    int32_t cursor_coord = segment.begin;
    while (signal_cursor < signal_indices.size()) {
      Instance& signal = database.get_instance_list()[signal_indices[signal_cursor]];
      const int32_t span = getCellSpan(signal, edge, edgeOrientation(edge));
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
  const int32_t group_count = packageCount(layout.line_length, layout.package_span, layout.dbu_spacing);
  if (group_count < 0) {
    layout.error = "IO Cell power packages cannot meet target_group_spacing on " + edgeName(edge) + ".";
    return false;
  }
  layout.group_count = group_count;

  for (const IOCellFillerMaster& filler : definition.filler_masters) {
    layout.filler_widths.push_back(fillerWidth(filler));
  }
  const int32_t filler_gcd = std::max<int32_t>(1, fillerGcd(definition));

  // Keep the first/last power group away from the corner cells.
  int32_t corner_clearance = micronToDbu(_config.io_cell_corner_clearance_micron);
  corner_clearance = std::max(0, corner_clearance);
  const int32_t total_free = layout.line_length - layout.group_count * layout.package_span;
  const int32_t max_clearance = total_free / 2;
  corner_clearance = std::min(corner_clearance, max_clearance);
  // Snap the clearance to the filler grid so both end gaps stay fillable.
  corner_clearance = (corner_clearance / filler_gcd) * filler_gcd;
  layout.clearance = corner_clearance;

  if (total_free % filler_gcd != 0) {
    layout.error = "IO Cell free length on " + edgeName(edge) + " is not representable by filler widths (gcd "
                   + std::to_string(filler_gcd) + ").";
    return false;
  }

  if (layout.group_count == 1) {
    const int32_t left = layout.line_begin + layout.clearance;
    const int32_t right = layout.line_end - layout.clearance - layout.package_span;
    if (right < left) {
      layout.error = "IO Cell power package does not fit on " + edgeName(edge) + ".";
      return false;
    }
    const int32_t anchor = left + ((right - left) / 2 / filler_gcd) * filler_gcd;
    layout.anchors.push_back(anchor);
    if (anchor > layout.line_begin) {
      layout.free_segments.push_back({layout.line_begin, anchor});
    }
    if (anchor + layout.package_span < layout.line_end) {
      layout.free_segments.push_back({anchor + layout.package_span, layout.line_end});
    }
    return true;
  }

  // Distribute the internal free length into gaps that are multiples of the
  // filler gcd; the leftover (also a multiple) widens the leading gaps.
  const int32_t gap_count = layout.group_count - 1;
  const int32_t internal_free = total_free - 2 * layout.clearance;
  const int32_t base_gap = (internal_free / gap_count) / filler_gcd * filler_gcd;
  const int32_t wide_gaps = (internal_free - base_gap * gap_count) / filler_gcd;
  int32_t anchor = layout.line_begin + layout.clearance;
  if (layout.clearance > 0) {
    layout.free_segments.push_back({layout.line_begin, anchor});
  }
  for (int32_t group_idx = 0; group_idx < layout.group_count; ++group_idx) {
    layout.anchors.push_back(anchor);
    if (group_idx + 1 == layout.group_count) {
      break;
    }
    const int32_t gap = base_gap + (group_idx < wide_gaps ? filler_gcd : 0);
    if (gap > 0) {
      layout.free_segments.push_back({anchor + layout.package_span, anchor + layout.package_span + gap});
    }
    anchor += layout.package_span + gap;
  }
  if (layout.clearance > 0) {
    layout.free_segments.push_back({anchor + layout.package_span, layout.line_end});
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

bool IOCellPlacer::packSignalsDryRun(IOEdgeType edge, const std::vector<int32_t>& signal_indices, const SideLayout& layout,
                                     int32_t& deficit) const
{
  const Database& database = FPDM.getDatabase();
  deficit = 0;
  int32_t filler_deficit = 0;
  size_t signal_cursor = 0;
  for (const Segment& segment : layout.free_segments) {
    int32_t cursor_coord = segment.begin;
    while (signal_cursor < signal_indices.size()) {
      const Instance& signal = database.get_instance_list()[signal_indices[signal_cursor]];
      const int32_t span = getCellSpan(signal, edge, edgeOrientation(edge));
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
      remaining += getCellSpan(database.get_instance_list()[signal_indices[index]], edge, edgeOrientation(edge));
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

int32_t IOCellPlacer::requiredLineLength(int32_t signal_length, int32_t package_span, int32_t spacing, int32_t clearance,
                                         int32_t filler_gcd) const
{
  if (package_span <= 0) {
    return 0;
  }
  const int32_t g = std::max<int32_t>(1, filler_gcd);
  const int32_t sm = std::max<int32_t>(1, spacing);
  const int64_t base = static_cast<int64_t>(signal_length) + 2 * clearance;
  const int64_t two_p = 2LL * package_span;

  // Free length must satisfy (L - nP) % gcd == 0 so every gap stays fillable.
  auto aligned_for = [&](int64_t low, int64_t n) {
    int32_t value = alignUp(static_cast<int32_t>(low), g);
    if (g > 1) {
      const int32_t residue = static_cast<int32_t>((n * package_span) % g);
      value += (residue - value % g + g) % g;
    }
    return value;
  };

  // One centered package: only possible while the line stays below 2P.
  if (package_span + base < two_p) {
    return aligned_for(package_span + base, 1);
  }

  for (int64_t n = 2; n <= 4096; ++n) {
    const int64_t lo = std::max(n * package_span + base, two_p);
    const int64_t hi = n * (package_span + sm) - sm + 2LL * clearance;
    if (lo > hi) {
      continue;
    }
    const int32_t value = aligned_for(lo, n);
    if (value <= hi) {
      return value;
    }
  }
  return -1;
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
  const int32_t filler_gcd = std::max<int32_t>(1, fillerGcd(definition));

  for (int32_t side_idx = 0; side_idx < 4; ++side_idx) {
    const IOEdgeType edge = edges[side_idx];
    IOSideCapacity& side = report.sides[side_idx];
    side.edge = edge;
    side.edge_name = edgeName(edge);

    const PlacementOrientation orient = edgeOrientation(edge);
    int32_t signal_length = 0;
    for (int32_t instance_idx : signal_indices[side_idx]) {
      signal_length += getCellSpan(database.get_instance_list()[instance_idx], edge, orient);
    }
    side.signal_length = signal_length;

    SideLayout layout;
    const bool layout_ok = buildSideLayout(edge, definition, layout);
    side.line_length = layout.line_length;
    side.package_span = layout.package_span;
    side.group_count = layout.group_count;
    side.clearance = layout.clearance;
    side.power_length = layout.group_count * layout.package_span;
    side.free_length = std::max<int32_t>(0, layout.line_length - side.power_length);

    bool fits = false;
    if (layout_ok) {
      int32_t deficit = 0;
      fits = packSignalsDryRun(edge, signal_indices[side_idx], layout, deficit);
      side.deficit = fits ? 0 : std::max<int32_t>(deficit, 1);
    } else {
      side.error = layout.error;
      side.deficit = std::max<int32_t>(signal_length + side.power_length - layout.line_length, 1);
    }
    if (!fits) {
      report.fits = false;
      const int32_t constructive = requiredLineLength(signal_length, layout.package_span, layout.dbu_spacing, layout.clearance, filler_gcd);
      // Prefer the constructive size; only fall back to the dry-run shortfall
      // when no feasible length exists (e.g. an unrepresentable filler gap).
      const int32_t fallback = layout.line_length + std::max<int32_t>(side.deficit, 1);
      side.required_line = alignUp(constructive > 0 ? constructive : fallback, filler_gcd);
      if (edge == IOEdgeType::kBottom || edge == IOEdgeType::kTop) {
        required_width = std::max(required_width, side.required_line + 2 * corner_master.get_width());
      } else {
        required_height = std::max(required_height, side.required_line + 2 * corner_master.get_height());
      }
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
  auto existing = database.get_instance_name_to_idx_map().find(name);
  if (existing != database.get_instance_name_to_idx_map().end()) {
    Instance& instance = database.get_instance_list()[existing->second];
    // Filler/corner cells are identified by name; the filler solver may pick a
    // different master for the same index when the die or definition changes.
    const bool generated = name.rfind("IO_FILL_", 0) == 0 || name.rfind("IO_CORNER_", 0) == 0 || name.rfind("IO_POWER_", 0) == 0;
    if (!instance.is_delete_instance() || (!generated && instance.get_master_name() != master_name)) {
      FPLOG.error(Loc::current(), "Instance name '", name, "' is already used by master '", instance.get_master_name(),
                  "' (requested '", master_name, "').");
      return nullptr;
    }
    if (instance.get_master_name() != master_name) {
      const CellMaster& master = database.get_cell_master_map().at(master_name);
      instance.set_master_name(master_name);
      instance.set_width(master.get_width());
      instance.set_height(master.get_height());
    }
    // Reuse the record from a previous run instead of adding a duplicate name.
    instance.set_delete_instance(false);
    instance.set_new_instance(!instance.is_from_netlist());
    instance.set_placed(false);
    instance.set_placement_updated(false);
    instance.set_fixed(true);
    instance.set_coord(-1, -1);
    instance.set_bounding_rect(-1, -1, -1, -1);
    instance.set_io_cell_net_name("");
    instance.set_io_cell_pin_name("");
    return &instance;
  }
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
    widths.push_back(fillerWidth(filler));
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

int32_t IOCellPlacer::getCellSpan(const Instance& instance, IOEdgeType edge, PlacementOrientation orient) const
{
  // Tangent dimension along the edge: x for horizontal edges, y for vertical.
  return (edge == IOEdgeType::kBottom || edge == IOEdgeType::kTop) ? getOrientedWidth(instance, orient)
                                                                   : getOrientedHeight(instance, orient);
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

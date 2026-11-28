#include "PyPlaceDB.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "IdbCellMaster.h"
#include "IdbDesign.h"
#include "IdbInstance.h"
#include "IdbRegularWire.h"
#include "IdbTerm.h"
#include "liberty/Lib.hh"

namespace python_interface {
namespace py = pybind11;

namespace {

py::dict make_summary()
{
  py::dict summary;
  summary["ok"] = false;
  summary["accepted_count"] = 0;
  summary["rejected_count"] = 0;
  summary["failed_count"] = 0;
  summary["committed_instance_names"] = py::list();
  summary["rejected_actions"] = py::list();
  summary["requires_refresh"] = false;
  summary["message"] = "";
  return summary;
}

void add_rejection(py::dict& summary, int index, const std::string& reason)
{
  auto actions = summary["rejected_actions"].cast<py::list>();
  py::dict action;
  action["index"] = index;
  action["reason"] = reason;
  actions.append(std::move(action));
  summary["rejected_count"] = actions.size();
}

bool has_compatible_terms(idb::IdbCellMaster& source, idb::IdbCellMaster& target)
{
  const auto source_terms = source.get_term_list();
  const auto target_terms = target.get_term_list();
  if (source_terms.size() != target_terms.size()) {
    return false;
  }

  for (const auto* source_term : source_terms) {
    if (source_term == nullptr) {
      return false;
    }
    const auto* target_term = target.findTerm(source_term->get_name());
    if (target_term == nullptr || target_term->get_direction() != source_term->get_direction()
        || target_term->get_type() != source_term->get_type()) {
      return false;
    }
  }
  return true;
}

bool is_sizable_master(idb::IdbCellMaster* master)
{
  return master != nullptr && master->is_core() && !master->is_core_filler() && !master->is_endcap();
}

bool buffer_signal_pin_names(idb::IdbCellMaster* master, bool require_single, std::string* input_pin_name,
                             std::string* output_pin_name)
{
  if (master == nullptr || input_pin_name == nullptr || output_pin_name == nullptr) {
    return false;
  }
  input_pin_name->clear();
  output_pin_name->clear();
  int input_count = 0;
  int output_count = 0;
  for (auto* term : master->get_term_list()) {
    if (term == nullptr || term->get_type() == idb::IdbConnectType::kPower
        || term->get_type() == idb::IdbConnectType::kGround) {
      continue;
    }
    if (term->get_direction() == idb::IdbConnectDirection::kInput) {
      ++input_count;
      if (input_pin_name->empty()) {
        *input_pin_name = term->get_name();
      }
    } else if (term->get_direction() == idb::IdbConnectDirection::kOutput
               || term->get_direction() == idb::IdbConnectDirection::kOutputTriState) {
      ++output_count;
      if (output_pin_name->empty()) {
        *output_pin_name = term->get_name();
      }
    }
  }
  if (require_single) {
    return input_count == 1 && output_count == 1;
  }
  return input_count >= 1 && output_count >= 1;
}

std::string action_string(const py::dict& action, const char* key)
{
  if (!action.contains(key) || action[key].is_none()) {
    return {};
  }
  try {
    return py::cast<std::string>(action[key]);
  } catch (const py::cast_error&) {
    return {};
  }
}

std::string buffer_action_digest(const py::list& actions)
{
  py::module_ json = py::module_::import("json");
  py::module_ hashlib = py::module_::import("hashlib");
  py::kwargs kwargs;
  kwargs["allow_nan"] = false;
  kwargs["separators"] = py::make_tuple(",", ":");
  kwargs["sort_keys"] = true;
  py::tuple positional = py::make_tuple(actions);
  py::object serialized = json.attr("dumps")(*positional, **kwargs);
  py::object encoded = serialized.attr("encode")("utf-8");
  return py::cast<std::string>(hashlib.attr("sha256")(encoded).attr("hexdigest")());
}

bool action_coordinate(const py::dict& action, const char* key, int32_t* result)
{
  if (!action.contains(key) || action[key].is_none()) {
    return false;
  }
  double value = 0.0;
  try {
    value = py::cast<double>(action[key]);
  } catch (const py::cast_error&) {
    return false;
  }
  if (!std::isfinite(value) || std::round(value) != value || value < std::numeric_limits<int32_t>::min()
      || value > std::numeric_limits<int32_t>::max()) {
    return false;
  }
  *result = static_cast<int32_t>(value);
  return true;
}

bool pin_name_matches(idb::IdbPin* pin, const std::string& requested)
{
  if (pin == nullptr) {
    return false;
  }
  const std::string pin_name = pin->get_pin_name();
  if (pin->get_instance() == nullptr) {
    return pin_name == requested;
  }
  const std::string instance_name = pin->get_instance()->get_name();
  return requested == instance_name + "/" + pin_name || requested == instance_name + ":" + pin_name
         || requested == instance_name + " " + pin_name;
}

idb::IdbPin* find_net_pin(idb::IdbNet* net, const std::string& requested)
{
  if (net == nullptr) {
    return nullptr;
  }
  for (auto* pin : net->get_instance_pin_list()->get_pin_list()) {
    if (pin_name_matches(pin, requested)) {
      return pin;
    }
  }
  for (auto* pin : net->get_io_pins()->get_pin_list()) {
    if (pin_name_matches(pin, requested)) {
      return pin;
    }
  }
  return nullptr;
}

std::string pin_display_name(idb::IdbPin* pin)
{
  if (pin == nullptr || pin->get_instance() == nullptr) {
    return pin == nullptr ? std::string{} : pin->get_pin_name();
  }
  return pin->get_instance()->get_name() + "/" + pin->get_pin_name();
}

struct PinRef
{
  bool is_io = false;
  std::string instance_name;
  std::string pin_name;
};

PinRef make_pin_ref(idb::IdbPin* pin)
{
  if (pin == nullptr) {
    return {};
  }
  auto* instance = pin->get_instance();
  return {instance == nullptr, instance == nullptr ? std::string{} : instance->get_name(), pin->get_pin_name()};
}

idb::IdbPin* resolve_pin(idb::IdbDesign* design, const PinRef& ref)
{
  if (design == nullptr) {
    return nullptr;
  }
  if (ref.is_io) {
    return design->get_io_pin_list() == nullptr ? nullptr : design->get_io_pin_list()->find_pin(ref.pin_name);
  }
  if (design->get_instance_list() == nullptr) {
    return nullptr;
  }
  auto* instance = design->get_instance_list()->find_instance(ref.instance_name);
  if (instance == nullptr) {
    return nullptr;
  }
  auto* pin = instance->get_pin(ref.pin_name);
  return pin == nullptr ? instance->get_pin_by_term(ref.pin_name) : pin;
}

std::string pin_display_name(const PinRef& ref)
{
  return ref.is_io || ref.instance_name.empty() ? ref.pin_name : ref.instance_name + "/" + ref.pin_name;
}

bool validate_mutated_net(idb::IdbNet* net)
{
  if (net == nullptr) {
    return false;
  }
  const std::string& net_name = net->get_net_name();
  std::unordered_set<idb::IdbPin*> pin_refs;
  std::unordered_set<idb::IdbInstance*> insts_from_pins;
  if (net->get_io_pins() != nullptr) {
    for (auto* pin : net->get_io_pins()->get_pin_list()) {
      if (pin == nullptr || !pin_refs.insert(pin).second || pin->get_net() != net
          || (!pin->get_net_name().empty() && pin->get_net_name() != net_name)) {
        return false;
      }
    }
  }
  if (net->get_instance_pin_list() != nullptr) {
    for (auto* pin : net->get_instance_pin_list()->get_pin_list()) {
      if (pin == nullptr || !pin_refs.insert(pin).second || pin->get_net() != net
          || (!pin->get_net_name().empty() && pin->get_net_name() != net_name)) {
        return false;
      }
      if (!pin->is_io_pin()) {
        auto* inst = pin->get_instance();
        if (inst == nullptr) {
          return false;
        }
        insts_from_pins.insert(inst);
      }
    }
  }
  std::unordered_set<idb::IdbInstance*> insts_from_index;
  if (net->get_instance_list() != nullptr) {
    for (auto* inst : net->get_instance_list()->get_instance_list()) {
      if (inst == nullptr || !insts_from_pins.contains(inst)) {
        return false;
      }
      insts_from_index.insert(inst);
    }
  }
  return insts_from_index.size() == insts_from_pins.size();
}

py::dict make_buffer_summary()
{
  py::dict summary;
  summary["status"] = "failed";
  summary["ok"] = false;
  summary["accepted_count"] = 0;
  summary["rejected_count"] = 0;
  summary["failed_count"] = 0;
  summary["committed_buffer_instance_names"] = py::list();
  summary["committed_downstream_net_names"] = py::list();
  summary["moved_load_pin_names"] = py::list();
  summary["rejected_actions"] = py::list();
  summary["topology_mutated"] = false;
  summary["requires_refresh"] = false;
  summary["refresh_required"] = false;
  summary["message"] = "";
  return summary;
}

void add_buffer_rejection(py::dict& summary, int index, const std::string& reason)
{
  auto actions = summary["rejected_actions"].cast<py::list>();
  py::dict action;
  action["index"] = index;
  action["reason"] = reason;
  actions.append(std::move(action));
  summary["rejected_count"] = actions.size();
}

void refresh_instance_geometry(idb::IdbInstance* instance)
{
  if (instance == nullptr || instance->get_cell_master() == nullptr) {
    return;
  }
  instance->set_bounding_box();
  instance->set_pin_list_coodinate();
  instance->set_halo_coodinate();
  instance->set_obs_box_list();
}

}  // namespace

py::list PyPlaceDB::getNodeMasterNames() const
{
  if (_db == nullptr || _db->get_idb_design() == nullptr) {
    throw std::runtime_error("native IDB is not initialized");
  }
  py::list result;
  auto* instances = _db->get_idb_design()->get_instance_list();
  for (const auto& name : node_names) {
    auto* instance = instances->find_instance(py::cast<std::string>(name));
    result.append(instance == nullptr || instance->get_cell_master() == nullptr
                      ? std::string{} : instance->get_cell_master()->get_name());
  }
  return result;
}

void PyPlaceDB::syncPlacementFromIdb()
{
  if (_db == nullptr || _db->get_idb_design() == nullptr) {
    return;
  }
  _design = _db->get_idb_design();
  auto* instances = _design->get_instance_list();
  if (instances == nullptr) {
    return;
  }
  const std::size_t movable_count =
      (num_nodes >= num_terminals + num_terminal_NIs)
          ? static_cast<std::size_t>(num_nodes - num_terminals - num_terminal_NIs)
          : 0;
  const std::size_t limit = std::min(movable_count, node_names.size());
  for (std::size_t node_id = 0; node_id < limit; ++node_id) {
    const std::string instance_name = py::cast<std::string>(node_names[node_id]);
    auto* inst = instances->find_instance(instance_name);
    if (inst == nullptr) {
      continue;
    }
    auto* bbox = inst->get_bounding_box();
    if (bbox != nullptr && node_id < node_x.size() && node_id < node_y.size()) {
      node_x[node_id] = bbox->get_low_x();
      node_y[node_id] = bbox->get_low_y();
    }
    py::str orient_str(IdbOrientToString(inst->get_orient()));
    if (node_id < node_orient.size()) {
      node_orient[node_id] = orient_str;
    }
    auto* inst_coord = inst->get_coordinate();
    if (inst_coord != nullptr && node_id + 1 < flat_node2pin_start_map.size()) {
      const int pin_begin = flat_node2pin_start_map[node_id].cast<int>();
      const int pin_end = flat_node2pin_start_map[node_id + 1].cast<int>();
      const std::string prefix = instance_name + ":";
      for (int p = pin_begin; p < pin_end; ++p) {
        if (p < 0 || static_cast<std::size_t>(p) >= flat_node2pin_map.size()) {
          continue;
        }
        const int pin_id = flat_node2pin_map[p].cast<int>();
        if (pin_id < 0 || static_cast<std::size_t>(pin_id) >= pin_names.size()
            || static_cast<std::size_t>(pin_id) >= pin_offset_x.size()
            || static_cast<std::size_t>(pin_id) >= pin_offset_y.size()) {
          continue;
        }
        const std::string full_name = pin_names[pin_id].cast<std::string>();
        if (full_name.rfind(prefix, 0) != 0) {
          continue;
        }
        const std::string term_name = full_name.substr(prefix.size());
        auto* pin = inst->get_pin(term_name);
        if (pin == nullptr) {
          pin = inst->get_pin_by_term(term_name);
        }
        auto* pin_coord = pin == nullptr ? nullptr : pin->get_average_coordinate();
        if (pin_coord != nullptr) {
          pin_offset_x[pin_id] = pin_coord->get_x() - inst_coord->get_x();
          pin_offset_y[pin_id] = pin_coord->get_y() - inst_coord->get_y();
        }
        if (static_cast<std::size_t>(pin_id) < pin_direct.size()) {
          pin_direct[pin_id] = orient_str;
        }
      }
    }
  }
}

py::dict PyPlaceDB::applySizing(const std::vector<int>& cell_ids, const std::vector<std::string>& target_master_names)
{
  py::dict summary = make_summary();
  if (_db == nullptr || _db->get_idb_design() == nullptr) {
    summary["message"] = "native IDB is not initialized";
    summary["failed_count"] = 1;
    return summary;
  }
  if (cell_ids.size() != target_master_names.size()) {
    summary["message"] = "cell_ids and target_master_names must have equal lengths";
    summary["failed_count"] = 1;
    return summary;
  }

  _design = _db->get_idb_design();
  if (cell_ids.empty()) {
    summary["ok"] = true;
    summary["message"] = "no sizing actions";
    return summary;
  }

  struct Candidate
  {
    int node_id;
    std::string instance_name;
    std::string old_master_name;
    std::string target_master_name;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(cell_ids.size());
  std::unordered_set<int> seen_ids;

  for (std::size_t i = 0; i < cell_ids.size(); ++i) {
    const int node_id = cell_ids[i];
    if (node_id < 0 || static_cast<std::size_t>(node_id) >= node_names.size()) {
      add_rejection(summary, static_cast<int>(i), "cell id is outside the PyPlaceDB node range");
      continue;
    }
    if (!seen_ids.insert(node_id).second) {
      add_rejection(summary, static_cast<int>(i), "duplicate cell id in one sizing transaction");
      continue;
    }

    const std::string instance_name = py::cast<std::string>(node_names[static_cast<std::size_t>(node_id)]);
    auto* instance = _design->get_instance_list()->find_instance(instance_name);
    if (instance == nullptr || instance->get_cell_master() == nullptr) {
      add_rejection(summary, static_cast<int>(i), "cell id does not resolve to an IDB instance");
      continue;
    }
    if (instance->is_fixed() || instance->is_cover()) {
      add_rejection(summary, static_cast<int>(i), "fixed or cover instances cannot be resized");
      continue;
    }
    if (!is_sizable_master(instance->get_cell_master())) {
      add_rejection(summary, static_cast<int>(i), "instance is not a movable standard cell");
      continue;
    }

    const std::string& target_name = target_master_names[i];
    if (_design->get_layout() == nullptr || _design->get_layout()->get_cell_master_list() == nullptr) {
      add_rejection(summary, static_cast<int>(i), "native cell-master database is not initialized");
      continue;
    }
    auto* target_master = _design->get_layout()->get_cell_master_list()->find_cell_master(target_name);
    if (!is_sizable_master(target_master)) {
      add_rejection(summary, static_cast<int>(i), "target master is not a movable standard cell");
      continue;
    }
    if (!has_compatible_terms(*instance->get_cell_master(), *target_master)) {
      add_rejection(summary, static_cast<int>(i), "target master pin names, directions or net types are incompatible");
      continue;
    }

    candidates.push_back({node_id, instance_name, instance->get_cell_master()->get_name(), target_name});
  }

  // Preflight is all-or-nothing. No native object is touched when any action
  // is rejected, which gives callers a deterministic rollback boundary.
  if (summary["rejected_count"].cast<std::size_t>() != 0) {
    summary["message"] = "sizing preflight rejected one or more actions; database unchanged";
    return summary;
  }

  auto sync_pydb_instance_geometry = [&](int node_id, idb::IdbInstance* inst) {
    if (inst == nullptr || node_id < 0) {
      return;
    }
    auto* bbox = inst->get_bounding_box();
    if (bbox != nullptr && static_cast<std::size_t>(node_id) < node_size_x.size()
        && static_cast<std::size_t>(node_id) < node_size_y.size()) {
      node_size_x[node_id] = bbox->get_width();
      node_size_y[node_id] = bbox->get_height();
    }
    auto* inst_coord = inst->get_coordinate();
    if (inst_coord != nullptr
        && static_cast<std::size_t>(node_id + 1) < flat_node2pin_start_map.size()) {
      const int pin_begin = flat_node2pin_start_map[node_id].cast<int>();
      const int pin_end = flat_node2pin_start_map[node_id + 1].cast<int>();
      const std::string prefix = inst->get_name() + ":";
      for (int p = pin_begin; p < pin_end; ++p) {
        if (p < 0 || static_cast<std::size_t>(p) >= flat_node2pin_map.size()) {
          continue;
        }
        const int pin_id = flat_node2pin_map[p].cast<int>();
        if (pin_id < 0 || static_cast<std::size_t>(pin_id) >= pin_names.size()
            || static_cast<std::size_t>(pin_id) >= pin_offset_x.size()
            || static_cast<std::size_t>(pin_id) >= pin_offset_y.size()) {
          continue;
        }
        const std::string full_name = pin_names[pin_id].cast<std::string>();
        if (full_name.rfind(prefix, 0) != 0) {
          continue;
        }
        const std::string term_name = full_name.substr(prefix.size());
        auto* pin = inst->get_pin(term_name);
        if (pin == nullptr) {
          pin = inst->get_pin_by_term(term_name);
        }
        auto* pin_coord = pin == nullptr ? nullptr : pin->get_average_coordinate();
        if (pin_coord != nullptr) {
          pin_offset_x[pin_id] = pin_coord->get_x() - inst_coord->get_x();
          pin_offset_y[pin_id] = pin_coord->get_y() - inst_coord->get_y();
        }
      }
    }
  };

  std::vector<Candidate> applied;
  applied.reserve(candidates.size());
  auto rollback = [&]() {
    bool rollback_ok = true;
    for (auto it = applied.rbegin(); it != applied.rend(); ++it) {
      const bool replaced = _design->replaceInstanceMaster(it->instance_name, it->old_master_name, true);
      auto* restored = _design->get_instance_list()->find_instance(it->instance_name);
      refresh_instance_geometry(restored);
      sync_pydb_instance_geometry(it->node_id, restored);
      rollback_ok = replaced && rollback_ok;
    }
    return rollback_ok;
  };

  for (const auto& candidate : candidates) {
    if (!_design->replaceInstanceMaster(candidate.instance_name, candidate.target_master_name, true)) {
      const bool rollback_ok = rollback();
      summary["failed_count"] = 1;
      summary["message"] = rollback_ok ? "native sizing failed; journal rollback applied"
                                        : "native sizing failed and journal rollback failed";
      return summary;
    }
    auto* updated_inst = _design->get_instance_list()->find_instance(candidate.instance_name);
    refresh_instance_geometry(updated_inst);
    sync_pydb_instance_geometry(candidate.node_id, updated_inst);
    applied.push_back(candidate);
  }

  py::list committed_names;
  for (const auto& candidate : applied) {
    committed_names.append(candidate.instance_name);
  }
  summary["ok"] = true;
  summary["accepted_count"] = applied.size();
  summary["committed_instance_names"] = std::move(committed_names);
  summary["requires_refresh"] = true;
  summary["message"] = "sizing committed to native IDB; rebuild PyPlaceDB before reuse";
  native_state_dirty = true;
  ++native_mutation_epoch;
  return summary;
}

py::dict PyPlaceDB::applyBufferRemovals(const py::list& actions)
{
  py::dict summary = make_buffer_summary();
  summary["action_count"] = actions.size();
  summary["inserted_count"] = 0;
  summary["removed_count"] = 0;
  summary["removed_buffer_instance_names"] = py::list();
  summary["removed_net_names"] = py::list();
  summary["survivor_net_names"] = py::list();
  summary["removed_downstream_net_names"] = py::list();
  summary["merged_load_pin_names"] = py::list();
  if (_db == nullptr || _db->get_idb_design() == nullptr) {
    summary["failed_count"] = actions.size();
    summary["message"] = "native IDB is not initialized";
    return summary;
  }
  if (actions.empty()) {
    summary["status"] = "skipped";
    summary["ok"] = true;
    summary["message"] = "no buffer removals";
    return summary;
  }

  _design = _db->get_idb_design();
  if (_design->get_instance_list() == nullptr || _design->get_net_list() == nullptr || _design->get_layout() == nullptr
      || _design->get_layout()->get_cell_master_list() == nullptr) {
    summary["failed_count"] = actions.size();
    summary["message"] = "native IDB connectivity or cell-master database is not initialized";
    return summary;
  }

  struct Candidate
  {
    std::string instance_name;
    std::string master_name;
    std::string input_pin_name;
    std::string output_pin_name;
    std::string input_net_name;
    std::string output_net_name;
    idb::IdbInstanceType type;
    idb::IdbPlacementStatus status;
    idb::IdbOrient orient;
    int32_t x;
    int32_t y;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(actions.size());
  std::unordered_set<std::string> seen_instances;
  const auto liberty_generation = _db->get_liberty_generation();

  for (std::size_t index = 0; index < actions.size(); ++index) {
    if (!py::isinstance<py::dict>(actions[index])) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer action must be a mapping");
      continue;
    }
    py::dict action = py::reinterpret_borrow<py::dict>(actions[index]);
    if (action_string(action, "action_kind") != "buffer_remove") {
      add_buffer_rejection(summary, static_cast<int>(index), "action_kind must be buffer_remove");
      continue;
    }
    const std::string instance_name = action_string(action, "instance_name");
    if (instance_name.empty()) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer_remove requires instance_name");
      continue;
    }
    if (!seen_instances.insert(instance_name).second) {
      add_buffer_rejection(summary, static_cast<int>(index), "duplicate buffer instance in removal batch");
      continue;
    }
    auto* instance = _design->get_instance_list()->find_instance(instance_name);
    if (instance == nullptr) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer instance does not exist");
      continue;
    }
    if (instance->is_fixed()) {
      add_buffer_rejection(summary, static_cast<int>(index), "fixed instances cannot be removed");
      continue;
    }
    if (instance->is_physical()) {
      add_buffer_rejection(summary, static_cast<int>(index), "physical instances cannot be removed");
      continue;
    }
    if (instance->get_region() != nullptr) {
      add_buffer_rejection(summary, static_cast<int>(index), "region-assigned instances cannot be removed");
      continue;
    }
    auto* master = instance->get_cell_master();
    if (master == nullptr) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer instance has no cell master");
      continue;
    }
    const std::string expected_master = action_string(action, "buffer_master_name");
    if (!expected_master.empty() && expected_master != master->get_name()) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer master does not match instance");
      continue;
    }
    std::string input_pin_name;
    std::string output_pin_name;
    if (!buffer_signal_pin_names(master, true, &input_pin_name, &output_pin_name)) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer master must have exactly one signal input and output");
      continue;
    }
    idb::LibCell* liberty_cell = nullptr;
    if (liberty_generation != nullptr) {
      for (const auto& library : liberty_generation->get_libraries()) {
        auto* cell = library == nullptr ? nullptr : library->findCell(master->get_name().c_str());
        if (cell != nullptr) {
          liberty_cell = cell;
        }
      }
    }
    if (liberty_cell == nullptr || !liberty_cell->isBuffer() || liberty_cell->isSequentialCell()
        || liberty_cell->isMacroCell()) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer removal requires a noninverting Liberty buffer");
      continue;
    }
    auto* input_pin = instance->get_pin_by_term(input_pin_name);
    auto* output_pin = instance->get_pin_by_term(output_pin_name);
    if (input_pin == nullptr || output_pin == nullptr) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer instance pins do not match its master");
      continue;
    }
    auto* input_net = input_pin->get_net();
    auto* output_net = output_pin->get_net();
    if (input_net == nullptr || output_net == nullptr || input_net == output_net) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer input and output must be on distinct connected nets");
      continue;
    }
    if (input_net->is_clock() || input_net->is_pdn() || output_net->is_clock() || output_net->is_pdn()) {
      add_buffer_rejection(summary, static_cast<int>(index), "clock, power and ground nets cannot be un-buffered");
      continue;
    }
    const std::string expected_net = action_string(action, "net_name");
    if (!expected_net.empty() && expected_net != input_net->get_net_name()) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer input net does not match action net_name");
      continue;
    }
    const std::string expected_downstream = action_string(action, "downstream_net_name");
    if (!expected_downstream.empty() && expected_downstream != output_net->get_net_name()) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer output net does not match action downstream_net_name");
      continue;
    }
    if (output_net->get_driving_pin() != output_pin) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer output pin is not the downstream net driver");
      continue;
    }
    int output_pin_count = 0;
    for (auto* pin : output_net->get_instance_pin_list()->get_pin_list()) {
      if (pin == nullptr || pin->get_term() == nullptr) {
        continue;
      }
      const auto direction = pin->get_term()->get_direction();
      if (direction == idb::IdbConnectDirection::kOutput
          || direction == idb::IdbConnectDirection::kOutputTriState) {
        ++output_pin_count;
      }
    }
    if (output_pin_count != 1) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer output net must have exactly one driver pin");
      continue;
    }
    auto* coordinate = instance->get_coordinate();
    if (coordinate == nullptr) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer instance has no coordinate");
      continue;
    }
    candidates.push_back({instance_name,
                          master->get_name(),
                          input_pin_name,
                          output_pin_name,
                          input_net->get_net_name(),
                          output_net->get_net_name(),
                          instance->get_type(),
                          instance->get_status(),
                          instance->get_orient(),
                          coordinate->get_x(),
                          coordinate->get_y()});
  }

  if (summary["rejected_count"].cast<std::size_t>() != 0) {
    summary["message"] = "buffer removal preflight rejected one or more actions; database unchanged";
    return summary;
  }

  // Remove downstream buffers first so each upstream buffer keeps its
  // downstream net alive until its own turn.
  std::unordered_map<std::string, std::size_t> output_to_index;
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    output_to_index[candidates[index].output_net_name] = index;
  }
  std::vector<int> depth(candidates.size(), -1);
  std::function<int(std::size_t)> depth_of = [&](std::size_t index) -> int {
    if (depth[index] >= 0) {
      return depth[index];
    }
    if (depth[index] == -2) {
      return -1;
    }
    depth[index] = -2;
    int upstream_depth = -1;
    auto upstream = output_to_index.find(candidates[index].input_net_name);
    if (upstream != output_to_index.end()) {
      upstream_depth = depth_of(upstream->second);
      if (upstream_depth < 0) {
        return -1;
      }
    }
    depth[index] = upstream_depth + 1;
    return depth[index];
  };
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    if (depth_of(index) < 0) {
      summary["message"] = "buffer removal dependency cycle detected; database unchanged";
      return summary;
    }
  }
  std::vector<std::size_t> order(candidates.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](std::size_t lhs, std::size_t rhs) {
    if (depth[lhs] != depth[rhs]) {
      return depth[lhs] > depth[rhs];
    }
    return candidates[lhs].instance_name < candidates[rhs].instance_name;
  });

  struct Removed
  {
    std::string instance_name;
    std::string master_name;
    std::string input_pin_name;
    std::string output_pin_name;
    std::string input_net_name;
    std::string output_net_name;
    idb::IdbInstanceType type;
    idb::IdbPlacementStatus status;
    idb::IdbOrient orient;
    int32_t x;
    int32_t y;
    idb::IdbConnectType input_connect_type;
    idb::IdbConnectType output_connect_type;
    std::string survivor_net_name;
    std::string removed_net_name;
    // Keep both sides of the pre-removal boundary.  The buffer pins are
    // intentionally omitted because removeInstanceSafe destroys them; the
    // rollback recreates the instance and obtains fresh pin objects instead.
    std::vector<PinRef> input_pins;
    std::vector<PinRef> output_pins;
    std::vector<idb::IdbRegularWire*> input_wires;
    std::vector<idb::IdbRegularWire*> output_wires;
    bool instance_removed = false;
  };
  std::vector<Removed> removed;
  auto rollback = [&]() {
    bool rollback_ok = true;
    for (auto it = removed.rbegin(); it != removed.rend(); ++it) {
      auto* input_net = _design->get_net_list()->find_net(it->input_net_name);
      auto* output_net = _design->get_net_list()->find_net(it->output_net_name);
      if (input_net == nullptr) {
        input_net = _design->createOrFindNet(it->input_net_name, it->input_connect_type,
                                             idb::IdbCreatePolicy::kErrorIfExists);
      }
      if (output_net == nullptr) {
        output_net = _design->createOrFindNet(it->output_net_name, it->output_connect_type,
                                              idb::IdbCreatePolicy::kErrorIfExists);
      }
      idb::IdbInstance* instance = _design->get_instance_list()->find_instance(it->instance_name);
      if (instance == nullptr) {
        instance = _design->createInstance(it->instance_name, it->master_name, it->type, it->status, it->orient,
                                           it->x, it->y, idb::IdbCreatePolicy::kErrorIfExists);
      }
      if (input_net == nullptr || output_net == nullptr || instance == nullptr || input_net == output_net) {
        rollback_ok = false;
        continue;
      }
      auto* input_pin = instance->get_pin_by_term(it->input_pin_name);
      auto* output_pin = instance->get_pin_by_term(it->output_pin_name);
      if (input_pin == nullptr || output_pin == nullptr) {
        rollback_ok = false;
        continue;
      }
      for (const auto& ref : it->input_pins) {
        auto* pin = resolve_pin(_design, ref);
        if (pin == nullptr) {
          rollback_ok = false;
          continue;
        }
        rollback_ok = _design->connectPinToNet(pin, input_net) && rollback_ok;
      }
      for (const auto& ref : it->output_pins) {
        auto* pin = resolve_pin(_design, ref);
        if (pin == nullptr) {
          rollback_ok = false;
          continue;
        }
        rollback_ok = _design->connectPinToNet(pin, output_net) && rollback_ok;
      }
      rollback_ok = _design->connectPinToNet(input_pin, input_net) && rollback_ok;
      rollback_ok = _design->connectPinToNet(output_pin, output_net) && rollback_ok;
      auto& input_wires = input_net->get_wire_list()->get_wire_list();
      auto& output_wires = output_net->get_wire_list()->get_wire_list();
      input_wires = it->input_wires;
      output_wires = it->output_wires;
    }
    return rollback_ok;
  };

  for (std::size_t order_index : order) {
    const auto& candidate = candidates[order_index];
    auto* instance = _design->get_instance_list()->find_instance(candidate.instance_name);
    if (instance == nullptr) {
      const bool rollback_ok = rollback();
      summary["failed_count"] = 1;
      summary["message"] = rollback_ok ? "buffer removal instance disappeared; journal rollback applied"
                                        : "buffer removal instance disappeared and journal rollback failed";
      return summary;
    }
    auto* input_pin = instance->get_pin_by_term(candidate.input_pin_name);
    auto* output_pin = instance->get_pin_by_term(candidate.output_pin_name);
    auto* input_net = input_pin == nullptr ? nullptr : input_pin->get_net();
    auto* output_net = output_pin == nullptr ? nullptr : output_pin->get_net();
    if (input_net == nullptr || output_net == nullptr || input_net == output_net) {
      const bool rollback_ok = rollback();
      summary["failed_count"] = 1;
      summary["message"] = rollback_ok ? "buffer removal topology changed; journal rollback applied"
                                        : "buffer removal topology changed and journal rollback failed";
      return summary;
    }
    Removed record;
    record.instance_name = candidate.instance_name;
    record.master_name = candidate.master_name;
    record.input_pin_name = candidate.input_pin_name;
    record.output_pin_name = candidate.output_pin_name;
    record.input_net_name = input_net->get_net_name();
    record.output_net_name = output_net->get_net_name();
    record.type = candidate.type;
    record.status = candidate.status;
    record.orient = candidate.orient;
    record.x = candidate.x;
    record.y = candidate.y;
    record.input_connect_type = input_net->get_connect_type();
    record.output_connect_type = output_net->get_connect_type();
    for (auto* pin : input_net->get_instance_pin_list()->get_pin_list()) {
      if (pin != input_pin) {
        record.input_pins.push_back(make_pin_ref(pin));
      }
    }
    for (auto* pin : input_net->get_io_pins()->get_pin_list()) {
      record.input_pins.push_back(make_pin_ref(pin));
    }
    for (auto* pin : output_net->get_instance_pin_list()->get_pin_list()) {
      if (pin != output_pin) {
        record.output_pins.push_back(make_pin_ref(pin));
      }
    }
    for (auto* pin : output_net->get_io_pins()->get_pin_list()) {
      record.output_pins.push_back(make_pin_ref(pin));
    }
    record.input_wires = input_net->get_wire_list()->get_wire_list();
    record.output_wires = output_net->get_wire_list()->get_wire_list();
    // OpenROAD preserves an output-port net when it removes a buffer from an
    // ordinary input net.  If both sides carry ports, keep the input side as
    // the conservative survivor, matching its feedthrough handling.
    const bool output_survives = output_net->has_io_pins() && !input_net->has_io_pins();
    auto* survivor_net = output_survives ? output_net : input_net;
    auto* removed_net = output_survives ? input_net : output_net;
    record.survivor_net_name = survivor_net->get_net_name();
    record.removed_net_name = removed_net->get_net_name();
    removed.push_back(std::move(record));
    if (!_design->mergeNetInto(survivor_net->get_net_name(), removed_net->get_net_name(), true)) {
      const bool rollback_ok = rollback();
      summary["failed_count"] = 1;
      summary["message"] = rollback_ok ? "buffer downstream net merge failed; journal rollback applied"
                                        : "buffer downstream net merge failed and journal rollback failed";
      return summary;
    }
    if (!_design->removeInstanceSafe(candidate.instance_name)) {
      const bool rollback_ok = rollback();
      summary["failed_count"] = 1;
      summary["message"] = rollback_ok ? "buffer instance removal failed; journal rollback applied"
                                        : "buffer instance removal failed and journal rollback failed";
      return summary;
    }
    removed.back().instance_removed = true;
  }

  bool removal_connectivity_ok = true;
  if (_design->get_net_list() != nullptr) {
    for (const auto& item : removed) {
      auto* survivor = _design->get_net_list()->find_net(item.survivor_net_name);
      if (survivor != nullptr && !validate_mutated_net(survivor)) {
        removal_connectivity_ok = false;
        break;
      }
    }
  }
  if (!removal_connectivity_ok) {
    const bool rollback_ok = rollback();
    summary["failed_count"] = 1;
    summary["message"] = rollback_ok ? "buffer removal broke IDB connectivity; journal rollback applied"
                                      : "buffer removal broke IDB connectivity and journal rollback failed";
    return summary;
  }

  _design->canonicalizeNetNames();

  py::list removed_names;
  py::list removed_net_names;
  py::list survivor_net_names;
  py::list removed_nets;
  py::list merged_loads;
  for (const auto& item : removed) {
    removed_names.append(item.instance_name);
    removed_net_names.append(item.removed_net_name);
    survivor_net_names.append(item.survivor_net_name);
    // Historical callers use this field as the buffer's pre-removal output
    // net.  Keep that meaning even when OpenROAD's survivor rule keeps the
    // output-port net alive; removed_net_names/survivor_net_names carry the
    // unambiguous new journal result.
    removed_nets.append(item.output_net_name);
    for (const auto& ref : item.output_pins) {
      merged_loads.append(pin_display_name(ref));
    }
  }
  summary["status"] = "accepted";
  summary["ok"] = true;
  summary["accepted_count"] = removed.size();
  summary["removed_count"] = removed.size();
  summary["removed_buffer_instance_names"] = std::move(removed_names);
  summary["removed_net_names"] = std::move(removed_net_names);
  summary["survivor_net_names"] = std::move(survivor_net_names);
  summary["removed_downstream_net_names"] = std::move(removed_nets);
  summary["merged_load_pin_names"] = std::move(merged_loads);
  summary["topology_mutated"] = true;
  summary["requires_refresh"] = true;
  summary["refresh_required"] = true;
  summary["message"] = "buffer removal committed to native IDB; rebuild PyPlaceDB before reuse";
  native_state_dirty = true;
  ++native_mutation_epoch;
  return summary;
}

py::dict PyPlaceDB::applyBufferActions(const py::list& actions, const std::string& action_digest)
{
  py::dict summary = make_buffer_summary();
  summary["action_count"] = actions.size();
  if (_db == nullptr || _db->get_idb_design() == nullptr) {
    summary["failed_count"] = actions.size();
    summary["message"] = "native IDB is not initialized";
    return summary;
  }
  if (actions.empty()) {
    summary["status"] = "skipped";
    summary["ok"] = true;
    summary["message"] = "no buffer actions";
    return summary;
  }

  const std::string computed_digest = buffer_action_digest(actions);
  summary["computed_action_digest"] = computed_digest;
  summary["action_digest"] = action_digest;
  if (action_digest.empty() || action_digest != computed_digest) {
    summary["failed_count"] = actions.size();
    summary["message"] = "buffer action digest mismatch; database unchanged";
    return summary;
  }

  _design = _db->get_idb_design();
  if (_design->get_instance_list() == nullptr || _design->get_net_list() == nullptr || _design->get_layout() == nullptr
      || _design->get_layout()->get_cell_master_list() == nullptr) {
    summary["failed_count"] = actions.size();
    summary["message"] = "native IDB connectivity or cell-master database is not initialized";
    return summary;
  }

  bool has_insert = false;
  bool has_remove = false;
  for (std::size_t index = 0; index < actions.size(); ++index) {
    if (!py::isinstance<py::dict>(actions[index])) {
      continue;
    }
    py::dict action = py::reinterpret_borrow<py::dict>(actions[index]);
    const std::string kind = action_string(action, "action_kind");
    has_insert = has_insert || kind == "buffer_insert";
    has_remove = has_remove || kind == "buffer_remove";
  }
  if (has_remove && !has_insert) {
    return applyBufferRemovals(actions);
  }
  if (has_remove && has_insert) {
    summary["failed_count"] = actions.size();
    summary["message"] = "mixed buffer_insert and buffer_remove actions are not supported in one batch";
    return summary;
  }

  struct NetPinCacheEntry
  {
    idb::IdbPin* driving_pin = nullptr;
    std::unordered_map<std::string, idb::IdbPin*> pins;
  };
  std::unordered_map<idb::IdbNet*, NetPinCacheEntry> net_pin_cache;
  auto get_net_entry = [&](idb::IdbNet* net) -> const NetPinCacheEntry& {
    auto it = net_pin_cache.find(net);
    if (it != net_pin_cache.end()) {
      return it->second;
    }
    NetPinCacheEntry entry;
    entry.driving_pin = net->get_driving_pin();
    const auto& inst_pins = net->get_instance_pin_list()->get_pin_list();
    const auto& io_pins = net->get_io_pins()->get_pin_list();
    entry.pins.reserve(inst_pins.size() + io_pins.size());
    for (auto* pin : inst_pins) {
      if (pin != nullptr && pin->get_instance() != nullptr) {
        entry.pins.emplace(pin->get_instance()->get_name() + "/" + pin->get_pin_name(), pin);
      }
    }
    for (auto* pin : io_pins) {
      if (pin != nullptr && pin->get_instance() == nullptr) {
        entry.pins.emplace(pin->get_pin_name(), pin);
      }
    }
    return net_pin_cache.emplace(net, std::move(entry)).first->second;
  };
  auto find_cached_net_pin = [&](idb::IdbNet* net, const NetPinCacheEntry& entry, const std::string& requested) -> idb::IdbPin* {
    auto pin_it = entry.pins.find(requested);
    if (pin_it != entry.pins.end()) {
      return pin_it->second;
    }
    return find_net_pin(net, requested);
  };

  struct Candidate
  {
    idb::IdbNet* source_net;
    idb::IdbPin* driver_pin;
    std::vector<idb::IdbPin*> loads;
    std::string master_name;
    std::string input_pin_name;
    std::string output_pin_name;
    int32_t x;
    int32_t y;
    int32_t tree_depth;
    int32_t parent_id;
    int32_t child_id;
    double split_ratio;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(actions.size());

  for (std::size_t index = 0; index < actions.size(); ++index) {
    if (!py::isinstance<py::dict>(actions[index])) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer action must be a mapping");
      continue;
    }
    py::dict action = py::reinterpret_borrow<py::dict>(actions[index]);
    if (action_string(action, "action_kind") != "buffer_insert") {
      add_buffer_rejection(summary, static_cast<int>(index), "action_kind must be buffer_insert");
      continue;
    }

    const std::string net_name = action_string(action, "net_name");
    auto* source_net = _design->get_net_list()->find_net(net_name);
    if (source_net == nullptr) {
      add_buffer_rejection(summary, static_cast<int>(index), "source net does not exist");
      continue;
    }
    if (source_net->is_clock() || source_net->is_pdn()) {
      add_buffer_rejection(summary, static_cast<int>(index), "clock, power and ground nets cannot be buffered");
      continue;
    }

    const auto& net_entry = get_net_entry(source_net);
    const std::string driver_name = action_string(action, "driver_pin_name");
    auto* driver_pin = find_cached_net_pin(source_net, net_entry, driver_name);
    if (driver_pin == nullptr || net_entry.driving_pin != driver_pin) {
      add_buffer_rejection(summary, static_cast<int>(index), "driver pin is not the source net driver");
      continue;
    }

    std::vector<std::string> load_names;
    if (action.contains("downstream_pin_names") && !action["downstream_pin_names"].is_none()) {
      if (!py::isinstance<py::list>(action["downstream_pin_names"])) {
        add_buffer_rejection(summary, static_cast<int>(index), "downstream_pin_names must be a list");
        continue;
      }
      bool load_name_error = false;
      for (const auto& item : action["downstream_pin_names"].cast<py::list>()) {
        try {
          load_names.push_back(py::cast<std::string>(item));
        } catch (const py::cast_error&) {
          load_name_error = true;
          break;
        }
      }
      if (load_name_error) {
        add_buffer_rejection(summary, static_cast<int>(index), "downstream_pin_names must contain strings");
        continue;
      }
    }
    if (load_names.empty()) {
      const std::string singleton = action_string(action, "load_pin_name");
      if (!singleton.empty()) {
        load_names.push_back(singleton);
      }
    }
    if (load_names.empty()) {
      add_buffer_rejection(summary, static_cast<int>(index), "downstream load partition is empty");
      continue;
    }
    std::vector<idb::IdbPin*> loads;
    loads.reserve(load_names.size());
    std::unordered_set<idb::IdbPin*> seen_loads;
    seen_loads.reserve(load_names.size());
    bool load_error = false;
    for (const auto& load_name : load_names) {
      auto* load = find_cached_net_pin(source_net, net_entry, load_name);
      if (load == nullptr || load == driver_pin || !seen_loads.insert(load).second || load->get_special_net() != nullptr) {
        load_error = true;
        break;
      }
      loads.push_back(load);
    }
    if (load_error) {
      add_buffer_rejection(summary, static_cast<int>(index), "downstream load is missing, duplicated or not a regular-net pin");
      continue;
    }

    const std::string master_name = action_string(action, "buffer_master_name");
    auto* master = _design->get_layout()->get_cell_master_list()->find_cell_master(master_name);
    if (!is_sizable_master(master)) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer master does not exist as a standard cell");
      continue;
    }
    std::string input_pin_name;
    std::string output_pin_name;
    for (auto* term : master->get_term_list()) {
      if (term == nullptr || term->get_type() == idb::IdbConnectType::kPower || term->get_type() == idb::IdbConnectType::kGround) {
        continue;
      }
      if (term->get_direction() == idb::IdbConnectDirection::kInput && input_pin_name.empty()) {
        input_pin_name = term->get_name();
      } else if ((term->get_direction() == idb::IdbConnectDirection::kOutput
                  || term->get_direction() == idb::IdbConnectDirection::kOutputTriState)
                 && output_pin_name.empty()) {
        output_pin_name = term->get_name();
      }
    }
    if (input_pin_name.empty() || output_pin_name.empty()) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer master must have one signal input and output");
      continue;
    }

    int32_t x = 0;
    int32_t y = 0;
    if (!action_coordinate(action, "candidate_location_x_dbu", &x) || !action_coordinate(action, "candidate_location_y_dbu", &y)) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer candidate coordinates must be finite integer DBU values");
      continue;
    }
    const int32_t depth = action.contains("segment_tree_depth") ? action["segment_tree_depth"].cast<int32_t>() : -1;
    const int32_t parent = action.contains("segment_parent_node_id") ? action["segment_parent_node_id"].cast<int32_t>() : -1;
    const int32_t child = action.contains("segment_child_node_id") ? action["segment_child_node_id"].cast<int32_t>() : -1;
    const double ratio = action.contains("segment_split_ratio") ? action["segment_split_ratio"].cast<double>() : 0.0;
    candidates.push_back({source_net, driver_pin, std::move(loads), master_name, input_pin_name, output_pin_name, x, y,
                          depth, parent, child, ratio});
  }

  if (summary["rejected_count"].cast<std::size_t>() != 0) {
    summary["message"] = "buffer preflight rejected one or more actions; database unchanged";
    return summary;
  }

  // Insert ancestor segments first. Descendants then resolve the upstream net
  // from their current loads, including repeaters on the same original edge.
  std::stable_sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
    return std::make_tuple(lhs.source_net->get_net_name(), lhs.tree_depth, lhs.parent_id, lhs.child_id, lhs.split_ratio)
           < std::make_tuple(rhs.source_net->get_net_name(), rhs.tree_depth, rhs.parent_id, rhs.child_id, rhs.split_ratio);
  });

  struct Applied
  {
    std::string instance_name;
    idb::IdbNet* downstream_net;
    idb::IdbNet* source_net;
    idb::IdbPin* input_pin;
    idb::IdbPin* output_pin;
    std::vector<idb::IdbPin*> loads;
  };
  std::vector<Applied> applied;
  applied.reserve(candidates.size());
  std::unordered_set<idb::IdbPin*> committed_loads;
  std::vector<idb::IdbNet*> dirty_nets;
  std::unordered_set<idb::IdbNet*> dirty_net_set;
  dirty_nets.reserve(candidates.size() * 2U);
  dirty_net_set.reserve(candidates.size() * 2U);
  auto mark_dirty_net = [&](idb::IdbNet* net) {
    if (net != nullptr && dirty_net_set.insert(net).second) {
      dirty_nets.push_back(net);
    }
  };
  auto compact_dirty_nets = [&]() {
    for (auto* net : dirty_nets) {
      if (net == nullptr) {
        continue;
      }
      if (auto* io_pins = net->get_io_pins(); io_pins != nullptr) {
        auto& vec = io_pins->get_pin_list();
        std::vector<idb::IdbPin*> kept;
        kept.reserve(vec.size());
        for (auto* pin : vec) {
          if (pin != nullptr && pin->get_net() == net) {
            kept.push_back(pin);
          }
        }
        io_pins->clear_pin_refs();
        io_pins->get_pin_list() = std::move(kept);
      }
      std::vector<idb::IdbInstance*> kept_insts;
      if (auto* inst_pins = net->get_instance_pin_list(); inst_pins != nullptr) {
        auto& vec = inst_pins->get_pin_list();
        std::vector<idb::IdbPin*> kept;
        kept.reserve(vec.size());
        kept_insts.reserve(vec.size());
        for (auto* pin : vec) {
          if (pin != nullptr && pin->get_net() == net) {
            kept.push_back(pin);
            if (auto* inst = pin->get_instance(); inst != nullptr) {
              kept_insts.push_back(inst);
            }
          }
        }
        inst_pins->clear_pin_refs();
        inst_pins->get_pin_list() = std::move(kept);
      }
      if (auto* inst_list = net->get_instance_list(); inst_list != nullptr) {
        inst_list->reset(false);
        inst_list->add_instance_refs(kept_insts);
      }
    }
  };

  std::unordered_set<std::string> io_pin_names;
  if (_design->get_io_pin_list() != nullptr) {
    const auto& io_pins = _design->get_io_pin_list()->get_pin_list();
    io_pin_names.reserve(io_pins.size());
    for (auto* pin : io_pins) {
      if (pin != nullptr) {
        io_pin_names.insert(pin->get_pin_name());
      }
    }
  }
  uint64_t next_inst_index = 0;
  bool tried_bare_inst_prefix = false;
  auto make_unique_buffer_inst_name = [&]() {
    const std::string base_name = "buffer_coord";
    auto* inst_list = _design->get_instance_list();
    if (!tried_bare_inst_prefix) {
      tried_bare_inst_prefix = true;
      if (inst_list == nullptr || inst_list->find_instance(base_name) == nullptr) {
        return base_name;
      }
    }
    for (;; ++next_inst_index) {
      std::string candidate_name = base_name + std::to_string(next_inst_index);
      if (inst_list == nullptr || inst_list->find_instance(candidate_name) == nullptr) {
        ++next_inst_index;
        return candidate_name;
      }
    }
  };
  std::unordered_map<idb::IdbNet*, std::pair<bool, uint64_t>> next_net_index;
  next_net_index.reserve(net_pin_cache.size());
  auto make_unique_buffer_net_name = [&](idb::IdbNet* source_net) {
    const std::string base_name = source_net->get_net_name() + "__buffer";
    auto& state = next_net_index[source_net];
    auto available = [&](const std::string& name) {
      return (_design->get_net_list() == nullptr || _design->get_net_list()->find_net(name) == nullptr)
             && !io_pin_names.contains(name)
             && (_design->get_instance_list() == nullptr || _design->get_instance_list()->find_instance(name) == nullptr);
    };
    if (!state.first) {
      state.first = true;
      if (available(base_name)) {
        return base_name;
      }
    }
    for (;; ++state.second) {
      std::string candidate_name = base_name + std::to_string(state.second);
      if (available(candidate_name)) {
        ++state.second;
        return candidate_name;
      }
    }
  };

  auto attach_pin_fast = [](idb::IdbPin* pin, idb::IdbNet* net) {
    if (pin->is_io_pin()) {
      net->get_io_pins()->get_pin_list().push_back(pin);
    } else {
      net->get_instance_pin_list()->get_pin_list().push_back(pin);
    }
    pin->set_net(net);
    pin->set_net_name(net->get_net_name());
  };

  auto rollback = [&]() {
    bool rollback_ok = true;
    for (auto it = applied.rbegin(); it != applied.rend(); ++it) {
      if (it->input_pin != nullptr) {
        it->input_pin->remove_net();
      }
      if (it->output_pin != nullptr) {
        it->output_pin->remove_net();
      }
      if (it->source_net != nullptr) {
        const std::string& src_name = it->source_net->get_net_name();
        for (auto* load : it->loads) {
          if (load != nullptr) {
            load->set_net(it->source_net);
            load->set_net_name(src_name);
          }
        }
      }
      if (it->downstream_net != nullptr) {
        _design->disconnectAllPinsFromNet(it->downstream_net);
      }
    }
    compact_dirty_nets();
    for (auto it = applied.rbegin(); it != applied.rend(); ++it) {
      if (it->downstream_net != nullptr) {
        const std::string downstream_name = it->downstream_net->get_net_name();
        rollback_ok = _design->removeNetSafe(downstream_name) && rollback_ok;
      }
      rollback_ok = _design->removeInstanceSafe(it->instance_name) && rollback_ok;
    }
    return rollback_ok;
  };

  for (const auto& candidate : candidates) {
    if (candidate.tree_depth < 0 && std::any_of(candidate.loads.begin(), candidate.loads.end(), [&](auto* load) {
          return committed_loads.find(load) != committed_loads.end();
        })) {
      const bool rollback_ok = rollback();
      summary["failed_count"] = 1;
      summary["message"] = rollback_ok ? "buffer load partition overlaps a prior action; journal rollback applied"
                                        : "buffer load partition overlaps a prior action and journal rollback failed";
      return summary;
    }
    auto* upstream = candidate.loads.front()->get_net();
    if (upstream == nullptr || std::any_of(candidate.loads.begin(), candidate.loads.end(), [&](auto* load) {
          return load->get_net() != upstream;
        })) {
      const bool rollback_ok = rollback();
      summary["failed_count"] = 1;
      summary["message"] = rollback_ok ? "buffer partition spans current branches; journal rollback applied"
                                        : "buffer partition spans current branches and journal rollback failed";
      return summary;
    }
    const std::string instance_name = make_unique_buffer_inst_name();
    auto* buffer = _design->createInstance(instance_name, candidate.master_name, idb::IdbInstanceType::kTiming,
                                            idb::IdbPlacementStatus::kPlaced, idb::IdbOrient::kN_R0, candidate.x, candidate.y,
                                            idb::IdbCreatePolicy::kErrorIfExists);
    if (buffer == nullptr) {
      const bool rollback_ok = rollback();
      summary["failed_count"] = 1;
      summary["message"] = rollback_ok ? "buffer instance creation failed; journal rollback applied"
                                        : "buffer instance creation failed and journal rollback failed";
      return summary;
    }
    auto* input_pin = buffer->get_pin_by_term(candidate.input_pin_name);
    auto* output_pin = buffer->get_pin_by_term(candidate.output_pin_name);
    const std::string downstream_name = make_unique_buffer_net_name(candidate.source_net);
    auto* downstream = _design->createOrFindNet(downstream_name, idb::IdbConnectType::kSignal, idb::IdbCreatePolicy::kErrorIfExists);
    if (input_pin == nullptr || output_pin == nullptr || downstream == nullptr) {
      _design->removeInstanceSafe(instance_name);
      if (downstream != nullptr) {
        _design->removeNetSafe(downstream_name);
      }
      const bool rollback_ok = rollback();
      summary["failed_count"] = 1;
      summary["message"] = rollback_ok ? "buffer topology setup failed; journal rollback applied"
                                        : "buffer topology setup failed and journal rollback failed";
      return summary;
    }
    mark_dirty_net(upstream);
    mark_dirty_net(downstream);
    attach_pin_fast(input_pin, upstream);
    attach_pin_fast(output_pin, downstream);
    applied.push_back({instance_name, downstream, upstream, input_pin, output_pin, candidate.loads});
    for (auto* load : candidate.loads) {
      if (load == nullptr) {
        const bool rollback_ok = rollback();
        summary["failed_count"] = 1;
        summary["message"] = rollback_ok ? "buffer load reconnect failed; journal rollback applied"
                                          : "buffer load reconnect failed and journal rollback failed";
        return summary;
      }
      attach_pin_fast(load, downstream);
    }
    committed_loads.insert(candidate.loads.begin(), candidate.loads.end());
  }

  compact_dirty_nets();

  bool buffer_connectivity_ok = true;
  for (auto* net : dirty_nets) {
    if (!validate_mutated_net(net)) {
      buffer_connectivity_ok = false;
      break;
    }
  }
  if (!buffer_connectivity_ok) {
    const bool rollback_ok = rollback();
    summary["failed_count"] = 1;
    summary["message"] = rollback_ok ? "buffer topology broke IDB connectivity; journal rollback applied"
                                      : "buffer topology broke IDB connectivity and journal rollback failed";
    return summary;
  }

  // Moving an output port can leave its former net in the port namespace.
  // Resolve names before rebuilding timing so all exported views agree.
  _design->canonicalizeNetNames();

  py::list buffer_names;
  py::list downstream_names;
  py::list moved_load_names;
  for (const auto& item : applied) {
    buffer_names.append(item.instance_name);
    downstream_names.append(item.downstream_net->get_net_name());
    for (auto* load : item.loads) {
      moved_load_names.append(pin_display_name(load));
    }
  }
  summary["status"] = "accepted";
  summary["ok"] = true;
  summary["accepted_count"] = applied.size();
  summary["committed_buffer_instance_names"] = std::move(buffer_names);
  summary["committed_downstream_net_names"] = std::move(downstream_names);
  summary["moved_load_pin_names"] = std::move(moved_load_names);
  summary["topology_mutated"] = true;
  summary["requires_refresh"] = true;
  summary["refresh_required"] = true;
  summary["message"] = "buffer topology committed to native IDB; rebuild PyPlaceDB before reuse";
  native_state_dirty = true;
  ++native_mutation_epoch;
  return summary;
}

}  // namespace python_interface
